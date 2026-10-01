#pragma once
// Colours, fonts and layout constants (loosely Obsidian's default dark theme).

#include "board.h"

constexpr uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x1F);
}

namespace theme {
constexpr uint16_t BG = rgb(0x1e1e1e);
constexpr uint16_t BG_ALT = rgb(0x262626);
constexpr uint16_t BAR = rgb(0x2b2b2b);
constexpr uint16_t BORDER = rgb(0x3a3a3a);
constexpr uint16_t TEXT = rgb(0xdadada);
constexpr uint16_t TEXT_BRIGHT = rgb(0xf2f2f2);
constexpr uint16_t MUTED = rgb(0x999999);
constexpr uint16_t FAINT = rgb(0x666666);
constexpr uint16_t ACCENT = rgb(0xa88bfa);
constexpr uint16_t ACCENT_DIM = rgb(0x6f5fa8);
constexpr uint16_t ACCENT_BG = rgb(0x3b3159);
constexpr uint16_t CODE_BG = rgb(0x2d2d2d);
constexpr uint16_t CODE_TEXT = rgb(0xe8a87c);
constexpr uint16_t HIGHLIGHT_BG = rgb(0x6b5a14);
constexpr uint16_t QUOTE_BAR = rgb(0x7f6df2);
constexpr uint16_t DANGER = rgb(0xc0392b);
constexpr uint16_t OK = rgb(0x2e9e5b);
constexpr uint16_t FOLDER = rgb(0xd7b46a);

constexpr int BAR_H = 36;
constexpr int MARGIN = 12;
}  // namespace theme

namespace font {
inline const lgfx::IFont* body() { return &fonts::FreeSans9pt7b; }
inline const lgfx::IFont* bold() { return &fonts::FreeSansBold9pt7b; }
inline const lgfx::IFont* italic() { return &fonts::FreeSansOblique9pt7b; }
inline const lgfx::IFont* boldItalic() { return &fonts::FreeSansBoldOblique9pt7b; }
inline const lgfx::IFont* mono() { return &fonts::FreeMono9pt7b; }
inline const lgfx::IFont* h1() { return &fonts::FreeSansBold18pt7b; }
inline const lgfx::IFont* h2() { return &fonts::FreeSansBold12pt7b; }
inline const lgfx::IFont* ui() { return &fonts::FreeSans9pt7b; }
inline const lgfx::IFont* uiBold() { return &fonts::FreeSansBold9pt7b; }
inline const lgfx::IFont* small() { return &fonts::Font2; }
}  // namespace font
