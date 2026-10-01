#include "btkbd.h"

#include <NimBLEDevice.h>
#include <Preferences.h>

#include <atomic>
#include <mutex>

namespace btkbd {

namespace {

// ---------------------------------------------------------------------------
// HID report descriptor parsing: just enough to locate keyboard and mouse fields.

struct Field {
  int off = -1, size = 0, count = 0;  // bit offset within the report (without report ID)
  int usageMin = 0;
  bool isSigned = false;
};

struct KbdLayout {
  int id = -1;
  Field mods;    // 8 x 1-bit modifier flags (E0..E7)
  Field keys;    // array of key usages
  Field bitmap;  // N-key-rollover bitmap
};

struct MouseLayout {
  int id = -1;
  Field buttons, x, y, wheel, pan;
};

struct Layouts {
  std::vector<KbdLayout> kbd;
  std::vector<MouseLayout> mouse;
};

uint32_t itemValue(const uint8_t* d, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) v |= (uint32_t)d[i] << (8 * i);
  return v;
}

Layouts parseReportMap(const uint8_t* d, size_t len) {
  Layouts out;
  struct Globals {
    uint32_t page = 0;
    int32_t logMin = 0;
    int size = 0, count = 0, id = 0;
  } g;
  std::vector<Globals> stack;
  std::vector<uint32_t> usages;
  uint32_t uMin = 0, uMax = 0;
  bool haveRange = false;
  int app = 0;  // 1 keyboard, 2 mouse in the current application collection
  int depth = 0;
  std::vector<std::pair<int, int>> offsets;  // report id -> next input bit offset

  auto offsetFor = [&](int id) -> int& {
    for (auto& o : offsets)
      if (o.first == id) return o.second;
    offsets.push_back({id, 0});
    return offsets.back().second;
  };
  auto kbdFor = [&](int id) -> KbdLayout& {
    for (auto& k : out.kbd)
      if (k.id == id) return k;
    out.kbd.push_back(KbdLayout());
    out.kbd.back().id = id;
    return out.kbd.back();
  };
  auto mouseFor = [&](int id) -> MouseLayout& {
    for (auto& m : out.mouse)
      if (m.id == id) return m;
    out.mouse.push_back(MouseLayout());
    out.mouse.back().id = id;
    return out.mouse.back();
  };

  size_t i = 0;
  while (i < len) {
    uint8_t prefix = d[i];
    if (prefix == 0xFE) {  // long item
      if (i + 1 >= len) break;
      i += 3 + d[i + 1];
      continue;
    }
    int n = prefix & 3;
    if (n == 3) n = 4;
    int type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (i + 1 + n > len) break;
    uint32_t v = itemValue(d + i + 1, n);
    i += 1 + n;

    if (type == 1) {  // global
      switch (tag) {
        case 0: g.page = v; break;
        case 1: g.logMin = n == 1 ? (int8_t)v : n == 2 ? (int16_t)v : (int32_t)v; break;
        case 7: g.size = v; break;
        case 8: g.id = v; break;
        case 9: g.count = v; break;
        case 10: stack.push_back(g); break;
        case 11:
          if (!stack.empty()) {
            g = stack.back();
            stack.pop_back();
          }
          break;
      }
    } else if (type == 2) {  // local
      uint32_t full = n == 4 ? v : (g.page << 16) | v;
      if (tag == 0) usages.push_back(full);
      if (tag == 1) uMin = full, haveRange = true;
      if (tag == 2) uMax = full, haveRange = true;
    } else if (type == 0) {  // main
      if (tag == 10) {  // collection
        if (v == 1 && depth == 0 && !usages.empty()) {
          uint32_t u = usages[0];
          app = u == 0x10006 ? 1 : (u == 0x10002 ? 2 : 0);
        }
        depth++;
      } else if (tag == 12) {
        if (--depth == 0) app = 0;
      } else if (tag == 8) {  // input
        int& off = offsetFor(g.id);
        bool constant = v & 1, variable = v & 2;
        if (!constant) {
          auto usageAt = [&](int k) -> uint32_t {
            if (k < (int)usages.size()) return usages[k];
            if (haveRange) return uMin + k;
            return usages.empty() ? 0 : usages.back();
          };
          uint32_t first = usageAt(0);
          uint32_t page = first >> 16 ? first >> 16 : g.page;
          if (app == 1 && page == 0x07) {
            KbdLayout& k = kbdFor(g.id);
            if (variable && g.size == 1 && (first & 0xFFFF) >= 0xE0 && (first & 0xFFFF) <= 0xE7) {
              k.mods = {off, 1, g.count, (int)(first & 0xFFFF)};
            } else if (variable && g.size == 1) {
              k.bitmap = {off, 1, g.count, (int)((haveRange ? uMin : first) & 0xFFFF)};
            } else if (!variable) {
              k.keys = {off, g.size, g.count, 0};
            }
          } else if (app == 2) {
            MouseLayout& m = mouseFor(g.id);
            if (page == 0x09) {
              m.buttons = {off, g.size, g.count, 1};
            } else {
              for (int k = 0; k < g.count; k++) {
                uint32_t u = usageAt(k);
                Field f{off + k * g.size, g.size, 1, 0, g.logMin < 0};
                if (u == 0x10030) m.x = f;
                if (u == 0x10031) m.y = f;
                if (u == 0x10038) m.wheel = f;
                if (u == 0xC0238) m.pan = f;
              }
            }
          }
        }
        off += g.size * g.count;
      }
      usages.clear();
      haveRange = false;
    }
  }
  return out;
}

int32_t readBits(const uint8_t* d, size_t len, const Field& f, int index = 0) {
  int start = f.off + index * f.size;
  if (f.off < 0 || start + f.size > (int)len * 8) return 0;
  uint32_t v = 0;
  for (int b = 0; b < f.size; b++) {
    int bit = start + b;
    if (d[bit / 8] & (1 << (bit % 8))) v |= 1u << b;
  }
  if (f.isSigned && f.size < 32 && (v & (1u << (f.size - 1)))) v |= ~0u << f.size;
  return (int32_t)v;
}

// ---------------------------------------------------------------------------
// Shared state between the NimBLE task, our BT task and the main loop.

struct Report {
  uint8_t id;
  uint8_t len;
  uint8_t data[30];
};

std::atomic<State> state_{State::Off};
std::atomic<uint32_t> passkey_{0};
std::atomic<bool> rawLog_{false};
std::atomic<int> request_{0};  // 1 scan, 2 pair, 3 cancel, 4 forget
std::mutex mu_;
std::vector<Found> found_;
Found pairTarget_;
std::string name_;
Layouts layouts_;
QueueHandle_t queue_ = nullptr;
bool enabled_ = true;

struct ReportChar {
  uint16_t handle;
  uint8_t id;
};
std::vector<ReportChar> reportChars_;

void onNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool) {
  Report r{};
  r.id = 0;
  for (auto& rc : reportChars_)
    if (rc.handle == c->getHandle()) r.id = rc.id;
  r.len = std::min(len, sizeof(r.data));
  memcpy(r.data, data, r.len);
  xQueueSend(queue_, &r, 0);
}

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*) override { Serial.println("[bt] disconnected"); }
  bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) override { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    Serial.printf("[bt] auth complete: encrypted %d bonded %d authenticated %d\n", desc->sec_state.encrypted,
                  desc->sec_state.bonded, desc->sec_state.authenticated);
  }
} clientCb_;

