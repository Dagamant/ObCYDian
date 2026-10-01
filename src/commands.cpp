// Command palette and the list views built on PickerScreen: outline, backlinks, tags, tasks,
// recent notes, bookmarks, templates, plus daily notes and shared note/folder actions.

#include "app.h"
#include "btkbd.h"
#include "clock.h"
#include "osk.h"
#include "power.h"
#include "radio.h"
#include "screens.h"
#include "vault.h"

namespace app {

namespace {

// Whole-vault scans take a moment on big vaults: say so before starting
void busy(const char* what) { toast(what, 400); }

std::string folderLabel(const std::string& path) {
  std::string dir = storage::parentDir(path);
  return dir == "/" ? "" : dir.substr(1);
}

PickItem noteItem(const std::string& path, const std::string& sub = "") {
  PickItem it;
  it.label = storage::baseName(path);
  it.detail = folderLabel(path);
  it.sub = sub;
  return it;
}

// A list of notes that open when picked
void notesList(const std::string& title, const std::string& empty, const std::vector<std::string>& paths) {
  PickerData d;
  d.title = title;
  d.empty = empty;
  for (auto& p : paths) d.items.push_back(noteItem(p));
  d.onPick = [paths](int i) { openNote(paths[i]); };
  pick(std::move(d));
}

// A list of lines in notes that open the note at that line
void hitsList(const std::string& title, const std::string& empty, const std::vector<storage::Hit>& hits) {
  PickerData d;
  d.title = title;
  d.empty = empty;
  for (auto& h : hits) d.items.push_back(noteItem(h.path, h.text));
  d.onPick = [hits](int i) { openNoteAt(hits[i].path, hits[i].line); };
  pick(std::move(d));
}

void showOutline(const std::string& path) {
  std::string text = noteText();
  if (text.empty()) storage::readFile(path, text);
  PickerData d;
  d.title = "Outline: " + storage::baseName(path);
  d.empty = "This note has no headings";
  std::vector<int> lines;
  bool fence = false;
  int n = 0;
  for (size_t i = 0; i <= text.size(); n++) {
    size_t e = text.find('\n', i);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(i, e - i);
    if (line.rfind("```", 0) == 0 || line.rfind("~~~", 0) == 0) fence = !fence;
    size_t h = 0;
    while (h < line.size() && line[h] == '#') h++;
    if (!fence && h >= 1 && h <= 6 && h < line.size() && line[h] == ' ') {
      PickItem it;
      it.label = line.substr(h + 1);
      it.indent = (h - 1) * 18;
      d.items.push_back(it);
      lines.push_back(n);
    }
    i = e + 1;
  }
  d.closeOnPick = true;
  d.onPick = [lines](int i) { revealLine(lines[i]); };
  pick(std::move(d));
}

void showTags() {
  busy("Collecting tags...");
  auto tags = vault::tags();
  PickerData d;
  d.title = "Tags";
  d.empty = "No #tags in the vault yet";
  d.filterable = tags.size() > 8;
  for (auto& t : tags) {
    PickItem it;
    it.label = "#" + t.name;
    it.detail = std::to_string(t.refs.size()) + (t.refs.size() == 1 ? " note" : " notes");
    d.items.push_back(it);
  }
  d.onPick = [tags](int i) { hitsList("#" + tags[i].name, "", vault::tagHits(tags[i])); };
  pick(std::move(d));
}

void showTasks() {
  busy("Collecting tasks...");
  auto tasks = std::make_shared<std::vector<vault::Task>>(vault::tasks(false));
  PickerData d;
  d.title = "Open tasks (" + std::to_string(tasks->size()) + (tasks->size() >= 200 ? "+" : "") + ")";
  d.empty = "No open tasks. Nice!";
  for (auto& t : *tasks) {
    PickItem it = noteItem(t.path);
    it.label = t.text.empty() ? "(empty task)" : t.text;
    it.detail = storage::baseName(t.path);
    it.check = 0;
    d.items.push_back(it);
  }
  d.onPick = [tasks](int i) { openNoteAt((*tasks)[i].path, (*tasks)[i].line); };
  d.onToggle = [tasks](int i) {
    auto& t = (*tasks)[i];
    if (!vault::setTaskDone(t.path, t.line, !t.done)) {
      toast("Couldn't update that task");
      return t.done ? 1 : 0;
    }
    t.done = !t.done;
    externalChange(t.path);
    return t.done ? 1 : 0;
  };
  pick(std::move(d));
}

void insertTemplate() {
  auto tpls = vault::templates();
  if (tpls.empty()) return toast(std::string("Put templates in ") + vault::kTemplatesDir + "/", 3000);
  Context c = context();
  PickerData d;
  d.title = "Insert template";
  for (auto& t : tpls) d.items.push_back(noteItem(t));
  d.closeOnPick = true;
  d.onPick = [tpls, c](int i) {
    std::string text = vault::applyTemplate(tpls[i], storage::baseName(c.path));
    if (!c.editing) editNote(c.path);
    insertIntoNote(text);
  };
  pick(std::move(d));
}

struct Cmd {
  std::string name, hint;
  std::function<void()> run;
};

}  // namespace

// ---------------------------------------------------------------------------
// Shared actions

void newNoteIn(const std::string& dir) {
  std::string p = storage::createNote(dir, storage::baseName(storage::untitledPath(dir)));
  if (p.empty()) return toast("Couldn't create note");
  editNote(p);
}

void newFolderIn(const std::string& dir) {
  std::string where = dir == "/" ? "the vault" : storage::baseName(dir);
  prompt("New folder", "Name for a new folder in " + where, "", false, [dir](const std::string& n) {
    std::string clean = storage::sanitizeName(n);
    if (clean.empty()) return;
    std::string path = storage::joinPath(dir, clean);
    if (storage::exists(path)) return toast("That folder already exists");
    if (!storage::mkdirs(path)) return toast("Couldn't create folder");
    storage::rescan();
    openFolder(path);
  });
}

void renameFolderPrompt(const std::string& dir) {
  prompt("Rename folder", "New name (use / to move it, e.g. Archive/2026)", storage::baseName(dir), false,
         [dir](const std::string& n) {
           if (n.empty()) return;
           // A plain name stays in the same parent; a path is from the vault root
           std::string to;
           if (n.find('/') == std::string::npos) {
             to = storage::joinPath(storage::parentDir(dir), storage::sanitizeName(n));
           } else {
             size_t i = 0;
             while (i <= n.size()) {
               size_t j = n.find('/', i);
               if (j == std::string::npos) j = n.size();
               std::string seg = storage::sanitizeName(n.substr(i, j - i));
               if (!seg.empty()) to += "/" + seg;
               i = j + 1;
             }
           }
           if (to.empty() || to == dir) return;
           if (storage::exists(to)) return toast("Something with that name exists");
           int links = 0;
           saveCurrent();
           if (!storage::renameFolder(dir, to, &links)) return toast("Rename failed");
           folderPathChanged(dir, to);
           if (links) toast("Updated " + std::to_string(links) + (links == 1 ? " link" : " links"));
         });
}

void deleteFolderConfirm(const std::string& dir) {
  int n = storage::countNotesIn(dir);
  std::string what = n ? "It holds " + std::to_string(n) + (n == 1 ? " note" : " notes") + ", also deleted." : "The folder is empty.";
  confirm("Delete " + tf::printable(storage::baseName(dir)) + "?", what, "Delete", theme::DANGER, [dir] {
    saveCurrent();
    bool ok = storage::removeFolder(dir);
    folderDeleted(dir);
    toast(ok ? "Folder deleted" : "Couldn't delete everything");
  });
}

void deleteNoteConfirm(const std::string& path) {
  confirm("Delete note?", storage::baseName(path) + " will be removed from the card.", "Delete", theme::DANGER, [path] {
    saveCurrent();
    if (storage::remove(path)) {
      noteDeleted(path);
      toast("Note deleted");
    } else {
      toast("Delete failed");
    }
  });
}

void openDailyNote(int offsetDays) {
  auto open = [offsetDays] {
    std::string p = vault::dailyNote(offsetDays, true);
    if (p.empty()) return toast("Couldn't create the daily note");
    editNote(p);
  };
  if (wallclock::valid()) return open();
  // No trustworthy clock (no WiFi since power-on): ask for the date
  prompt("Today's date", "The clock isn't set. Enter today's date as YYYY-MM-DD", wallclock::format("YYYY-MM-DD"), false,
         [open](const std::string& d) {
           if (!wallclock::setDate(d)) return toast("Use the form 2026-10-01");
           open();
         });
}

// ---------------------------------------------------------------------------
// Command palette

void commandPalette(bool filter) {
  const Context c = context();
  std::vector<Cmd> cmds;
  if (c.where == Context::Note) {
    const std::string p = c.path;
    cmds.push_back({c.editing ? "Reading view" : "Edit note", "Ctrl+E", [p, c] { c.editing ? viewNote(p) : editNote(p); }});
    cmds.push_back({"Outline", "", [p] { showOutline(p); }});
    cmds.push_back({"Backlinks", "", [p] {
                      busy("Finding backlinks...");
                      hitsList("Backlinks: " + storage::baseName(p), "No other notes link here", storage::backlinks(p));
                    }});
    cmds.push_back({vault::isBookmarked(p) ? "Remove bookmark" : "Bookmark this note", "", [p] {
                      vault::toggleBookmark(p);
                      toast(vault::isBookmarked(p) ? "Bookmarked" : "Bookmark removed");
                    }});
    cmds.push_back({"Insert template", "", [] { insertTemplate(); }});
    if (storage::parentDir(p) == vault::kDailyDir) {
      cmds.push_back({"Previous daily note", "", [p] {
                        std::string q = vault::adjacentDaily(p, -1);
                        q.empty() ? toast("No earlier daily note") : editNote(q);
                      }});
      cmds.push_back({"Next daily note", "", [p] {
                        std::string q = vault::adjacentDaily(p, 1);
                        q.empty() ? toast("No later daily note") : editNote(q);
                      }});
    }
    cmds.push_back({"Rename / move note", "F2", [p] { openSwitcher(SwitcherMode::Rename, storage::parentDir(p), p); }});
    cmds.push_back({"Delete note", "", [p] { deleteNoteConfirm(p); }});
    cmds.push_back({"New note in this folder", "", [p] { newNoteIn(storage::parentDir(p)); }});
  } else if (c.where == Context::Folder) {
    const std::string dir = c.path;
    cmds.push_back({"New note here", "", [dir] { newNoteIn(dir); }});
    cmds.push_back({"New folder here", "", [dir] { newFolderIn(dir); }});
    if (dir != "/") {
      cmds.push_back({"Rename / move folder", "", [dir] { renameFolderPrompt(dir); }});
      cmds.push_back({"Delete folder", "", [dir] { deleteFolderConfirm(dir); }});
    }
  }
  cmds.push_back({"Today's daily note", "Ctrl+D", [] { openDailyNote(0); }});
  cmds.push_back({"Open note...", "Ctrl+O", [] { openSwitcher(SwitcherMode::Open, "/"); }});
  cmds.push_back({"Search note text", "", [] { openSearch(); }});
  cmds.push_back({"Bookmarks", "", [] { notesList("Bookmarks", "Bookmark notes from their menu", vault::bookmarks()); }});
  cmds.push_back({"Recent notes", "", [] { notesList("Recent notes", "Nothing opened yet", vault::recent()); }});
  cmds.push_back({"Tags", "", [] { showTags(); }});
  cmds.push_back({"Open tasks", "", [] { showTasks(); }});
  cmds.push_back({"New note...", "Ctrl+N", [c] { openSwitcher(SwitcherMode::New, c.where == Context::Folder ? c.path : "/"); }});
  cmds.push_back({"Go to vault", "", [] { home(); }});
  cmds.push_back({"Settings", "", [] { openTools(); }});
  if (radio::mode() == radio::Mode::Wifi) cmds.push_back({"Switch to Bluetooth keyboard", "", [] { radio::switchTo(radio::Mode::Bluetooth); }});
  else cmds.push_back({"Switch to WiFi web server", "", [] { radio::switchTo(radio::Mode::Wifi); }});
  cmds.push_back({"Set today's date", "", [] {
                    prompt("Today's date", "YYYY-MM-DD", wallclock::format("YYYY-MM-DD"), false, [](const std::string& d) {
                      toast(wallclock::setDate(d) ? "Date set" : "Use the form 2026-10-01");
                    });
                  }});
  cmds.push_back({"Screen off", "", [] { power::screenOff(); }});
  cmds.push_back({"Sleep now", "", [] { power::deepSleep(); }});

  PickerData d;
  d.title = filter ? "Command palette" : c.where == Context::Note ? storage::baseName(c.path)
                                       : c.where == Context::Folder ? (c.path == "/" ? "Vault" : storage::baseName(c.path))
                                                                    : "Commands";
  d.filterable = filter;
  d.closeOnPick = true;
  auto actions = std::make_shared<std::vector<std::function<void()>>>();
  for (auto& cmd : cmds) {
    PickItem it;
    it.label = cmd.name;
    it.detail = cmd.hint;
    d.items.push_back(it);
    actions->push_back(cmd.run);
  }
  d.onPick = [actions](int i) { (*actions)[i](); };
  pick(std::move(d));
}

}  // namespace app
