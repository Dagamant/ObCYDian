#include "screens.h"

static constexpr int kRowH = 44;

void BrowserScreen::open(const std::string& dir, int scroll) {
  if (dir != dir_) sel_ = -1;
  dir_ = dir;
  entries_ = storage::list(dir_);
  scroll_ = std::max(0, std::min(scroll, maxScroll()));
}

int BrowserScreen::maxScroll() const {
  int content = (int)entries_.size() * kRowH;
  return std::max(0, content - (gfx.height() - theme::BAR_H));
}

void BrowserScreen::draw() {
  // "Vault > Projects > Ideas"
  std::string title = "Vault";
  for (char c : tf::printable(dir_ == "/" ? "" : dir_)) title += c == '/' ? std::string(" > ") : std::string(1, c);
  const bool card = storage::state() == storage::State::Mounted;
  ui::topBar(title, dir_ == "/" ? ui::Icon::None : ui::Icon::Back, card ? ui::Icon::More : ui::Icon::Gear,
             card ? ui::Icon::Search : ui::Icon::None, card ? ui::Icon::Gear : ui::Icon::None);

  const int top = theme::BAR_H;
  action_ = {};
  if (storage::state() != storage::State::Mounted || entries_.empty()) {
    gfx.fillRect(0, top, gfx.width(), gfx.height() - top, theme::BG);
    const int bw = 240, bx = (gfx.width() - bw) / 2;
    switch (storage::state()) {
      case storage::State::NoCard:
        ui::message("No SD card", "Insert a microSD card and tap Retry.", top + 50);
        action_ = {bx, top + 150, bw, 48};
        ui::button(action_, "Retry", theme::ACCENT_BG);
        break;
      case storage::State::NoFilesystem:
        ui::message("Card not formatted", "The card has no FAT32/exFAT filesystem.", top + 50);
        action_ = {bx, top + 150, bw, 48};
        ui::button(action_, "Format card...", theme::DANGER);
        break;
      case storage::State::Mounted:
        if (dir_ == "/") {
          ui::message("No notes yet", "This vault is empty.", top + 50);
          action_ = {bx, top + 150, bw, 48};
          ui::button(action_, "Create sample notes", theme::ACCENT_BG);
        } else {
          ui::message("Empty folder", "", top + 60);
        }
        break;
    }
    return;
  }
  drawList();
}

void BrowserScreen::drawList() {
  const int top = theme::BAR_H;
  const int h = gfx.height() - top;
  const int w = gfx.width();
  renderBands(top, h, theme::BG, [&](LGFX_Sprite& s, int off) {
    int first = (scroll_ + off) / kRowH;
    for (int i = first; i < (int)entries_.size(); i++) {
      int y = i * kRowH - scroll_ - off;
      if (y > s.height()) break;
      const auto& e = entries_[i];
      if (i == sel_) s.fillRoundRect(4, y + 2, w - 8, kRowH - 4, 6, theme::ACCENT_BG);
      if (e.isDir) {
        ui::folderIcon(s, theme::MARGIN, y + 12, theme::FOLDER);
      } else {
        ui::noteIcon(s, theme::MARGIN, y + 12, theme::MUTED);
      }
      std::string name = e.isDir ? e.name : storage::baseName(e.name);
      s.setFont(font::ui());
      s.setTextColor(theme::TEXT);
      s.setTextDatum(textdatum_t::middle_left);
      s.drawString(ui::ellipsize(tf::printable(name), w - 90, font::ui()).c_str(), 44,
                   y + kRowH / 2);
      if (e.isDir) {
        int cx = w - 24, cy = y + kRowH / 2;
        s.drawWideLine(cx - 3, cy - 6, cx + 3, cy, 1.2f, theme::FAINT);
        s.drawWideLine(cx + 3, cy, cx - 3, cy + 6, 1.2f, theme::FAINT);
      }
      s.drawFastHLine(44, y + kRowH - 1, w - 44 - theme::MARGIN, theme::BG_ALT);
    }
    // Scrollbar
    int ms = maxScroll();
    if (ms > 0) {
      int thumb = std::max(24, h * h / (h + ms));
      int ty = (h - thumb) * scroll_ / ms - off;
      s.fillRoundRect(w - 4, ty, 3, thumb, 1, theme::BORDER);
    }
  });
}

void BrowserScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y) && dir_ != "/") return app::back();
  int slot = ui::hitRightSlot(x, y);
  const bool card = storage::state() == storage::State::Mounted;
  if (slot == 0) return card ? folderMenu() : app::openTools();
  if (slot == 1 && card) return app::openSearch();
  if (slot == 2 && card) return app::openTools();
  if (y < theme::BAR_H) return;

  if (action_.w && action_.contains(x, y)) {
    switch (storage::state()) {
      case storage::State::NoCard:
        storage::begin();
        open(dir_, 0);
        draw();
        if (storage::state() == storage::State::NoCard) app::toast("Still no card detected");
        break;
      case storage::State::NoFilesystem:
        app::openTools();
        break;
      case storage::State::Mounted:
        storage::createSampleVault();
        open(dir_, 0);
        draw();
        break;
    }
    return;
  }

  activate((y - theme::BAR_H + scroll_) / kRowH);
}

