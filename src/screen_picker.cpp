#include "screens.h"

static constexpr int kInputY = theme::BAR_H + 8;
static constexpr int kInputH = 36;

void PickerScreen::load(PickerData* data) {
  d_ = data;
  query_.clear();
  sel_ = first_ = 0;
  refilter();
}

int PickerScreen::listY() const { return d_ && d_->filterable ? kInputY + kInputH + 8 : theme::BAR_H + 4; }

int PickerScreen::rowH() const {
  for (auto& it : d_->items)
    if (!it.sub.empty()) return 46;
  return 36;
}

int PickerScreen::rows() const { return std::max(1, (ui::contentBottom() - listY()) / rowH()); }

// Case-insensitive: every query character appears in order (prefix/substring rank first)
static int matchScore(const std::string& text, const std::string& q) {
  if (q.empty()) return 0;
  std::string t, l;
  for (char c : text) t += tolower((unsigned char)c);
  for (char c : q) l += tolower((unsigned char)c);
  size_t at = t.find(l);
  if (at == 0) return 0;
  if (at != std::string::npos) return 1;
  size_t k = 0;
  for (char c : t)
    if (k < l.size() && c == l[k]) k++;
  return k == l.size() ? 2 : -1;
}

void PickerScreen::refilter() {
  shown_.clear();
  std::vector<std::pair<int, int>> scored;
  for (int i = 0; i < (int)d_->items.size(); i++) {
    int s = matchScore(d_->items[i].label, query_);
    if (s >= 0) scored.push_back({s, i});
  }
  if (!query_.empty())
    std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (auto& s : scored) shown_.push_back(s.second);
  sel_ = std::min(sel_, std::max(0, (int)shown_.size() - 1));
  first_ = std::min(first_, std::max(0, (int)shown_.size() - rows()));
}

void PickerScreen::draw() {
  ui::topBar(d_->title, ui::Icon::Back, d_->filterable ? ui::Icon::Keyboard : ui::Icon::None);
  gfx.fillRect(0, theme::BAR_H, gfx.width(), ui::contentBottom() - theme::BAR_H, theme::BG);
  refilter();
  if (d_->filterable) drawInput();
  drawList();
}

void PickerScreen::drawInput() {
  const int x = theme::MARGIN, w = gfx.width() - 2 * theme::MARGIN;
  gfx.fillRoundRect(x, kInputY, w, kInputH, 6, theme::BG_ALT);
  gfx.drawRoundRect(x, kInputY, w, kInputH, 6, theme::ACCENT_DIM);
  gfx.setFont(font::ui());
  gfx.setTextDatum(textdatum_t::middle_left);
  const int cy = kInputY + kInputH / 2;
  if (query_.empty()) {
    gfx.setTextColor(theme::FAINT);
    gfx.drawString("Type to filter", x + 12, cy);
  } else {
    gfx.setTextColor(theme::TEXT_BRIGHT);
    gfx.drawString(ui::ellipsize(query_, w - 30, font::ui()).c_str(), x + 12, cy);
  }
  gfx.fillRect(std::min(x + 12 + gfx.textWidth(query_.c_str(), font::ui()), x + w - 8), cy - 9, 2, 18, theme::ACCENT);
  gfx.setTextDatum(textdatum_t::top_left);
}

