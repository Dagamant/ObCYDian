#include "battery.h"

#include <Arduino.h>

#include "app.h"
#include "board.h"
#include "power.h"
#include "ui.h"

namespace battery {

namespace {

constexpr float kDivider = 2.0f;           // board's resistor divider
constexpr uint32_t kSampleMs = 3000;
constexpr float kLowWarn = 3.55f;          // ~10%
constexpr float kCutoff = 3.35f;           // sleep to protect the cell

float volts_ = 0;
int percent_ = -1;
bool charging_ = false;
uint32_t lastSample_ = 0;
float trendRef_ = 0;  // voltage a couple of minutes ago
uint32_t trendAt_ = 0;
int lowCount_ = 0;
bool warned_ = false;

float readVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(pins::BAT_ADC);
  return sum / 16.0f / 1000.0f * kDivider;
}

// Typical resting voltage of a single Li-ion / LiPo cell vs remaining charge
int toPercent(float v) {
  static const struct { float v; int p; } curve[] = {
      {4.20f, 100}, {4.10f, 92}, {4.00f, 80}, {3.92f, 70}, {3.85f, 60}, {3.80f, 50}, {3.75f, 40},
      {3.71f, 30},  {3.67f, 20}, {3.60f, 10}, {3.50f, 5},  {3.30f, 0}};
  if (v >= curve[0].v) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (v >= curve[i].v) {
      float t = (v - curve[i].v) / (curve[i - 1].v - curve[i].v);
      return curve[i].p + (int)(t * (curve[i - 1].p - curve[i].p) + 0.5f);
    }
  }
  return 0;
}

}  // namespace

void begin() {
  analogSetPinAttenuation(pins::BAT_ADC, ADC_11db);
  volts_ = readVolts();
  percent_ = toPercent(volts_);
  trendRef_ = volts_;
  trendAt_ = lastSample_ = millis();
  Serial.printf("[battery] %.2f V (%d%%)%s\n", volts_, percent_, present() ? "" : " - no battery?");
}

void loop() {
  if (millis() - lastSample_ < kSampleMs) return;
  lastSample_ = millis();
  volts_ = volts_ * 0.8f + readVolts() * 0.2f;  // smooth out ADC and load noise

  // Charging guess: voltage climbed noticeably over the last two minutes
  if (millis() - trendAt_ > 120000) {
    charging_ = volts_ - trendRef_ > 0.012f && volts_ < 4.25f;
    trendRef_ = volts_;
    trendAt_ = millis();
  }

  int p = toPercent(volts_);
  if (p != percent_) {
    percent_ = p;
    if (power::screenOn()) ui::refreshBattery();
  }
  if (!present() || charging_) {
    lowCount_ = 0;
    warned_ = false;
    return;
  }
  if (volts_ < kLowWarn && !warned_ && power::screenOn()) {
    warned_ = true;
    app::toast("Battery low - charge soon", 4000);
  }
  // A few readings in a row below the cutoff (not just a load dip): save and sleep
  lowCount_ = volts_ < kCutoff ? lowCount_ + 1 : 0;
  if (lowCount_ >= 3) {
    Serial.println("[battery] critically low, sleeping");
    power::deepSleep();
  }
}

bool present() { return volts_ > 2.8f && volts_ < 4.5f; }
float voltage() { return volts_; }
int percent() { return percent_; }
bool charging() { return charging_; }

}  // namespace battery
