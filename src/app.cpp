#include "app.h"

#include <vector>

#include "screens.h"

namespace app {

namespace {

enum class Kind { Folder, Note, Tools };

struct Nav {
  Kind kind;
  std::string path;
  int scroll;
};

BrowserScreen browser;
ViewerScreen viewer;
ToolsScreen tools;

std::vector<Nav> history;
Screen* current = nullptr;

struct Dialog {
  bool active = false;
  std::string title, message, okLabel;
  uint16_t okColor;
  std::function<void()> onOk;
  ui::Rect ok, cancel;
} dialog;

uint32_t toastUntil = 0;

void saveScroll() {
  if (current && !history.empty()) history.back().scroll = current->scroll();
}

void show() {
  const Nav& n = history.back();
  switch (n.kind) {
    case Kind::Folder:
      browser.open(n.path, n.scroll);
      current = &browser;
      break;
    case Kind::Note:
      viewer.open(n.path, n.scroll);
      current = &viewer;
      break;
    case Kind::Tools:
      current = &tools;
      break;
  }
  redraw();
}

void push(Kind k, const std::string& path, int scroll) {
  saveScroll();
  if (history.size() >= 40) history.erase(history.begin());
  history.push_back({k, path, scroll});
  show();
}

void drawDialog() {
  const int w = 380, h = 170;
  const int x = (gfx.width() - w) / 2, y = (gfx.height() - h) / 2;
  gfx.fillRoundRect(x - 2, y - 2, w + 4, h + 4, 10, theme::BORDER);
  gfx.fillRoundRect(x, y, w, h, 9, theme::BG_ALT);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString(dialog.title.c_str(), x + w / 2, y + 18);
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString(dialog.message.c_str(), x + w / 2, y + 58);
  gfx.setTextDatum(textdatum_t::top_left);
  dialog.cancel = {x + 20, y + h - 62, 160, 44};
  dialog.ok = {x + w - 180, y + h - 62, 160, 44};
  ui::button(dialog.cancel, "Cancel", theme::BORDER);
  ui::button(dialog.ok, dialog.okLabel.c_str(), dialog.okColor);
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
  if (dialog.active) {
    if (e.type != input::Type::Tap) return;
    if (dialog.ok.contains(e.x, e.y)) {
      dialog.active = false;
      auto cb = dialog.onOk;
      redraw();
      if (cb) cb();
    } else if (dialog.cancel.contains(e.x, e.y)) {
      dialog.active = false;
      redraw();
    }
    return;
  }
  if (!current) return;
  switch (e.type) {
    case input::Type::Tap: current->onTap(e.x, e.y); break;
    case input::Type::Drag: current->onDrag(e.dy); break;
    case input::Type::DragEnd: current->onDragEnd(); break;
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

void back() {
  if (history.size() > 1) {
    history.pop_back();
    show();
    return;
  }
  Nav& n = history.back();
  if (n.kind == Kind::Note || (n.kind == Kind::Folder && n.path != "/")) {
    n = {Kind::Folder, storage::parentDir(n.path), 0};
    show();
  }
}

void home() {
  history.clear();
  history.push_back({Kind::Folder, "/", 0});
  show();
}

void toast(const std::string& msg, uint32_t ms) {
  gfx.setFont(font::ui());
  int tw = std::min<int>(gfx.textWidth(msg.c_str()) + 32, gfx.width() - 20);
  int x = (gfx.width() - tw) / 2, y = gfx.height() - 52;
  gfx.fillRoundRect(x, y, tw, 36, 8, theme::BORDER);
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(ui::ellipsize(msg, tw - 24, font::ui()).c_str(), gfx.width() / 2, y + 18);
  gfx.setTextDatum(textdatum_t::top_left);
  toastUntil = millis() + ms;
}

void confirm(const std::string& title, const std::string& message, const char* okLabel,
             uint16_t okColor, std::function<void()> onOk) {
  dialog.active = true;
  dialog.title = title;
  dialog.message = message;
  dialog.okLabel = okLabel;
  dialog.okColor = okColor;
  dialog.onOk = std::move(onOk);
  drawDialog();
}

}  // namespace app
