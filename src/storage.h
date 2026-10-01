#pragma once
// microSD access (SdFat on the VSPI bus) and the note vault index.

#include <Print.h>

#include "board.h"

#include <functional>
#include <string>
#include <utility>
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
// Writes via a temporary file + rename so a power cut never leaves a half-written note.
bool writeFile(const std::string& path, const std::string& data);
bool exists(const std::string& path);
bool mkdirs(const std::string& path);
int64_t fileSize(const std::string& path);  // -1 if missing
// Every file under `dir` (recursively, including hidden folders), with size and
// modification time (UTC seconds; 0 if unknown). Temporary .tmp files are skipped.
struct FileInfo {
  std::string path;
  uint32_t size;
  uint32_t mtime;
};
std::vector<FileInfo> listAll(const std::string& dir = "/");
// Directory entries of `dir` (all files and folders, including hidden ones)
std::vector<FileInfo> listRaw(const std::string& dir, std::vector<std::string>* subdirs);
bool isDir(const std::string& path);
uint32_t modifiedTime(const std::string& path);  // UTC seconds, 0 if unknown

// Streaming writes for uploads (one at a time). Data goes to a .tmp file that replaces the
// target on finish(), so an interrupted upload never leaves a partial file.
bool writeBegin(const std::string& path);
bool writeChunk(const uint8_t* data, size_t n);
bool writeFinish();
void writeAbort();

// Reads a file of any size in chunks; `sink` returns false to stop early.
bool streamFile(const std::string& path, const std::function<bool(const uint8_t*, size_t)>& sink);
bool remove(const std::string& path);
bool rename(const std::string& from, const std::string& to);
// Renames/moves a note and rewrites [[links]] to it in every other note, like Obsidian.
// Returns false if the rename failed; *linksUpdated gets the number of links rewritten.
bool renameNote(const std::string& from, const std::string& to, int* linksUpdated);

// Folders
int countNotesIn(const std::string& dir);  // recursive
// Renames/moves a folder and rewrites path-qualified [[links]] into it ([[Old/Note]]).
bool renameFolder(const std::string& from, const std::string& to, int* linksUpdated);
// Deletes a folder and everything inside it.
bool removeFolder(const std::string& dir);

// A line in a note
struct Hit {
  std::string path;
  int line;          // 0-based line number of the match
  std::string text;  // that line, trimmed
};
// Full-text search: case-insensitive substring over every note's text.
std::vector<Hit> searchText(const std::string& query, size_t maxResults);
// Notes linking to `path`: the first matching line in each.
std::vector<Hit> backlinks(const std::string& path);

// Replaces characters FAT can't store; trims spaces and dots at the ends.
std::string sanitizeName(const std::string& name);
// Creates an empty note. `nameOrPath` may contain folders ("Projects/Plan"); it is
// relative to `dir`. Returns the new path, or the existing one if the note already exists.
std::string createNote(const std::string& dir, const std::string& nameOrPath);
// First "Untitled", "Untitled 1", ... path not taken in `dir`.
std::string untitledPath(const std::string& dir);

// Vault index: every .md path under the root, rebuilt by rescan().
void rescan();
const std::vector<std::string>& notes();
const std::vector<std::string>& folders();  // every folder path except "/"
// Increments whenever the index changes; lets caches of resolved links invalidate.
uint32_t generation();
// Notes whose name fuzzy-matches `query`, best first (all notes, by name, if query is empty).
std::vector<std::string> search(const std::string& query, size_t maxResults);
// Shortest unambiguous link text for a note: its name, or vault path if names collide.
std::string linkText(const std::string& path);

// Values of a frontmatter list property ("key: [a, b]", "key: a" or a "- a" list).
std::vector<std::string> frontmatterList(const std::string& text, const char* key);
// Aliases from each note's frontmatter (rebuilt by rescan()).
const std::vector<std::pair<std::string, std::string>>& aliases();  // (alias, path)
// Obsidian-style link resolution. `target` is the [[link]] text without alias/heading.
// Returns "" if no note matches.
std::string resolveLink(const std::string& target, const std::string& fromPath);

// --- Images (attachments)
bool isImageName(const std::string& name);  // .jpg/.jpeg/.png/.bmp
// Obsidian-style lookup of an embedded file: a path (relative to the note or the vault)
// or a bare file name found anywhere in the vault. "" if missing.
std::string resolveAttachment(const std::string& name, const std::string& fromPath);
// Pixel size from the file header (JPEG SOF / PNG IHDR / BMP header).
bool imageSize(const std::string& path, int* w, int* h);
// Decodes the image straight from the card onto `g` at (x, y), scaled by `scale`.
bool drawImage(LovyanGFX& g, const std::string& path, int x, int y, float scale);

void createSampleVault();

// Path helpers
std::string parentDir(const std::string& path);
std::string baseName(const std::string& path);  // without directory or .md extension
std::string joinPath(const std::string& dir, const std::string& name);

}  // namespace storage
