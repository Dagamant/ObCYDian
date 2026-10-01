#include "debug_console.h"

#include <Arduino.h>

#include <string>

#include "app.h"
#include "display.h"
#include "input.h"
#include "storage.h"
#include "touch_calib.h"

namespace debug_console {

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
  } else if (s == "back") {
    app::back();
  } else if (s == "home") {
    app::home();
  } else if (s == "ls") {
    for (auto& e : storage::list(rest.empty() ? "/" : rest))
      Serial.printf("%s%s  %u\n", e.name.c_str(), e.isDir ? "/" : "", e.size);
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
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (!buf.empty()) run(buf);
      buf.clear();
    } else if (buf.size() < 256) {
      buf += ch;
    }
  }
}

}  // namespace debug_console
