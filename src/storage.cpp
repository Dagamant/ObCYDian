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
  FsFile f;
  if (!f.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC)) return false;
  size_t n = f.write(data.data(), data.size());
  f.close();
  return n == data.size();
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
  for (auto& s : subdirs) scanDir(s, depth + 1);
}

void rescan() {
  index_.clear();
  if (st != State::Mounted) return;
  uint32_t t = millis();
  scanDir("/", 0);
  std::sort(index_.begin(), index_.end(), lessCaseInsensitive);
  Serial.printf("[sd] indexed %u notes in %lu ms\n", (unsigned)index_.size(), millis() - t);
}

const std::vector<std::string>& notes() { return index_; }

static bool endsWithCI(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         strcasecmp(s.c_str() + s.size() - suffix.size(), suffix.c_str()) == 0;
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

  // Bare name: prefer a note in the same folder, then the shortest path.
  std::string sameDir = joinPath(parentDir(fromPath), target);
  std::string best;
  for (auto& n : index_) {
    if (strcasecmp(n.c_str(), sameDir.c_str()) == 0) return n;
    if (endsWithCI(n, "/" + target) && (best.empty() || n.size() < best.size())) best = n;
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

static const char* kWelcome = R"MD(# Welcome to CYD Notes

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
