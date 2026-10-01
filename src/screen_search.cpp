#include "osk.h"
#include "screens.h"

static constexpr int kInputY = theme::BAR_H + 8;
static constexpr int kInputH = 38;
static constexpr int kListY = kInputY + kInputH + 26;
static constexpr int kRowH = 44;

int SearchScreen::rowsVisible() const { return std::max(1, (ui::contentBottom() - kListY) / kRowH); }

void SearchScreen::draw() {
  ui::topBar("Search notes", ui::Icon::Back, ui::Icon::Keyboard);
  gfx.fillRect(0, theme::BAR_H, gfx.width(), ui::contentBottom() - theme::BAR_H, theme::BG);
  drawInput();
  drawResults();
}

void SearchScreen::drawInput() {
  const int x = theme::MARGIN, w = gfx.width() - 2 * theme::MARGIN;
  gfx.fillRoundRect(x, kInputY, w, kInputH, 6, theme::BG_ALT);
  gfx.drawRoundRect(x, kInputY, w, kInputH, 6, theme::ACCENT_DIM);
  ui::icon(gfx, ui::Icon::Search, x + 18, kInputY + kInputH / 2, theme::MUTED);
  gfx.setFont(font::ui());
  gfx.setTextDatum(textdatum_t::middle_left);
  const int tx = x + 36, cy = kInputY + kInputH / 2;
  if (query_.empty()) {
    gfx.setTextColor(theme::FAINT);
    gfx.drawString("Text to find in your notes", tx + 4, cy);
  } else {
    gfx.setTextColor(theme::TEXT_BRIGHT);
    gfx.drawString(ui::ellipsize(query_, w - 50, font::ui()).c_str(), tx, cy);
  }
  int cx = tx + gfx.textWidth(query_.substr(0, cursor_).c_str(), font::ui());
  gfx.fillRect(std::min(cx, x + w - 8), cy - 10, 2, 20, theme::ACCENT);
  gfx.setTextDatum(textdatum_t::top_left);
}

void SearchScreen::drawResults() {
  const int W = gfx.width(), M = theme::MARGIN;
  gfx.fillRect(0, kInputY + kInputH + 2, W, ui::contentBottom() - kInputY - kInputH - 2, theme::BG);
  gfx.setFont(font::small());
  gfx.setTextColor(theme::MUTED);
  std::string status = dueAt_ ? "Searching..."
                       : ran_.empty() ? "Matches note text in every folder (not case sensitive)"
                       : hits_.empty() ? "No matches"
                                       : std::to_string(hits_.size()) + (hits_.size() >= 50 ? "+" : "") + " matches";
  gfx.drawString(status.c_str(), M + 4, kInputY + kInputH + 6);

  const int rows = rowsVisible();
  for (int i = first_; i < (int)hits_.size() && i < first_ + rows; i++) {
    const auto& h = hits_[i];
    const int y = kListY + (i - first_) * kRowH;
    if (i == sel_) gfx.fillRoundRect(M - 4, y, W - 2 * M + 8, kRowH - 4, 6, theme::ACCENT_BG);
    gfx.setFont(font::uiBold());
    gfx.setTextColor(theme::TEXT_BRIGHT);
    std::string title = storage::baseName(h.path);
    std::string folder = storage::parentDir(h.path);
    gfx.drawString(ui::ellipsize(tf::printable(title), W / 2, font::uiBold()).c_str(), M + 4, y + 2);
    gfx.setFont(font::small());
    gfx.setTextColor(theme::FAINT);
    std::string where = (folder == "/" ? "" : tf::toAscii(folder.substr(1)) + "  ") + "line " + std::to_string(h.line + 1);
    gfx.setTextDatum(textdatum_t::top_right);
    gfx.drawString(where.c_str(), W - M - 4, y + 4);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setFont(font::ui());
    gfx.setTextColor(theme::MUTED);
    gfx.drawString(ui::ellipsize(tf::printable(h.text), W - 2 * M - 8, font::ui()).c_str(), M + 4, y + 21);
  }
  if ((int)hits_.size() > rows) {  // scrollbar
    int track = rows * kRowH, thumb = std::max(16, track * rows / (int)hits_.size());
    int ty = kListY + (track - thumb) * first_ / std::max(1, (int)hits_.size() - rows);
    gfx.fillRoundRect(W - 4, ty, 3, thumb, 1, theme::BORDER);
  }
}

void SearchScreen::run() {
  dueAt_ = 0;
  ran_ = query_;
  hits_ = query_.size() >= 2 ? storage::searchText(query_, 50) : std::vector<storage::Hit>();
  sel_ = first_ = 0;
  drawResults();
}

void SearchScreen::tick() {
  if (dueAt_ && millis() >= dueAt_) run();
}

void SearchScreen::onKey(const input::Event& e) {
  using namespace input;
  const int n = hits_.size();
  bool edited = false;
  switch (e.key) {
    case K_ESC: return app::back();
    case K_ENTER:
      if (dueAt_ || ran_ != query_) {  // not searched yet: search now
        if (osk::visible()) app::toggleKeyboard();
        return run();
      }
      if (osk::visible()) return app::toggleKeyboard();  // first Enter: show more results
      if (n) app::openNoteAt(hits_[sel_].path, hits_[sel_].line);
      return;
    case K_UP:
    case K_DOWN:
      if (!n) return;
      sel_ = std::max(0, std::min(n - 1, sel_ + (e.key == K_UP ? -1 : 1)));
      if (sel_ < first_) first_ = sel_;
      if (sel_ >= first_ + rowsVisible()) first_ = sel_ - rowsVisible() + 1;
      return drawResults();
    case K_LEFT: cursor_ = tf::prevChar(query_, cursor_); return drawInput();
    case K_RIGHT: cursor_ = tf::nextChar(query_, cursor_); return drawInput();
    case K_HOME: cursor_ = 0; return drawInput();
    case K_END: cursor_ = query_.size(); return drawInput();
    case K_BACKSPACE:
      if (cursor_ > 0) {
        size_t from = tf::prevChar(query_, cursor_);
        query_.erase(from, cursor_ - from);
        cursor_ = from;
        edited = true;
      }
      break;
    case K_DELETE:
      if (cursor_ < query_.size()) {
        query_.erase(cursor_, tf::nextChar(query_, cursor_) - cursor_);
        edited = true;
      }
      break;
    case K_CHAR:
      if (e.ctrl() || e.alt() || query_.size() >= 80) return;
      query_.insert(cursor_++, 1, e.ch);
      edited = true;
      break;
    default:
      return;
  }
  if (edited) {
    dueAt_ = millis() + 500;
    drawInput();
    drawResults();
  }
}

void SearchScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (ui::hitRight(x, y)) return app::toggleKeyboard();
  if (y < kListY) return;
  int i = first_ + (y - kListY) / kRowH;
  if (i >= 0 && i < (int)hits_.size() && i < first_ + rowsVisible()) app::openNoteAt(hits_[i].path, hits_[i].line);
}

void SearchScreen::onDrag(int dy) {
  int n = hits_.size(), rows = rowsVisible();
  static int acc = 0;
  acc += dy;
  int step = -acc / kRowH;
  if (!step) return;
  acc += step * kRowH;
  first_ = std::max(0, std::min(std::max(0, n - rows), first_ + step));
  drawResults();
}
