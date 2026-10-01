#include "ui.h"

#include "osk.h"

namespace ui {

static constexpr int kIconHit = 48;

int contentBottom() { return gfx.height() - osk::height(); }

static constexpr int kSlotW = 44;

void topBar(const std::string& title, Icon left, Icon right1, Icon right2, Icon right3) {
  const int w = gfx.width(), H = theme::BAR_H;
  gfx.fillRect(0, 0, w, H - 1, theme::BAR);
  gfx.drawFastHLine(0, H - 1, w, theme::BORDER);
  if (left != Icon::None) icon(gfx, left, 22, H / 2, theme::TEXT);
  const Icon rights[3] = {right1, right2, right3};
  int slots = 0;
  for (int i = 0; i < 3; i++) {
    if (rights[i] == Icon::None) continue;
    icon(gfx, rights[i], w - kSlotW / 2 - 2 - i * kSlotW, H / 2, theme::TEXT);
    slots = i + 1;
  }
  int tx = left != Icon::None ? kIconHit : theme::MARGIN;
  int maxW = w - tx - (slots ? slots * kSlotW + 4 : theme::MARGIN);
  gfx.setFont(font::uiBold());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_left);
  gfx.drawString(ellipsize(title, maxW, font::uiBold()).c_str(), tx, H / 2);
  gfx.setTextDatum(textdatum_t::top_left);
}

bool hitLeft(int x, int y) { return y < theme::BAR_H && x < kIconHit; }
int hitRightSlot(int x, int y) {
  if (y >= theme::BAR_H) return -1;
  int fromRight = gfx.width() - 2 - x;
  if (fromRight < 0) return 0;
  int slot = fromRight / kSlotW;
  return slot < 3 ? slot : -1;
}

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
    case Icon::Pencil:
      g.drawWideLine(cx - 6, cy + 6, cx + 5, cy - 5, 2.2f, c);
      g.fillTriangle(cx - 9, cy + 9, cx - 8, cy + 4, cx - 4, cy + 8, c);
      g.drawWideLine(cx + 4, cy - 8, cx + 8, cy - 4, 1.2f, c);
      break;
    case Icon::Eye:
      g.drawEllipse(cx, cy, 11, 6, c);
      g.drawEllipse(cx, cy, 10, 5, c);
      g.fillCircle(cx, cy, 3, c);
      break;
    case Icon::More:
      for (int k = -1; k <= 1; k++) g.fillCircle(cx, cy + k * 6, 2, c);
      break;
    case Icon::Plus:
      g.fillRoundRect(cx - 9, cy - 1, 18, 3, 1, c);
      g.fillRoundRect(cx - 1, cy - 9, 3, 18, 1, c);
      break;
    case Icon::Keyboard:
      g.drawRoundRect(cx - 11, cy - 7, 22, 14, 2, c);
      for (int k = 0; k < 4; k++) g.fillRect(cx - 8 + k * 5, cy - 4, 3, 2, c);
      for (int k = 0; k < 4; k++) g.fillRect(cx - 8 + k * 5, cy - 1, 3, 2, c);
      g.fillRect(cx - 5, cy + 3, 10, 2, c);
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
