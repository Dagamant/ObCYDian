#pragma once
// Note index kept on the card (/.obcydian/index.txt) so a large vault costs little RAM.
//
// The file holds one record per note: path, size, modification time, and what the rest of
// the app needs without opening the note: aliases, outgoing links, tags and tasks (each
// with its line number). RAM only keeps ~20 bytes per note: where the record is, hashes of
// its name and path (for link resolution), and its size/time (to spot changed notes).
//
// rebuild() walks the card at startup and re-reads only notes whose size or time changed;
// unchanged records are copied from the previous index. While running, a changed note gets
// a fresh record appended and its old one marked dead (one byte); dead records disappear at
// the next rebuild.

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

namespace nidx {

struct Link {
  uint16_t line;
  std::string target;  // as written, without |alias or #heading
};
struct Tag {
  uint16_t line;
  std::string name;  // without '#'
};
struct Task {
  uint16_t line;
  bool done;
  std::string text;
};
struct Note {
  uint32_t id;  // record offset in the index file (valid until the next rebuild)
  std::string path;
  uint32_t size = 0, mtime = 0;  // mtime: raw FAT date/time stamp (only compared for changes)
  std::vector<std::string> aliases;
  std::vector<Link> links;
  std::vector<Tag> tags;
  std::vector<Task> tasks;
};

// Builds a fresh index while storage walks the card (see storage::rescan).
class Builder {
 public:
  Builder();
  ~Builder();
  // `read` fills the note's text; it is only called if the note changed since last time.
  void add(const std::string& path, uint32_t size, uint32_t mtime, const std::function<bool(std::string&)>& read);
  void finish();

 private:
  struct Impl;
  Impl* p_;
};

// (Re)indexes one note from its current text (after any write).
void update(const std::string& path, const std::string& text);
void remove(const std::string& path);
void clear();  // no card

size_t count();
uint32_t generation();

// Streams every live note in index order; return false from fn to stop early. With
// details=false only path/size/mtime/aliases are filled in (faster).
void forEach(const std::function<bool(const Note&)>& fn, bool details = true);
std::string pathOf(uint32_t id);

// Obsidian link resolution: "" if no note matches.
std::string resolve(const std::string& target, const std::string& fromPath);
// Same, returning the note's id (kNone if none); reads no paths when the name is unique.
constexpr uint32_t kNone = 0xFFFFFFFF;
uint32_t resolveId(const std::string& target, const std::string& fromPath);
uint32_t idOf(const std::string& path);  // kNone if not indexed
// How many notes share this name (case-insensitive, without .md)
int countWithName(const std::string& name);

// Parses a note's text into a record (exposed for tests/tools).
void parse(const std::string& path, const std::string& text, Note& out);

}  // namespace nidx
