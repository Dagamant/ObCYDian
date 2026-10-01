#include "noteindex.h"

#include <Arduino.h>
#include <SdFat.h>

#include <algorithm>

#include "storage.h"

namespace nidx {

namespace {

constexpr const char* kFile = "/.obcydian/index.txt";
constexpr const char* kNewFile = "/.obcydian/index.new";
constexpr size_t kMaxLinks = 300, kMaxTags = 100, kMaxTasks = 200, kMaxField = 200;

struct Slot {
  uint32_t id, nameHash, pathHash, size, mtime;
};
std::vector<Slot> slots_;
std::vector<std::pair<uint32_t, uint32_t>> aliases_;  // (alias hash, note id)
uint32_t gen_ = 0;

// Small cache of id -> path: link resolution asks for the same few paths repeatedly
std::vector<std::pair<uint32_t, std::string>> pathCache_;

uint32_t hashLower(const char* s, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint8_t)tolower((unsigned char)s[i]);
    h *= 16777619u;
  }
  return h;
}
uint32_t hashLower(const std::string& s) { return hashLower(s.data(), s.size()); }
uint32_t nameHashOf(const std::string& path) { return hashLower(storage::baseName(path)); }

bool endsWithCI(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && strcasecmp(s.c_str() + s.size() - suf.size(), suf.c_str()) == 0;
}

std::string field(const std::string& s) {
  std::string o = s.size() > kMaxField ? s.substr(0, kMaxField) : s;
  for (auto& c : o)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return o;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Buffered line reader that knows the file offset where each line starts
class LineReader {
 public:
  explicit LineReader(FsFile& f) : f_(f) { base_ = f_.curPosition(); }
  bool next(std::string& line, uint32_t* start) {
    line.clear();
    *start = base_ + pos_;
    for (;;) {
      if (pos_ >= len_) {
        base_ += len_;
        int n = f_.read(buf_, sizeof(buf_));
        if (n <= 0) return !line.empty();
        len_ = n;
        pos_ = 0;
      }
      char* nl = (char*)memchr(buf_ + pos_, '\n', len_ - pos_);
      size_t end = nl ? nl - buf_ : len_;
      line.append(buf_ + pos_, end - pos_);
      pos_ = end;
      if (nl) {
        pos_++;
        return true;
      }
    }
  }

 private:
  FsFile& f_;
  char buf_[512];
  size_t len_ = 0, pos_ = 0;
  uint32_t base_ = 0;
};

// Splits "X\ta\tb..." into fields after the record letter
std::vector<std::string> fields(const std::string& line) {
  std::vector<std::string> out;
  size_t i = 2;
  while (i <= line.size()) {
    size_t t = line.find('\t', i);
    if (t == std::string::npos) t = line.size();
    out.push_back(line.substr(i, t - i));
    i = t + 1;
  }
  return out;
}

std::string serialize(const Note& n) {
  std::string s = "N\t" + field(n.path) + "\t" + std::to_string(n.size) + "\t" + std::to_string(n.mtime) + "\n";
  for (auto& a : n.aliases) s += "A\t" + field(a) + "\n";
  for (auto& l : n.links) s += "L\t" + std::to_string(l.line) + "\t" + field(l.target) + "\n";
  for (auto& t : n.tags) s += "G\t" + std::to_string(t.line) + "\t" + field(t.name) + "\n";
  for (auto& k : n.tasks) s += "K\t" + std::to_string(k.line) + "\t" + (k.done ? "1" : "0") + "\t" + field(k.text) + "\n";
  return s;
}

// Calls fn(lineNo, line) for each line outside frontmatter and fenced code
template <typename F>
void forEachProseLine(const std::string& text, F fn) {
  size_t i = 0;
  int n = 0;
  bool front = text.compare(0, 4, "---\n") == 0, fence = false;
  while (i <= text.size()) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(i, e - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (front) {
      if (n > 0 && line == "---") front = false;
    } else {
      size_t s = line.find_first_not_of(" \t>");
      bool isFence = s != std::string::npos && (line.compare(s, 3, "```") == 0 || line.compare(s, 3, "~~~") == 0);
      if (isFence) fence = !fence;
      else if (!fence) fn(n, line);
    }
    n++;
    i = e + 1;
  }
}

size_t taskBox(const std::string& line) {
  size_t i = line.find_first_not_of(" \t>");
  if (i == std::string::npos) return std::string::npos;
  if (strchr("-*+", line[i]) && i + 1 < line.size() && line[i + 1] == ' ') {
    i += 2;
  } else {
    size_t d = i;
    while (d < line.size() && isdigit((unsigned char)line[d])) d++;
    if (d == i || d + 1 >= line.size() || (line[d] != '.' && line[d] != ')') || line[d + 1] != ' ') return std::string::npos;
    i = d + 2;
  }
  if (i + 2 < line.size() && line[i] == '[' && line[i + 2] == ']' && strchr(" xX", line[i + 1])) return i;
  return std::string::npos;
}

void cachePath(uint32_t id, const std::string& p) {
  if (pathCache_.size() >= 32) pathCache_.erase(pathCache_.begin());
  pathCache_.push_back({id, p});
}

const Slot* findPath(const std::string& path) {
  uint32_t h = hashLower(path);
  for (auto& s : slots_)
    if (s.pathHash == h) return &s;
  return nullptr;
}

void markDead(uint32_t id) {
  FsFile f;
  if (!f.open(kFile, O_RDWR)) return;
  f.seekSet(id);
  f.write('X');
  f.close();
}

}  // namespace

