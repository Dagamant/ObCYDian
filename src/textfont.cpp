#include "textfont.h"

#include "theme.h"

namespace tf {

static FontInfo fonts_[COUNT];
static bool ready_ = false;

static void init() {
  const lgfx::IFont* f[COUNT] = {font::body(), font::bold(), font::italic(), font::boldItalic(),
                                 font::mono(), font::h1(),   font::h2(),     font::bold()};
  for (int i = 0; i < COUNT; i++) {
    lgfx::FontMetrics m;
    f[i]->getDefaultMetric(&m);
    fonts_[i].font = f[i];
    fonts_[i].ascent = m.baseline;
    fonts_[i].descent = m.height - m.baseline;
    auto* gfxFont = static_cast<const lgfx::GFXfont*>(f[i]);
    for (int c = 32; c < 127; c++) {
      uint8_t adv = 0;
      if (c >= gfxFont->first && c <= gfxFont->last) adv = gfxFont->glyph[c - gfxFont->first].xAdvance;
      fonts_[i].advance[c - 32] = adv;
    }
  }
  ready_ = true;
}

const FontInfo& get(uint8_t id) {
  if (!ready_) init();
  return fonts_[id];
}

int width(uint8_t id, const char* s, size_t len) {
  int w = 0;
  for (size_t i = 0; i < len; i++) w += advance(id, s[i]);
  return w;
}

int decodeUtf8(const char* s, size_t len, size_t i, uint32_t* cp) {
  uint8_t c = s[i];
  if (c < 0x80) {
    *cp = c;
    return 1;
  }
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t v = c & (0x3F >> extra);
  int n = 1;
  for (; n <= extra && i + n < len && ((uint8_t)s[i + n] & 0xC0) == 0x80; n++) {
    v = (v << 6) | (s[i + n] & 0x3F);
  }
  *cp = v;
  return n;
}

const char* asciiFor(uint32_t cp) {
  switch (cp) {
    case 0x2018: case 0x2019: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x2033: return "\"";
    case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2026: return "...";
    case 0x2022: case 0x00B7: return "*";
    case 0x00A0: case 0x2009: case 0x200A: return " ";
    case 0x2192: return "->";
    case 0x2190: return "<-";
    case 0x2713: case 0x2714: case 0x2705: return "v";
    case 0x00D7: return "x";
    case 0x00B0: return " deg";
    case 0x200B: case 0xFE0F: case 0x200D: return "";
    default: return "?";
  }
}

std::string toAscii(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    i += decodeUtf8(s.data(), s.size(), i, &cp);
    if (cp == '\t') out += "    ";
    else if (cp == '\r') continue;
    else if (cp < 0x80) out += (char)cp;
    else out += asciiFor(cp);
  }
  return out;
}

}  // namespace tf
