#include "screens.h"

void ViewerScreen::open(const std::string& path, int scroll) {
  if (path != path_ || !loaded_) {
    path_ = path;
    std::string text;
    loaded_ = storage::readFile(path_, text);
    uint32_t t = millis();
    md::layout(text, gfx.width(), [this](const std::string& target) {
      return storage::resolveLink(target, path_);
    }, doc_);
    Serial.printf("[viewer] %s: %u bytes, %u runs, %d px, layout %lu ms, heap %u\n", path_.c_str(),
                  (unsigned)text.size(), (unsigned)doc_.runs.size(), (int)doc_.height,
                  millis() - t, ESP.getFreeHeap());
  }
  scroll_ = std::max(0, std::min(scroll, maxScroll()));
}

int ViewerScreen::maxScroll() const {
  return std::max(0, (int)doc_.height - (gfx.height() - theme::BAR_H));
}

void ViewerScreen::draw() {
  ui::topBar(md::toAscii(storage::baseName(path_)), ui::Icon::Back, ui::Icon::Files);
  if (!loaded_) {
    gfx.fillRect(0, theme::BAR_H, gfx.width(), gfx.height() - theme::BAR_H, theme::BG);
    ui::message("Can't open note", path_, theme::BAR_H + 60);
    return;
  }
  drawContent();
}

void ViewerScreen::drawContent() {
  const int top = theme::BAR_H;
  const int h = gfx.height() - top;
  const int w = gfx.width();
  const int ms = maxScroll();
  renderBands(top, h, theme::BG, [&](LGFX_Sprite& s, int off) {
    md::render(s, doc_, scroll_ + off);
    if (ms > 0) {
      int thumb = std::max(24, h * h / (h + ms));
      int ty = (h - thumb) * scroll_ / ms - off;
      s.fillRoundRect(w - 4, ty, 3, thumb, 1, theme::BORDER);
    }
  });
}

void ViewerScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (ui::hitRight(x, y)) return app::openFolder(storage::parentDir(path_));
  if (y < theme::BAR_H) return;

  int li = md::linkAt(doc_, x, y - theme::BAR_H + scroll_);
  if (li < 0) return;
  const md::Link& l = doc_.links[li];
  Serial.printf("[viewer] link '%s' -> '%s'\n", l.target.c_str(), l.resolved.c_str());
  if (l.external) {
    app::toast("External link: " + l.target);
  } else if (l.resolved.empty()) {
    app::toast("\"" + l.target + "\" doesn't exist yet");
  } else if (l.resolved == path_) {
    app::toast("Already here");
  } else {
    app::openNote(l.resolved);
  }
}

void ViewerScreen::onDrag(int dy) {
  int s = std::max(0, std::min(scroll_ - dy, maxScroll()));
  if (s != scroll_) {
    scroll_ = s;
    drawContent();
  }
}