// ---------------------------------------------------------------------------
// Parsing

void parse(const std::string& path, const std::string& text, Note& n) {
  n.path = path;
  n.aliases = storage::frontmatterList(text, "aliases");
  for (auto& a : storage::frontmatterList(text, "alias")) n.aliases.push_back(a);
  n.links.clear();
  n.tags.clear();
  n.tasks.clear();
  for (auto& t : storage::frontmatterList(text, "tags"))
    if (n.tags.size() < kMaxTags) n.tags.push_back({0, t[0] == '#' ? t.substr(1) : t});

  forEachProseLine(text, [&](int ln, const std::string& line) {
    // [[links]] (and ![[embeds]] of notes)
    for (size_t a = line.find("[["); a != std::string::npos && n.links.size() < kMaxLinks; a = line.find("[[", a + 2)) {
      size_t b = line.find("]]", a + 2);
      if (b == std::string::npos) break;
      std::string t = line.substr(a + 2, b - a - 2);
      t = t.substr(0, t.find('|'));
      t = t.substr(0, t.find('#'));
      if (!t.empty() && !storage::isImageName(t)) n.links.push_back({(uint16_t)ln, t});
    }
    // [text](relative/note.md) links
    for (size_t a = line.find("]("); a != std::string::npos && n.links.size() < kMaxLinks; a = line.find("](", a + 2)) {
      size_t e = line.find(')', a + 2);
      if (e == std::string::npos) break;
      std::string url = line.substr(a + 2, e - a - 2);
      if (url.find("://") != std::string::npos || url.empty() || storage::isImageName(url)) continue;
      std::string dec;
      for (size_t i = 0; i < url.size(); i++) {
        if (url[i] == '%' && i + 2 < url.size()) dec += (char)strtol(url.substr(i + 1, 2).c_str(), nullptr, 16), i += 2;
        else dec += url[i];
      }
      dec = dec.substr(0, dec.find('#'));
      if (!dec.empty()) n.links.push_back({(uint16_t)ln, dec});
    }
    // #tags
    bool code = false;
    for (size_t i = 0; i < line.size() && n.tags.size() < kMaxTags; i++) {
      if (line[i] == '`') code = !code;
      if (code || line[i] != '#' || (i > 0 && line[i - 1] != ' ' && line[i - 1] != '\t')) continue;
      size_t j = i + 1;
      if (j >= line.size() || !(isalpha((unsigned char)line[j]) || line[j] == '_' || (uint8_t)line[j] >= 0x80)) continue;
      while (j < line.size() && (isalnum((unsigned char)line[j]) || strchr("_-/", line[j]) || (uint8_t)line[j] >= 0x80)) j++;
      n.tags.push_back({(uint16_t)ln, line.substr(i + 1, j - i - 1)});
      i = j;
    }
    // - [ ] tasks
    size_t box = taskBox(line);
    if (box != std::string::npos && n.tasks.size() < kMaxTasks)
      n.tasks.push_back({(uint16_t)ln, line[box + 1] != ' ', trim(box + 3 < line.size() ? line.substr(box + 3) : "")});
  });
}

// ---------------------------------------------------------------------------
// Building

struct Builder::Impl {
  struct Old {
    uint32_t pathHash, size, mtime, off;
  };
  std::vector<Old> old;
  FsFile oldF, newF;
  std::vector<Slot> slots;
  std::vector<std::pair<uint32_t, uint32_t>> aliases;
  uint32_t reused = 0, parsed = 0, t0 = 0;
  bool ok = false;
};

