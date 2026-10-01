#include "display.h"

LGFX gfx;

static constexpr int kBandH = 20;

void renderBands(int top, int height, uint16_t bg,
                 const std::function<void(LGFX_Sprite&, int bandOffset)>& draw) {
  // Two band buffers: while DMA sends one to the panel, the next is drawn into the other.
  static LGFX_Sprite bands[2] = {LGFX_Sprite(&gfx), LGFX_Sprite(&gfx)};
  if (bands[0].width() != gfx.width()) {
    for (auto& b : bands) {
      b.deleteSprite();
      b.setColorDepth(16);
      if (!b.createSprite(gfx.width(), kBandH)) {
        Serial.println("[display] band sprite alloc failed");
        return;
      }
    }
  }
  gfx.startWrite();
  int which = 0;
  for (int off = 0; off < height; off += kBandH) {
    LGFX_Sprite& b = bands[which];
    which ^= 1;
    // pushImageDMA below waits for the previous transfer, so by the time this buffer
    // comes round again its own transfer has finished.
    b.fillSprite(bg);
    draw(b, off);
    int h = std::min(kBandH, height - off);
    gfx.pushImageDMA(0, top + off, b.width(), h, (const lgfx::swap565_t*)b.getBuffer());
  }
  gfx.endWrite();
}
