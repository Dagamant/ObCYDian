#pragma once
// Vault-wide features built on storage: tags, tasks, recent notes, bookmarks, templates and
// daily notes. ObCYDian's own state lives in the hidden /.obcydian folder on the card.

#include <string>
#include <vector>

#include "storage.h"

namespace vault {

// --- Tags (inline #tags and frontmatter "tags:")
struct Tag {
  std::string name;  // without '#'
  std::vector<storage::Hit> hits;
};
std::vector<Tag> tags();  // sorted by name

// --- Tasks ("- [ ] ..." list items)
struct Task {
  std::string path;
  int line;
  bool done;
  std::string text;  // without the "- [ ]" marker
};
std::vector<Task> tasks(bool includeDone);
// Ticks or unticks the task on `line` of `path`. Returns false if that line isn't a task.
bool setTaskDone(const std::string& path, int line, bool done);

// --- Recent notes and bookmarks (most recent first)
void load();  // after the card is mounted
void noteOpened(const std::string& path);
const std::vector<std::string>& recent();
const std::vector<std::string>& bookmarks();
bool isBookmarked(const std::string& path);
void toggleBookmark(const std::string& path);
// Keeps the lists in step with renames and deletes
void pathChanged(const std::string& from, const std::string& to);  // note or folder
void pathDeleted(const std::string& path);

// --- Templates (notes in /Templates) and daily notes (/Daily/YYYY-MM-DD.md)
extern const char* const kTemplatesDir;
extern const char* const kDailyDir;
std::vector<std::string> templates();
// Template text with {{title}}, {{date}}, {{time}} and {{date:FORMAT}} filled in.
std::string applyTemplate(const std::string& templatePath, const std::string& title);
// Path of the daily note for the day `offsetDays` from today; creates it (from
// Templates/Daily.md if present) when `create` is set.
std::string dailyNote(int offsetDays, bool create);
// If `path` is a daily note, the neighbouring existing daily note (dir -1 or +1).
std::string adjacentDaily(const std::string& path, int dir);

}  // namespace vault
