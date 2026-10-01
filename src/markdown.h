#pragma once
// Markdown -> positioned text runs. Layout happens once per note (and on width change);
// rendering then just draws the runs that intersect the visible region.

#include <functional>
#include <string>
#include <vector>

#include "board.h"

namespace md {

enum Font : uint8_t { F_BODY, F_BOLD, F_ITALIC, F_BOLD_ITALIC, F_MONO, F_H1, F_H2, F_H3, F_COUNT };

enum RunFlag : uint8_t { RF_STRIKE = 1, RF_BG = 2 };

struct Run {
  int32_t top;
  int16_t x, w, h;
  uint8_t font, flags;
  uint16_t fg, bg;
  int16_t link;  // index into Doc::links, -1 if none
  uint32_t off;  // into Doc::pool
  uint16_t len;
};

enum DecoType : uint8_t { D_FILL, D_ROUND_FILL, D_HLINE, D_BULLET, D_CHECKBOX, D_CHECKBOX_DONE };

struct Deco {
  int32_t y;
  int16_t x, w, h;
  uint8_t type;
  uint16_t color;
};

struct Link {
  std::string target;    // as written (note name / path / url)
  std::string resolved;  // vault path, "" if unresolved or external
  bool external;
};

struct Doc {
  std::string pool;
  std::vector<Run> runs;
  std::vector<Deco> decos;
  std::vector<Link> links;
  int32_t height = 0;
};

// Maps a link target to a vault path ("" if it doesn't exist).
using Resolver = std::function<std::string(const std::string& target)>;

void layout(const std::string& source, int width, const Resolver& resolve, Doc& out);

// Draws the part of `doc` that falls in [docTop, docTop + dst.height()) into dst.
void render(LGFX_Sprite& dst, const Doc& doc, int32_t docTop);

// Returns the link index under document point (x, y), or -1.
int linkAt(const Doc& doc, int x, int32_t y);

// Converts UTF-8 to the ASCII subset the built-in fonts can draw.
std::string toAscii(const std::string& s);

}  // namespace md
