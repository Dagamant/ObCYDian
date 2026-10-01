#include "osk.h"

#include "app.h"
#include "input.h"
#include "ui.h"

namespace osk {

namespace {

enum Kind : uint8_t { K_CHR, K_BACK, K_ENTER, K_SHIFT, K_LAYER, K_SPACE, K_LEFT, K_RIGHT, K_LINK, K_HIDE, K_GAP };

struct Key {
  Kind kind;
  char ch;      // character, or target layer for K_LAYER
  float w;      // width in units (each row is 11.5 units wide)
  const char* label;
};

#define C(c) {K_CHR, c, 1, nullptr}
const Key kBottomLetters[] = {{K_LAYER, 1, 1.5, "123"}, {K_LINK, 0, 1.25, "[["}, {K_SPACE, ' ', 4.5, nullptr},
                              {K_LEFT, 0, 1.25, nullptr}, {K_RIGHT, 0, 1.25, nullptr}, {K_HIDE, 0, 1.75, nullptr}};
const Key kBottomSymbols[] = {{K_LAYER, 0, 1.5, "ABC"}, {K_LINK, 0, 1.25, "[["}, {K_SPACE, ' ', 4.5, nullptr},
                              {K_LEFT, 0, 1.25, nullptr}, {K_RIGHT, 0, 1.25, nullptr}, {K_HIDE, 0, 1.75, nullptr}};

struct Row {
  const Key* keys;
  int n;
};

// Layer 0: letters
const Key L0R1[] = {C('q'), C('w'), C('e'), C('r'), C('t'), C('y'), C('u'), C('i'), C('o'), C('p'), {K_BACK, 0, 1.5, nullptr}};
const Key L0R2[] = {{K_GAP, 0, 0.5, nullptr}, C('a'), C('s'), C('d'), C('f'), C('g'), C('h'), C('j'), C('k'), C('l'), {K_ENTER, 0, 2, nullptr}};
const Key L0R3[] = {{K_SHIFT, 0, 1.5, nullptr}, C('z'), C('x'), C('c'), C('v'), C('b'), C('n'), C('m'), C(','), C('.'), C('?')};
// Layer 1: numbers and markdown punctuation
const Key L1R1[] = {C('1'), C('2'), C('3'), C('4'), C('5'), C('6'), C('7'), C('8'), C('9'), C('0'), {K_BACK, 0, 1.5, nullptr}};
const Key L1R2[] = {{K_GAP, 0, 0.5, nullptr}, C('#'), C('*'), C('-'), C('_'), C('('), C(')'), C('['), C(']'), C('"'), {K_ENTER, 0, 2, nullptr}};
const Key L1R3[] = {{K_LAYER, 2, 1.5, "#+="}, C('!'), C(':'), C(';'), C('/'), C('\''), C('`'), C('>'), C('~'), C('='), C('|')};
// Layer 2: the rest
const Key L2R1[] = {C('@'), C('&'), C('%'), C('+'), C('\\'), C('{'), C('}'), C('<'), C('$'), C('^'), {K_BACK, 0, 1.5, nullptr}};
const Key L2R2[] = {{K_GAP, 0, 0.5, nullptr}, C('#'), C('*'), C('-'), C('_'), C('('), C(')'), C('['), C(']'), C('"'), {K_ENTER, 0, 2, nullptr}};
const Key L2R3[] = {{K_LAYER, 1, 1.5, "123"}, C('!'), C('?'), C(':'), C(';'), C('/'), C('\''), C(','), C('.'), C('='), C('|')};
#undef C

const Row kLayers[3][4] = {
    {{L0R1, 11}, {L0R2, 11}, {L0R3, 11}, {kBottomLetters, 6}},
    {{L1R1, 11}, {L1R2, 11}, {L1R3, 11}, {kBottomSymbols, 6}},
    {{L2R1, 11}, {L2R2, 11}, {L2R3, 11}, {kBottomSymbols, 6}},
};

constexpr int kRowH = 34, kGap = 4, kPad = 3;
constexpr int kHeight = kPad + 4 * (kRowH + kGap);
constexpr float kUnits = 11.5f;

bool visible_ = false;
int layer_ = 0;
int shift_ = 0;  // 0 off, 1 next letter, 2 caps lock
uint32_t lastShift_ = 0;
int pressedRow_ = -1, pressedCol_ = -1;

int top() { return gfx.height() - kHeight; }

ui::Rect keyRect(int r, int c) {
  const float unit = (gfx.width() - 2 * kPad) / kUnits;
  const Row& row = kLayers[layer_][r];
  float x = kPad;
  for (int i = 0; i < c; i++) x += row.keys[i].w * unit;
  return {(int)x + 2, top() + kPad + r * (kRowH + kGap), (int)(row.keys[c].w * unit) - 4, kRowH};
}

void drawKey(int r, int c) {
  const Key& k = kLayers[layer_][r].keys[c];
  if (k.kind == K_GAP) return;
  ui::Rect rc = keyRect(r, c);
  const bool pressed = r == pressedRow_ && c == pressedCol_;
  const bool special = k.kind != K_CHR && k.kind != K_SPACE;
  uint16_t bg = pressed ? theme::ACCENT_BG : special ? theme::BG_ALT : theme::BORDER;
  if (k.kind == K_SHIFT && shift_) bg = theme::ACCENT_DIM;
  gfx.fillRoundRect(rc.x, rc.y, rc.w, rc.h, 5, bg);
  const int cx = rc.x + rc.w / 2, cy = rc.y + rc.h / 2;
  const uint16_t fg = theme::TEXT_BRIGHT;
  gfx.setTextColor(fg);
  gfx.setTextDatum(textdatum_t::middle_center);
  switch (k.kind) {
    case K_CHR: {
      char s[2] = {k.ch, 0};
      if (shift_ && s[0] >= 'a' && s[0] <= 'z') s[0] -= 32;
      gfx.setFont(font::h2());
      gfx.drawString(s, cx, cy - 1);
      break;
    }
    case K_BACK:
      gfx.fillTriangle(cx - 12, cy, cx - 5, cy - 7, cx - 5, cy + 7, fg);
      gfx.fillRect(cx - 5, cy - 7, 16, 15, fg);
      gfx.drawLine(cx - 1, cy - 3, cx + 6, cy + 4, bg);
      gfx.drawLine(cx + 6, cy - 3, cx - 1, cy + 4, bg);
      break;
    case K_ENTER:
      gfx.drawWideLine(cx + 10, cy - 7, cx + 10, cy + 3, 1.2f, fg);
      gfx.drawWideLine(cx + 10, cy + 3, cx - 8, cy + 3, 1.2f, fg);
      gfx.fillTriangle(cx - 12, cy + 3, cx - 6, cy - 2, cx - 6, cy + 8, fg);
      break;
    case K_SHIFT:
      gfx.fillTriangle(cx, cy - 9, cx - 9, cy + 1, cx + 9, cy + 1, fg);
      gfx.fillRect(cx - 4, cy + 1, 9, 6, fg);
      if (shift_ == 2) gfx.fillRect(cx - 7, cy + 9, 15, 2, fg);
      break;
    case K_LEFT:
      gfx.fillTriangle(cx - 6, cy, cx + 5, cy - 7, cx + 5, cy + 7, fg);
      break;
    case K_RIGHT:
      gfx.fillTriangle(cx + 6, cy, cx - 5, cy - 7, cx - 5, cy + 7, fg);
      break;
    case K_HIDE:
      gfx.drawRoundRect(cx - 12, cy - 9, 24, 13, 2, fg);
      for (int i = 0; i < 4; i++) gfx.fillRect(cx - 9 + i * 5, cy - 6, 3, 2, fg);
      gfx.fillRect(cx - 6, cy - 1, 12, 2, fg);
      gfx.fillTriangle(cx - 4, cy + 7, cx + 4, cy + 7, cx, cy + 11, fg);
      break;
    case K_SPACE:
      gfx.fillRect(cx - 20, cy + 4, 40, 2, theme::MUTED);
      break;
    default:
      gfx.setFont(font::uiBold());
      gfx.drawString(k.label, cx, cy);
  }
  gfx.setTextDatum(textdatum_t::top_left);
}

bool hit(int x, int y, int* row, int* col) {
  if (!visible_ || y < top()) return false;
  int r = std::max(0, std::min(3, (y - top() - kPad + kGap / 2) / (kRowH + kGap)));
  const Row& rw = kLayers[layer_][r];
  for (int c = 0; c < rw.n; c++) {
    ui::Rect rc = keyRect(r, c);
    if (x >= rc.x - 2 && x < rc.x + rc.w + 2) {
      if (rw.keys[c].kind == K_GAP) return true;  // on the keyboard, but no key
      *row = r;
      *col = c;
      return true;
    }
  }
  return true;
}

void send(input::Key k, char ch = 0) { input::inject(input::keyEvent(k, 0, ch)); }

void unpress() {
  if (pressedRow_ < 0) return;
  int r = pressedRow_, c = pressedCol_;
  pressedRow_ = pressedCol_ = -1;
  if (visible_) drawKey(r, c);
}

}  // namespace

bool visible() { return visible_; }
void show() { visible_ = true; }
void hide() {
  visible_ = false;
  pressedRow_ = -1;
}
void toggle() { visible_ ? hide() : show(); }
int height() { return visible_ ? kHeight : 0; }
bool contains(int x, int y) { return visible_ && y >= top(); }

void draw() {
  if (!visible_) return;
  gfx.fillRect(0, top(), gfx.width(), kHeight, theme::BAR);
  gfx.drawFastHLine(0, top(), gfx.width(), theme::BORDER);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < kLayers[layer_][r].n; c++) drawKey(r, c);
}

