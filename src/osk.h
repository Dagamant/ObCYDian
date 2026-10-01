#pragma once
// On-screen keyboard. Occupies the bottom of the screen while visible; key presses are
// injected as ordinary key events, so every text-accepting screen works with it unchanged.

namespace osk {

bool visible();
void show();
void hide();
void toggle();

// Height in pixels while visible (0 when hidden).
int height();
void draw();

// Touch handling; each returns true if the point is on the keyboard (event consumed).
bool onDown(int x, int y);
bool onTap(int x, int y);
bool contains(int x, int y);
// Clears a key highlight left by a press that turned into a drag.
void cancelPress();

}  // namespace osk
