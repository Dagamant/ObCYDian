#include "storage.h"

#include <SPI.h>
#include <SdFat.h>

#include <algorithm>

#include "board.h"

namespace storage {

static SPIClass sdSpi(VSPI);
static SdFs sd;
static State st = State::NoCard;
static std::vector<std::string> index_;
static std::vector<std::string> folders_;
static std::vector<std::pair<std::string, std::string>> aliases_;  // (alias, note path)
static uint32_t generation_ = 0;

static bool endsWithCI(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         strcasecmp(s.c_str() + s.size() - suffix.size(), suffix.c_str()) == 0;
}

static SdSpiConfig spiConfig() {
  return SdSpiConfig(pins::SD_CS, DEDICATED_SPI, SD_SCK_MHZ(20), &sdSpi);
}

State begin() {
  static bool spiStarted = false;
  if (!spiStarted) {
    sdSpi.begin(pins::SD_SCK, pins::SD_MISO, pins::SD_MOSI, pins::SD_CS);
    spiStarted = true;
  }
  sd.end();
  index_.clear();
  if (!sd.cardBegin(spiConfig())) {
    Serial.printf("[sd] no card (error 0x%02x)\n", sd.card() ? sd.card()->errorCode() : 0);
    return st = State::NoCard;
  }
  if (!sd.volumeBegin()) {
    Serial.println("[sd] card present but no FAT/exFAT filesystem");
    return st = State::NoFilesystem;
  }
  st = State::Mounted;
  CardInfo ci = info();
  Serial.printf("[sd] mounted %s %s, %.1f GB\n", ci.cardType, ci.fsType, ci.capacity / 1e9);
  rescan();
  return st;
}

State state() { return st; }

CardInfo info() {
  CardInfo ci{"none", "none", 0};
  if (st == State::NoCard) return ci;
  switch (sd.card()->type()) {
    case SD_CARD_TYPE_SD1: ci.cardType = "SD1"; break;
    case SD_CARD_TYPE_SD2: ci.cardType = "SD2"; break;
    case SD_CARD_TYPE_SDHC: ci.cardType = "SDHC/SDXC"; break;
    default: ci.cardType = "unknown";
  }
  ci.capacity = (uint64_t)sd.card()->sectorCount() * 512;
  if (st == State::Mounted) {
    switch (sd.fatType()) {
      case FAT_TYPE_FAT12: ci.fsType = "FAT12"; break;
      case FAT_TYPE_FAT16: ci.fsType = "FAT16"; break;
      case FAT_TYPE_FAT32: ci.fsType = "FAT32"; break;
      case FAT_TYPE_EXFAT: ci.fsType = "exFAT"; break;
    }
  }
  return ci;
}

uint64_t freeBytes() {
  if (st != State::Mounted) return 0;
  int32_t clusters = sd.freeClusterCount();
  if (clusters < 0) return 0;
  return (uint64_t)clusters * sd.bytesPerCluster();
}

bool format(Print* progress) {
  sd.end();
  index_.clear();
  st = State::NoCard;
  if (!sd.cardBegin(spiConfig())) return false;
  st = State::NoFilesystem;
  static uint8_t sector[512];
  FsFormatter fmt;
  bool ok = fmt.format(sd.card(), sector, progress);
  Serial.printf("\n[sd] format %s\n", ok ? "ok" : "FAILED");
  if (!ok) return false;
  return begin() == State::Mounted;
}

static bool isMarkdown(const char* name) {
  size_t n = strlen(name);
  return n > 3 && strcasecmp(name + n - 3, ".md") == 0;
}

static bool isHiddenName(const char* name) {
  return name[0] == '.' || strcmp(name, "System Volume Information") == 0;
}

static bool lessCaseInsensitive(const std::string& a, const std::string& b) {
  return strcasecmp(a.c_str(), b.c_str()) < 0;
}

std::vector<Entry> list(const std::string& dir) {
  std::vector<Entry> out;
  if (st != State::Mounted) return out;
  FsFile d, f;
  if (!d.open(dir.c_str(), O_RDONLY) || !d.isDir()) return out;
  char name[256];
  while (f.openNext(&d, O_RDONLY)) {
    f.getName(name, sizeof(name));
    bool isDir = f.isDir();
    if (!isHiddenName(name) && (isDir || isMarkdown(name))) {
      out.push_back({name, isDir, (uint32_t)f.fileSize()});
    }
    f.close();
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    if (a.isDir != b.isDir) return a.isDir;
    return lessCaseInsensitive(a.name, b.name);
  });
  return out;
}

