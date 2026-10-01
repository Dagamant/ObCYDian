#pragma once
// Screen navigation, modal dialogs/menus and toasts.

#include <functional>
#include <string>
#include <vector>

#include "input.h"

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void draw() = 0;
  virtual void onTap(int x, int y) {}
  virtual void onDrag(int dy) {}
  virtual void onDragEnd() {}
  virtual void onKey(const input::Event& e) {}
  // Called before navigating away (the editor saves here).
  virtual void onLeave() {}
  virtual void tick() {}
  virtual int scroll() const { return 0; }
};

namespace app {

enum class SwitcherMode { Open, New, Rename };

void begin();
void loop();
void handle(const input::Event& e);

void openFolder(const std::string& dir, int scroll = 0);
void openNote(const std::string& path, int scroll = 0);  // reading view
// Switches the current note between reading and editing in place, or opens `path` anew.
void editNote(const std::string& path);
void viewNote(const std::string& path);
void openTools();
// Quick switcher / name prompt. `dir` is where new notes go; `path` is the note to rename.
void openSwitcher(SwitcherMode mode, const std::string& dir, const std::string& path = "");
// Closes the switcher and opens `path` (in the editor if `edit`).
void finishSwitcher(const std::string& path, bool edit);
void noteMenu(const std::string& path);
void back();
void home();

// Keep navigation history consistent after a note is renamed or deleted.
void notePathChanged(const std::string& from, const std::string& to);
void noteDeleted(const std::string& path);

void redraw();
void toast(const std::string& msg, uint32_t ms = 2000);
void confirm(const std::string& title, const std::string& message, const char* okLabel,
             uint16_t okColor, std::function<void()> onOk);
// Vertical list of choices; `onPick` gets the chosen index.
void menu(const std::string& title, const std::vector<std::string>& items,
          std::function<void(int)> onPick);

}  // namespace app
