#include "input.h"

#include <deque>

#include "display.h"

namespace input {

static std::deque<Event> queue_;

void inject(const Event& e) { queue_.push_back(e); }

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
      e = {Type::Drag, (int)x, (int)y, (int)(y - lastY)};
      lastY = y;
      return true;
    }
    return false;
  }
  if (!now && down) {
    down = false;
    if (dragging) {
      e = {Type::DragEnd, lastX, lastY, 0};
    } else {
      e = {Type::Tap, startX, startY, 0};
    }
    return true;
  }
  return false;
}

}  // namespace input
