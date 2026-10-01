#include "radio.h"

#include <Arduino.h>
#include <Preferences.h>

namespace radio {

Mode mode() {
  static int cached = -1;
  if (cached < 0) {
    Preferences p;
    p.begin("radio", true);
    cached = p.getUChar("mode", (uint8_t)Mode::Bluetooth);
    p.end();
  }
  return (Mode)cached;
}

const char* name(Mode m) {
  switch (m) {
    case Mode::Off: return "Off";
    case Mode::Bluetooth: return "Bluetooth keyboard";
    case Mode::Wifi: return "WiFi web server";
  }
  return "";
}

void switchTo(Mode m) {
  Preferences p;
  p.begin("radio", false);
  p.putUChar("mode", (uint8_t)m);
  p.end();
  Serial.printf("[radio] switching to %s, restarting\n", name(m));
  Serial.flush();
  delay(100);
  ESP.restart();
}

}  // namespace radio
