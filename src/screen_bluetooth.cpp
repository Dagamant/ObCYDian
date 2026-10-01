#include "btkbd.h"
#include "screens.h"

using btkbd::State;

void BluetoothScreen::draw() {
  ui::topBar("Bluetooth keyboard", ui::Icon::Back);
  lastState_ = -1;
  drawBody();
}

void BluetoothScreen::drawBody() {
  const int W = gfx.width(), M = theme::MARGIN;
  const State st = btkbd::state();
  auto found = btkbd::scanResults();
  lastState_ = (int)st;
  lastFound_ = found.size();
  rows_.clear();
  gfx.fillRect(0, theme::BAR_H, W, gfx.height() - theme::BAR_H, theme::BG);

  // Status
  int y = theme::BAR_H + 14;
  uint16_t dot = st == State::Connected ? theme::OK : st == State::Off ? theme::FAINT : theme::FOLDER;
  gfx.fillCircle(M + 8, y + 11, 6, dot);
  gfx.setFont(font::h2());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.drawString(btkbd::stateText(), M + 24, y);
  std::string name = btkbd::keyboardName();
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  if (!name.empty() && st != State::Scanning && st != State::ScanDone)
    gfx.drawString(tf::printable(name).c_str(), M + 24, y + 30);
  y += 64;

  gfx.setTextDatum(textdatum_t::top_left);
  switch (st) {
    case State::Off:
      ui::message("", "Turn Bluetooth on to use a keyboard. This turns WiFi\noff; the device restarts to switch radios.", y - 30);
      break;
    case State::NoKeyboard:
      gfx.drawString("Put your keyboard in pairing mode, then tap Pair.", M, y);
      break;
    case State::Scanning:
      gfx.drawString("Looking for keyboards in pairing mode...", M, y);
      break;
    case State::ScanDone:
      if (found.empty()) gfx.drawString("No keyboards found. Is it in pairing mode?", M, y);
      for (size_t i = 0; i < found.size() && i < 3; i++) {
        ui::Rect r{M, y + (int)i * 46, W - 2 * M, 40};
        rows_.push_back(r);
        gfx.fillRoundRect(r.x, r.y, r.w, r.h, 6, theme::BG_ALT);
        gfx.setTextColor(theme::TEXT_BRIGHT);
        gfx.setTextDatum(textdatum_t::middle_left);
        gfx.drawString(tf::printable(found[i].name).c_str(), r.x + 12, r.y + r.h / 2);
        gfx.setTextDatum(textdatum_t::middle_right);
        gfx.setTextColor(theme::MUTED);
        char sig[32];
        snprintf(sig, sizeof(sig), "%s  %d dBm", found[i].addr.substr(9).c_str(), found[i].rssi);
        gfx.drawString(sig, r.x + r.w - 12, r.y + r.h / 2);
        gfx.setTextDatum(textdatum_t::top_left);
      }
      break;
    case State::Connecting:
      gfx.drawString("Connecting to the keyboard...", M, y);
      break;
    case State::Pairing: {
      gfx.drawString("If asked, type this code on the keyboard, then press Enter:", M, y);
      char code[12];
      snprintf(code, sizeof(code), "%06u", (unsigned)btkbd::passkey());
      gfx.setFont(font::h1());
      gfx.setTextColor(theme::ACCENT);
      gfx.setTextDatum(textdatum_t::top_center);
      gfx.drawString(code, W / 2, y + 34);
      gfx.setTextDatum(textdatum_t::top_left);
      break;
    }
    case State::Connected:
      gfx.drawString("Ready. Try typing: keys work everywhere in the app.", M, y);
      break;
    case State::Reconnecting:
      gfx.drawString("Press a key on the keyboard to wake it up.", M, y);
      break;
  }

  const int bw = (W - 2 * M - 16) / 3, by = gfx.height() - 56;
  btnScan_ = {M, by, bw, 44};
  btnForget_ = {M + bw + 8, by, bw, 44};
  btnPower_ = {M + 2 * (bw + 8), by, bw, 44};
  bool on = st != State::Off;
  ui::button(btnScan_, st == State::ScanDone ? "Search again" : "Pair", on ? theme::ACCENT_BG : theme::BORDER);
  ui::button(btnForget_, "Forget", on && !name.empty() ? theme::BORDER : theme::BG_ALT);
  ui::button(btnPower_, on ? "Turn off" : "Turn on", on ? theme::BORDER : theme::ACCENT_BG);
}

void BluetoothScreen::tick() {
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();
  if ((int)btkbd::state() != lastState_ || btkbd::scanResults().size() != lastFound_) drawBody();
}

void BluetoothScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  const State st = btkbd::state();
  if (btnPower_.contains(x, y)) {
    bool on = st != State::Off;
    app::confirm(on ? "Turn Bluetooth off?" : "Turn Bluetooth on?", "The device will restart.", "Restart",
                 theme::ACCENT_BG, [on] { btkbd::setEnabled(!on); });
    return;
  }
  if (st == State::Off) return;
  if (btnScan_.contains(x, y) && st != State::Scanning && st != State::Connecting && st != State::Pairing) {
    btkbd::startScan();
    return;
  }
  if (btnForget_.contains(x, y) && !btkbd::keyboardName().empty()) {
    app::confirm("Forget keyboard?", "You'll need to pair it again.", "Forget", theme::DANGER,
                 [] { btkbd::forget(); });
    return;
  }
  auto found = btkbd::scanResults();
  for (size_t i = 0; i < rows_.size() && i < found.size(); i++)
    if (rows_[i].contains(x, y) && st == State::ScanDone) return btkbd::pair(found[i]);
}

void BluetoothScreen::onKey(const input::Event& e) {
  if (e.key == input::K_ESC) app::back();
}
