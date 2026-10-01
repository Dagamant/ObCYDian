// ObCYDian: an Obsidian-style markdown vault on the 3.5" ESP32-32E Cheap Yellow Display.
//
// Touch calibration runs on first boot or when BOOT is held during reset.

#include <Arduino.h>

#include "app.h"
#include "btkbd.h"
#include "debug_console.h"
#include "display.h"
#include "input.h"
#include "power.h"
#include "storage.h"
#include "theme.h"
#include "touch_calib.h"
#include "webserver.h"

static constexpr uint8_t kRotation = 1;  // landscape 480x320, USB-C on the left

void setup() {
  Serial.begin(921600);
  pinMode(pins::BOOT_BTN, INPUT_PULLUP);
  power::begin();  // releases pins parked during deep sleep
  for (int p : {pins::LED_R, pins::LED_G, pins::LED_B}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);  // off (active low)
  }

  gfx.init();
  gfx.setRotation(kRotation);
  gfx.setBrightness(power::brightness());
  gfx.fillScreen(theme::BG);
  Serial.printf("\n[display] %dx%d\n", gfx.width(), gfx.height());

  // BOOT held at power-on forces calibration (but not when BOOT was used to wake from sleep)
  bool forceCal = digitalRead(pins::BOOT_BTN) == LOW && !power::wokeFromSleep();
  if (forceCal || !touch_calib::load(gfx)) touch_calib::run(gfx);

  gfx.fillScreen(theme::BG);
  gfx.setTextDatum(textdatum_t::middle_center);
  // "Ob" + "CYD" + "ian", with CYD in the accent colour
  gfx.setFont(font::h1());
  gfx.setTextDatum(textdatum_t::middle_left);
  int x = (gfx.width() - gfx.textWidth("ObCYDian")) / 2;
  const int y = gfx.height() / 2 - 20;
  for (auto part : {std::make_pair("Ob", theme::TEXT_BRIGHT), std::make_pair("CYD", theme::ACCENT),
                    std::make_pair("ian", theme::TEXT_BRIGHT)}) {
    gfx.setTextColor(part.second);
    gfx.drawString(part.first, x, y);
    x += gfx.textWidth(part.first);
  }
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString("Mounting SD card...", gfx.width() / 2, gfx.height() / 2 + 30);
  gfx.setTextDatum(textdatum_t::top_left);
  storage::begin();

  btkbd::begin();
  web::begin();
  app::begin();
  if (power::wokeFromSleep()) {
    // Don't let the touch that woke us register as a tap
    lgfx::touch_point_t tp;
    uint32_t t = millis();
    while (gfx.getTouchRaw(&tp, 1) && millis() - t < 3000) delay(20);
    bool editing = false;
    std::string path = power::resumePath(&editing);
    if (!path.empty() && storage::exists(path)) editing ? app::editNote(path) : app::openNote(path);
  }
  Serial.printf("[app] ready, heap free %u\n", ESP.getFreeHeap());
}

void loop() {
  debug_console::poll();
  input::Event e;
  while (input::poll(e))
    if (!power::activity()) app::handle(e);  // the first touch/key with the screen off just wakes it
  while (btkbd::poll(e))
    if (!power::activity()) app::handle(e);
  app::loop();
  web::loop();
  power::loop();
  delay(5);
}