void PickerScreen::drawList() {
  const int W = gfx.width(), M = theme::MARGIN, y0 = listY(), h = rowH(), n = rows();
  gfx.fillRect(0, y0 - 2, W, ui::contentBottom() - y0 + 2, theme::BG);
  if (shown_.empty()) {
    gfx.setFont(font::ui());
    gfx.setTextColor(theme::MUTED);
    gfx.drawString(query_.empty() ? d_->empty.c_str() : "No matches", M + 4, y0 + 10);
    return;
  }
  for (int r = 0; r < n && first_ + r < (int)shown_.size(); r++) {
    const int vi = first_ + r;
    const PickItem& it = d_->items[shown_[vi]];
    const int y = y0 + r * h;
    if (vi == sel_) gfx.fillRoundRect(M - 6, y + 1, W - 2 * M + 12, h - 3, 6, theme::ACCENT_BG);
    int x = M + 2 + it.indent;
    if (it.check >= 0) {  // tappable checkbox
      const int by = y + (it.sub.empty() ? h / 2 : 14) - 7;
      if (it.check) {
        gfx.fillRoundRect(x, by, 15, 15, 3, theme::ACCENT);
        gfx.drawLine(x + 3, by + 7, x + 6, by + 10, theme::BG);
        gfx.drawLine(x + 6, by + 10, x + 11, by + 4, theme::BG);
        gfx.drawLine(x + 3, by + 8, x + 6, by + 11, theme::BG);
        gfx.drawLine(x + 6, by + 11, x + 11, by + 5, theme::BG);
      } else {
        gfx.drawRoundRect(x, by, 15, 15, 3, theme::MUTED);
      }
      x += 26;
    }
    int detailW = 0;
    if (!it.detail.empty()) {
      gfx.setFont(font::small());
      std::string det = ui::ellipsize(tf::toAscii(it.detail), 150, font::small());
      detailW = gfx.textWidth(det.c_str()) + 10;
      gfx.setTextColor(theme::FAINT);
      gfx.setTextDatum(textdatum_t::top_right);
      gfx.drawString(det.c_str(), W - M - 2, y + (it.sub.empty() ? h / 2 - 8 : 6));
      gfx.setTextDatum(textdatum_t::top_left);
    }
    gfx.setFont(it.sub.empty() ? font::ui() : font::uiBold());
    gfx.setTextColor(it.check == 1 ? theme::MUTED : theme::TEXT_BRIGHT);
    gfx.setTextDatum(it.sub.empty() ? textdatum_t::middle_left : textdatum_t::top_left);
    gfx.drawString(ui::ellipsize(tf::printable(tf::plainLine(it.label)), W - x - M - detailW, font::ui()).c_str(), x,
                   it.sub.empty() ? y + h / 2 : y + 3);
    gfx.setTextDatum(textdatum_t::top_left);
    if (!it.sub.empty()) {
      gfx.setFont(font::ui());
      gfx.setTextColor(theme::MUTED);
      gfx.drawString(ui::ellipsize(tf::printable(tf::plainLine(it.sub)), W - x - M, font::ui()).c_str(), x, y + 23);
    }
  }
  if ((int)shown_.size() > n) {
    int track = n * h, thumb = std::max(16, track * n / (int)shown_.size());
    int ty = y0 + (track - thumb) * first_ / std::max(1, (int)shown_.size() - n);
    gfx.fillRoundRect(W - 4, ty, 3, thumb, 1, theme::BORDER);
  }
}

void PickerScreen::pick(int vi) {
  if (vi < 0 || vi >= (int)shown_.size()) return;
  app::pickerChose(shown_[vi]);
}

void PickerScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (d_->filterable && ui::hitRight(x, y)) return app::toggleKeyboard();
  if (y < listY()) return;
  int vi = first_ + (y - listY()) / rowH();
  if (vi >= (int)shown_.size() || vi >= first_ + rows()) return;
  PickItem& it = d_->items[shown_[vi]];
  if (it.check >= 0 && d_->onToggle && x < theme::MARGIN + it.indent + 34) {
    it.check = d_->onToggle(shown_[vi]);
    sel_ = vi;
    return drawList();
  }
  sel_ = vi;
  pick(vi);
}

void PickerScreen::onDrag(int dy) {
  dragAcc_ += dy;
  int step = -dragAcc_ / rowH();
  if (!step) return;
  dragAcc_ += step * rowH();
  first_ = std::max(0, std::min(std::max(0, (int)shown_.size() - rows()), first_ + step));
  drawList();
}

void PickerScreen::onKey(const input::Event& e) {
  using namespace input;
  const int n = shown_.size();
  switch (e.key) {
    case K_ESC: return app::back();
    case K_ENTER: return pick(sel_);
    case K_UP:
    case K_DOWN:
    case K_PGUP:
    case K_PGDN: {
      if (!n) return;
      int step = (e.key == K_PGUP || e.key == K_PGDN) ? rows() : 1;
      if (e.key == K_UP || e.key == K_PGUP) step = -step;
      sel_ = std::max(0, std::min(n - 1, sel_ + step));
      if (sel_ < first_) first_ = sel_;
      if (sel_ >= first_ + rows()) first_ = sel_ - rows() + 1;
      return drawList();
    }
    case K_CHAR:
      if (e.isChar(' ') && !d_->filterable && n && d_->items[shown_[sel_]].check >= 0 && d_->onToggle) {
        d_->items[shown_[sel_]].check = d_->onToggle(shown_[sel_]);
        return drawList();
      }
      if (!d_->filterable || e.ctrl() || e.alt()) return;
      query_ += e.ch;
      break;
    case K_BACKSPACE:
      if (!d_->filterable || query_.empty()) return;
      query_.erase(tf::prevChar(query_, query_.size()));
      break;
    default:
      return;
  }
  sel_ = first_ = 0;
  refilter();
  drawInput();
  drawList();
}
