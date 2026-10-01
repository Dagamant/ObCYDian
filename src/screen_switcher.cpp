#include "osk.h"
#include "screens.h"

static constexpr int kInputY = theme::BAR_H + 8;
static constexpr int kInputH = 40;
static constexpr int kListY = kInputY + kInputH + 12;
static constexpr int kRowH = 36;
static constexpr int kMaxRows = 6;

// Rows that fit above the on-screen keyboard
static int maxRows() { return std::max(1, std::min(kMaxRows, (ui::contentBottom() - kListY) / kRowH)); }

void SwitcherScreen::open(app::SwitcherMode mode, const std::string& dir, const std::string& path) {
  mode_ = mode;
  dir_ = dir;
  path_ = path;
  query_ = mode == app::SwitcherMode::Rename ? tf::toAscii(storage::baseName(path)) : "";
  cursor_ = query_.size();
  sel_ = 0;
  refresh();
}

std::string SwitcherScreen::target() const {
  std::string q = query_;
  while (!q.empty() && q.back() == ' ') q.pop_back();
  if (q.empty()) return "";
  // Names with a '/' are paths from the vault root; plain names stay in dir_.
  std::string base = q.find('/') != std::string::npos ? "/" : dir_;
  std::string path = storage::joinPath(base, q);
  while (path.find("//") != std::string::npos) path.erase(path.find("//"), 1);
  if (path.size() < 3 || strcasecmp(path.c_str() + path.size() - 3, ".md") != 0) path += ".md";
  return path;
}

void SwitcherScreen::refresh() {
  results_.clear();
  offerCreate_ = false;
  if (mode_ == app::SwitcherMode::Open) {
    results_ = storage::search(query_, maxRows());
    if (!query_.empty()) {
      bool exact = false;
      for (auto& r : results_)
        if (strcasecmp(storage::baseName(r).c_str(), query_.c_str()) == 0) exact = true;
      offerCreate_ = !exact;
      if (offerCreate_ && (int)results_.size() == maxRows()) results_.pop_back();
    }
  }
  int n = results_.size() + (offerCreate_ ? 1 : 0);
  sel_ = n ? std::min(sel_, n - 1) : 0;
}

void SwitcherScreen::draw() {
  const char* title = mode_ == app::SwitcherMode::Open  ? "Open note"
                      : mode_ == app::SwitcherMode::New ? "New note"
                                                        : "Rename note";
  ui::topBar(title, ui::Icon::Back, ui::Icon::Keyboard);
  refresh();  // the keyboard may have changed how many rows fit
  gfx.fillRect(0, theme::BAR_H, gfx.width(), ui::contentBottom() - theme::BAR_H, theme::BG);
  drawInput();
  drawList();
}

void SwitcherScreen::drawInput() {
  const int x = theme::MARGIN, w = gfx.width() - 2 * theme::MARGIN;
  gfx.fillRoundRect(x, kInputY, w, kInputH, 6, theme::BG_ALT);
  gfx.drawRoundRect(x, kInputY, w, kInputH, 6, theme::ACCENT_DIM);
  gfx.setFont(font::ui());
  gfx.setTextDatum(textdatum_t::middle_left);
  const int tx = x + 12, cy = kInputY + kInputH / 2;
  if (query_.empty()) {
    gfx.setTextColor(theme::FAINT);
    gfx.drawString(mode_ == app::SwitcherMode::Open ? "Find or create a note..." : "Note name", tx + 4, cy);
  } else {
    // Keep the cursor in view for long names
    std::string shown = query_;
    size_t off = 0;
    while (gfx.textWidth(shown.substr(off, cursor_ - off).c_str(), font::ui()) > w - 40) off++;
    shown = shown.substr(off);
    gfx.setTextColor(theme::TEXT_BRIGHT);
    gfx.drawString(ui::ellipsize(shown, w - 30, font::ui()).c_str(), tx, cy);
    int cx = tx + gfx.textWidth(query_.substr(off, cursor_ - off).c_str(), font::ui());
    gfx.fillRect(cx, cy - 10, 2, 20, theme::ACCENT);
  }
  if (query_.empty()) gfx.fillRect(tx, cy - 10, 2, 20, theme::ACCENT);
  gfx.setTextDatum(textdatum_t::top_left);
}

