#include "theme.h"

#include <Preferences.h>

namespace theme {

namespace {
bool light_ = false;
struct Pal {
  uint16_t* c;
  uint32_t dark, light;
};
}  // namespace

void applyTheme(bool light) {
  const Pal pals[] = {
      {&BG, 0x1e1e1e, 0xffffff},
      {&BG_ALT, 0x262626, 0xf2f2f2},
      {&BAR, 0x2b2b2b, 0xf3f3f3},
      {&BORDER, 0x3a3a3a, 0xd9d9d9},
      {&TEXT, 0xdadada, 0x2e2e2e},
      {&TEXT_BRIGHT, 0xf2f2f2, 0x111111},
      {&MUTED, 0x999999, 0x6e6e6e},
      {&FAINT, 0x666666, 0xa0a0a0},
      {&ACCENT, 0xa88bfa, 0x6a4fd8},
      {&ACCENT_DIM, 0x6f5fa8, 0xa596e6},
      {&ACCENT_BG, 0x3b3159, 0xe4ddff},
      {&CODE_BG, 0x2d2d2d, 0xf0f0f0},
      {&CODE_TEXT, 0xe8a87c, 0xb4541c},
      {&HIGHLIGHT_BG, 0x6b5a14, 0xfff0a0},
      {&QUOTE_BAR, 0x7f6df2, 0x7c5cf0},
      {&DANGER, 0xc0392b, 0xd64535},
      {&OK, 0x2e9e5b, 0x23864b},
      {&FOLDER, 0xd7b46a, 0xc49a3a},
  };
  for (auto& p : pals) *p.c = rgb(light ? p.light : p.dark);
  light_ = light;
}

bool lightTheme() { return light_; }

void loadTheme() {
  Preferences p;
  p.begin("ui", false);
  applyTheme(p.getBool("light", false));
  p.end();
}

void setLight(bool light) {
  applyTheme(light);
  Preferences p;
  p.begin("ui", false);
  p.putBool("light", light);
  p.end();
}

}  // namespace theme
