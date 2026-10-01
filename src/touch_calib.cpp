#include "touch_calib.h"

#include <Preferences.h>

namespace touch_calib {

static constexpr const char* kNamespace = "touch";
static constexpr const char* kKey = "cal";
static constexpr size_t kLen = 8;

bool load(LGFX& gfx) {
  Preferences prefs;
  prefs.begin(kNamespace, true);
  uint16_t cal[kLen];
  size_t n = prefs.getBytes(kKey, cal, sizeof(cal));
  prefs.end();
  if (n != sizeof(cal)) return false;
  gfx.setTouchCalibrate(cal);
  Serial.printf("[touch] loaded calibration: %u %u %u %u %u %u %u %u\n", cal[0], cal[1],
                cal[2], cal[3], cal[4], cal[5], cal[6], cal[7]);
  return true;
}

void run(LGFX& gfx) {
  // Wait for the finger to lift so a held touch doesn't register as the first corner.
  lgfx::touch_point_t tp;
  while (gfx.getTouchRaw(&tp, 1)) delay(10);

  gfx.fillScreen(TFT_BLACK);
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setFont(&fonts::FreeSans12pt7b);
  gfx.drawString("Touch calibration", gfx.width() / 2, gfx.height() / 2 - 20);
  gfx.setFont(&fonts::FreeSans9pt7b);
  gfx.drawString("Tap each arrow tip precisely", gfx.width() / 2, gfx.height() / 2 + 14);
  gfx.drawString("(a stylus works best)", gfx.width() / 2, gfx.height() / 2 + 36);
  gfx.setTextDatum(textdatum_t::top_left);

  uint16_t cal[kLen];
  gfx.calibrateTouch(cal, TFT_MAGENTA, TFT_BLACK, std::max(gfx.width(), gfx.height()) >> 3);

  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putBytes(kKey, cal, sizeof(cal));
  prefs.end();
  Serial.printf("[touch] saved calibration: %u %u %u %u %u %u %u %u\n", cal[0], cal[1], cal[2],
                cal[3], cal[4], cal[5], cal[6], cal[7]);
}

void clear() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.remove(kKey);
  prefs.end();
}

}  // namespace touch_calib