void SwitcherScreen::drawList() {
  const int W = gfx.width();
  gfx.fillRect(0, kListY, W, ui::contentBottom() - kListY, theme::BG);
  gfx.setTextDatum(textdatum_t::middle_left);
  if (mode_ != app::SwitcherMode::Open) {
    std::string t = target();
    gfx.setFont(font::ui());
    gfx.setTextColor(theme::MUTED);
    std::string hint = t.empty() ? "Type a name, then press Enter" : "Enter: " +
                       std::string(mode_ == app::SwitcherMode::New ? "create " : "rename to ") + t.substr(1);
    gfx.drawString(ui::ellipsize(tf::toAscii(hint), W - 2 * theme::MARGIN, font::ui()).c_str(),
                   theme::MARGIN + 4, kListY + 14);
    gfx.setFont(font::small());
    gfx.setTextColor(theme::FAINT);
    gfx.drawString("Use / for folders, e.g. Projects/Plan.   Esc cancels.", theme::MARGIN + 4, kListY + 42);
    gfx.setTextDatum(textdatum_t::top_left);
    return;
  }
  int n = results_.size() + (offerCreate_ ? 1 : 0);
  for (int i = 0; i < n; i++) {
    int y = kListY + i * kRowH;
    if (i == sel_) gfx.fillRoundRect(theme::MARGIN, y, W - 2 * theme::MARGIN, kRowH - 2, 5, theme::ACCENT_BG);
    gfx.setFont(font::ui());
    if (i < (int)results_.size()) {
      const std::string& p = results_[i];
      ui::noteIcon(gfx, theme::MARGIN + 8, y + 8, theme::MUTED);
      gfx.setTextColor(theme::TEXT_BRIGHT);
      gfx.drawString(ui::ellipsize(tf::toAscii(storage::baseName(p)), W - 200, font::ui()).c_str(),
                     theme::MARGIN + 36, y + kRowH / 2 - 1);
      std::string folder = storage::parentDir(p);
      if (folder != "/") {
        gfx.setFont(font::small());
        gfx.setTextColor(theme::MUTED);
        gfx.setTextDatum(textdatum_t::middle_right);
        gfx.drawString(ui::ellipsize(tf::toAscii(folder.substr(1)), 140, font::small()).c_str(),
                       W - theme::MARGIN - 10, y + kRowH / 2 - 1);
        gfx.setTextDatum(textdatum_t::middle_left);
      }
    } else {
      ui::icon(gfx, ui::Icon::Plus, theme::MARGIN + 18, y + kRowH / 2 - 1, theme::ACCENT);
      gfx.setTextColor(theme::ACCENT);
      gfx.drawString(ui::ellipsize("Create \"" + tf::toAscii(query_) + "\"", W - 80, font::ui()).c_str(),
                     theme::MARGIN + 36, y + kRowH / 2 - 1);
    }
  }
  if (n == 0) {
    gfx.setFont(font::ui());
    gfx.setTextColor(theme::FAINT);
    gfx.drawString("No notes yet", theme::MARGIN + 4, kListY + 14);
  }
  gfx.setTextDatum(textdatum_t::top_left);
}

void SwitcherScreen::commit(int index) {
  if (mode_ == app::SwitcherMode::Open && index < (int)results_.size() && index >= 0) {
    return app::finishSwitcher(results_[index], false);
  }
  std::string t = target();
  if (t.empty()) return;
  if (mode_ == app::SwitcherMode::Rename) {
    if (strcasecmp(t.c_str(), path_.c_str()) == 0) return app::back();
    if (storage::exists(t)) return app::toast("A note with that name exists");
    int links = 0;
    if (!storage::renameNote(path_, t, &links)) return app::toast("Rename failed");
    app::notePathChanged(path_, t);
    app::back();
    if (links) app::toast("Updated " + std::to_string(links) + (links == 1 ? " link" : " links"));
    return;
  }
  std::string p = storage::createNote("/", t);
  if (p.empty()) return app::toast("Couldn't create note");
  app::finishSwitcher(p, true);
}

void SwitcherScreen::onKey(const input::Event& e) {
  using namespace input;
  int n = results_.size() + (offerCreate_ ? 1 : 0);
  bool edited = false;
  switch (e.key) {
    case K_ESC: return app::back();
    case K_ENTER:
      if (mode_ == app::SwitcherMode::Open && !e.shift()) return commit(n ? sel_ : -1);
      return commit(-1);
    case K_UP:
    case K_DOWN:
      if (n) sel_ = (sel_ + (e.key == K_UP ? n - 1 : 1)) % n;
      return drawList();
    case K_LEFT:
      if (cursor_ > 0) cursor_--;
      return drawInput();
    case K_RIGHT:
      if (cursor_ < query_.size()) cursor_++;
      return drawInput();
    case K_HOME:
      cursor_ = 0;
      return drawInput();
    case K_END:
      cursor_ = query_.size();
      return drawInput();
    case K_BACKSPACE:
      if (cursor_ > 0) {
        size_t from = cursor_ - 1;
        if (e.ctrl()) {
          while (from > 0 && query_[from - 1] == ' ') from--;
          while (from > 0 && query_[from - 1] != ' ' && query_[from - 1] != '/') from--;
        }
        query_.erase(from, cursor_ - from);
        cursor_ = from;
        edited = true;
      }
      break;
    case K_DELETE:
      if (cursor_ < query_.size()) {
        query_.erase(cursor_, 1);
        edited = true;
      }
      break;
    case K_CHAR:
      if (e.ctrl() || e.alt() || query_.size() >= 120) return;
      query_.insert(cursor_++, 1, e.ch);
      edited = true;
      break;
    default:
      return;
  }
  if (edited) {
    sel_ = 0;
    refresh();
    drawInput();
    drawList();
  }
}

void SwitcherScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (ui::hitRight(x, y)) return app::toggleKeyboard();
  if (mode_ != app::SwitcherMode::Open || y < kListY) return;
  int i = (y - kListY) / kRowH;
  int n = results_.size() + (offerCreate_ ? 1 : 0);
  if (i >= 0 && i < n) commit(i < (int)results_.size() ? i : -1);
}