bool onDown(int x, int y) {
  int r = -1, c = -1;
  if (!hit(x, y, &r, &c)) return false;
  unpress();
  if (r >= 0) {
    pressedRow_ = r;
    pressedCol_ = c;
    drawKey(r, c);
  }
  return true;
}

bool onTap(int x, int y) {
  int r = -1, c = -1;
  if (!hit(x, y, &r, &c)) {
    unpress();
    return false;
  }
  unpress();
  if (r < 0) return true;
  const Key& k = kLayers[layer_][r].keys[c];
  switch (k.kind) {
    case K_CHR: {
      char ch = k.ch;
      if (shift_ && ch >= 'a' && ch <= 'z') ch -= 32;
      send(input::K_CHAR, ch);
      if (shift_ == 1) {
        shift_ = 0;
        draw();
      }
      break;
    }
    case K_SPACE: send(input::K_CHAR, ' '); break;
    case K_BACK: send(input::K_BACKSPACE); break;
    case K_ENTER: send(input::K_ENTER); break;
    case K_LEFT: send(input::K_LEFT); break;
    case K_RIGHT: send(input::K_RIGHT); break;
    case K_LINK:
      send(input::K_CHAR, '[');
      send(input::K_CHAR, '[');
      break;
    case K_SHIFT:
      // Tap: one capital. Double tap: caps lock. Tap again: off.
      if (shift_ == 1 && millis() - lastShift_ < 400) shift_ = 2;
      else shift_ = shift_ ? 0 : 1;
      lastShift_ = millis();
      draw();
      break;
    case K_LAYER:
      layer_ = k.ch;
      draw();
      break;
    case K_HIDE:
      app::toggleKeyboard();
      break;
    case K_GAP:
      break;
  }
  return true;
}

void cancelPress() { unpress(); }

}  // namespace osk
