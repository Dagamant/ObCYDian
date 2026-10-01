#pragma once
// Small immediate-mode widgets shared by the screens.

#include <string>

#include "display.h"
#include "theme.h"

namespace ui {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class Icon { None, Back, Gear, Files, Pencil, Eye, More, Plus, Keyboard };

// Bottom edge of the area screens may draw in (above the on-screen keyboard).
int contentBottom();

// Title bar with an optional left icon and up to three right icons (right1 is rightmost).
void topBar(const std::string& title, Icon left, Icon right1 = Icon::None,
            Icon right2 = Icon::None, Icon right3 = Icon::None);
// Redraws just the battery indicator in the current top bar (when the level changes).
void refreshBattery();
bool hitLeft(int x, int y);
// Index of the right-hand icon slot under (x, y): 0 = rightmost, -1 = none.
int hitRightSlot(int x, int y);
inline bool hitRight(int x, int y) { return hitRightSlot(x, y) == 0; }

void icon(LovyanGFX& g, Icon i, int cx, int cy, uint16_t color);
void folderIcon(LovyanGFX& g, int x, int y, uint16_t color);
void noteIcon(LovyanGFX& g, int x, int y, uint16_t color);

void button(const Rect& r, const char* label, uint16_t bg, uint16_t fg = theme::TEXT_BRIGHT);

// Shortens `s` with "..." so it fits in maxW pixels.
std::string ellipsize(const std::string& s, int maxW, const lgfx::IFont* f);

// Centred multi-line message (lines separated by '\n') in the content area.
void message(const std::string& title, const std::string& body, int y);

}  // namespace ui