bool readFile(const std::string& path, std::string& out, size_t maxBytes) {
  out.clear();
  if (st != State::Mounted) return false;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return false;
  size_t n = std::min((size_t)f.fileSize(), maxBytes);
  out.resize(n);
  int got = f.read(&out[0], n);
  f.close();
  if (got < 0) return false;
  out.resize(got);
  return true;
}

bool writeFile(const std::string& path, const std::string& data) {
  if (st != State::Mounted) return false;
  mkdirs(parentDir(path));
  std::string tmp = path + ".tmp";
  FsFile f;
  if (!f.open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) return false;
  size_t n = f.write(data.data(), data.size());
  bool ok = n == data.size() && f.sync();
  f.close();
  if (!ok) {
    sd.remove(tmp.c_str());
    return false;
  }
  if (sd.exists(path.c_str()) && !sd.remove(path.c_str())) return false;
  return sd.rename(tmp.c_str(), path.c_str());
}

int64_t fileSize(const std::string& path) {
  if (st != State::Mounted) return -1;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return -1;
  int64_t n = f.fileSize();
  f.close();
  return n;
}

bool streamFile(const std::string& path, const std::function<bool(const uint8_t*, size_t)>& sink) {
  if (st != State::Mounted) return false;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return false;
  static uint8_t buf[2048];
  int n;
  bool ok = true;
  while ((n = f.read(buf, sizeof(buf))) > 0)
    if (!sink(buf, n)) {
      ok = false;
      break;
    }
  f.close();
  return ok && n == 0;
}

bool remove(const std::string& path) {
  if (st != State::Mounted || !sd.remove(path.c_str())) return false;
  rescan();
  return true;
}

bool rename(const std::string& from, const std::string& to) {
  if (st != State::Mounted || sd.exists(to.c_str())) return false;
  mkdirs(parentDir(to));
  if (!sd.rename(from.c_str(), to.c_str())) return false;
  rescan();
  return true;
}

bool renameNote(const std::string& from, const std::string& to, int* linksUpdated) {
  *linksUpdated = 0;
  // 1. With the old index, find every [[target]] that points at `from`.
  struct Fix {
    std::string note;
    std::vector<std::pair<size_t, size_t>> ranges;  // target text spans
  };
  std::vector<Fix> fixes;
  for (const auto& note : std::vector<std::string>(index_)) {
    std::string text;
    if (!readFile(note, text)) continue;
    Fix fix{note, {}};
    for (size_t a = text.find("[["); a != std::string::npos; a = text.find("[[", a + 2)) {
      size_t b = text.find("]]", a + 2);
      size_t nl = text.find('\n', a);
      if (b == std::string::npos || (nl != std::string::npos && nl < b)) continue;
      size_t end = std::min(text.find_first_of("|#", a + 2), b);
      std::string target = text.substr(a + 2, end - a - 2);
      if (!target.empty() && resolveLink(target, note) == from) fix.ranges.push_back({a + 2, end});
    }
    if (!fix.ranges.empty()) fixes.push_back(fix);
  }
  // 2. Rename, then rewrite the links with the new (shortest unambiguous) name.
  if (!rename(from, to)) return false;
  const std::string newText = linkText(to);
  for (auto& fix : fixes) {
    std::string path = fix.note == from ? to : fix.note;
    std::string text;
    if (!readFile(path, text)) continue;
    for (auto it = fix.ranges.rbegin(); it != fix.ranges.rend(); ++it) {
      text.replace(it->first, it->second - it->first, newText);
      (*linksUpdated)++;
    }
    writeFile(path, text);
  }
  return true;
}

