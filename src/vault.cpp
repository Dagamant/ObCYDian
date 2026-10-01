#include "vault.h"

#include <Arduino.h>

#include <algorithm>

#include "clock.h"

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

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// Tags

std::vector<Tag> tags() {
  std::vector<Tag> out;
  auto add = [&](std::string name, const std::string& path, int line, const std::string& text) {
    while (!name.empty() && name[0] == '#') name.erase(0, 1);
    if (name.empty()) return;
    for (auto& t : out) {
      if (strcasecmp(t.name.c_str(), name.c_str()) == 0) {
        if (t.hits.empty() || t.hits.back().path != path) t.hits.push_back({path, line, text});
        return;
      }
    }
    out.push_back({name, {{path, line, text}}});
  };
  for (const auto& note : std::vector<std::string>(storage::notes())) {
    std::string text;
    if (!storage::readFile(note, text)) continue;
    for (auto& t : storage::frontmatterList(text, "tags")) add(t, note, 0, "tags: " + t);
    forEachProseLine(text, [&](int n, const std::string& line) {
      bool code = false;
      for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '`') code = !code;
        if (code || line[i] != '#' || (i > 0 && line[i - 1] != ' ' && line[i - 1] != '\t')) continue;
        size_t j = i + 1;
        if (j >= line.size() || !(isalpha((unsigned char)line[j]) || line[j] == '_' || (uint8_t)line[j] >= 0x80)) continue;
        while (j < line.size() && (isalnum((unsigned char)line[j]) || strchr("_-/", line[j]) || (uint8_t)line[j] >= 0x80)) j++;
        add(line.substr(i + 1, j - i - 1), note, n, trim(line));
        i = j;
      }
    });
  }
  std::sort(out.begin(), out.end(), [](const Tag& a, const Tag& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
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

std::vector<Task> tasks(bool includeDone) {
  std::vector<Task> out;
  for (const auto& note : std::vector<std::string>(storage::notes())) {
    std::string text;
    if (!storage::readFile(note, text)) continue;
    forEachProseLine(text, [&](int n, const std::string& line) {
      size_t b = taskBox(line);
      if (b == std::string::npos) return;
      bool done = line[b + 1] != ' ';
      if (done && !includeDone) return;
      out.push_back({note, n, done, trim(line.substr(b + 3))});
    });
  }
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
  for (auto& n : storage::notes())
    if (inside(n, kTemplatesDir) && n != kTemplatesDir) out.push_back(n);
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
  storage::rescan();
  return path;
}

std::string adjacentDaily(const std::string& path, int dir) {
  std::vector<std::string> days;
  for (auto& n : storage::notes()) {
    std::string b = storage::baseName(n);
    if (storage::parentDir(n) == kDailyDir && b.size() == 10 && b[4] == '-' && b[7] == '-') days.push_back(n);
  }
  std::sort(days.begin(), days.end());
  auto it = std::find(days.begin(), days.end(), path);
  if (it == days.end()) return "";
  if (dir < 0) return it == days.begin() ? "" : *(it - 1);
  return it + 1 == days.end() ? "" : *(it + 1);
}

}  // namespace vault