Builder::Builder() : p_(new Impl) {
  p_->t0 = millis();
  storage::mkdirs("/.obcydian");
  if (p_->oldF.open(kFile, O_RDONLY)) {
    LineReader r(p_->oldF);
    std::string line;
    uint32_t at;
    while (r.next(line, &at)) {
      if (line.size() < 3 || line[0] != 'N') continue;
      auto f = fields(line);
      if (f.size() >= 3) p_->old.push_back({hashLower(f[0]), (uint32_t)strtoul(f[1].c_str(), nullptr, 10), (uint32_t)strtoul(f[2].c_str(), nullptr, 10), at});
    }
    std::sort(p_->old.begin(), p_->old.end(), [](const Impl::Old& a, const Impl::Old& b) { return a.pathHash < b.pathHash; });
  }
  p_->ok = p_->newF.open(kNewFile, O_WRONLY | O_CREAT | O_TRUNC);
}

Builder::~Builder() {
  if (p_->newF.isOpen()) p_->newF.close();
  if (p_->oldF.isOpen()) p_->oldF.close();
  delete p_;
}

void Builder::add(const std::string& path, uint32_t size, uint32_t mtime, const std::function<bool(std::string&)>& read) {
  if (!p_->ok) return;
  const uint32_t h = hashLower(path);
  const uint32_t off = p_->newF.curPosition();
  std::vector<std::string> aliases;
  bool copied = false;

  // Unchanged since the last index? Copy its record instead of re-reading the note.
  auto it = std::lower_bound(p_->old.begin(), p_->old.end(), h, [](const Impl::Old& o, uint32_t v) { return o.pathHash < v; });
  for (; it != p_->old.end() && it->pathHash == h && !copied; ++it) {
    if (it->size != size || it->mtime != mtime || !p_->oldF.seekSet(it->off)) continue;
    LineReader r(p_->oldF);
    std::string line, block;
    uint32_t at;
    bool first = true;
    while (r.next(line, &at)) {
      if (first) {
        auto f = fields(line);
        if (line[0] != 'N' || f.empty() || f[0] != path) break;  // hash collision: not ours
        first = false;
      } else if (line[0] == 'N' || line[0] == 'X') {
        break;
      }
      if (line[0] == 'A' && line.size() > 2) aliases.push_back(line.substr(2));
      block += line + "\n";
    }
    if (!first) {
      p_->newF.write(block.data(), block.size());
      copied = true;
      p_->reused++;
    }
  }
  if (!copied) {
    std::string text;
    if (!read(text)) text.clear();
    Note n;
    parse(path, text, n);
    n.size = size;
    n.mtime = mtime;
    std::string rec = serialize(n);
    p_->newF.write(rec.data(), rec.size());
    aliases = n.aliases;
    p_->parsed++;
  }
  p_->slots.push_back({off, nameHashOf(path), h, size, mtime});
  for (auto& a : aliases) p_->aliases.push_back({hashLower(a), off});
}

void Builder::finish() {
  if (!p_->ok) return;
  p_->newF.close();
  p_->oldF.close();
  storage::rawRemove(kFile);
  storage::rawRename(kNewFile, kFile);
  slots_ = std::move(p_->slots);
  aliases_ = std::move(p_->aliases);
  slots_.shrink_to_fit();
  aliases_.shrink_to_fit();
  pathCache_.clear();
  gen_++;
  Serial.printf("[index] %u notes (%u unchanged, %u read) in %u ms\n", (unsigned)slots_.size(), (unsigned)p_->reused,
                (unsigned)p_->parsed, (unsigned)(millis() - p_->t0));
}

// ---------------------------------------------------------------------------
// Updates

void update(const std::string& path, const std::string& text) {
  Note n;
  parse(path, text, n);
  n.size = text.size();
  n.mtime = storage::modifiedStamp(path);
  remove(path);
  FsFile f;
  if (!f.open(kFile, O_RDWR | O_CREAT)) return;
  f.seekEnd();
  const uint32_t off = f.curPosition();
  std::string rec = serialize(n);
  f.write(rec.data(), rec.size());
  f.close();
  slots_.push_back({off, nameHashOf(path), hashLower(path), n.size, n.mtime});
  for (auto& a : n.aliases) aliases_.push_back({hashLower(a), off});
  gen_++;
}

void remove(const std::string& path) {
  const Slot* s = findPath(path);
  if (!s) return;
  const uint32_t id = s->id;
  markDead(id);
  slots_.erase(slots_.begin() + (s - slots_.data()));
  aliases_.erase(std::remove_if(aliases_.begin(), aliases_.end(), [&](auto& a) { return a.second == id; }), aliases_.end());
  pathCache_.erase(std::remove_if(pathCache_.begin(), pathCache_.end(), [&](auto& c) { return c.first == id; }), pathCache_.end());
  gen_++;
}

void clear() {
  slots_.clear();
  aliases_.clear();
  pathCache_.clear();
  gen_++;
}

