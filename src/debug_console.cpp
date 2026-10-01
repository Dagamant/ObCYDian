#include "debug_console.h"

#include <Arduino.h>

#include <string>

#include "app.h"
#include "btkbd.h"
#include "display.h"
#include "input.h"
#include "storage.h"
#include "touch_calib.h"

namespace debug_console {

using namespace input;

// --- Raw keyboard mode: bytes from a terminal (VT100/xterm sequences) become key events.
static bool rawMode = false;

static void key(Key k, uint8_t mods = 0, char ch = 0) { inject(keyEvent(k, mods, ch)); }

static uint8_t xtermMods(int p) {
  if (p < 2) return 0;
  int m = p - 1;
  return ((m & 1) ? M_SHIFT : 0) | ((m & 2) ? M_ALT : 0) | ((m & 4) ? M_CTRL : 0);
}

static int escState = 0;  // 0 normal, 1 ESC, 2 CSI, 3 SS3
static uint32_t escAt = 0;

// A lone ESC (no sequence following within 50 ms) is the Esc key.
static void rawIdle() {
  if (escState == 1 && millis() - escAt > 50) {
    key(K_ESC);
    escState = 0;
  }
}

static void rawByte(uint8_t c) {
  int& state = escState;
  static std::string params;
  static bool lastCR = false;

  rawIdle();
  switch (state) {
    case 0:
      if (c == 0x1D) {
        rawMode = false;
        Serial.println("\n[kbd] raw keyboard mode off");
      } else if (c == 0x1B) {
        state = 1;
        escAt = millis();
      } else if (c == '\r' || c == '\n') {
        if (!(c == '\n' && lastCR)) key(K_ENTER);
      } else if (c == 0x7F || c == 0x08) {
        key(K_BACKSPACE);
      } else if (c == '\t') {
        key(K_TAB);
      } else if (c >= 1 && c <= 26) {
        key(K_CHAR, M_CTRL, 'a' + c - 1);
      } else if (c >= 0x20 && c != 0x7F) {
        key(K_CHAR, 0, (char)c);
      }
      lastCR = c == '\r';
      return;
    case 1:
      if (c == '[') {
        state = 2;
        params.clear();
      } else if (c == 'O') {
        state = 3;
      } else if (c == 0x1B) {
        key(K_ESC);
        escAt = millis();
      } else {
        state = 0;
        if (c == '\r') key(K_ENTER, M_ALT);
        else if (c == 0x7F) key(K_BACKSPACE, M_ALT);
        else if (c >= 0x20) key(K_CHAR, M_ALT, (char)c);
      }
      return;
    case 2: {
      if ((c >= '0' && c <= '9') || c == ';') {
        params += (char)c;
        return;
      }
      state = 0;
      int p1 = 0, p2 = 0;
      sscanf(params.c_str(), "%d;%d", &p1, &p2);
      uint8_t m = xtermMods(p2);
      switch (c) {
        case 'A': key(K_UP, m); break;
        case 'B': key(K_DOWN, m); break;
        case 'C': key(K_RIGHT, m); break;
        case 'D': key(K_LEFT, m); break;
        case 'H': key(K_HOME, m); break;
        case 'F': key(K_END, m); break;
        case 'Z': key(K_TAB, M_SHIFT); break;
        case '~':
          switch (p1) {
            case 1: case 7: key(K_HOME, m); break;
            case 4: case 8: key(K_END, m); break;
            case 3: key(K_DELETE, m); break;
            case 5: key(K_PGUP, m); break;
            case 6: key(K_PGDN, m); break;
            case 12: key(K_F2, m); break;
          }
          break;
      }
      return;
    }
    case 3:
      state = 0;
      if (c == 'H') key(K_HOME);
      else if (c == 'F') key(K_END);
      else if (c == 'Q') key(K_F2);
      return;
  }
}

// "key ctrl+shift+left", "key enter", "key a"
static void namedKey(const std::string& spec) {
  uint8_t mods = 0;
  std::string name = spec;
  size_t plus;
  while ((plus = name.find('+')) != std::string::npos && plus + 1 < name.size()) {
    std::string m = name.substr(0, plus);
    if (m == "ctrl") mods |= M_CTRL;
    else if (m == "shift") mods |= M_SHIFT;
    else if (m == "alt") mods |= M_ALT;
    name = name.substr(plus + 1);
  }
  static const struct { const char* n; Key k; } names[] = {
      {"enter", K_ENTER}, {"backspace", K_BACKSPACE}, {"delete", K_DELETE}, {"tab", K_TAB},
      {"esc", K_ESC},     {"left", K_LEFT},           {"right", K_RIGHT},   {"up", K_UP},
      {"down", K_DOWN},   {"home", K_HOME},           {"end", K_END},       {"pgup", K_PGUP},
      {"pgdn", K_PGDN},   {"f2", K_F2},               {"space", K_CHAR}};
  for (auto& n : names) {
    if (name == n.n) return key(n.k, mods, n.k == K_CHAR ? ' ' : 0);
  }
  if (name.size() == 1) return key(K_CHAR, mods, name[0]);
  Serial.printf("unknown key: %s\n", spec.c_str());
}

// "type Hello\nworld" -- \n is Enter, \t is Tab
static void typeText(const std::string& t) {
  for (size_t i = 0; i < t.size(); i++) {
    if (t[i] == '\\' && i + 1 < t.size() && (t[i + 1] == 'n' || t[i + 1] == 't')) {
      key(t[i + 1] == 'n' ? K_ENTER : K_TAB);
      i++;
    } else {
      key(K_CHAR, 0, t[i]);
    }
  }
}

static void screenshot() {
  const int w = gfx.width(), h = gfx.height();
  static uint8_t row[480 * 3];
  Serial.printf("\nSHOT %d %d\n", w, h);
  Serial.flush();
  for (int y = 0; y < h; y++) {
    gfx.readRectRGB(0, y, w, 1, row);
    Serial.write(row, w * 3);
  }
  Serial.flush();
  Serial.println("\nSHOT END");
}

static void run(const std::string& line) {
  char cmd[16] = {0}, arg[200] = {0};
  int a = 0, b = 0, c = 0;
  sscanf(line.c_str(), "%15s", cmd);
  std::string rest = line.size() > strlen(cmd) ? line.substr(strlen(cmd) + 1) : "";
  std::string s = cmd;

  if (s == "shot") {
    screenshot();
  } else if (s == "tap" && sscanf(rest.c_str(), "%d %d", &a, &b) == 2) {
    input::inject({input::Type::Tap, a, b, 0});
  } else if (s == "drag" && sscanf(rest.c_str(), "%d %d %d", &a, &b, &c) == 3) {
    int step = c > 0 ? 20 : -20, moved = 0;
    while (abs(moved) < abs(c)) {
      int d = abs(c - moved) < 20 ? c - moved : step;
      moved += d;
      input::inject({input::Type::Drag, a, b + moved, d});
    }
    input::inject({input::Type::DragEnd, a, b + c, 0});
  } else if (s == "open" && !rest.empty()) {
    if (rest.size() > 3 && rest.compare(rest.size() - 3, 3, ".md") == 0) app::openNote(rest);
    else app::openFolder(rest);
  } else if (s == "kbd") {
    rawMode = true;
    Serial.println("[kbd] raw keyboard mode on (Ctrl+] to exit)");
  } else if (s == "type") {
    typeText(rest);
  } else if (s == "key" && !rest.empty()) {
    namedKey(rest);
  } else if (s == "back") {
    app::back();
  } else if (s == "home") {
    app::home();
  } else if (s == "ls") {
    for (auto& e : storage::list(rest.empty() ? "/" : rest))
      Serial.printf("%s%s  %u\n", e.name.c_str(), e.isDir ? "/" : "", e.size);
  } else if (s == "cat" && !rest.empty()) {
    std::string text;
    if (storage::readFile(rest, text)) {
      Serial.println("----8<----");
      Serial.print(text.c_str());
      Serial.println("\n----8<----");
    } else {
      Serial.println("can't read file");
    }
  } else if (s == "gen" && sscanf(rest.c_str(), "%d %199s", &a, arg) == 2) {
    // gen N PATH: write a large note for performance testing
    std::string t = "# Generated note\n\n";
    for (int i = 0; i < a; i++) {
      switch (i % 6) {
        case 0: t += "## Section " + std::to_string(i) + "\n"; break;
        case 1: t += "Some **bold** text, a [[Welcome]] link and `code` in paragraph " + std::to_string(i) + " that wraps onto another row.\n"; break;
        case 2: t += "- list item with *italic* words " + std::to_string(i) + "\n"; break;
        case 3: t += "- [ ] task number " + std::to_string(i) + "\n"; break;
        case 4: t += "> a quote line " + std::to_string(i) + "\n"; break;
        case 5: t += "\n"; break;
      }
    }
    bool ok = storage::writeFile(arg, t);
    storage::rescan();
    Serial.printf("gen %s: %u bytes %s\n", arg, (unsigned)t.size(), ok ? "ok" : "FAILED");
  } else if (s == "bt") {
    if (rest == "scan") btkbd::startScan();
    else if (rest.rfind("pair ", 0) == 0) {
      auto found = btkbd::scanResults();
      size_t i = atoi(rest.c_str() + 5);
      if (i < found.size()) btkbd::pair(found[i]);
    } else if (rest == "forget") btkbd::forget();
    else if (rest == "raw on" || rest == "raw off") btkbd::setRawLogging(rest == "raw on");
    else if (rest == "on" || rest == "off") btkbd::setEnabled(rest == "on");
    Serial.printf("[bt] state: %s, keyboard '%s'\n", btkbd::stateText(), btkbd::keyboardName().c_str());
    auto found = btkbd::scanResults();
    for (size_t i = 0; i < found.size(); i++)
      Serial.printf("  %u: %s %s (type %u) %d dBm\n", (unsigned)i, found[i].name.c_str(), found[i].addr.c_str(),
                    found[i].addrType, found[i].rssi);
  } else if (s == "cal") {
    touch_calib::run(gfx);
    app::redraw();
  } else if (s == "mem") {
    Serial.printf("heap free %u, min %u, largest block %u\n", ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  } else if (!s.empty()) {
    Serial.printf("unknown command: %s\n", line.c_str());
  }
  (void)arg;
}

void poll() {
  static std::string buf;
  if (rawMode) rawIdle();

  while (Serial.available()) {
    char ch = Serial.read();
    if (rawMode) {
      rawByte((uint8_t)ch);
      continue;
    }
    if (ch == '\n' || ch == '\r') {
      if (!buf.empty()) run(buf);
      buf.clear();
    } else if (buf.size() < 256) {
      buf += ch;
    }
  }
}

}  // namespace debug_console