void BrowserScreen::activate(int i) {
  if (i < 0 || i >= (int)entries_.size()) return;
  const auto& e = entries_[i];
  std::string path = storage::joinPath(dir_, e.name);
  if (e.isDir) {
    app::openFolder(path);
  } else {
    app::openNote(path);
  }
}

void BrowserScreen::newNote() {
  std::string p = storage::createNote(dir_, storage::baseName(storage::untitledPath(dir_)));
  if (p.empty()) return app::toast("Couldn't create note");
  app::editNote(p);
}

void BrowserScreen::folderMenu() {
  const std::string dir = dir_;
  const std::string name = dir == "/" ? "Vault" : storage::baseName(dir);
  std::vector<std::string> items = {"New note", "New folder"};
  if (dir != "/") {
    items.push_back("Rename / move folder");
    items.push_back("Delete folder");
  }
  app::menu(tf::printable(name), items, [this, dir, name](int i) {
    if (i == 0) return newNote();
    if (i == 1) {
      return app::prompt("New folder", "Name for a new folder in " + name, "", false, [dir](const std::string& n) {
        std::string clean = storage::sanitizeName(n);
        if (clean.empty()) return;
        std::string path = storage::joinPath(dir, clean);
        if (storage::exists(path)) return app::toast("That folder already exists");
        if (!storage::mkdirs(path)) return app::toast("Couldn't create folder");
        storage::rescan();
        app::openFolder(path);
      });
    }
    if (i == 2) {
      return app::prompt("Rename folder", "New name (use / to move it, e.g. Archive/2026)", storage::baseName(dir), false,
                         [dir](const std::string& n) {
                           if (n.empty()) return;
                           // A plain name stays in the same parent; a path is from the vault root
                           std::string to;
                           if (n.find('/') == std::string::npos) to = storage::joinPath(storage::parentDir(dir), storage::sanitizeName(n));
                           else {
                             to = "";
                             size_t i = 0;
                             while (i <= n.size()) {
                               size_t j = n.find('/', i);
                               if (j == std::string::npos) j = n.size();
                               std::string seg = storage::sanitizeName(n.substr(i, j - i));
                               if (!seg.empty()) to += "/" + seg;
                               i = j + 1;
                             }
                           }
                           if (to.empty() || to == dir) return;
                           if (storage::exists(to)) return app::toast("Something with that name exists");
                           int links = 0;
                           if (!storage::renameFolder(dir, to, &links)) return app::toast("Rename failed");
                           app::folderPathChanged(dir, to);  // re-shows the folder under its new name
                           if (links) app::toast("Updated " + std::to_string(links) + (links == 1 ? " link" : " links"));
                         });
    }
    if (i == 3) {
      int n = storage::countNotesIn(dir);
      std::string what = n ? "It holds " + std::to_string(n) + (n == 1 ? " note" : " notes") + ", also deleted." : "The folder is empty.";
      app::confirm("Delete " + tf::printable(name) + "?", what, "Delete", theme::DANGER, [dir] {
        bool ok = storage::removeFolder(dir);
        app::folderDeleted(dir);
        app::toast(ok ? "Folder deleted" : "Couldn't delete everything");
      });
    }
  });
}

void BrowserScreen::onKey(const input::Event& e) {
  using namespace input;
  const int n = entries_.size();
  const int viewH = gfx.height() - theme::BAR_H;
  switch (e.key) {
    case K_UP:
    case K_DOWN:
    case K_PGUP:
    case K_PGDN: {
      if (!n) return;
      int step = (e.key == K_PGUP || e.key == K_PGDN) ? viewH / kRowH : 1;
      if (e.key == K_UP || e.key == K_PGUP) step = -step;
      sel_ = sel_ < 0 ? 0 : std::max(0, std::min(n - 1, sel_ + step));
      int top = sel_ * kRowH, bot = top + kRowH;
      if (top < scroll_) scroll_ = top;
      if (bot > scroll_ + viewH) scroll_ = bot - viewH;
      return drawList();
    }
    case K_ENTER:
    case K_RIGHT:
      return activate(sel_);
    case K_BACKSPACE:
    case K_ESC:
    case K_LEFT:
      if (dir_ != "/") app::back();
      return;
    default:
      return;
  }
}

void BrowserScreen::onDrag(int dy) {
  int s = std::max(0, std::min(scroll_ - dy, maxScroll()));
  if (s != scroll_) {
    scroll_ = s;
    drawList();
  }
}
