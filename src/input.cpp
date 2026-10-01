#include "input.h"

#include <deque>

#include "display.h"

namespace input {

static std::deque<Event> queue_;

void inject(const Event& e) {
  if (queue_.size() < 256) queue_.push_back(e);
}

Event keyEvent(Key k, uint8_t mods, char ch) {
  Event e;
  e.type = Type::Key;
  e.key = k;
  e.mods = mods;
  e.ch = ch;
  return e;
}

bool poll(Event& e) {
  if (!queue_.empty()) {
    e = queue_.front();
    queue_.pop_front();
    return true;
  }

  static bool down = false, dragging = false;
  static int startX, startY, lastX, lastY;
  constexpr int kDragThreshold = 10;

  int32_t x, y;
  bool now = gfx.getTouch(&x, &y);
  if (now && !down) {
    down = true;
    dragging = false;
    startX = lastX = x;
    startY = lastY = y;
    return false;
  }
  if (now && down) {
    if (!dragging && abs(y - startY) > kDragThreshold) {
      dragging = true;
      lastY = startY;
    }
    lastX = x;
    if (dragging && y != lastY) {
      e = Event();
      e.type = Type::Drag;
      e.x = x;
      e.y = y;
      e.dy = y - lastY;
      lastY = y;
      return true;
    }
    return false;
  }
  if (!now && down) {
    down = false;
    e = Event();
    e.type = dragging ? Type::DragEnd : Type::Tap;
    e.x = dragging ? lastX : startX;
    e.y = dragging ? lastY : startY;
    return true;
  }
  return false;
}

}  // namespace input
