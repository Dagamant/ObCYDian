#include "ui.h"

#include "battery.h"
#include "btkbd.h"
#include "radio.h"
#include "webserver.h"
#include "osk.h"

namespace ui {

// Top bar geometry. Icons are drawn in a 20x20 box with a 2 px stroke and centred in
// 44 px slots, so every icon has the same visual weight and spacing.
static constexpr int kSlotW = 44;
static constexpr int kEdge = 22;          // centre of the outermost icon from the screen edge
static constexpr int kIconCy = (theme::BAR_H - 1) / 2;
static constexpr float kStroke = 1.0f;    // drawWideLine radius: ~2 px lines

int contentBottom() { return gfx.height() - osk::height(); }

// Status group (radio glyph, battery, percentage), right-aligned before the slots
static constexpr int kRadioW = 16, kBatteryW = 24, kGap = 6;
static int statusX_ = -1, statusRight_ = -1;

static int pctWidth() { return gfx.textWidth("100%", font::small()); }

static void line(LovyanGFX& g, float x0, float y0, float x1, float y1, uint16_t c) {
  g.drawWideLine(x0, y0, x1, y1, kStroke, c);
}

static void ring(LovyanGFX& g, int cx, int cy, int r, uint16_t c) {
  g.drawCircle(cx, cy, r, c);
  g.drawCircle(cx, cy, r - 1, c);
}

static void drawRadio(int x, int cy) {
  if (radio::mode() == radio::Mode::Wifi) {
    uint16_t c = web::state() == web::State::Connected ? theme::TEXT : theme::FAINT;
    const int ox = x + kRadioW / 2, oy = cy + 5;
    gfx.drawArc(ox, oy, 10, 9, 225, 315, c);
    gfx.drawArc(ox, oy, 6, 5, 225, 315, c);
    gfx.fillCircle(ox, oy - 1, 1, c);
  } else if (radio::mode() == radio::Mode::Bluetooth) {
    uint16_t c = btkbd::state() == btkbd::State::Connected ? theme::TEXT : theme::FAINT;
    const float bx = x + kRadioW / 2 - 1;
    const float k = 0.7f;
    gfx.drawWideLine(bx, cy - 7, bx, cy + 7, k, c);
    gfx.drawWideLine(bx, cy - 7, bx + 4, cy - 3, k, c);
    gfx.drawWideLine(bx + 4, cy - 3, bx - 4, cy + 4, k, c);
    gfx.drawWideLine(bx, cy + 7, bx + 4, cy + 3, k, c);
    gfx.drawWideLine(bx + 4, cy + 3, bx - 4, cy - 4, k, c);
  }
}

static void drawBattery(int x, int cy) {
  if (!battery::present()) return;
  const int p = battery::percent();
  const uint16_t c = battery::charging() ? theme::OK : p <= 10 ? theme::DANGER : p <= 25 ? theme::FOLDER : theme::TEXT;
  gfx.drawRoundRect(x, cy - 5, 21, 11, 2, c);   // body
  gfx.fillRect(x + 21, cy - 2, 2, 5, c);        // terminal
  gfx.fillRect(x + 2, cy - 3, std::max(1, 17 * p / 100), 7, c);
  if (battery::charging()) {
    gfx.fillTriangle(x + 11, cy - 5, x + 7, cy + 1, x + 11, cy + 1, theme::BAR);
    gfx.fillTriangle(x + 10, cy + 5, x + 14, cy - 1, x + 10, cy - 1, theme::BAR);
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", p);
  gfx.setFont(font::small());
  gfx.setTextColor(c);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.drawString(buf, x + kBatteryW + 4, cy + 1);
  gfx.setTextDatum(textdatum_t::top_left);
}

static void drawStatus() {
  if (statusX_ < 0) return;
  gfx.fillRect(statusX_, 2, statusRight_ - statusX_, theme::BAR_H - 5, theme::BAR);
  drawRadio(statusX_, kIconCy);
  drawBattery(statusX_ + kRadioW + kGap, kIconCy);
}

void refreshBattery() { drawStatus(); }

bool hitStatus(int x, int y) { return statusX_ >= 0 && y < theme::BAR_H && x >= statusX_ - 4 && x < statusRight_ + 4; }

void topBar(const std::string& title, Icon left, Icon right1, Icon right2, Icon right3) {
  const int w = gfx.width(), H = theme::BAR_H;
  gfx.fillRect(0, 0, w, H - 1, theme::BAR);
  gfx.drawFastHLine(0, H - 1, w, theme::BORDER);
  if (left != Icon::None) icon(gfx, left, kEdge, kIconCy, theme::TEXT);
  const Icon rights[3] = {right1, right2, right3};
  int slots = 0;
  for (int i = 0; i < 3; i++) {
    if (rights[i] == Icon::None) continue;
    icon(gfx, rights[i], w - kEdge - i * kSlotW, kIconCy, theme::TEXT);
    slots = i + 1;
  }
  // The status group ends where the first slot's icon box begins (or at the margin)
  statusRight_ = slots ? w - kEdge - (slots - 1) * kSlotW - kSlotW / 2 - 4 : w - theme::MARGIN;
  statusX_ = statusRight_ - (kRadioW + kGap + kBatteryW + 4 + pctWidth());
  drawStatus();

  const int tx = left != Icon::None ? 2 * kEdge : theme::MARGIN;
  const int maxW = statusX_ - tx - 10;
  gfx.setFont(font::uiBold());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.drawString(ellipsize(title, maxW, font::uiBold()).c_str(), tx, kIconCy + 1);
  gfx.setTextDatum(textdatum_t::top_left);
}

bool hitLeft(int x, int y) { return y < theme::BAR_H && x < 2 * kEdge + 4; }

int hitRightSlot(int x, int y) {
  if (y >= theme::BAR_H) return -1;
  int fromRight = gfx.width() - x;  // slot i spans [i*44, (i+1)*44) from the right edge
  if (fromRight < 0) return 0;
  int slot = fromRight / kSlotW;
  return slot < 3 ? slot : -1;
}

void icon(LovyanGFX& g, Icon i, int cx, int cy, uint16_t c) {
  switch (i) {
    case Icon::Back:
      line(g, cx + 3, cy - 7, cx - 4, cy, c);
      line(g, cx - 4, cy, cx + 3, cy + 7, c);
      break;
    case Icon::Gear:
      // 2 px ring with eight short teeth and an open centre
      for (int k = 0; k < 8; k++) {
        float a = k * PI / 4 + PI / 8;
        line(g, cx + cosf(a) * 7, cy + sinf(a) * 7, cx + cosf(a) * 9.5f, cy + sinf(a) * 9.5f, c);
      }
      ring(g, cx, cy, 7, c);
      ring(g, cx, cy, 3, c);
      break;
    case Icon::Files:
      for (int k = -1; k <= 1; k++) line(g, cx - 8, cy + k * 6, cx + 8, cy + k * 6, c);
      break;
    case Icon::Pencil: {
      // Outline of a pencil at 45 degrees: tip bottom-left, body to the top-right
      const float dx = 0.7071f, dy = -0.7071f, px = 0.7071f, py = 0.7071f, w = 2.8f;
      const float tx = cx - 8, ty = cy + 8;                    // tip
      const float bx = tx + 6 * dx, by = ty + 6 * dy;          // where the tip meets the body
      const float ex = tx + 19 * dx, ey = ty + 19 * dy;        // end of the body
      line(g, tx, ty, bx + w * px, by + w * py, c);
      line(g, tx, ty, bx - w * px, by - w * py, c);
      line(g, bx + w * px, by + w * py, ex + w * px, ey + w * py, c);
      line(g, bx - w * px, by - w * py, ex - w * px, ey - w * py, c);
      line(g, ex + w * px, ey + w * py, ex - w * px, ey - w * py, c);
      break;
    }
    case Icon::Eye:
      g.drawEllipse(cx, cy, 10, 6, c);
      g.drawEllipse(cx, cy, 9, 5, c);
      g.fillCircle(cx, cy, 2, c);
      break;
    case Icon::More:
      for (int k = -1; k <= 1; k++) g.fillRoundRect(cx - 2, cy + k * 6 - 2, 4, 4, 1, c);
      break;
    case Icon::Plus:
      line(g, cx - 8, cy, cx + 8, cy, c);
      line(g, cx, cy - 8, cx, cy + 8, c);
      break;
    case Icon::Keyboard:
      g.drawRoundRect(cx - 10, cy - 7, 20, 14, 2, c);
      g.drawRoundRect(cx - 9, cy - 6, 18, 12, 1, c);
      for (int k = 0; k < 4; k++) {
        g.fillRect(cx - 6 + k * 4, cy - 3, 2, 2, c);
        g.fillRect(cx - 6 + k * 4, cy, 2, 2, c);
      }
      g.fillRect(cx - 4, cy + 3, 8, 1, c);
      break;
    case Icon::Search:
      ring(g, cx - 2, cy - 2, 6, c);
      line(g, cx + 2.5f, cy + 2.5f, cx + 7, cy + 7, c);
      break;
    case Icon::None:
      break;
  }
}

void folderIcon(LovyanGFX& g, int x, int y, uint16_t c) {
  g.fillRoundRect(x, y + 2, 9, 5, 1, c);
  g.fillRoundRect(x, y + 4, 20, 14, 2, c);
}

void noteIcon(LovyanGFX& g, int x, int y, uint16_t c) {
  g.drawRoundRect(x + 2, y, 15, 19, 2, c);
  for (int k = 0; k < 3; k++) g.drawFastHLine(x + 5, y + 5 + k * 4, 9, c);
}

void button(const Rect& r, const char* label, uint16_t bg, uint16_t fg) {
  gfx.fillRoundRect(r.x, r.y, r.w, r.h, 6, bg);
  gfx.setFont(font::uiBold());
  gfx.setTextColor(fg);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

std::string ellipsize(const std::string& s, int maxW, const lgfx::IFont* f) {
  if (gfx.textWidth(s.c_str(), f) <= maxW) return s;
  std::string t = s;
  while (!t.empty() && gfx.textWidth((t + "...").c_str(), f) > maxW) t.pop_back();
  return t + "...";
}

void message(const std::string& title, const std::string& body, int y) {
  const int cx = gfx.width() / 2;
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString(title.c_str(), cx, y);
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  int ly = y + 36;
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t nl = body.find('\n', pos);
    if (nl == std::string::npos) nl = body.size();
    gfx.drawString(body.substr(pos, nl - pos).c_str(), cx, ly);
    ly += 22;
    pos = nl + 1;
  }
  gfx.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui
