#include "vault.h"

#include <Arduino.h>

#include <algorithm>

#include "clock.h"
#include "noteindex.h"

namespace vault {

const char* const kTemplatesDir = "/Templates";
const char* const kDailyDir = "/Daily";

namespace {

constexpr const char* kRecentFile = "/.obcydian/recent.txt";
constexpr const char* kBookmarksFile = "/.obcydian/bookmarks.txt";
constexpr size_t kMaxRecent = 20;

std::vector<std::string> recent_, bookmarks_;

std::vector<std::string> readList(const char* file) {
  std::vector<std::string> out;
  std::string text;
  if (!storage::readFile(file, text)) return out;
  size_t i = 0;
  while (i < text.size()) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(i, e - i);
    if (!line.empty() && line[0] == '/') out.push_back(line);
    i = e + 1;
  }
  return out;
}

void writeList(const char* file, const std::vector<std::string>& list) {
  std::string text;
  for (auto& p : list) text += p + "\n";
  storage::writeFile(file, text);
}

bool inside(const std::string& p, const std::string& dir) {
  return p == dir || (p.size() > dir.size() && p.compare(0, dir.size(), dir) == 0 && p[dir.size()] == '/');
}



std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// Tags

std::vector<Tag> tags() {
  // Straight from the card index: no notes are opened
  std::vector<Tag> out;
  nidx::forEach([&](const nidx::Note& n) {
    for (auto& t : n.tags) {
      bool found = false;
      for (auto& o : out) {
        if (strcasecmp(o.name.c_str(), t.name.c_str()) == 0) {
          if (o.refs.empty() || o.refs.back().first != n.id) o.refs.push_back({n.id, t.line});
          found = true;
          break;
        }
      }
      if (!found) out.push_back({t.name, {{n.id, t.line}}});
    }
    return true;
  });
  std::sort(out.begin(), out.end(), [](const Tag& a, const Tag& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
  return out;
}

std::vector<storage::Hit> tagHits(const Tag& tag) {
  std::vector<storage::Hit> out;
  for (auto& r : tag.refs) {
    std::string path = nidx::pathOf(r.first);
    if (path.empty()) continue;
    std::string text, line;
    storage::readFile(path, text, 16 * 1024);
    size_t i = 0;
    for (int n = 0; n < r.second && i != std::string::npos; n++) {
      i = text.find('\n', i);
      if (i != std::string::npos) i++;
    }
    if (i != std::string::npos) line = trim(text.substr(i, text.find('\n', i) == std::string::npos ? std::string::npos : text.find('\n', i) - i));
    out.push_back({path, r.second, line});
  }
  return out;
}

// ---------------------------------------------------------------------------
// Tasks

// Finds "[ ]"/"[x]" of a list-item task; returns its offset in the line or npos.
static size_t taskBox(const std::string& line) {
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
  if (i + 2 < line.size() + 0 && line[i] == '[' && line[i + 2] == ']' && strchr(" xX", line[i + 1])) return i;
  return std::string::npos;
}

std::vector<Task> tasks(bool includeDone, size_t maxResults) {
  // Tasks are kept in the card index, refreshed whenever a note is saved
  std::vector<Task> out;
  nidx::forEach([&](const nidx::Note& n) {
    for (auto& t : n.tasks) {
      if (t.done && !includeDone) continue;
      if (out.size() >= maxResults) return false;
      out.push_back({n.path, t.line, t.done, t.text});
    }
    return true;
  });
  return out;
}

bool setTaskDone(const std::string& path, int line, bool done) {
  std::string text;
  if (!storage::readFile(path, text)) return false;
  size_t i = 0;
  for (int n = 0; n < line && i != std::string::npos; n++) {
    i = text.find('\n', i);
    if (i != std::string::npos) i++;
  }
  if (i == std::string::npos) return false;
  size_t e = text.find('\n', i);
  std::string l = text.substr(i, (e == std::string::npos ? text.size() : e) - i);
  size_t b = taskBox(l);
  if (b == std::string::npos) return false;
  text[i + b + 1] = done ? 'x' : ' ';
  return storage::writeFile(path, text);
}

// ---------------------------------------------------------------------------
// Recent notes and bookmarks

void load() {
  recent_ = readList(kRecentFile);
  bookmarks_ = readList(kBookmarksFile);
}

void noteOpened(const std::string& path) {
  if (!recent_.empty() && recent_[0] == path) return;
  recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
  recent_.insert(recent_.begin(), path);
  if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
  writeList(kRecentFile, recent_);
}

const std::vector<std::string>& recent() { return recent_; }
const std::vector<std::string>& bookmarks() { return bookmarks_; }

bool isBookmarked(const std::string& path) {
  return std::find(bookmarks_.begin(), bookmarks_.end(), path) != bookmarks_.end();
}

void toggleBookmark(const std::string& path) {
  if (isBookmarked(path)) bookmarks_.erase(std::remove(bookmarks_.begin(), bookmarks_.end(), path), bookmarks_.end());
  else bookmarks_.insert(bookmarks_.begin(), path);
  writeList(kBookmarksFile, bookmarks_);
}

void pathChanged(const std::string& from, const std::string& to) {
  for (auto* list : {&recent_, &bookmarks_}) {
    bool changed = false;
    for (auto& p : *list)
      if (inside(p, from)) p = to + p.substr(from.size()), changed = true;
    if (changed) writeList(list == &recent_ ? kRecentFile : kBookmarksFile, *list);
  }
}

void pathDeleted(const std::string& path) {
  for (auto* list : {&recent_, &bookmarks_}) {
    size_t before = list->size();
    list->erase(std::remove_if(list->begin(), list->end(), [&](const std::string& p) { return inside(p, path); }), list->end());
    if (list->size() != before) writeList(list == &recent_ ? kRecentFile : kBookmarksFile, *list);
  }
}

// ---------------------------------------------------------------------------
// Templates and daily notes

std::vector<std::string> templates() {
  std::vector<std::string> out;
  nidx::forEach([&](const nidx::Note& n) {
    if (inside(n.path, kTemplatesDir)) out.push_back(n.path);
    return true;
  }, false);
  std::sort(out.begin(), out.end());
  return out;
}

std::string applyTemplate(const std::string& templatePath, const std::string& title) {
  std::string t;
  if (!storage::readFile(templatePath, t)) return "";
  std::string out;
  size_t i = 0;
  while (i < t.size()) {
    size_t a = t.find("{{", i);
    if (a == std::string::npos) {
      out += t.substr(i);
      break;
    }
    size_t b = t.find("}}", a + 2);
    if (b == std::string::npos) {
      out += t.substr(i);
      break;
    }
    out += t.substr(i, a - i);
    std::string var = trim(t.substr(a + 2, b - a - 2));
    std::string name = var.substr(0, var.find(':'));
    std::string fmt = var.find(':') == std::string::npos ? "" : var.substr(var.find(':') + 1);
    for (auto& c : name) c = tolower(c);
    if (name == "title") out += title;
    else if (name == "date") out += wallclock::format(fmt.empty() ? "YYYY-MM-DD" : fmt);
    else if (name == "time") out += wallclock::format(fmt.empty() ? "HH:mm" : fmt);
    else out += t.substr(a, b + 2 - a);  // unknown: leave as written
    i = b + 2;
  }
  return out;
}

std::string dailyNote(int offsetDays, bool create) {
  const time_t day = wallclock::now() + (time_t)offsetDays * 86400;
  const std::string name = wallclock::format("YYYY-MM-DD", day);
  const std::string path = std::string(kDailyDir) + "/" + name + ".md";
  if (!create || storage::exists(path)) return path;
  std::string body;
  const std::string tpl = std::string(kTemplatesDir) + "/Daily.md";
  if (storage::exists(tpl)) body = applyTemplate(tpl, name);
  else body = "# " + wallclock::format("dddd, MMMM D, YYYY", day) + "\n\n";
  if (!storage::writeFile(path, body)) return "";
  storage::indexAdd(path);
  return path;
}

std::string adjacentDaily(const std::string& path, int dir) {
  std::vector<std::string> days;
  nidx::forEach([&](const nidx::Note& n) {
    std::string b = storage::baseName(n.path);
    if (storage::parentDir(n.path) == kDailyDir && b.size() == 10 && b[4] == '-' && b[7] == '-') days.push_back(n.path);
    return true;
  }, false);
  std::sort(days.begin(), days.end());
  auto it = std::find(days.begin(), days.end(), path);
  if (it == days.end()) return "";
  if (dir < 0) return it == days.begin() ? "" : *(it - 1);
  return it + 1 == days.end() ? "" : *(it + 1);
}

}  // namespace vault
