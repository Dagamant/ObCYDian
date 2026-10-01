#include "power.h"
#include "screens.h"
#include "touch_calib.h"

namespace {

const uint32_t kScreenOptions[] = {0, 30, 60, 120, 300, 600};
const uint32_t kSleepOptions[] = {0, 300, 600, 1800, 3600};

std::string durationText(uint32_t s) {
  if (s == 0) return "Never";
  if (s < 60) return std::to_string(s) + " sec";
  return std::to_string(s / 60) + " min";
}

template <size_t N>
uint32_t nextOption(const uint32_t (&opts)[N], uint32_t current) {
  for (size_t i = 0; i < N; i++)
    if (opts[i] == current) return opts[(i + 1) % N];
  return opts[0];
}

}  // namespace

void PowerScreen::draw() {
  ui::topBar("Display & power", ui::Icon::Back);
  gfx.fillRect(0, theme::BAR_H, gfx.width(), gfx.height() - theme::BAR_H, theme::BG);
  drawRows();

  const int W = gfx.width(), M = theme::MARGIN;
  const int bw = (W - 2 * M - 16) / 3, by = gfx.height() - 54;
  btnOffNow_ = {M, by, bw, 44};
  btnSleepNow_ = {M + bw + 8, by, bw, 44};
  btnCalibrate_ = {M + 2 * (bw + 8), by, bw, 44};
  ui::button(btnOffNow_, "Screen off", theme::BORDER);
  ui::button(btnSleepNow_, "Sleep now", theme::ACCENT_BG);
  ui::button(btnCalibrate_, "Calibrate touch", theme::BORDER);
}

void PowerScreen::drawRows() {
  const int W = gfx.width(), M = theme::MARGIN;
  const int vx = 250, vw = W - M - vx;
  int y = theme::BAR_H + 14;
  gfx.fillRect(0, y, W, 200, theme::BG);

  auto label = [&](const char* text, const char* sub) {
    gfx.setFont(font::ui());
    gfx.setTextColor(theme::TEXT);
    gfx.drawString(text, M + 4, y + 4);
    gfx.setFont(font::small());
    gfx.setTextColor(theme::FAINT);
    gfx.drawString(sub, M + 4, y + 25);
  };

  // Brightness: [-] 60% [+]
  label("Brightness", "Lower saves power");
  btnDim_ = {vx, y, 50, 40};
  btnBright_ = {vx + vw - 50, y, 50, 40};
  ui::button(btnDim_, "-", theme::BORDER);
  ui::button(btnBright_, "+", theme::BORDER);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", (power::brightness() * 100 + 127) / 255);
  gfx.setFont(font::uiBold());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(pct, vx + vw / 2, y + 20);
  gfx.setTextDatum(textdatum_t::top_left);
  y += 56;

  label("Screen off after", "Keyboard & WiFi keep running");
  btnScreen_ = {vx, y, vw, 40};
  ui::button(btnScreen_, durationText(power::screenTimeout()).c_str(), theme::BORDER);
  y += 56;

  label("Sleep after", "Deep sleep: lowest power");
  btnSleep_ = {vx, y, vw, 40};
  ui::button(btnSleep_, durationText(power::sleepTimeout()).c_str(), theme::BORDER);
  y += 50;

  gfx.setFont(font::small());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString("BOOT button: press = screen on/off, hold 2 s = sleep.", M + 4, y);
  gfx.drawString("Touch the screen or press BOOT to wake.", M + 4, y + 16);
}

void PowerScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (btnDim_.contains(x, y) || btnBright_.contains(x, y)) {
    int level = power::brightness() + (btnBright_.contains(x, y) ? 25 : -25);
    power::setBrightness(std::max(10, std::min(255, level)));
    return drawRows();
  }
  if (btnScreen_.contains(x, y)) {
    power::setScreenTimeout(nextOption(kScreenOptions, power::screenTimeout()));
    return drawRows();
  }
  if (btnSleep_.contains(x, y)) {
    power::setSleepTimeout(nextOption(kSleepOptions, power::sleepTimeout()));
    return drawRows();
  }
  if (btnOffNow_.contains(x, y)) {
    // Wait for the finger to lift, or the release would wake the screen again
    lgfx::touch_point_t tp;
    while (gfx.getTouchRaw(&tp, 1)) delay(20);
    delay(200);
    return power::screenOff();
  }
  if (btnSleepNow_.contains(x, y)) return power::deepSleep();
  if (btnCalibrate_.contains(x, y)) {
    touch_calib::run(gfx);
    draw();
  }
}

void PowerScreen::onKey(const input::Event& e) {
  if (e.key == input::K_ESC) app::back();
}
