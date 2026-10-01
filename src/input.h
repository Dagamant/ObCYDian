#pragma once
// Touch gestures (tap / vertical drag) plus events injected from the serial debug console.

namespace input {

enum class Type { None, Tap, Drag, DragEnd };

struct Event {
  Type type = Type::None;
  int x = 0, y = 0;
  int dy = 0;  // for Drag: movement since the previous Drag event
};

bool poll(Event& e);
void inject(const Event& e);

}  // namespace input
