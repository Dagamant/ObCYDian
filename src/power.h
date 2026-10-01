#pragma once
// Power management: brightness, screen-off standby, deep sleep, idle timers and the BOOT
// button (short press: screen on/off, hold 2 s: deep sleep).
//
// Screen off keeps everything running (keyboard, web server) with the backlight and panel
// off and the CPU slowed. Deep sleep powers down the ESP32; touching the screen or pressing
// BOOT restarts it, and it reopens the note that was on screen.

#include <stdint.h>

#include <string>

namespace power {

// Call early in setup(): releases pins held through deep sleep, applies brightness.
void begin();
// True if this boot is a wake-up from deep sleep.
bool wokeFromSleep();
// The note that was open when the device went to sleep ("" if none), and whether it was
// being edited.
std::string resumePath(bool* editing);

void loop();
// Any user (or web) activity: resets idle timers. Returns true if the event only woke the
// screen and should be discarded.
bool activity();
// Activity that shouldn't light the screen (e.g. web requests): only resets idle timers.
void keepAwake();

bool screenOn();
void screenOff();
void screenWake();
void deepSleep();

uint8_t brightness();
void setBrightness(uint8_t level);

// Idle timeouts in seconds (0 = never)
uint32_t screenTimeout();
uint32_t sleepTimeout();
void setScreenTimeout(uint32_t s);
void setSleepTimeout(uint32_t s);

}  // namespace power