static bool startsWithCI(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && strncasecmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

int countNotesIn(const std::string& dir) {
  int n = 0;
  for (auto& p : index_)
    if (startsWithCI(p, dir + "/")) n++;
  return n;
}

bool renameFolder(const std::string& from, const std::string& to, int* linksUpdated) {
  *linksUpdated = 0;
  if (st != State::Mounted || from == "/" || sd.exists(to.c_str())) return false;
  if (startsWithCI(to + "/", from + "/")) return false;  // can't move a folder into itself
  mkdirs(parentDir(to));
  if (!sd.rename(from.c_str(), to.c_str())) return false;
  rescan();
  // Links written with the folder path ([[Old/Note]], [[/Old/Note]]) need the new path
  const std::string oldRel = from.substr(1), newRel = to.substr(1);
  for (const auto& note : std::vector<std::string>(index_)) {
    std::string text;
    if (!readFile(note, text)) continue;
    bool changed = false;
    for (size_t a = text.find("[["); a != std::string::npos; a = text.find("[[", a + 2)) {
      size_t t = a + 2;
      if (t < text.size() && text[t] == '/') t++;
      if (startsWithCI(text.substr(t, oldRel.size() + 1), oldRel + "/")) {
        text.replace(t, oldRel.size(), newRel);
        changed = true;
        (*linksUpdated)++;
      }
    }
    if (changed) writeFile(note, text);
  }
  return true;
}

static bool removeTree(const std::string& dir, int depth) {
  if (depth > 10) return false;
  FsFile d, f;
  if (!d.open(dir.c_str(), O_RDONLY)) return false;
  std::vector<std::pair<std::string, bool>> entries;
  char name[256];
  while (f.openNext(&d, O_RDONLY)) {
    f.getName(name, sizeof(name));
    entries.push_back({joinPath(dir, name), f.isDir()});
    f.close();
  }
  d.close();
  for (auto& e : entries) {
    if (e.second ? !removeTree(e.first, depth + 1) : !sd.remove(e.first.c_str())) return false;
  }
  return sd.rmdir(dir.c_str());
}

bool removeFolder(const std::string& dir) {
  if (st != State::Mounted || dir == "/" || dir.empty()) return false;
  bool ok = removeTree(dir, 0);
  rescan();
  return ok;
}

std::vector<Hit> searchText(const std::string& query, size_t maxResults) {
  std::vector<Hit> out;
  if (query.empty()) return out;
  std::string q;
  for (char c : query) q += tolower((unsigned char)c);
  for (const auto& note : std::vector<std::string>(index_)) {
    std::string text;
    if (!readFile(note, text)) continue;
    std::string lower = text;
    for (auto& c : lower) c = tolower((unsigned char)c);
    int perNote = 0;
    for (size_t at = lower.find(q); at != std::string::npos && perNote < 3; at = lower.find(q, at + q.size())) {
      size_t ls = text.rfind('\n', at);
      ls = ls == std::string::npos ? 0 : ls + 1;
      size_t le = text.find('\n', at);
      if (le == std::string::npos) le = text.size();
      std::string line = text.substr(ls, le - ls);
      size_t s = line.find_first_not_of(" \t");
      line = s == std::string::npos ? "" : line.substr(s);
      out.push_back({note, (int)std::count(text.begin(), text.begin() + ls, '\n'), line});
      perNote++;
      at = le;  // one hit per line
      if (out.size() >= maxResults) return out;
    }
  }
  return out;
}

std::string sanitizeName(const std::string& name) {
  std::string out;
  for (char c : name) out += strchr("\\:*?\"<>|", c) || (uint8_t)c < 32 ? '-' : c;
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  while (!out.empty() && (out[0] == ' ' || out[0] == '.')) out.erase(0, 1);
  return out;
}

std::string createNote(const std::string& dir, const std::string& nameOrPath) {
  std::string path;
  size_t i = 0;
  std::string base = nameOrPath[0] == '/' ? "/" : dir;
  while (i <= nameOrPath.size()) {
    size_t j = nameOrPath.find('/', i);
    if (j == std::string::npos) j = nameOrPath.size();
    std::string seg = sanitizeName(nameOrPath.substr(i, j - i));
    if (!seg.empty()) path = path.empty() ? joinPath(base, seg) : joinPath(path, seg);
    i = j + 1;
  }
  if (path.empty()) return "";
  if (!endsWithCI(path, ".md")) path += ".md";
  if (exists(path)) return path;
  if (!writeFile(path, "")) return "";
  rescan();
  return path;
}

std::string untitledPath(const std::string& dir) {
  for (int n = 0;; n++) {
    std::string name = n == 0 ? "Untitled.md" : "Untitled " + std::to_string(n) + ".md";
    std::string p = joinPath(dir, name);
    if (!exists(p)) return p;
  }
}

bool exists(const std::string& path) { return st == State::Mounted && sd.exists(path.c_str()); }

bool mkdirs(const std::string& path) {
  if (path.empty() || path == "/") return true;
  return sd.exists(path.c_str()) || sd.mkdir(path.c_str(), true);
}

static void scanDir(const std::string& dir, int depth) {
  if (depth > 8) return;
  FsFile d, f;
  if (!d.open(dir.c_str(), O_RDONLY)) return;
  char name[256];
  std::vector<std::string> subdirs;
  while (f.openNext(&d, O_RDONLY)) {
    f.getName(name, sizeof(name));
    if (!isHiddenName(name)) {
      if (f.isDir()) {
        subdirs.push_back(joinPath(dir, name));
      } else if (isMarkdown(name)) {
        index_.push_back(joinPath(dir, name));
      }
    }
    f.close();
  }
  d.close();
  for (auto& s : subdirs) {
    folders_.push_back(s);
    scanDir(s, depth + 1);
  }
}

void rescan() {
  index_.clear();
  folders_.clear();
  aliases_.clear();
  generation_++;
  if (st != State::Mounted) return;
  uint32_t t = millis();
  scanDir("/", 0);
  std::sort(index_.begin(), index_.end(), lessCaseInsensitive);
  std::sort(folders_.begin(), folders_.end(), lessCaseInsensitive);
  // Aliases live in frontmatter, so only the start of each note needs reading
  for (auto& n : index_) {
    std::string head;
    if (!readFile(n, head, 2048)) continue;
    for (auto& a : frontmatterList(head, "aliases")) aliases_.push_back({a, n});
    for (auto& a : frontmatterList(head, "alias")) aliases_.push_back({a, n});
  }
  generation_++;
  Serial.printf("[sd] indexed %u notes in %lu ms\n", (unsigned)index_.size(), millis() - t);
}

const std::vector<std::string>& notes() { return index_; }
const std::vector<std::string>& folders() { return folders_; }

uint32_t generation() { return generation_; }

// Lower is better; -1 = no match. Prefix < word start < substring < subsequence.
static int fuzzyScore(const std::string& name, const std::string& q) {
  if (q.empty()) return 0;
  std::string n, l;
  for (char c : name) n += tolower(c);
  for (char c : q) l += tolower(c);
  size_t at = n.find(l);
  if (at == 0) return 0;
  if (at != std::string::npos) return (n[at - 1] == ' ' || n[at - 1] == '-' || n[at - 1] == '/') ? 1 : 2;
  size_t k = 0;
  for (char c : n)
    if (k < l.size() && c == l[k]) k++;
  return k == l.size() ? 3 : -1;
}

std::vector<std::string> search(const std::string& query, size_t maxResults) {
  struct Hit {
    int score;
    const std::string* path;
  };
  std::vector<Hit> hits;
  for (auto& p : index_) {
    std::string name = baseName(p);
    int sc = fuzzyScore(name, query);
    if (sc < 0) {
      // Also allow matching on the folder path
      int ps = fuzzyScore(p.substr(1, p.size() - 4), query);
      if (ps < 0) continue;
      sc = 4 + ps;
    }
    hits.push_back({sc, &p});
  }
  for (auto& a : aliases_) {  // notes found by an alias rank just after name matches
    int sc = fuzzyScore(a.first, query);
    if (sc < 0) continue;
    bool dup = false;
    for (auto& h : hits)
      if (*h.path == a.second) dup = true;
    if (!dup) hits.push_back({sc + 1, &a.second});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    if (a.score != b.score) return a.score < b.score;
    return a.path->size() < b.path->size();
  });
  std::vector<std::string> out;
  for (auto& h : hits) {
    if (out.size() >= maxResults) break;
    out.push_back(*h.path);
  }
  return out;
}