NimBLEClient* client_ = nullptr;

// Discovers the HID service, parses the report map and subscribes to input reports.
bool setupHid() {
  NimBLERemoteService* hid = client_->getService(NimBLEUUID((uint16_t)0x1812));
  if (!hid) {
    Serial.println("[bt] no HID service");
    return false;
  }
  if (auto* map = hid->getCharacteristic(NimBLEUUID((uint16_t)0x2A4B))) {
    std::string m = map->readValue();
    Serial.printf("[bt] report map (%u bytes):", (unsigned)m.size());
    for (size_t i = 0; i < m.size(); i++) Serial.printf("%s%02x", i % 32 ? " " : "\n  ", (uint8_t)m[i]);
    Serial.println();
    Layouts l = parseReportMap((const uint8_t*)m.data(), m.size());
    for (auto& k : l.kbd)
      Serial.printf("[bt] keyboard report id %d: mods@%d keys@%d x%d bitmap@%d x%d\n", k.id, k.mods.off,
                    k.keys.off, k.keys.count, k.bitmap.off, k.bitmap.count);
    for (auto& ms : l.mouse)
      Serial.printf("[bt] mouse report id %d: buttons@%d x@%d/%d y@%d/%d wheel@%d pan@%d\n", ms.id,
                    ms.buttons.off, ms.x.off, ms.x.size, ms.y.off, ms.y.size, ms.wheel.off, ms.pan.off);
    std::lock_guard<std::mutex> lock(mu_);
    layouts_ = l;
  }
  reportChars_.clear();
  int subscribed = 0;
  for (auto* c : *hid->getCharacteristics(true)) {
    if (c->getUUID() != NimBLEUUID((uint16_t)0x2A4D) || !c->canNotify()) continue;
    uint8_t id = 0, type = 1;
    if (auto* ref = c->getDescriptor(NimBLEUUID((uint16_t)0x2908))) {
      std::string v = ref->readValue();
      if (v.size() >= 2) {
        id = v[0];
        type = v[1];
      }
    }
    Serial.printf("[bt] report char handle %u id %u type %u\n", c->getHandle(), id, type);
    if (type != 1) continue;
    reportChars_.push_back({c->getHandle(), id});
    if (c->subscribe(true, onNotify, true)) subscribed++;
  }
  Serial.printf("[bt] subscribed to %d input reports\n", subscribed);
  return subscribed > 0;
}

