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

int glyphAdvance(uint8_t id, uint32_t cp) {
  if (cp < 0x80) return advance(id, (char)cp);
  if (cp > 0xFFFF) return -1;
  // Same lookup as GFXfont::getGlyph (which is private): find the EncodeRange holding cp
  auto* f = static_cast<const lgfx::GFXfont*>(get(id).font);
  if (cp < f->first || cp > f->last) return -1;
  if (f->range_num == 0) return f->glyph[cp - f->first].xAdvance;
  for (uint16_t i = 0; i < f->range_num; i++) {
    const auto& r = f->range[i];
    if (cp >= r.start && cp <= r.end) return f->glyph[cp - r.start + r.base].xAdvance;
  }
  return -1;
}

std::string printable(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    int n = decodeUtf8(s.data(), s.size(), i, &cp);
    if (cp == '\t') out += "    ";
    else if (cp == '\r') {
    } else if (cp < 0x80 || glyphAdvance(BODY, cp) >= 0) out.append(s, i, n);
    else out += asciiFor(cp);
    i += n;
  }
  return out;
}

std::string plainLine(const std::string& md) {
  std::string s = md;
  // Line prefixes: quote, list marker, task box, heading
  size_t p = s.find_first_not_of(" \t>");
  s = p == std::string::npos ? "" : s.substr(p);
  if (s.size() > 1 && strchr("-*+", s[0]) && s[1] == ' ') s = s.substr(2);
  if (s.size() > 3 && s[0] == '[' && s[2] == ']' && s[3] == ' ') s = s.substr(4);
  size_t h = 0;
  while (h < s.size() && s[h] == '#') h++;
  if (h && h < s.size() && s[h] == ' ') s = s.substr(h + 1);
  std::string out;
  for (size_t i = 0; i < s.size();) {
    if (s.compare(i, 2, "[[") == 0) {
      size_t e = s.find("]]", i + 2);
      if (e != std::string::npos) {
        std::string inner = s.substr(i + 2, e - i - 2);
        size_t bar = inner.find('|');
        out += bar == std::string::npos ? inner : inner.substr(bar + 1);
        i = e + 2;
        continue;
      }
    }
    if (s.compare(i, 2, "**") == 0 || s.compare(i, 2, "__") == 0 || s.compare(i, 2, "==") == 0 ||
        s.compare(i, 2, "~~") == 0 || s.compare(i, 2, "%%") == 0) {
      i += 2;
      continue;
    }
    if (s[i] == '`' || (s[i] == '*' && (i + 1 >= s.size() || s[i + 1] != ' '))) {
      i++;
      continue;
    }
    out += s[i++];
  }
  return out;
}

size_t prevChar(const std::string& s, size_t i) {
  if (i == 0) return 0;
  i--;
  while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--;
  return i;
}

size_t nextChar(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  i++;
  while (i < s.size() && ((uint8_t)s[i] & 0xC0) == 0x80) i++;
  return i;
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
