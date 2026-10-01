#include "ui.h"

namespace ui {

static constexpr int kIconHit = 48;

void topBar(const std::string& title, Icon left, Icon right) {
  const int w = gfx.width(), H = theme::BAR_H;
  gfx.fillRect(0, 0, w, H - 1, theme::BAR);
  gfx.drawFastHLine(0, H - 1, w, theme::BORDER);
  if (left != Icon::None) icon(gfx, left, 22, H / 2, theme::TEXT);
  if (right != Icon::None) icon(gfx, right, w - 24, H / 2, theme::TEXT);
  int tx = left != Icon::None ? kIconHit : theme::MARGIN;
  int maxW = w - tx - (right != Icon::None ? kIconHit : theme::MARGIN);
  gfx.setFont(font::uiBold());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.drawString(ellipsize(title, maxW, font::uiBold()).c_str(), tx, H / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

bool hitLeft(int x, int y) { return y < theme::BAR_H && x < kIconHit; }
bool hitRight(int x, int y) { return y < theme::BAR_H && x >= gfx.width() - kIconHit; }

void icon(LovyanGFX& g, Icon i, int cx, int cy, uint16_t c) {
  switch (i) {
    case Icon::Back:
      g.drawWideLine(cx + 4, cy - 8, cx - 4, cy, 1.5f, c);
      g.drawWideLine(cx - 4, cy, cx + 4, cy + 8, 1.5f, c);
      break;
    case Icon::Gear:
      for (int k = 0; k < 8; k++) {
        float a = k * PI / 4;
        g.drawWideLine(cx + cosf(a) * 6, cy + sinf(a) * 6, cx + cosf(a) * 10, cy + sinf(a) * 10,
                       1.8f, c);
      }
      g.fillCircle(cx, cy, 7, c);
      g.fillCircle(cx, cy, 3, theme::BAR);
      break;
    case Icon::Files:
      for (int k = -1; k <= 1; k++) g.fillRoundRect(cx - 9, cy + k * 6 - 1, 18, 3, 1, c);
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
