#pragma once
// Battery monitoring via the board's divider on GPIO34 (half the battery voltage).

#include <stdint.h>

namespace battery {

void begin();
void loop();  // samples every few seconds; warns and sleeps when the battery runs low

bool present();      // a battery seems to be connected
float voltage();     // smoothed, volts
int percent();       // 0..100 (estimate from a Li-ion discharge curve)
bool charging();     // best guess: voltage steadily rising (the board has no charge signal)

}  // namespace battery
