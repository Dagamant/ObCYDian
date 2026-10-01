#include "app.h"

#include <vector>

#include "btkbd.h"
#include "editor.h"
#include "screens.h"

namespace app {

namespace {

enum class Kind { Folder, Note, Edit, Tools, Switcher, Bluetooth, Wifi, Prompt };

struct Nav {
  Kind kind;
  std::string path;
  int scroll;
};

BrowserScreen browser;
EditorScreen editor;
ToolsScreen tools;
SwitcherScreen switcher;
BluetoothScreen bluetooth;
WifiScreen wifi;
PromptScreen promptScreen;

std::vector<Nav> history;
Screen* current = nullptr;

struct Dialog {
  bool active = false;
  bool isMenu = false;
  std::string title, message, okLabel;
  uint16_t okColor = 0;
  std::function<void()> onOk;
  std::vector<std::string> items;
  std::function<void(int)> onPick;
  int sel = 0;
  ui::Rect ok, cancel;
  std::vector<ui::Rect> itemRects;
} dialog;

uint32_t toastUntil = 0;

void leaveCurrent() {
  if (!current) return;
  current->onLeave();
  if (!history.empty()) history.back().scroll = current->scroll();
}

void show() {
  const Nav& n = history.back();
  switch (n.kind) {
    case Kind::Folder:
      browser.open(n.path, n.scroll);
      current = &browser;
      break;
    case Kind::Note:
    case Kind::Edit:
      editor.open(n.path, n.scroll, n.kind == Kind::Note);
      current = &editor;
      break;
    case Kind::Tools:
      current = &tools;
      break;
    case Kind::Switcher:
      current = &switcher;
      break;
    case Kind::Bluetooth:
      current = &bluetooth;
      break;
    case Kind::Wifi:
      current = &wifi;
      break;
    case Kind::Prompt:
      current = &promptScreen;
      break;
  }
  redraw();
}

void push(Kind k, const std::string& path, int scroll) {
  leaveCurrent();
  if (history.size() >= 40) history.erase(history.begin());
  history.push_back({k, path, scroll});
  show();
}

// Swap reading/editing for the note on top of the stack, keeping the scroll position.
bool switchMode(Kind from, Kind to, const std::string& path) {
  if (history.empty() || history.back().kind != from || history.back().path != path) return false;
  leaveCurrent();
  history.back().kind = to;
  show();
  return true;
}

void drawDialog() {
  const int w = 380;
  const int h = dialog.isMenu ? 64 + dialog.items.size() * 48 + 8 : 170;
  const int x = (gfx.width() - w) / 2, y = (gfx.height() - h) / 2;
  gfx.fillRoundRect(x - 2, y - 2, w + 4, h + 4, 10, theme::BORDER);
  gfx.fillRoundRect(x, y, w, h, 9, theme::BG_ALT);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString(ui::ellipsize(dialog.title, w - 30, font::h2()).c_str(), x + w / 2, y + 18);
  if (dialog.isMenu) {
    dialog.itemRects.clear();
    for (size_t i = 0; i < dialog.items.size(); i++) {
      ui::Rect r{x + 20, y + 60 + (int)i * 48, w - 40, 40};
      dialog.itemRects.push_back(r);
      ui::button(r, dialog.items[i].c_str(), (int)i == dialog.sel ? theme::ACCENT_BG : theme::BORDER);
    }
    return;
  }
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString(ui::ellipsize(dialog.message, w - 30, font::ui()).c_str(), x + w / 2, y + 58);
  gfx.setTextDatum(textdatum_t::top_left);
  dialog.cancel = {x + 20, y + h - 62, 160, 44};
  dialog.ok = {x + w - 180, y + h - 62, 160, 44};
  ui::button(dialog.cancel, "Cancel", theme::BORDER);
  ui::button(dialog.ok, dialog.okLabel.c_str(), dialog.okColor);
}

void closeDialog() {
  dialog.active = false;
  redraw();
}

void dialogInput(const input::Event& e) {
  using namespace input;
  if (dialog.isMenu) {
    int pick = -1;
    if (e.type == Type::Tap) {
      for (size_t i = 0; i < dialog.itemRects.size(); i++)
        if (dialog.itemRects[i].contains(e.x, e.y)) pick = i;
      if (pick < 0) return closeDialog();
    } else if (e.type == Type::Key) {
      int n = dialog.items.size();
      if (e.key == K_ESC) return closeDialog();
      if (e.key == K_UP || e.key == K_DOWN) {
        dialog.sel = (dialog.sel + (e.key == K_UP ? n - 1 : 1)) % n;
        return drawDialog();
      }
      if (e.key == K_ENTER) pick = dialog.sel;
    }
    if (pick >= 0) {
      auto cb = dialog.onPick;
      closeDialog();
      if (cb) cb(pick);
    }
    return;
  }
  bool ok = false, cancel = false;
  if (e.type == Type::Tap) {
    ok = dialog.ok.contains(e.x, e.y);
    cancel = dialog.cancel.contains(e.x, e.y);
  } else if (e.type == Type::Key) {
    ok = e.key == K_ENTER;
    cancel = e.key == K_ESC;
  }
  if (ok) {
    auto cb = dialog.onOk;
    closeDialog();
    if (cb) cb();
  } else if (cancel) {
    closeDialog();
  }
}

std::string contextDir() {
  if (history.empty()) return "/";
  const Nav& n = history.back();
  if (n.kind == Kind::Folder) return n.path;
  if (n.kind == Kind::Note || n.kind == Kind::Edit) return storage::parentDir(n.path);
  return "/";
}

}  // namespace

void begin() {
  history.clear();
  history.push_back({Kind::Folder, "/", 0});
  show();
}

void redraw() {
  if (current) current->draw();
  if (dialog.active) drawDialog();
}

void handle(const input::Event& e) {
  using namespace input;
  if (dialog.active) return dialogInput(e);
  if (!current) return;
  if (e.type == Type::Key && e.ctrl() && e.key == K_CHAR && current != &switcher) {
    if (e.ch == 'o') return openSwitcher(SwitcherMode::Open, contextDir());
    if (e.ch == 'n') return openSwitcher(SwitcherMode::New, contextDir());
  }
  switch (e.type) {
    case Type::Tap: current->onTap(e.x, e.y); break;
    case Type::Drag: current->onDrag(e.dy); break;
    case Type::DragEnd: current->onDragEnd(); break;
    case Type::Key: current->onKey(e); break;
    default: break;
  }
}

void loop() {
  if (toastUntil && millis() > toastUntil) {
    toastUntil = 0;
    redraw();
  }
  if (current) current->tick();
}

void openFolder(const std::string& dir, int scroll) { push(Kind::Folder, dir, scroll); }
void openNote(const std::string& path, int scroll) { push(Kind::Note, path, scroll); }
void openTools() { push(Kind::Tools, "", 0); }
void openBluetooth() { push(Kind::Bluetooth, "", 0); }
void openWifi() { push(Kind::Wifi, "", 0); }

void prompt(const std::string& title, const std::string& hint, const std::string& initial, bool secret,
            std::function<void(const std::string&)> done) {
  promptScreen.open(title, hint, initial, secret, std::move(done));
  push(Kind::Prompt, "", 0);
}

bool keyboardAvailable() { return btkbd::state() == btkbd::State::Connected; }

void editNote(const std::string& path) {
  if (!switchMode(Kind::Note, Kind::Edit, path)) push(Kind::Edit, path, 0);
}

void viewNote(const std::string& path) {
  if (!switchMode(Kind::Edit, Kind::Note, path)) push(Kind::Note, path, 0);
}

void openSwitcher(SwitcherMode mode, const std::string& dir, const std::string& path) {
  if (storage::state() != storage::State::Mounted) return toast("No SD card");
  switcher.open(mode, dir, path);
  push(Kind::Switcher, "", 0);
}

void finishSwitcher(const std::string& path, bool edit) {
  if (!history.empty() && history.back().kind == Kind::Switcher) history.pop_back();
  current = nullptr;  // the switcher has nothing to save
  if (history.empty()) history.push_back({Kind::Folder, "/", 0});
  history.push_back({edit ? Kind::Edit : Kind::Note, path, 0});
  show();
}

void noteMenu(const std::string& path) {
  menu(storage::baseName(path), {"Rename / move...", "Delete note", "New note in this folder"},
       [path](int i) {
         if (i == 0) {
           openSwitcher(SwitcherMode::Rename, storage::parentDir(path), path);
         } else if (i == 1) {
           confirm("Delete note?", storage::baseName(path) + " will be removed from the card.",
                   "Delete", theme::DANGER, [path] {
                     if (current) current->onLeave();
                     if (storage::remove(path)) {
                       noteDeleted(path);
                       toast("Note deleted");
                     } else {
                       toast("Delete failed");
                     }
                   });
         } else if (i == 2) {
           std::string p = storage::createNote(storage::parentDir(path),
                                               storage::baseName(storage::untitledPath(storage::parentDir(path))));
           if (!p.empty()) editNote(p);
         }
       });
}

void back() {
  leaveCurrent();
  if (history.size() > 1) {
    history.pop_back();
    show();
    return;
  }
  Nav& n = history.back();
  if (n.kind != Kind::Folder || n.path != "/") {
    n = {Kind::Folder, n.kind == Kind::Folder ? storage::parentDir(n.path) : storage::parentDir(n.path), 0};
    show();
  }
}

void home() {
  leaveCurrent();
  history.clear();
  history.push_back({Kind::Folder, "/", 0});
  show();
}

void notePathChanged(const std::string& from, const std::string& to) {
  for (auto& n : history)
    if (n.path == from) n.path = to;
}

void externalChange(const std::string& path) {
  if (history.empty() || dialog.active) return;
  const Nav& n = history.back();
  if ((n.kind == Kind::Note || n.kind == Kind::Edit) && n.path == path) {
    editor.reloadIfClean();
  } else if (n.kind == Kind::Folder && storage::parentDir(path) == n.path) {
    browser.open(n.path, browser.scroll());
    browser.draw();
  }
}

void noteDeleted(const std::string& path) {
  std::string dir = storage::parentDir(path);
  std::vector<Nav> kept;
  for (auto& n : history)
    if (n.path != path || n.kind == Kind::Folder) kept.push_back(n);
  history = kept;
  current = nullptr;
  if (history.empty() || history.back().kind == Kind::Switcher) history.push_back({Kind::Folder, dir, 0});
  show();
}

void toast(const std::string& msg, uint32_t ms) {
  gfx.setFont(font::ui());
  int tw = std::min<int>(gfx.textWidth(msg.c_str()) + 32, gfx.width() - 20);
  int x = (gfx.width() - tw) / 2, y = gfx.height() - 60;
  gfx.fillRoundRect(x, y, tw, 36, 8, theme::BORDER);
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(ui::ellipsize(msg, tw - 24, font::ui()).c_str(), gfx.width() / 2, y + 18);
  gfx.setTextDatum(textdatum_t::top_left);
  toastUntil = millis() + ms;
}

void confirm(const std::string& title, const std::string& message, const char* okLabel,
             uint16_t okColor, std::function<void()> onOk) {
  dialog = Dialog();
  dialog.active = true;
  dialog.title = title;
  dialog.message = message;
  dialog.okLabel = okLabel;
  dialog.okColor = okColor;
  dialog.onOk = std::move(onOk);
  drawDialog();
}

void menu(const std::string& title, const std::vector<std::string>& items,
          std::function<void(int)> onPick) {
  dialog = Dialog();
  dialog.active = true;
  dialog.isMenu = true;
  dialog.title = title;
  dialog.items = items;
  dialog.onPick = std::move(onPick);
  drawDialog();
}

}  // namespace app