std::vector<Hit> backlinks(const std::string& path) {
  std::vector<Hit> out;
  for (const auto& note : std::vector<std::string>(index_)) {
    if (note == path) continue;
    std::string text;
    if (!readFile(note, text)) continue;
    for (size_t a = text.find("[["); a != std::string::npos; a = text.find("[[", a + 2)) {
      size_t b = text.find("]]", a + 2);
      if (b == std::string::npos) break;
      size_t end = std::min(text.find_first_of("|#", a + 2), b);
      if (resolveLink(text.substr(a + 2, end - a - 2), note) != path) continue;
      size_t ls = text.rfind('\n', a);
      ls = ls == std::string::npos ? 0 : ls + 1;
      size_t le = text.find('\n', a);
      std::string line = text.substr(ls, (le == std::string::npos ? text.size() : le) - ls);
      if (line.size() > 200) line = line.substr(0, 200) + "...";
      out.push_back({note, (int)std::count(text.begin(), text.begin() + ls, '\n'), line});
      break;
    }
  }
  return out;
}

std::vector<std::string> frontmatterList(const std::string& text, const char* key) {
  std::vector<std::string> out;
  if (text.compare(0, 4, "---\n") != 0) return out;
  size_t end = text.find("\n---", 3);
  if (end == std::string::npos) return out;
  const std::string fm = text.substr(4, end - 3);
  const std::string k = std::string(key) + ":";
  size_t pos = 0;
  while (pos < fm.size()) {
    size_t le = fm.find('\n', pos);
    if (le == std::string::npos) le = fm.size();
    std::string line = fm.substr(pos, le - pos);
    pos = le + 1;
    if (strncasecmp(line.c_str(), k.c_str(), k.size()) != 0) continue;
    std::string v = line.substr(k.size());
    auto trim = [](std::string s) {
      size_t a = s.find_first_not_of(" \t\"'"), b = s.find_last_not_of(" \t\"'");
      return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    v = trim(v);
    if (!v.empty() && v[0] == '[') {  // inline list
      v = v.substr(1, v.find(']') == std::string::npos ? std::string::npos : v.find(']') - 1);
      size_t i = 0;
      while (i <= v.size()) {
        size_t j = v.find(',', i);
        if (j == std::string::npos) j = v.size();
        std::string item = trim(v.substr(i, j - i));
        if (!item.empty()) out.push_back(item);
        i = j + 1;
      }
    } else if (!v.empty()) {
      out.push_back(v);
    } else {  // block list: following "  - item" lines
      while (pos < fm.size()) {
        size_t e = fm.find('\n', pos);
        if (e == std::string::npos) e = fm.size();
        std::string item = fm.substr(pos, e - pos);
        size_t dash = item.find_first_not_of(" \t");
        if (dash == std::string::npos || item[dash] != '-') break;
        item = trim(item.substr(dash + 1));
        if (!item.empty()) out.push_back(item);
        pos = e + 1;
      }
    }
    break;
  }
  return out;
}

const std::vector<std::pair<std::string, std::string>>& aliases() { return aliases_; }

std::string linkText(const std::string& path) {
  std::string name = baseName(path);
  int same = 0;
  for (auto& p : index_)
    if (strcasecmp(baseName(p).c_str(), name.c_str()) == 0) same++;
  if (same <= 1) return name;
  return path.substr(1, path.size() - 4);  // vault path without leading '/' and ".md"
}

std::string resolveLink(const std::string& rawTarget, const std::string& fromPath) {
  std::string target = rawTarget;
  while (!target.empty() && target.back() == ' ') target.pop_back();
  while (!target.empty() && target.front() == ' ') target.erase(0, 1);
  if (target.empty()) return fromPath;  // [[#heading]] links to the current note
  if (!endsWithCI(target, ".md")) target += ".md";

  // Relative or absolute path link, e.g. [[Projects/Idea]] or [text](../Idea.md)
  if (target.find('/') != std::string::npos) {
    std::string abs = target[0] == '/' ? target : joinPath(parentDir(fromPath), target);
    // Normalise "." and ".." segments
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= abs.size()) {
      size_t j = abs.find('/', i);
      if (j == std::string::npos) j = abs.size();
      std::string seg = abs.substr(i, j - i);
      if (seg == "..") {
        if (!parts.empty()) parts.pop_back();
      } else if (!seg.empty() && seg != ".") {
        parts.push_back(seg);
      }
      i = j + 1;
    }
    std::string norm;
    for (auto& p : parts) norm += "/" + p;
    for (auto& n : index_)
      if (strcasecmp(n.c_str(), norm.c_str()) == 0) return n;
    // Obsidian also matches a path suffix from the vault root
    std::string suffix = target[0] == '/' ? target : "/" + target;
    for (auto& n : index_)
      if (endsWithCI(n, suffix)) return n;
    return "";
  }

  // Bare name: prefer a note in the same folder, then the shortest path, then an alias.
  std::string sameDir = joinPath(parentDir(fromPath), target);
  std::string best;
  for (auto& n : index_) {
    if (strcasecmp(n.c_str(), sameDir.c_str()) == 0) return n;
    if (endsWithCI(n, "/" + target) && (best.empty() || n.size() < best.size())) best = n;
  }
  if (best.empty()) {
    const std::string name = target.substr(0, target.size() - 3);
    for (auto& a : aliases_)
      if (strcasecmp(a.first.c_str(), name.c_str()) == 0) return a.second;
  }
  return best;
}

