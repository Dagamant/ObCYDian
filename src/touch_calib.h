#pragma once
#include "board.h"

namespace touch_calib {
// Loads stored calibration from NVS and applies it. Returns false if none stored.
bool load(LGFX& gfx);
// Runs the interactive 4-corner calibration, applies and stores the result.
void run(LGFX& gfx);
// Erases stored calibration.
void clear();
}  // namespace touch_calib
