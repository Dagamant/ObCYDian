#pragma once
// Screen navigation, modal dialogs and toasts.

#include <functional>
#include <string>

#include "input.h"

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void draw() = 0;
  virtual void onTap(int x, int y) {}
  virtual void onDrag(int dy) {}
  virtual void onDragEnd() {}
  virtual void tick() {}
  virtual int scroll() const { return 0; }
  virtual void setScroll(int s) {}
};

namespace app {

void begin();
void loop();
void handle(const input::Event& e);

void openFolder(const std::string& dir, int scroll = 0);
void openNote(const std::string& path, int scroll = 0);
void openTools();
void back();
// Replaces the whole navigation history with the vault root.
void home();

void redraw();
void toast(const std::string& msg, uint32_t ms = 2000);
void confirm(const std::string& title, const std::string& message, const char* okLabel,
             uint16_t okColor, std::function<void()> onOk);

}  // namespace app
