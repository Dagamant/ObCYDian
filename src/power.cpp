#include "power.h"

#include <Arduino.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "app.h"
#include "display.h"
#include "theme.h"

namespace power {

namespace {

// Survives deep sleep (RTC slow memory)
RTC_DATA_ATTR uint32_t rtcMagic = 0;
RTC_DATA_ATTR char rtcPath[192];
RTC_DATA_ATTR bool rtcEditing = false;
constexpr uint32_t kMagic = 0x0BC1D1A1;

// Pins parked in a fixed state through deep sleep
struct Park {
  int pin;
  int level;
};
constexpr Park kParked[] = {
    {pins::LCD_BL, LOW},     // backlight off
    {pins::LED_R, HIGH},     // RGB LED off (active low)
    {pins::LED_G, HIGH},
    {pins::LED_B, HIGH},
    {pins::AUDIO_EN, HIGH},  // speaker amplifier disabled (active low)
    {pins::LCD_CS, HIGH},    // deselect everything on the SPI buses
    {pins::TOUCH_CS, HIGH},
    {pins::SD_CS, HIGH},
};

bool woke_ = false;
bool screenOn_ = true;
uint8_t brightness_ = 200;
uint32_t screenTimeout_ = 0, sleepTimeout_ = 0;
uint32_t lastActivity_ = 0;

void savePrefs() {
  Preferences p;
  p.begin("power", false);
  p.putUChar("bright", brightness_);
  p.putUInt("screen", screenTimeout_);
  p.putUInt("sleep", sleepTimeout_);
  p.end();
}

}  // namespace

void begin() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  woke_ = cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1;
  gpio_deep_sleep_hold_dis();
  for (auto& p : kParked) gpio_hold_dis((gpio_num_t)p.pin);
  pinMode(pins::AUDIO_EN, OUTPUT);
  digitalWrite(pins::AUDIO_EN, HIGH);  // keep the amplifier off; nothing uses audio yet

  Preferences p;
  p.begin("power", false);
  brightness_ = p.getUChar("bright", 200);
  screenTimeout_ = p.getUInt("screen", 120);
  sleepTimeout_ = p.getUInt("sleep", 1800);
  p.end();
  lastActivity_ = millis();
  if (woke_) Serial.printf("[power] woke from deep sleep (cause %d)\n", cause);
}

bool wokeFromSleep() { return woke_; }

std::string resumePath(bool* editing) {
  if (!woke_ || rtcMagic != kMagic) return "";
  *editing = rtcEditing;
  return rtcPath;
}

bool activity() {
  lastActivity_ = millis();
  if (!screenOn_) {
    screenWake();
    return true;
  }
  return false;
}

void keepAwake() { lastActivity_ = millis(); }

bool screenOn() { return screenOn_; }

void screenOff() {
  if (!screenOn_) return;
  Serial.println("[power] screen off");
  gfx.sleep();
  setCpuFrequencyMhz(80);  // lowest speed that keeps Bluetooth and WiFi running
  screenOn_ = false;
}

void screenWake() {
  if (screenOn_) return;
  setCpuFrequencyMhz(240);
  gfx.wakeup();
  gfx.setBrightness(brightness_);
  screenOn_ = true;
  lastActivity_ = millis();
  Serial.println("[power] screen on");
}

void deepSleep() {
  Serial.println("[power] entering deep sleep");
  screenWake();
  bool editing = false;
  std::string path = app::currentNote(&editing);
  app::prepareSleep();  // saves any open note
  strlcpy(rtcPath, path.c_str(), sizeof(rtcPath));
  rtcEditing = editing;
  rtcMagic = kMagic;

  gfx.fillScreen(theme::BG);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString("Sleeping", gfx.width() / 2, gfx.height() / 2 - 14);
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString("Touch the screen or press BOOT to wake", gfx.width() / 2, gfx.height() / 2 + 18);
  gfx.setTextDatum(textdatum_t::top_left);

  // Anything still pressed would wake us straight away
  uint32_t t = millis();
  lgfx::touch_point_t tp;
  while ((gfx.getTouchRaw(&tp, 1) || digitalRead(pins::BOOT_BTN) == LOW) && millis() - t < 5000) delay(20);
  delay(900);
  gfx.getTouchRaw(&tp, 1);  // ends with a power-down command that leaves the pen IRQ armed

  gfx.sleep();
  ledcDetachPin(pins::LCD_BL);
  for (auto& p : kParked) {
    pinMode(p.pin, OUTPUT);
    digitalWrite(p.pin, p.level);
    gpio_hold_en((gpio_num_t)p.pin);
  }
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup((gpio_num_t)pins::TOUCH_IRQ, 0);               // pen down
  esp_sleep_enable_ext1_wakeup(1ULL << pins::BOOT_BTN, ESP_EXT1_WAKEUP_ALL_LOW);  // BOOT
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  // BOOT button: short press toggles the screen, a 2 s hold sleeps
  static uint32_t pressedAt = 0;
  static bool handled = false, armed = false;
  bool down = digitalRead(pins::BOOT_BTN) == LOW;
  if (!armed) {  // ignore a press still held from waking up / power-on
    armed = !down;
  } else if (down && !pressedAt) {
    pressedAt = millis();
    handled = false;
  } else if (down && !handled && millis() - pressedAt > 2000) {
    handled = true;
    deepSleep();
  } else if (!down && pressedAt) {
    if (!handled && millis() - pressedAt > 30) {
      if (screenOn_) screenOff();
      else screenWake();
      lastActivity_ = millis();
    }
    pressedAt = 0;
  }

  const uint32_t idle = (millis() - lastActivity_) / 1000;
  if (sleepTimeout_ && idle >= sleepTimeout_) deepSleep();
  if (screenOn_ && screenTimeout_ && idle >= screenTimeout_) screenOff();
}

uint8_t brightness() { return brightness_; }

void setBrightness(uint8_t level) {
  brightness_ = std::max<uint8_t>(10, level);
  gfx.setBrightness(brightness_);
  savePrefs();
}

uint32_t screenTimeout() { return screenTimeout_; }
uint32_t sleepTimeout() { return sleepTimeout_; }

void setScreenTimeout(uint32_t s) {
  screenTimeout_ = s;
  savePrefs();
}

void setSleepTimeout(uint32_t s) {
  sleepTimeout_ = s;
  savePrefs();
}

}  // namespace power
