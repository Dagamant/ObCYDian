#pragma once
// Fonts used for note text, with cached per-glyph advances so layout never has to call
// into the graphics library to measure strings.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "board.h"

namespace tf {

enum FontId : uint8_t { BODY, BOLD, ITALIC, BOLD_ITALIC, MONO, H1, H2, H3, COUNT };

struct FontInfo {
  const lgfx::IFont* font;
  int16_t ascent, descent;
  uint8_t advance[95];  // ' '..'~'
};

const FontInfo& get(uint8_t id);

inline int advance(uint8_t id, char c) {
  uint8_t u = (uint8_t)c;
  return (u >= 32 && u < 127) ? get(id).advance[u - 32] : get(id).advance['?' - 32];
}

// Width of an ASCII string as drawString would lay it out.
int width(uint8_t id, const char* s, size_t len);

// Decodes one UTF-8 codepoint at s[i]; returns its byte length (>= 1).
int decodeUtf8(const char* s, size_t len, size_t i, uint32_t* cp);

// ASCII stand-in for a codepoint the bitmap fonts can't draw ("" to skip it).
const char* asciiFor(uint32_t cp);

// Converts UTF-8 text to the ASCII subset the fonts can draw (for names and labels).
std::string toAscii(const std::string& s);

}  // namespace tf
