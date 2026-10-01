#include "display.h"

LGFX gfx;

static constexpr int kBandH = 32;

void renderBands(int top, int height, uint16_t bg,
                 const std::function<void(LGFX_Sprite&, int bandOffset)>& draw) {
  static LGFX_Sprite band(&gfx);
  if (band.width() != gfx.width()) {
    band.deleteSprite();
    band.setColorDepth(16);
    if (!band.createSprite(gfx.width(), kBandH)) {
      Serial.println("[display] band sprite alloc failed");
      return;
    }
  }
  gfx.setClipRect(0, top, gfx.width(), height);  // the last band may be partial
  for (int off = 0; off < height; off += kBandH) {
    band.fillSprite(bg);
    draw(band, off);
    band.pushSprite(0, top + off);
  }
  gfx.clearClipRect();
}
