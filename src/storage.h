#pragma once
// microSD access (SdFat on the VSPI bus) and the note vault index.

#include <Print.h>

#include <string>
#include <vector>

namespace storage {

enum class State { NoCard, NoFilesystem, Mounted };

struct Entry {
  std::string name;
  bool isDir;
  uint32_t size;
};

struct CardInfo {
  const char* cardType;  // "SD1", "SD2", "SDHC/SDXC"
  const char* fsType;    // "FAT16", "FAT32", "exFAT", "none"
  uint64_t capacity;     // bytes
};

State begin();
State state();
CardInfo info();
// Free space in bytes. Slow on large FAT32 cards (scans the FAT).
uint64_t freeBytes();

// Erases the card and writes a fresh FAT16/FAT32 (<=32GB) or exFAT filesystem.
// Progress text (dots) from the formatter is written to `progress`.
bool format(Print* progress);

// Folders and .md files in `dir`, folders first, sorted case-insensitively.
std::vector<Entry> list(const std::string& dir);
bool readFile(const std::string& path, std::string& out, size_t maxBytes = 96 * 1024);
bool writeFile(const std::string& path, const std::string& data);
bool exists(const std::string& path);
bool mkdirs(const std::string& path);

// Vault index: every .md path under the root, rebuilt by rescan().
void rescan();
const std::vector<std::string>& notes();
// Obsidian-style link resolution. `target` is the [[link]] text without alias/heading.
// Returns "" if no note matches.
std::string resolveLink(const std::string& target, const std::string& fromPath);

void createSampleVault();

// Path helpers
std::string parentDir(const std::string& path);
std::string baseName(const std::string& path);  // without directory or .md extension
std::string joinPath(const std::string& dir, const std::string& name);

}  // namespace storage