size_t count() { return slots_.size(); }
uint32_t generation() { return gen_; }

// ---------------------------------------------------------------------------
// Reading

void forEach(const std::function<bool(const Note&)>& fn, bool details) {
  FsFile f;
  if (!f.open(kFile, O_RDONLY)) return;
  LineReader r(f);
  std::string line;
  uint32_t at;
  Note n;
  bool live = false;
  auto emit = [&]() { return !live || fn(n); };
  while (r.next(line, &at)) {
    if (line.empty()) continue;
    char k = line[0];
    if (k == 'N' || k == 'X') {
      if (!emit()) return;
      live = k == 'N';
      if (!live) continue;
      auto fl = fields(line);
      n = Note();
      n.id = at;
      n.path = fl.size() > 0 ? fl[0] : "";
      n.size = fl.size() > 1 ? strtoul(fl[1].c_str(), nullptr, 10) : 0;
      n.mtime = fl.size() > 2 ? strtoul(fl[2].c_str(), nullptr, 10) : 0;
      continue;
    }
    if (!live || line.size() < 3) continue;
    if (k == 'A') {
      n.aliases.push_back(line.substr(2));
    } else if (details) {
      auto fl = fields(line);
      if (k == 'L' && fl.size() >= 2) n.links.push_back({(uint16_t)atoi(fl[0].c_str()), fl[1]});
      else if (k == 'G' && fl.size() >= 2) n.tags.push_back({(uint16_t)atoi(fl[0].c_str()), fl[1]});
      else if (k == 'K' && fl.size() >= 3) n.tasks.push_back({(uint16_t)atoi(fl[0].c_str()), fl[1] == "1", fl[2]});
    }
  }
  emit();
}

std::string pathOf(uint32_t id) {
  for (auto& c : pathCache_)
    if (c.first == id) return c.second;
  FsFile f;
  if (!f.open(kFile, O_RDONLY) || !f.seekSet(id)) return "";
  LineReader r(f);
  std::string line;
  uint32_t at;
  if (!r.next(line, &at) || line.size() < 3 || line[0] != 'N') return "";
  std::string p = fields(line)[0];
  cachePath(id, p);
  return p;
}

uint32_t idOf(const std::string& path) {
  const Slot* s = findPath(path);
  return s ? s->id : kNone;
}

int countWithName(const std::string& name) {
  uint32_t h = hashLower(name);
  int n = 0;
  for (auto& s : slots_)
    if (s.nameHash == h) n++;
  return n;
}

std::string resolve(const std::string& rawTarget, const std::string& fromPath) {
  if (trim(rawTarget).empty()) return fromPath;  // [[#heading]] links to the current note
  uint32_t id = resolveId(rawTarget, fromPath);
  return id == kNone ? "" : pathOf(id);
}

uint32_t resolveId(const std::string& rawTarget, const std::string& fromPath) {
  std::string t = trim(rawTarget);
  if (t.empty()) {
    const Slot* s = findPath(fromPath);
    return s ? s->id : kNone;
  }
  if (!endsWithCI(t, ".md")) t += ".md";
  const uint32_t nh = nameHashOf(t);

  if (t.find('/') != std::string::npos) {
    // A path: relative to the note, then from the vault root, then as a path suffix
    std::string abs = t[0] == '/' ? t : storage::joinPath(storage::parentDir(fromPath), t);
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
    if (const Slot* s = findPath(norm)) return s->id;
    const std::string suffix = t[0] == '/' ? t : "/" + t;
    for (auto& s : slots_) {
      if (s.nameHash != nh) continue;
      if (endsWithCI(pathOf(s.id), suffix)) return s.id;
    }
    return kNone;
  }

  // A bare name: the note in the same folder wins, then the shortest path, then an alias
  const Slot* only = nullptr;
  int matches = 0;
  for (auto& s : slots_)
    if (s.nameHash == nh) only = &s, matches++;
  if (matches == 1) return only->id;  // the common case: no need to read any path
  if (matches > 1) {
    const std::string same = storage::joinPath(storage::parentDir(fromPath), t);
    std::string best;
    uint32_t bestId = kNone;
    for (auto& s : slots_) {
      if (s.nameHash != nh) continue;
      std::string p = pathOf(s.id);
      if (strcasecmp(p.c_str(), same.c_str()) == 0) return s.id;
      if (best.empty() || p.size() < best.size()) best = p, bestId = s.id;
    }
    return bestId;
  }
  const uint32_t ah = hashLower(t.substr(0, t.size() - 3));
  for (auto& a : aliases_)
    if (a.first == ah) return a.second;
  return kNone;
}

}  // namespace nidx