std::string parentDir(const std::string& path) {
  size_t i = path.find_last_of('/');
  if (i == std::string::npos || i == 0) return "/";
  return path.substr(0, i);
}

std::string baseName(const std::string& path) {
  size_t i = path.find_last_of('/');
  std::string b = i == std::string::npos ? path : path.substr(i + 1);
  if (endsWithCI(b, ".md")) b.resize(b.size() - 3);
  return b;
}

std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir == "/") return "/" + name;
  return dir + "/" + name;
}

// ---------------------------------------------------------------------------

static const char* kWelcome = R"MD(# Welcome to ObCYDian

This is your **vault**: a folder of plain *markdown* files on the SD card, just like Obsidian.

## Linking notes
Link to another note with double brackets: [[Markdown cheatsheet]].
Give a link different text with a pipe: [[Projects/Ideas|my idea list]].
Links to notes that don't exist yet look faded: [[Not written yet]].

## Folders
Notes live in folders. Try [[Daily/2026-10-01]] or browse with the back button.

> [!tip] Tip
> Tap any purple link to follow it. Tap the back arrow to return.

#welcome #getting-started
)MD";

static const char* kCheatsheet = R"MD(---
tags: [reference]
created: 2026-10-01
---
# Markdown cheatsheet

## Text
Plain, **bold**, *italic*, ***bold italic***, ~~strikethrough~~, ==highlight== and `inline code`.

