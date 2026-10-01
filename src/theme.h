#pragma once
// Colours, fonts and layout constants (loosely Obsidian's default dark theme).

#include "board.h"
#include "fonts/fonts.h"

constexpr uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F);
}

namespace theme {
// Colours are variables so the theme can switch at runtime (see applyTheme()).
inline uint16_t BG = rgb(0x1e1e1e);
inline uint16_t BG_ALT = rgb(0x262626);
inline uint16_t BAR = rgb(0x2b2b2b);
inline uint16_t BORDER = rgb(0x3a3a3a);
inline uint16_t TEXT = rgb(0xdadada);
inline uint16_t TEXT_BRIGHT = rgb(0xf2f2f2);
inline uint16_t MUTED = rgb(0x999999);
inline uint16_t FAINT = rgb(0x666666);
inline uint16_t ACCENT = rgb(0xa88bfa);
inline uint16_t ACCENT_DIM = rgb(0x6f5fa8);
inline uint16_t ACCENT_BG = rgb(0x3b3159);
inline uint16_t CODE_BG = rgb(0x2d2d2d);
inline uint16_t CODE_TEXT = rgb(0xe8a87c);
inline uint16_t HIGHLIGHT_BG = rgb(0x6b5a14);
inline uint16_t QUOTE_BAR = rgb(0x7f6df2);
inline uint16_t DANGER = rgb(0xc0392b);
inline uint16_t OK = rgb(0x2e9e5b);
inline uint16_t FOLDER = rgb(0xd7b46a);

// Switches every colour above to the light or dark palette (callers redraw afterwards).
void applyTheme(bool light);
bool lightTheme();
// Saved preference: loadTheme() applies it at boot, setLight() applies and saves.
void loadTheme();
void setLight(bool light);

constexpr int BAR_H = 36;
constexpr int MARGIN = 12;
}  // namespace theme

namespace font {
// Text fonts cover accented Latin, Greek, Cyrillic and common symbols; small() is ASCII only.
inline const lgfx::IFont* body() { return &ObSans9; }
inline const lgfx::IFont* bold() { return &ObSansBold9; }
inline const lgfx::IFont* italic() { return &ObSansOblique9; }
inline const lgfx::IFont* boldItalic() { return &ObSansBoldOblique9; }
inline const lgfx::IFont* mono() { return &ObMono9; }
inline const lgfx::IFont* h1() { return &ObSansBold18; }
inline const lgfx::IFont* h2() { return &ObSansBold12; }
inline const lgfx::IFont* ui() { return &ObSans9; }
inline const lgfx::IFont* uiBold() { return &ObSansBold9; }
inline const lgfx::IFont* small() { return &fonts::Font2; }
}  // namespace font