bool connectTo(const NimBLEAddress& addr, bool pairing) {
  if (!client_) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&clientCb_, false);
    client_->setConnectionParams(6, 12, 0, 300);  // 7.5-15 ms interval, 3 s supervision timeout
  }
  client_->setConnectTimeout(pairing ? 10 : 30);
  if (!client_->connect(addr, true)) return false;
  Serial.printf("[bt] connected to %s\n", addr.toString().c_str());
  state_ = State::Pairing;
  if (!client_->secureConnection()) {
    Serial.println("[bt] securing the connection failed");
    client_->disconnect();
    return false;
  }
  if (!setupHid()) {
    client_->disconnect();
    return false;
  }
  return true;
}

void savePaired(const Found& f) {
  Preferences p;
  p.begin("bt", false);
  p.putString("addr", f.addr.c_str());
  p.putUChar("type", f.addrType);
  p.putString("name", f.name.c_str());
  p.end();
}

bool loadPaired(Found& f) {
  Preferences p;
  p.begin("bt", true);
  String a = p.getString("addr", "");
  f.addr = a.c_str();
  f.addrType = p.getUChar("type", 1);
  f.name = p.getString("name", "Keyboard").c_str();
  p.end();
  return !f.addr.empty();
}

void doScan() {
  state_ = State::Scanning;
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(80);
  scan->setWindow(60);
  NimBLEScanResults res = scan->start(6, false);
  std::vector<Found> list;
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    bool kbd = (d.haveAppearance() && d.getAppearance() == 0x03C1) ||
               d.isAdvertisingService(NimBLEUUID((uint16_t)0x1812));
    if (!kbd) continue;
    list.push_back({d.haveName() ? d.getName() : "Keyboard", d.getAddress().toString(),
                    d.getAddress().getType(), d.getRSSI()});
  }
  scan->clearResults();
  std::lock_guard<std::mutex> lock(mu_);
  found_ = list;
  state_ = State::ScanDone;
}

