#include "btkbd.h"
#include "radio.h"
#include "screens.h"
#include "touch_calib.h"

namespace {

// Shows the SdFat formatter's progress output (mostly dots) on screen.
class FormatProgress : public Print {
 public:
  size_t write(uint8_t c) override {
    Serial.write(c);
    if (c == '\n' || c == '\r') {
      if (!line_.empty()) last_ = line_;
      line_.clear();
    } else {
      line_ += (char)c;
    }
    if (millis() - drawn_ > 150) {
      drawn_ = millis();
      int y = gfx.height() / 2 + 20;
      gfx.fillRect(0, y, gfx.width(), 24, theme::BG);
      gfx.setFont(font::small());
      gfx.setTextColor(theme::MUTED);
      gfx.setTextDatum(textdatum_t::top_center);
      std::string shown = line_.empty() ? last_ : line_;
      if (shown.size() > 60) shown = shown.substr(shown.size() - 60);
      gfx.drawString(shown.c_str(), gfx.width() / 2, y);
      gfx.setTextDatum(textdatum_t::top_left);
    }
    return 1;
  }

 private:
  std::string line_, last_;
  uint32_t drawn_ = 0;
};

std::string humanBytes(uint64_t b) {
  char buf[32];
  if (b >= 1000000000ULL) snprintf(buf, sizeof(buf), "%.1f GB", b / 1e9);
  else if (b >= 1000000ULL) snprintf(buf, sizeof(buf), "%.1f MB", b / 1e6);
  else snprintf(buf, sizeof(buf), "%.0f KB", b / 1e3);
  return buf;
}

}  // namespace

void ToolsScreen::draw() {
  ui::topBar("Settings", ui::Icon::Back, ui::Icon::None);
  const int W = gfx.width(), M = theme::MARGIN;
  gfx.fillRect(0, theme::BAR_H, W, gfx.height() - theme::BAR_H, theme::BG);
  drawInfo();

  // Right column: connections
  const int rx = 248, rw = W - M - rx;
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString("Device", rx, theme::BAR_H + 10);
  gfx.setFont(font::small());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString((std::string("Radio: ") + radio::name(radio::mode())).c_str(), rx, theme::BAR_H + 40);
  btnBluetooth_ = {rx, theme::BAR_H + 62, rw, 40};
  btnWifi_ = {rx, theme::BAR_H + 108, rw, 40};
  btnPower_ = {rx, theme::BAR_H + 154, rw, 40};
  ui::button(btnBluetooth_, "Bluetooth keyboard",
             radio::mode() == radio::Mode::Bluetooth ? theme::ACCENT_BG : theme::BORDER);
  ui::button(btnWifi_, "WiFi & web server", radio::mode() == radio::Mode::Wifi ? theme::ACCENT_BG : theme::BORDER);
  ui::button(btnPower_, "Display & power", theme::BORDER);

  // Bottom row: SD card actions
  const int bw = (W - 2 * M - 16) / 3, by = gfx.height() - 54;
  btnFormat_ = {M, by, bw, 44};
  btnSample_ = {M + bw + 8, by, bw, 44};
  btnRemount_ = {M + 2 * (bw + 8), by, bw, 44};
  bool card = storage::state() != storage::State::NoCard;
  ui::button(btnFormat_, "Format SD", card ? theme::DANGER : theme::BORDER);
  ui::button(btnSample_, "Sample notes", storage::state() == storage::State::Mounted ? theme::BORDER : theme::BG_ALT);
  ui::button(btnRemount_, "Remount SD", theme::BORDER);
  if (storage::state() == storage::State::Mounted && !freeKnown_) needFree_ = true;
}

void ToolsScreen::drawInfo() {
  const int x = theme::MARGIN + 4, vx = 112;
  int y = theme::BAR_H + 10;
  gfx.fillRect(0, y, 240, gfx.height() - 64 - y, theme::BG);
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString("SD card", x, y);
  y += 34;

  auto row = [&](const char* k, const std::string& v) {
    gfx.setFont(font::ui());
    gfx.setTextColor(theme::MUTED);
    gfx.drawString(k, x, y);
    gfx.setTextColor(theme::TEXT);
    gfx.drawString(ui::ellipsize(v, 236 - vx, font::ui()).c_str(), vx, y);
    y += 24;
  };
  auto ci = storage::info();
  switch (storage::state()) {
    case storage::State::NoCard: row("Status", "No card"); break;
    case storage::State::NoFilesystem: row("Status", "Not formatted"); break;
    case storage::State::Mounted: row("Status", "Mounted"); break;
  }
  if (storage::state() != storage::State::NoCard) {
    std::string type = ci.cardType;
    row("Card", type.substr(0, type.find('/')) + " " + humanBytes(ci.capacity));
    row("Format", ci.fsType);
  }
  if (storage::state() == storage::State::Mounted) {
    row("Free", freeKnown_ ? humanBytes(free_) : "...");
    row("Notes", std::to_string(storage::noteCount()));
  }
}

void ToolsScreen::tick() {
  if (!needFree_) return;
  needFree_ = false;
  free_ = storage::freeBytes();
  freeKnown_ = true;
  drawInfo();
}

void ToolsScreen::format() {
  gfx.fillRect(0, theme::BAR_H, gfx.width(), gfx.height() - theme::BAR_H, theme::BG);
  ui::message("Formatting...", "Don't remove the card or power off.", gfx.height() / 2 - 70);
  FormatProgress progress;
  bool ok = storage::format(&progress);
  freeKnown_ = false;
  draw();
  if (!ok) {
    app::toast("Format failed - see serial log", 4000);
    return;
  }
  app::confirm("Card formatted", "Add the sample notes to the new vault?", "Add notes",
               theme::ACCENT_BG, [this] {
                 storage::createSampleVault();
                 freeKnown_ = false;
                 draw();
                 app::toast("Sample notes added");
               });
}

void ToolsScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();

  if (btnFormat_.contains(x, y) && storage::state() != storage::State::NoCard) {
    auto ci = storage::info();
    std::string fs = ci.capacity > 32ULL * 1024 * 1024 * 1024 ? "exFAT" : "FAT32";
    if (ci.capacity <= 2ULL * 1024 * 1024 * 1024) fs = "FAT16";
    app::confirm("Format SD card?", "Erases everything. New filesystem: " + fs, "Erase",
                 theme::DANGER, [this] { format(); });
  } else if (btnSample_.contains(x, y) && storage::state() == storage::State::Mounted) {
    storage::createSampleVault();
    freeKnown_ = false;
    draw();
    app::toast("Sample notes added");
  } else if (btnRemount_.contains(x, y)) {
    storage::begin();
    freeKnown_ = false;
    draw();
    app::toast(storage::state() == storage::State::Mounted ? "Card mounted" : "Card not mounted");
  } else if (btnBluetooth_.contains(x, y)) {
    app::openBluetooth();
  } else if (btnWifi_.contains(x, y)) {
    app::openWifi();
  } else if (btnPower_.contains(x, y)) {
    app::openPower();
  }
}
