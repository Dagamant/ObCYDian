#pragma once
// Wall-clock time. The board has no battery-backed RTC, so time comes from NTP (WiFi mode),
// from a browser using the web app, or from the user. The ESP32 keeps counting through deep
// sleep; across power loss the last known time is restored but marked as uncertain.

#include <stdint.h>
#include <time.h>

#include <string>

namespace wallclock {

void begin();
void loop();

// True when the time came from NTP, a browser or the user since power-on (or deep sleep).
bool valid();
// Local time now (may be the stale last-known time if !valid()).
time_t now();

// Sets UTC time and, optionally, the local offset from UTC in minutes.
void set(time_t utc, int offsetMinutes);
void setOffset(int offsetMinutes);
int offsetMinutes();
// Sets the local date (time unchanged) from "YYYY-MM-DD". Returns false if malformed.
bool setDate(const std::string& ymd);

// Formats local time with Obsidian/moment-style tokens: YYYY YY MMMM MMM MM M DD D
// dddd ddd HH H mm ss A.
std::string format(const std::string& pattern, time_t t);
inline std::string format(const std::string& pattern) { return format(pattern, now()); }

}  // namespace wallclock
