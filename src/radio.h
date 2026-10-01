#pragma once
// Which radio runs: the Bluetooth keyboard or WiFi (web server). Only one at a time keeps
// enough RAM free; switching is done by saving the choice and restarting.

namespace radio {

enum class Mode { Off = 0, Bluetooth = 1, Wifi = 2 };

Mode mode();
const char* name(Mode m);
// Saves the new mode and restarts the device.
void switchTo(Mode m);

}  // namespace radio