void task(void*) {
  Found paired;
  bool havePaired = loadPaired(paired);
  {
    std::lock_guard<std::mutex> lock(mu_);
    name_ = havePaired ? paired.name : "";
  }
  state_ = havePaired ? State::Reconnecting : State::NoKeyboard;

  for (;;) {
    int req = request_.exchange(0);
    if (req == 4) {  // forget
      if (client_ && client_->isConnected()) client_->disconnect();
      NimBLEDevice::deleteAllBonds();
      Preferences p;
      p.begin("bt", false);
      p.clear();
      p.end();
      havePaired = false;
      std::lock_guard<std::mutex> lock(mu_);
      name_.clear();
      state_ = State::NoKeyboard;
      continue;
    }
    if (req == 1) {
      if (client_ && client_->isConnected()) client_->disconnect();
      doScan();
      continue;
    }
    if (req == 3) {
      state_ = havePaired ? State::Reconnecting : State::NoKeyboard;
      continue;
    }
    if (req == 2) {
      Found target;
      {
        std::lock_guard<std::mutex> lock(mu_);
        target = pairTarget_;
      }
      // A fresh pairing: drop old bonds so the keyboard and we agree on new keys.
      NimBLEDevice::deleteAllBonds();
      passkey_ = 100000 + esp_random() % 900000;
      NimBLEDevice::setSecurityPasskey(passkey_);
      state_ = State::Connecting;
      if (connectTo(NimBLEAddress(target.addr, target.addrType), true)) {
        savePaired(target);
        paired = target;
        havePaired = true;
        std::lock_guard<std::mutex> lock(mu_);
        name_ = target.name;
        state_ = State::Connected;
      } else {
        state_ = State::ScanDone;
      }
      continue;
    }

    State s = state_;
    if (s == State::Connected) {
      if (!client_ || !client_->isConnected()) {
        state_ = State::Reconnecting;
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      continue;
    }
    if (s == State::Reconnecting && havePaired) {
      // connect() waits up to the timeout for the keyboard to advertise (e.g. after a keypress)
      if (connectTo(NimBLEAddress(paired.addr, paired.addrType), false)) {
        state_ = State::Connected;
      } else {
        if (state_ == State::Pairing) state_ = State::Reconnecting;
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---------------------------------------------------------------------------
// Key decoding (main loop)

using namespace input;

struct KeyState {
  uint8_t mods = 0;
  std::vector<uint8_t> down;
  uint8_t repeatKey = 0;
  uint32_t repeatAt = 0;
  bool caps = false;
  std::vector<Event> pending;
} ks_;

bool usageToEvent(uint8_t u, uint8_t mods, bool caps, Event& e) {
  const bool shift = mods & 0x22, ctrl = mods & 0x11, alt = mods & 0x44, gui = mods & 0x88;
  uint8_t m = (ctrl || gui ? M_CTRL : 0) | (shift ? M_SHIFT : 0) | (alt ? M_ALT : 0);
  static const char* numShift = ")!@#$%^&*(";
  static const struct { uint8_t u; char c, s; } punct[] = {
      {0x2C, ' ', ' '}, {0x2D, '-', '_'}, {0x2E, '=', '+'}, {0x2F, '[', '{'}, {0x30, ']', '}'},
      {0x31, '\\', '|'}, {0x32, '#', '~'}, {0x33, ';', ':'}, {0x34, '\'', '"'}, {0x35, '`', '~'},
      {0x36, ',', '<'}, {0x37, '.', '>'}, {0x38, '/', '?'}, {0x64, '\\', '|'}};
  char c = 0;
  if (u >= 0x04 && u <= 0x1D) {
    c = 'a' + (u - 0x04);
    if ((shift != caps) && !(ctrl || gui || alt)) c = toupper(c);
  } else if (u >= 0x1E && u <= 0x27) {
    int d = u == 0x27 ? 0 : u - 0x1E + 1;
    c = shift ? numShift[d] : '0' + d;
  } else {
    for (auto& p : punct)
      if (p.u == u) c = shift ? p.s : p.c;
  }
  if (u >= 0x54 && u <= 0x63) {  // keypad (num lock assumed on)
    static const char* kp = "/*-+\n1234567890.";
    c = kp[u - 0x54];
    if (c == '\n') c = 0, u = 0x28;
  }
  if (c) {
    // Shift is already applied to the character; keep it only for Ctrl/Alt combos
    e = keyEvent(K_CHAR, (ctrl || gui || alt) ? m : (m & ~M_SHIFT), c);
    return true;
  }
  Key k = K_NONE;
  switch (u) {
    case 0x28: k = K_ENTER; break;
    case 0x29: k = K_ESC; break;
    case 0x2A: k = K_BACKSPACE; break;
    case 0x2B: k = K_TAB; break;
    case 0x3B: k = K_F2; break;
    case 0x4A: k = K_HOME; break;
    case 0x4B: k = K_PGUP; break;
    case 0x4C: k = K_DELETE; break;
    case 0x4D: k = K_END; break;
    case 0x4E: k = K_PGDN; break;
    case 0x4F: k = K_RIGHT; break;
    case 0x50: k = K_LEFT; break;
    case 0x51: k = K_DOWN; break;
    case 0x52: k = K_UP; break;
  }
  if (k == K_NONE) return false;
  e = keyEvent(k, m);
  return true;
}

void handleKeyboard(const KbdLayout& L, const uint8_t* d, size_t len) {
  uint8_t mods = 0;
  for (int b = 0; b < 8 && L.mods.off >= 0; b++) mods |= readBits(d, len, {L.mods.off + b, 1, 1}) << b;
  std::vector<uint8_t> now;
  for (int k = 0; L.keys.off >= 0 && k < L.keys.count; k++) {
    int u = readBits(d, len, L.keys, k);
    if (u == 1) return;  // rollover error: ignore the report
    if (u > 3) now.push_back(u);
  }
  for (int k = 0; L.bitmap.off >= 0 && k < L.bitmap.count; k++)
    if (readBits(d, len, {L.bitmap.off + k, 1, 1})) now.push_back(L.bitmap.usageMin + k);

  for (uint8_t u : now) {
    if (std::find(ks_.down.begin(), ks_.down.end(), u) != ks_.down.end()) continue;
    if (u == 0x39) {
      ks_.caps = !ks_.caps;
      continue;
    }
    Event e;
    if (usageToEvent(u, mods, ks_.caps, e)) {
      ks_.pending.push_back(e);
      ks_.repeatKey = u;
      ks_.repeatAt = millis() + 450;
    }
  }
  if (std::find(now.begin(), now.end(), ks_.repeatKey) == now.end()) ks_.repeatKey = 0;
  ks_.down = now;
  ks_.mods = mods;
}

void handleMouse(const MouseLayout& L, const uint8_t* d, size_t len) {
  int wheel = readBits(d, len, L.wheel);
  if (wheel) {
    Event e;
    e.type = Type::Drag;
    e.dy = wheel * 40;
    ks_.pending.push_back(e);
  }
}

}  // namespace

// ---------------------------------------------------------------------------

void begin() {
  Preferences p;
  p.begin("bt", true);
  enabled_ = p.getBool("enabled", true);
  p.end();
  if (!enabled_) {
    state_ = State::Off;
    return;
  }
  queue_ = xQueueCreate(32, sizeof(Report));
  NimBLEDevice::init("CYD Notes");
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  xTaskCreatePinnedToCore(task, "btkbd", 6144, nullptr, 2, nullptr, 0);
  Serial.printf("[bt] started, heap free %u (largest %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

bool enabled() { return enabled_; }

void setEnabled(bool on) {
  Preferences p;
  p.begin("bt", false);
  p.putBool("enabled", on);
  p.end();
}

State state() { return state_; }

const char* stateText() {
  switch (state_.load()) {
    case State::Off: return "Bluetooth off";
    case State::NoKeyboard: return "No keyboard paired";
    case State::Scanning: return "Searching...";
    case State::ScanDone: return "Choose a keyboard";
    case State::Connecting: return "Connecting...";
    case State::Pairing: return "Pairing...";
    case State::Connected: return "Connected";
    case State::Reconnecting: return "Waiting for keyboard";
  }
  return "";
}

std::string keyboardName() {
  std::lock_guard<std::mutex> lock(mu_);
  return name_;
}

uint32_t passkey() { return passkey_; }

void startScan() {
  if (enabled_) request_ = 1;
}

std::vector<Found> scanResults() {
  std::lock_guard<std::mutex> lock(mu_);
  return found_;
}

void pair(const Found& f) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    pairTarget_ = f;
  }
  request_ = 2;
}

void cancelPairing() { request_ = 3; }
void forget() { request_ = 4; }
void setRawLogging(bool on) { rawLog_ = on; }

bool poll(Event& e) {
  if (!queue_) return false;
  Report r;
  while (ks_.pending.empty() && xQueueReceive(queue_, &r, 0) == pdTRUE) {
    if (rawLog_) {
      Serial.printf("[bt] report id %u:", r.id);
      for (int i = 0; i < r.len; i++) Serial.printf(" %02x", r.data[i]);
      Serial.println();
    }
    std::lock_guard<std::mutex> lock(mu_);
    bool handled = false;
    for (auto& k : layouts_.kbd)
      if (k.id == r.id) handleKeyboard(k, r.data, r.len), handled = true;
    for (auto& m : layouts_.mouse)
      if (m.id == r.id) handleMouse(m, r.data, r.len), handled = true;
    if (!handled && layouts_.kbd.empty() && r.len == 8) {
      // No usable report map: assume the boot keyboard format
      KbdLayout boot;
      boot.mods = {0, 1, 8, 0xE0};
      boot.keys = {16, 8, 6, 0};
      handleKeyboard(boot, r.data, r.len);
    }
  }
  if (ks_.pending.empty() && ks_.repeatKey && millis() >= ks_.repeatAt) {
    Event rep;
    if (usageToEvent(ks_.repeatKey, ks_.mods, ks_.caps, rep)) ks_.pending.push_back(rep);
    ks_.repeatAt = millis() + 40;
  }
  if (ks_.pending.empty()) return false;
  e = ks_.pending.front();
  ks_.pending.erase(ks_.pending.begin());
  return true;
}

}  // namespace btkbd