## Lists
- First item
- Second item
  - Nested item
    - Deeper still
1. Numbered
2. Lists too

## Tasks
- [x] Get the display working
- [x] Calibrate touch
- [ ] Write lots of notes

## Quotes
> Markdown is intended to be as easy-to-read and easy-to-write as is feasible.
> -- John Gruber

## Code
```cpp
void setup() {
  Serial.begin(115200);
}
```

---
Back to [[Welcome]].
)MD";

static const char* kIdeas = R"MD(# Ideas

- Bluetooth keyboard support
- Serve notes over [[Welcome|WiFi]]
- A daily note template, see [[2026-10-01]]

### Someday
1. Search across all notes
2. Backlinks panel
)MD";

static const char* kDaily = R"MD(# 2026-10-01

Got the **Cheap Yellow Display** showing notes today.

- [x] Display + touch
- [ ] [[Projects/Ideas|Next ideas]]

Related: [[Markdown cheatsheet#Tasks]]
)MD";

void createSampleVault() {
  if (st != State::Mounted) return;
  writeFile("/Welcome.md", kWelcome);
  writeFile("/Markdown cheatsheet.md", kCheatsheet);
  writeFile("/Projects/Ideas.md", kIdeas);
  writeFile("/Daily/2026-10-01.md", kDaily);
  rescan();
}

}  // namespace storage
