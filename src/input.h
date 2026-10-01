#pragma once
// Input events: touch gestures (tap / vertical drag) and key presses. Keys come from the
// serial console today and from a Bluetooth keyboard later; both feed inject().

#include <stdint.h>

namespace input {

enum class Type { None, Down, Tap, Drag, DragEnd, Key };  // Down: finger touched

enum Key : uint8_t {
  K_NONE,
  K_CHAR,  // printable ASCII in `ch`
  K_ENTER,
  K_BACKSPACE,
  K_DELETE,
  K_TAB,
  K_ESC,
  K_LEFT,
  K_RIGHT,
  K_UP,
  K_DOWN,
  K_HOME,
  K_END,
  K_PGUP,
  K_PGDN,
  K_F2,
};

enum Mod : uint8_t { M_CTRL = 1, M_SHIFT = 2, M_ALT = 4, M_GUI = 8 };

struct Event {
  Type type = Type::None;
  int x = 0, y = 0;
  int dy = 0;  // for Drag: movement since the previous Drag event
  Key key = K_NONE;
  char ch = 0;  // for K_CHAR: lower-case letter when Ctrl/Alt is held
  uint8_t mods = 0;

  bool ctrl() const { return mods & M_CTRL; }
  bool shift() const { return mods & M_SHIFT; }
  bool alt() const { return mods & M_ALT; }
  bool isChar(char c, uint8_t m = 0) const { return key == K_CHAR && ch == c && mods == m; }
};

Event keyEvent(Key k, uint8_t mods = 0, char ch = 0);

bool poll(Event& e);
void inject(const Event& e);

}  // namespace input
