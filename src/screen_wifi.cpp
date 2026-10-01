#include "radio.h"
#include "screens.h"
#include "webserver.h"

// ---------------------------------------------------------------------------
// WiFi & web server

void WifiScreen::draw() {
  ui::topBar("WiFi & web server", ui::Icon::Back);
  lastState_ = -1;
  drawBody();
}

void WifiScreen::drawBody() {
  const int W = gfx.width(), M = theme::MARGIN;
  const bool wifiMode = radio::mode() == radio::Mode::Wifi;
  const web::State st = web::state();
  lastState_ = (int)st;
  gfx.fillRect(0, theme::BAR_H, W, gfx.height() - theme::BAR_H, theme::BG);

  int y = theme::BAR_H + 12;
  // In WiFi mode a QR code fills the right side, so text must stop short of it
  const int textW = (wifiMode ? W - 148 - 3 * M : W - 2 * M);
  auto line = [&](const std::string& s, uint16_t color = theme::TEXT, const lgfx::IFont* f = font::ui(), int dy = 24) {
    gfx.setFont(f);
    gfx.setTextColor(color);
    gfx.drawString(ui::ellipsize(tf::printable(s), textW, f).c_str(), M, y);
    y += dy;
  };
  const std::string saved = web::savedSsid();

  if (!wifiMode) {
    line("WiFi is off", theme::TEXT_BRIGHT, font::h2(), 34);
    line("In WiFi mode the device serves your notes as a web", theme::MUTED);
    line("page. The Bluetooth keyboard is off while WiFi is on.", theme::MUTED, font::ui(), 36);
    line("Network: " + (saved.empty() ? std::string("none saved") : saved));
    if (saved.empty()) line("Set one now, or use the setup page WiFi mode opens.", theme::MUTED);
  } else {
    const int qr = 148, qx = W - M - qr, qy = theme::BAR_H + 12;
    switch (st) {
      case web::State::Connecting:
        line("Connecting...", theme::TEXT_BRIGHT, font::h2(), 34);
        line("Joining " + saved, theme::MUTED);
        break;
      case web::State::Connected: {
        line("Web server running", theme::TEXT_BRIGHT, font::h2(), 34);
        line("On the network " + saved, theme::MUTED);
        line("open this in a browser:", theme::MUTED, font::ui(), 28);
        line(web::url(), theme::ACCENT, font::uiBold(), 26);
        line("http://obcydian.local/", theme::ACCENT, font::uiBold(), 30);
        line("or scan the code.", theme::MUTED, font::ui(), 28);
        line("Obsidian sync (WebDAV): port 8080", theme::MUTED);
        gfx.qrcode(web::url().c_str(), qx, qy, qr, 3);
        break;
      }
      case web::State::AccessPoint: {
        line("Setup network", theme::TEXT_BRIGHT, font::h2(), 32);
        line(saved.empty() ? "No WiFi network saved yet." : "Couldn't connect to " + saved + ".", theme::MUTED, font::ui(), 28);
        line("1. Join WiFi  " + web::apSsid(), theme::TEXT);
        line("   Password  " + web::apPassword(), theme::TEXT);
        line("   (or scan the code)", theme::MUTED);
        line("2. Open http://192.168.4.1/", theme::TEXT);
        line("   to set your network", theme::MUTED);
        std::string join = "WIFI:T:WPA;S:" + web::apSsid() + ";P:" + web::apPassword() + ";;";
        gfx.qrcode(join.c_str(), qx, qy, qr, 3);
        break;
      }
      case web::State::Off:
        line("WiFi starting...", theme::TEXT_BRIGHT, font::h2(), 34);
        break;
    }
  }

  const int bw = (W - 2 * M - 16) / 3, by = gfx.height() - 54;
  btnNetwork_ = {M, by, bw, 44};
  btnForget_ = {M + bw + 8, by, bw, 44};
  btnMode_ = {M + 2 * (bw + 8), by, bw, 44};
  ui::button(btnNetwork_, "Set network", theme::BORDER);
  ui::button(btnForget_, "Forget", saved.empty() ? theme::BG_ALT : theme::BORDER);
  ui::button(btnMode_, wifiMode ? "Use Bluetooth" : "Use WiFi", theme::ACCENT_BG);
}

void WifiScreen::tick() {
  static uint32_t last = 0;
  if (millis() - last < 300) return;
  last = millis();
  if ((int)web::state() != lastState_) drawBody();
}

void WifiScreen::setNetwork() {
  app::prompt("WiFi network name", "The network the device should join", web::savedSsid(), false,
              [](const std::string& ssid) {
                if (ssid.empty()) return;
                app::prompt("WiFi password", "Password for " + ssid + " (empty if open)", "", true,
                            [ssid](const std::string& pass) {
                              web::setNetwork(ssid, pass);
                              app::toast("Saved " + ssid);
                            });
              });
}

void WifiScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (btnNetwork_.contains(x, y)) return setNetwork();
  if (btnForget_.contains(x, y) && !web::savedSsid().empty()) {
    app::confirm("Forget network?", "Removes " + web::savedSsid() + " and its password.", "Forget", theme::DANGER, [] {
      web::forgetNetwork();
      app::redraw();
    });
    return;
  }
  if (btnMode_.contains(x, y)) {
    const bool wifiMode = radio::mode() == radio::Mode::Wifi;
    app::confirm(wifiMode ? "Switch to Bluetooth?" : "Switch to WiFi?",
                 wifiMode ? "Web server stops; the keyboard reconnects." : "The Bluetooth keyboard turns off.",
                 "Restart", theme::ACCENT_BG,
                 [wifiMode] { radio::switchTo(wifiMode ? radio::Mode::Bluetooth : radio::Mode::Wifi); });
  }
}

void WifiScreen::onKey(const input::Event& e) {
  if (e.key == input::K_ESC) app::back();
}

// ---------------------------------------------------------------------------
// Text prompt

static constexpr int kInputY = theme::BAR_H + 44;
static constexpr int kInputH = 40;

void PromptScreen::open(const std::string& title, const std::string& hint, const std::string& initial,
                        bool secret, std::function<void(const std::string&)> done) {
  title_ = title;
  hint_ = hint;
  text_ = initial;
  cursor_ = text_.size();
  secret_ = secret;
  reveal_ = false;
  done_ = std::move(done);
}

void PromptScreen::draw() {
  ui::topBar(title_, ui::Icon::Back, ui::Icon::Keyboard);
  gfx.fillRect(0, theme::BAR_H, gfx.width(), ui::contentBottom() - theme::BAR_H, theme::BG);
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::MUTED);
  gfx.drawString(tf::printable(hint_).c_str(), theme::MARGIN + 4, theme::BAR_H + 14);
  gfx.setFont(font::small());
  gfx.setTextColor(theme::FAINT);
  gfx.drawString(secret_ ? "Enter to confirm, Esc to cancel, Tab shows/hides the text" : "Enter to confirm, Esc to cancel",
                 theme::MARGIN + 4, kInputY + kInputH + 12);
  drawInput();
}

void PromptScreen::drawInput() {
  const int x = theme::MARGIN, w = gfx.width() - 2 * theme::MARGIN;
  gfx.fillRoundRect(x, kInputY, w, kInputH, 6, theme::BG_ALT);
  gfx.drawRoundRect(x, kInputY, w, kInputH, 6, theme::ACCENT_DIM);
  std::string shown = (secret_ && !reveal_) ? std::string(text_.size(), '*') : tf::printable(text_);
  size_t cur = (secret_ && !reveal_) ? cursor_ : tf::printable(text_.substr(0, cursor_)).size();
  size_t off = 0;
  while (off < cur && gfx.textWidth(shown.substr(off, cur - off).c_str(), font::ui()) > w - 40) off++;
  gfx.setFont(font::ui());
  gfx.setTextColor(theme::TEXT_BRIGHT);
  gfx.setTextDatum(textdatum_t::middle_left);
  const int tx = x + 12, cy = kInputY + kInputH / 2;
  gfx.drawString(ui::ellipsize(shown.substr(off), w - 30, font::ui()).c_str(), tx, cy);
  gfx.fillRect(tx + gfx.textWidth(shown.substr(off, cur - off).c_str(), font::ui()), cy - 10, 2, 20, theme::ACCENT);
  gfx.setTextDatum(textdatum_t::top_left);
}

void PromptScreen::onKey(const input::Event& e) {
  using namespace input;
  switch (e.key) {
    case K_ESC: return app::back();
    case K_ENTER: {
      auto done = done_;
      std::string text = text_;
      app::back();
      if (done) done(text);
      return;
    }
    case K_TAB: reveal_ = !reveal_; break;
    case K_LEFT: cursor_ = tf::prevChar(text_, cursor_); break;
    case K_RIGHT: cursor_ = tf::nextChar(text_, cursor_); break;
    case K_HOME: cursor_ = 0; break;
    case K_END: cursor_ = text_.size(); break;
    case K_BACKSPACE:
      if (cursor_ > 0) {
        size_t from = tf::prevChar(text_, cursor_);
        text_.erase(from, cursor_ - from);
        cursor_ = from;
      }
      break;
    case K_DELETE:
      if (cursor_ < text_.size()) text_.erase(cursor_, tf::nextChar(text_, cursor_) - cursor_);
      break;
    case K_CHAR:
      if (e.ctrl() || e.alt() || text_.size() >= 64) return;
      text_.insert(cursor_++, 1, e.ch);
      break;
    default:
      return;
  }
  drawInput();
}

void PromptScreen::onTap(int x, int y) {
  if (ui::hitLeft(x, y)) return app::back();
  if (ui::hitRight(x, y)) app::toggleKeyboard();
}
