// Stage 1: full-screen display bring-up + persistent touch calibration.
//
// Calibration runs on first boot, when the BOOT button is held during reset,
// when the "Recal" button is tapped, or when 'c' is sent over serial.

#include <Arduino.h>

#include "board.h"
#include "touch_calib.h"

static LGFX gfx;

static constexpr uint8_t kRotation = 1;  // 1 = landscape 480x320, USB-C on the left
static constexpr int kBarH = 28;

struct Button {
  int x, y, w, h;
  const char* label;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

static Button btnClear;
static Button btnRecal;

static void drawButton(const Button& b, uint16_t color) {
  gfx.fillRoundRect(b.x, b.y, b.w, b.h, 4, color);
  gfx.setTextColor(TFT_WHITE, color);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

static void drawTestScreen() {
  const int w = gfx.width(), h = gfx.height();
  gfx.fillScreen(TFT_BLACK);

  // Title bar with buttons
  gfx.fillRect(0, 0, w, kBarH, 0x2945);
  gfx.setFont(&fonts::FreeSans9pt7b);
  gfx.setTextColor(TFT_WHITE, 0x2945);
  gfx.drawString("CYD Notes - touch test", 6, 6);
  btnRecal = {w - 66, 2, 62, kBarH - 4, "Recal"};
  btnClear = {w - 134, 2, 62, kBarH - 4, "Clear"};
  drawButton(btnRecal, 0x7800);
  drawButton(btnClear, 0x03EF);

  // 1px border on the outermost pixels: all four sides should be visible
  gfx.drawRect(0, 0, w, h, TFT_YELLOW);

  // Colour swatches to verify RGB order / inversion
  const struct { uint16_t c; const char* n; } sw[] = {
      {TFT_RED, "R"}, {TFT_GREEN, "G"}, {TFT_BLUE, "B"}, {TFT_WHITE, "W"}};
  gfx.setFont(&fonts::Font2);
  for (int i = 0; i < 4; i++) {
    int x = 6 + i * 30, y = h - 30;
    gfx.fillRect(x, y, 24, 24, sw[i].c);
    gfx.setTextColor(i == 3 ? TFT_BLACK : TFT_WHITE);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.drawString(sw[i].n, x + 12, y + 12);
  }
  gfx.setTextDatum(textdatum_t::top_left);

  // Corner + centre crosshairs: tap them to check calibration accuracy
  const int pts[][2] = {{20, kBarH + 20}, {w - 21, kBarH + 20}, {w / 2, h / 2}, {w - 21, h - 21}};
  for (auto& p : pts) {
    gfx.drawFastHLine(p[0] - 8, p[1], 17, TFT_DARKGREY);
    gfx.drawFastVLine(p[0], p[1] - 8, 17, TFT_DARKGREY);
  }
}

static void showCoords(int x, int y) {
  gfx.setFont(&fonts::Font2);
  gfx.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  gfx.setTextDatum(textdatum_t::bottom_right);
  char buf[24];
  snprintf(buf, sizeof(buf), "  x=%3d y=%3d", x, y);
  gfx.drawString(buf, gfx.width() - 4, gfx.height() - 4);
  gfx.setTextDatum(textdatum_t::top_left);
}

static void calibrate() {
  touch_calib::run(gfx);
  drawTestScreen();
}

void setup() {
  Serial.begin(115200);
  pinMode(pins::BOOT_BTN, INPUT_PULLUP);
  for (int p : {pins::LED_R, pins::LED_G, pins::LED_B}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);  // off
  }

  gfx.init();
  gfx.setRotation(kRotation);
  gfx.setBrightness(200);
  Serial.printf("\n[display] %dx%d rotation %d\n", gfx.width(), gfx.height(), kRotation);

  bool forceCal = digitalRead(pins::BOOT_BTN) == LOW;
  if (forceCal || !touch_calib::load(gfx)) {
    Serial.println(forceCal ? "[touch] BOOT held: recalibrating" : "[touch] no calibration stored");
    touch_calib::run(gfx);
  }
  drawTestScreen();
}

void loop() {
  static bool wasDown = false;
  static int lastX = -1, lastY = -1;

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c') calibrate();
    if (c == 'x') {
      touch_calib::clear();
      Serial.println("[touch] calibration cleared");
    }
  }

  int32_t x, y;
  bool down = gfx.getTouch(&x, &y);
  if (down) {
    if (!wasDown) {
      Serial.printf("[touch] down x=%d y=%d\n", x, y);
      if (btnRecal.hit(x, y)) {
        calibrate();
        wasDown = false;
        return;
      }
      if (btnClear.hit(x, y)) {
        drawTestScreen();
        wasDown = true;
        return;
      }
    }
    if (y > kBarH) {
      if (wasDown && lastX >= 0) {
        gfx.drawLine(lastX, lastY, x, y, TFT_CYAN);
      }
      gfx.fillCircle(x, y, 2, TFT_CYAN);
      lastX = x;
      lastY = y;
    }
    showCoords(x, y);
    digitalWrite(pins::LED_B, LOW);
  } else {
    lastX = lastY = -1;
    digitalWrite(pins::LED_B, HIGH);
  }
  wasDown = down;
  delay(5);
}
