#include "storage.h"

#include <SPI.h>
#include <SdFat.h>

#include <algorithm>

#include "board.h"
#include "clock.h"
#include "noteindex.h"

namespace storage {

static SPIClass sdSpi(VSPI);
static SdFs sd;
static State st = State::NoCard;
static std::vector<std::string> folders_;
static std::vector<std::string> attachments_;                       // image files
static uint32_t generation_ = 0;  // bumped with the note index's own generation (see generation())

static bool endsWithCI(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         strcasecmp(s.c_str() + s.size() - suffix.size(), suffix.c_str()) == 0;
}

static bool startsWithCI(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && strncasecmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static SdSpiConfig spiConfig() {
  return SdSpiConfig(pins::SD_CS, DEDICATED_SPI, SD_SCK_MHZ(20), &sdSpi);
}

// FAT stores local time; SdFat asks for it whenever a file is created or written
static void fatDateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {
  time_t t = wallclock::now();
  tm tm_;
  gmtime_r(&t, &tm_);
  *date = FS_DATE(tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday);
  *time = FS_TIME(tm_.tm_hour, tm_.tm_min, tm_.tm_sec);
  *ms10 = tm_.tm_sec & 1 ? 100 : 0;
}

// FAT local date/time -> UTC seconds
static uint32_t fatToUtc(uint16_t date, uint16_t time) {
  if (!date) return 0;
  tm tm_{};
  tm_.tm_year = FS_YEAR(date) - 1900;
  tm_.tm_mon = FS_MONTH(date) - 1;
  tm_.tm_mday = FS_DAY(date);
  tm_.tm_hour = FS_HOUR(time);
  tm_.tm_min = FS_MINUTE(time);
  tm_.tm_sec = FS_SECOND(time);
  return (uint32_t)(mktime(&tm_) - wallclock::offsetMinutes() * 60);  // device TZ is UTC
}

State begin() {
  FsDateTime::setCallback(fatDateTime);
  static bool spiStarted = false;
  if (!spiStarted) {
    sdSpi.begin(pins::SD_SCK, pins::SD_MISO, pins::SD_MOSI, pins::SD_CS);
    spiStarted = true;
  }
  sd.end();
  nidx::clear();
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
  nidx::clear();
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
  if (!sd.rename(tmp.c_str(), path.c_str())) return false;
  // Every note write updates the note index, so nothing can leave it stale
  if (isMarkdown(path.c_str()) && path.compare(0, 10, "/.obcydian") != 0) {
    nidx::update(path, data);
    generation_++;
  }
  return true;
}

int64_t fileSize(const std::string& path) {
  if (st != State::Mounted) return -1;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return -1;
  int64_t n = f.fileSize();
  f.close();
  return n;
}

static uint32_t entryTime(FsFile& f) {
  uint16_t d = 0, t = 0;
  f.getModifyDateTime(&d, &t);
  return fatToUtc(d, t);
}

std::vector<FileInfo> listRaw(const std::string& dir, std::vector<std::string>* subdirs) {
  std::vector<FileInfo> out;
  if (st != State::Mounted) return out;
  FsFile d, f;
  if (!d.open(dir.c_str(), O_RDONLY) || !d.isDir()) return out;
  char name[256];
  while (f.openNext(&d, O_RDONLY)) {
    f.getName(name, sizeof(name));
    if (strcmp(name, "System Volume Information") != 0) {
      if (f.isDir()) {
        if (subdirs) subdirs->push_back(joinPath(dir, name));
      } else if (!endsWithCI(name, ".tmp")) {
        out.push_back({joinPath(dir, name), (uint32_t)f.fileSize(), entryTime(f)});
      }
    }
    f.close();
  }
  return out;
}

static void listAllInto(const std::string& dir, std::vector<FileInfo>& out, int depth) {
  if (depth > 10) return;
  std::vector<std::string> subdirs;
  auto files = listRaw(dir, &subdirs);
  out.insert(out.end(), files.begin(), files.end());
  for (auto& s : subdirs) listAllInto(s, out, depth + 1);
}

std::vector<FileInfo> listAll(const std::string& dir) {
  std::vector<FileInfo> out;
  listAllInto(dir, out, 0);
  return out;
}

bool isDir(const std::string& path) {
  if (st != State::Mounted) return false;
  if (path == "/") return true;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return false;
  bool d = f.isDir();
  f.close();
  return d;
}

uint32_t modifiedStamp(const std::string& path) {
  if (st != State::Mounted) return 0;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return 0;
  uint16_t d = 0, t = 0;
  f.getModifyDateTime(&d, &t);
  f.close();
  return ((uint32_t)d << 16) | t;
}

uint32_t modifiedTime(const std::string& path) {
  if (st != State::Mounted) return 0;
  FsFile f;
  if (!f.open(path.c_str(), O_RDONLY)) return 0;
  uint32_t t = entryTime(f);
  f.close();
  return t;
}

static FsFile writer_;
static std::string writerPath_;

bool writeBegin(const std::string& path) {
  writeAbort();
  if (st != State::Mounted) return false;
  mkdirs(parentDir(path));
  writerPath_ = path;
  return writer_.open((path + ".tmp").c_str(), O_WRONLY | O_CREAT | O_TRUNC);
}

bool writeChunk(const uint8_t* data, size_t n) { return writer_.isOpen() && writer_.write(data, n) == n; }

static void afterWrite(const std::string& path);

bool writeFinish() {
  if (!writer_.isOpen()) return false;
  bool ok = writer_.sync();
  writer_.close();
  std::string tmp = writerPath_ + ".tmp";
  if (!ok) {
    sd.remove(tmp.c_str());
    return false;
  }
  if (sd.exists(writerPath_.c_str()) && !sd.remove(writerPath_.c_str())) return false;
  if (!sd.rename(tmp.c_str(), writerPath_.c_str())) return false;
  afterWrite(writerPath_);
  return true;
}

// A file arrived some other way than writeFile (uploads, sync): index it
static void afterWrite(const std::string& path) { indexAdd(path); }

void writeAbort() {
  if (!writer_.isOpen()) return;
  writer_.close();
  sd.remove((writerPath_ + ".tmp").c_str());
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
  if (isMarkdown(path.c_str())) nidx::remove(path);
  attachments_.erase(std::remove(attachments_.begin(), attachments_.end(), path), attachments_.end());
  generation_++;
  return true;
}

bool rawRemove(const std::string& path) { return st == State::Mounted && sd.remove(path.c_str()); }
bool rawRename(const std::string& from, const std::string& to) {
  return st == State::Mounted && sd.rename(from.c_str(), to.c_str());
}

bool rename(const std::string& from, const std::string& to) {
  if (st != State::Mounted || sd.exists(to.c_str())) return false;
  const bool dir = isDir(from);
  mkdirs(parentDir(to));
  if (!sd.rename(from.c_str(), to.c_str())) return false;
  if (dir) {
    rescan();  // a whole folder moved
  } else {
    if (isMarkdown(from.c_str())) nidx::remove(from);
    attachments_.erase(std::remove(attachments_.begin(), attachments_.end(), from), attachments_.end());
    indexAdd(to);
  }
  return true;
}

bool renameNote(const std::string& from, const std::string& to, int* linksUpdated) {
  *linksUpdated = 0;
  // 1. The index says which notes link to `from`; find the exact spans in those notes.
  std::vector<std::string> linking;
  const uint32_t fromId = nidx::idOf(from);
  nidx::forEach([&](const nidx::Note& n) {
    for (auto& l : n.links)
      if (nidx::resolveId(l.target, n.path) == fromId) {
        linking.push_back(n.path);
        break;
      }
    return true;
  });
  struct Fix {
    std::string note;
    std::vector<std::pair<size_t, size_t>> ranges;  // target text spans
  };
  std::vector<Fix> fixes;
  for (auto& note : linking) {
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



int countNotesIn(const std::string& dir) {
  int n = 0;
  nidx::forEach([&](const nidx::Note& note) {
    if (startsWithCI(note.path, dir + "/")) n++;
    return true;
  }, false);
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
  std::vector<std::string> affected;
  nidx::forEach([&](const nidx::Note& n) {
    for (auto& l : n.links) {
      std::string t = l.target[0] == '/' ? l.target.substr(1) : l.target;
      if (startsWithCI(t, oldRel + "/")) {
        affected.push_back(n.path);
        break;
      }
    }
    return true;
  });
  for (auto& note : affected) {
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

// Walks every non-hidden folder in directory order (no per-file path lookups, which are
// slow on FAT), calling fn(path, size, mtime, file) for each file and dirFn for folders.
static void walkTree(const std::string& dir, int depth,
                     const std::function<bool(const std::string&, FsFile&)>& fileFn,
                     const std::function<void(const std::string&)>& dirFn) {
  if (depth > 10) return;
  FsFile d, f;
  if (!d.open(dir.c_str(), O_RDONLY)) return;
  char name[256];
  std::vector<std::string> subdirs;
  while (f.openNext(&d, O_RDONLY)) {
    f.getName(name, sizeof(name));
    if (!isHiddenName(name)) {
      if (f.isDir()) subdirs.push_back(joinPath(dir, name));
      else if (!fileFn(joinPath(dir, name), f)) {
        f.close();
        return;
      }
    }
    f.close();
  }
  d.close();
  for (auto& sd_ : subdirs) {
    if (dirFn) dirFn(sd_);
    walkTree(sd_, depth + 1, fileFn, dirFn);
  }
}

std::vector<Hit> searchText(const std::string& query, size_t maxResults) {
  std::vector<Hit> out;
  if (query.empty() || st != State::Mounted) return out;
  std::string q;
  for (char c : query) q += tolower((unsigned char)c);
  walkTree("/", 0, [&](const std::string& note, FsFile& f) {
    if (!isMarkdown(note.c_str())) return true;
    std::string text;
    text.resize(std::min<uint64_t>(f.fileSize(), 96 * 1024));
    int got = f.read(&text[0], text.size());
    text.resize(got > 0 ? got : 0);
    std::string lower = text;
    for (auto& c : lower) c = tolower((unsigned char)c);
    int perNote = 0;
    for (size_t at = lower.find(q); at != std::string::npos && perNote < 3; at = lower.find(q, at + q.size())) {
      size_t ls = text.rfind('\n', at);
      ls = ls == std::string::npos ? 0 : ls + 1;
      size_t le = text.find('\n', at);
      if (le == std::string::npos) le = text.size();
      std::string line = text.substr(ls, le - ls);
      size_t s0 = line.find_first_not_of(" \t");
      line = s0 == std::string::npos ? "" : line.substr(s0);
      out.push_back({note, (int)std::count(text.begin(), text.begin() + ls, '\n'), line});
      perNote++;
      at = le;  // one hit per line
      if (out.size() >= maxResults) return false;
    }
    return true;
  }, nullptr);
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
  indexAdd(path);
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



// Adds `note`'s frontmatter aliases to the alias index (only notes starting with ---)


void indexAdd(const std::string& path) {
  if (isMarkdown(path.c_str())) {
    std::string text;
    if (readFile(path, text)) nidx::update(path, text);
  } else if (isImageName(path) && std::find(attachments_.begin(), attachments_.end(), path) == attachments_.end()) {
    attachments_.push_back(path);
  }
  // Any new folders on the way
  for (std::string d = parentDir(path); d != "/"; d = parentDir(d))
    if (std::find(folders_.begin(), folders_.end(), d) == folders_.end())
      folders_.insert(std::upper_bound(folders_.begin(), folders_.end(), d, lessCaseInsensitive), d);
  generation_++;
}

void indexUpdate(const std::string& path) { indexAdd(path); }

void rescan() {
  folders_.clear();
  attachments_.clear();
  generation_++;
  if (st != State::Mounted) return nidx::clear();
  uint32_t t = millis();
  nidx::Builder builder;
  walkTree("/", 0, [&](const std::string& path, FsFile& f) {
    if (isMarkdown(path.c_str())) {
      uint16_t d = 0, tm = 0;
      f.getModifyDateTime(&d, &tm);
      builder.add(path, f.fileSize(), ((uint32_t)d << 16) | tm, [&](std::string& out) {
        out.resize(std::min<uint64_t>(f.fileSize(), 96 * 1024));
        int got = f.read(&out[0], out.size());
        out.resize(got > 0 ? got : 0);
        return got >= 0;
      });
    } else if (isImageName(path)) {
      attachments_.push_back(path);
    }
    return true;
  }, [&](const std::string& dir) { folders_.push_back(dir); });
  builder.finish();
  std::sort(folders_.begin(), folders_.end(), lessCaseInsensitive);
  folders_.shrink_to_fit();
  attachments_.shrink_to_fit();
  generation_++;
  Serial.printf("[sd] scanned %u notes in %u ms\n", (unsigned)nidx::count(), (unsigned)(millis() - t));
}

size_t noteCount() { return nidx::count(); }
const std::vector<std::string>& folders() { return folders_; }

uint32_t generation() { return generation_ + nidx::generation(); }

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
  // Keep only the best few (a big vault would not fit in RAM as a full list)
  struct Hit {
    int score;
    std::string path;
  };
  std::vector<Hit> best;
  auto offer = [&](int score, const std::string& path) {
    for (auto& h : best)
      if (h.path == path) {
        if (score < h.score) h.score = score;
        return;
      }
    best.push_back({score, path});
    std::stable_sort(best.begin(), best.end(), [](const Hit& a, const Hit& b) {
      return a.score != b.score ? a.score < b.score : a.path.size() < b.path.size();
    });
    if (best.size() > maxResults) best.pop_back();
  };
  nidx::forEach([&](const nidx::Note& n) {
    int sc = fuzzyScore(baseName(n.path), query);
    if (sc < 0) {
      int ps = fuzzyScore(n.path.substr(1, n.path.size() - 4), query);  // the folder path
      if (ps >= 0) sc = 4 + ps;
    }
    for (auto& a : n.aliases) {  // found by an alias: just after name matches
      int as = fuzzyScore(a, query);
      if (as >= 0 && (sc < 0 || as + 1 < sc)) sc = as + 1;
    }
    if (sc >= 0) offer(sc, n.path);
    return true;
  }, false);
  std::vector<std::string> out;
  for (auto& h : best) out.push_back(h.path);
  return out;
}

std::vector<Hit> backlinks(const std::string& path) {
  // The index lists every note's links with line numbers: only matching notes are opened
  std::vector<Hit> out;
  const uint32_t id = nidx::idOf(path);
  if (id == nidx::kNone) return out;
  nidx::forEach([&](const nidx::Note& n) {
    if (n.path == path) return true;
    for (auto& l : n.links)
      if (nidx::resolveId(l.target, n.path) == id) {
        out.push_back({n.path, l.line, ""});
        break;
      }
    return true;
  });
  for (auto& h : out) {  // fetch the text of each linking line
    std::string text;
    if (!readFile(h.path, text)) continue;
    size_t i = 0;
    for (int k = 0; k < h.line && i != std::string::npos; k++) {
      i = text.find('\n', i);
      if (i != std::string::npos) i++;
    }
    if (i == std::string::npos) continue;
    size_t e = text.find('\n', i);
    h.text = text.substr(i, (e == std::string::npos ? text.size() : e) - i);
    if (h.text.size() > 200) h.text = h.text.substr(0, 200) + "...";
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


std::string linkText(const std::string& path) {
  std::string name = baseName(path);
  if (nidx::countWithName(name) <= 1) return name;
  return path.substr(1, path.size() - 4);  // vault path without leading '/' and ".md"
}

std::string resolveLink(const std::string& rawTarget, const std::string& fromPath) {
  return nidx::resolve(rawTarget, fromPath);
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

// ---------------------------------------------------------------------------
// Images

bool isImageName(const std::string& name) {
  return endsWithCI(name, ".jpg") || endsWithCI(name, ".jpeg") || endsWithCI(name, ".png") || endsWithCI(name, ".bmp");
}

std::string resolveAttachment(const std::string& name, const std::string& fromPath) {
  if (name.empty()) return "";
  if (name.find('/') != std::string::npos) {
    for (const std::string& cand : {name[0] == '/' ? name : joinPath(parentDir(fromPath), name), "/" + name})
      if (exists(cand)) return cand;
    return "";
  }
  std::string same = joinPath(parentDir(fromPath), name), best;
  for (auto& a : attachments_) {
    if (strcasecmp(a.c_str(), same.c_str()) == 0) return a;
    if (endsWithCI(a, "/" + name) && (best.empty() || a.size() < best.size())) best = a;
  }
  return best;
}

bool imageSize(const std::string& path, int* w, int* h) {
  FsFile f;
  if (st != State::Mounted || !f.open(path.c_str(), O_RDONLY)) return false;
  uint8_t b[32];
  bool ok = false;
  if (f.read(b, 26) == 26) {
    if (b[0] == 0x89 && b[1] == 'P') {  // PNG: IHDR width/height (big endian)
      *w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
      *h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
      ok = true;
    } else if (b[0] == 'B' && b[1] == 'M') {  // BMP
      *w = b[18] | (b[19] << 8) | (b[20] << 16) | (b[21] << 24);
      int32_t hh = b[22] | (b[23] << 8) | (b[24] << 16) | (b[25] << 24);
      *h = hh < 0 ? -hh : hh;
      ok = true;
    } else if (b[0] == 0xFF && b[1] == 0xD8) {  // JPEG: walk the markers to a SOFn
      uint32_t pos = 2;
      while (pos + 9 < f.fileSize() && f.seekSet(pos) && f.read(b, 9) == 9 && b[0] == 0xFF) {
        uint8_t m = b[1];
        uint16_t len = (b[2] << 8) | b[3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
          *h = (b[5] << 8) | b[6];
          *w = (b[7] << 8) | b[8];
          ok = true;
          break;
        }
        pos += 2 + len;
      }
    }
  }
  f.close();
  return ok && *w > 0 && *h > 0;
}

namespace {
// Feeds LovyanGFX's image decoders from a file on the card
struct CardReader : public lgfx::DataWrapper {
  FsFile f;
  int read(uint8_t* buf, uint32_t len) override { return f.read(buf, len); }
  void skip(int32_t offset) override { f.seekCur(offset); }
  bool seek(uint32_t offset) override { return f.seekSet(offset); }
  void close() override { f.close(); }
  int32_t tell() override { return f.curPosition(); }
};
}  // namespace

bool drawImage(LovyanGFX& g, const std::string& path, int x, int y, float scale) {
  CardReader r;
  if (st != State::Mounted || !r.f.open(path.c_str(), O_RDONLY)) return false;
  bool ok;
  if (endsWithCI(path, ".png")) ok = g.drawPng(&r, x, y, 0, 0, 0, 0, scale, scale);
  else if (endsWithCI(path, ".bmp")) ok = g.drawBmp(&r, x, y, 0, 0, 0, 0, scale, scale);
  else ok = g.drawJpg(&r, x, y, 0, 0, 0, 0, scale, scale);
  r.close();
  return ok;
}

void createSampleVault() {
  if (st != State::Mounted) return;
  writeFile("/Welcome.md", kWelcome);
  writeFile("/Markdown cheatsheet.md", kCheatsheet);
  writeFile("/Projects/Ideas.md", kIdeas);
  writeFile("/Daily/2026-10-01.md", kDaily);
  rescan();
}

}  // namespace storage
