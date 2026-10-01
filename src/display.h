#pragma once

#include <functional>

#include "board.h"

extern LGFX gfx;

// Renders a horizontal strip of the screen through a small off-screen sprite, one band
// at a time, so redraws (e.g. while scrolling) don't flicker. `draw` gets the sprite and
// the band's offset from `top`; it should draw at (y - bandOffset).
void renderBands(int top, int height, uint16_t bg,
                 const std::function<void(LGFX_Sprite&, int bandOffset)>& draw);
