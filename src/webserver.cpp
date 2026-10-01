#include "webserver.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "battery.h"
#include "power.h"
#include "radio.h"
#include "storage.h"

extern const char index_html_start[] asm("_binary_src_web_index_html_start");
extern const char index_html_end[] asm("_binary_src_web_index_html_end");

namespace web {

namespace {

constexpr const char* kHostname = "obcydian";
constexpr uint32_t kConnectTimeoutMs = 20000;

WebServer server(80);
DNSServer dns;
State state_ = State::Off;
uint32_t connectStarted_ = 0;
bool serverStarted_ = false;
std::string ssid_, pass_, apSsid_, apPass_;

void loadSettings() {
  Preferences p;
  p.begin("wifi", false);
  ssid_ = p.getString("ssid", "").c_str();
  pass_ = p.getString("pass", "").c_str();
  apPass_ = p.getString("appass", "").c_str();
  if (apPass_.empty()) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%08u", (unsigned)(esp_random() % 100000000));
    apPass_ = buf;
    p.putString("appass", apPass_.c_str());
  }
  p.end();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[32];
  snprintf(name, sizeof(name), "ObCYDian-%02X%02X", mac[4], mac[5]);
  apSsid_ = name;
}

std::string jsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof(b), "\\u%04x", c);
          o += b;
        } else {
          o += (char)c;
        }
    }
  }
  return o;
}

std::string q(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

// Only absolute paths inside the vault, no "..", no backslashes.
bool safePath(const std::string& p) {
  return !p.empty() && p[0] == '/' && p.find("..") == std::string::npos &&
         p.find('\\') == std::string::npos && p.find("//") == std::string::npos;
}

bool isNotePath(const std::string& p) {
  return safePath(p) && p.size() > 3 && strcasecmp(p.c_str() + p.size() - 3, ".md") == 0;
}

std::string arg(const char* name) { return server.arg(name).c_str(); }

void sendJson(int code, const std::string& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(body.size());
  server.send(code, "application/json", "");
  server.sendContent(body.data(), body.size());
}

void sendError(int code, const char* msg) { sendJson(code, "{\"error\":" + q(msg) + "}"); }

bool requireCard() {
  power::keepAwake();  // web use counts as activity for the sleep timer
  if (storage::state() == storage::State::Mounted) return true;
  sendError(503, "No SD card");
  return false;
}

const char* mimeFor(const std::string& p) {
  auto ext = p.substr(p.find_last_of('.') + 1);
  for (auto& c : ext) c = tolower(c);
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "webp") return "image/webp";
  if (ext == "pdf") return "application/pdf";
  if (ext == "md") return "text/markdown; charset=utf-8";
  if (ext == "txt" || ext == "csv") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

// --- Handlers ---------------------------------------------------------------

void handleIndex() {
  power::keepAwake();
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", index_html_start, index_html_end - index_html_start - 1);
}

void handleStatus() {
  power::keepAwake();
  std::string j = "{\"mode\":" + q(radio::name(radio::mode())) + ",\"state\":" + q(stateText()) +
                  ",\"ssid\":" + q(state_ == State::AccessPoint ? apSsid_ : ssid_) + ",\"savedSsid\":" + q(ssid_) +
                  ",\"ap\":" + (state_ == State::AccessPoint ? "true" : "false") + ",\"ip\":" + q(ip()) +
                  ",\"card\":" + (storage::state() == storage::State::Mounted ? "true" : "false") +
                  ",\"notes\":" + std::to_string(storage::notes().size()) +
                  ",\"heap\":" + std::to_string(ESP.getFreeHeap()) +
                  ",\"battery\":" + (battery::present() ? std::to_string(battery::percent()) : "null") +
                  ",\"volts\":" + std::to_string(battery::voltage()) +
                  ",\"charging\":" + (battery::charging() ? "true" : "false") + "}";
  sendJson(200, j);
}

void handleTree() {
  if (!requireCard()) return;
  std::string j = "{\"folders\":[";
  bool first = true;
  for (auto& f : storage::folders()) {
    j += (first ? "" : ",") + q(f);
    first = false;
  }
  j += "],\"notes\":[";
  first = true;
  for (auto& n : storage::notes()) {
    j += (first ? "" : ",") + q(n);
    first = false;
  }
  j += "]}";
  sendJson(200, j);
}

void handleGetNote() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isNotePath(path)) return sendError(400, "Bad path");
  std::string text;
  if (!storage::readFile(path, text, 512 * 1024)) return sendError(404, "Note not found");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(text.size());
  server.send(200, "text/markdown; charset=utf-8", "");
  server.sendContent(text.data(), text.size());
}

void handlePutNote() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isNotePath(path)) return sendError(400, "Bad path");
  // The note must arrive as a raw text body. WebServer folds form-encoded bodies into its
  // argument parsing (dropping most of the text), which would save a blank note, so refuse
  // those rather than wipe the file.
  String type = server.header("Content-Type");
  if (type.startsWith("application/x-www-form-urlencoded") || type.startsWith("multipart/"))
    return sendError(415, "Send the note as text/plain");
  bool existed = storage::exists(path);
  std::string body = server.arg("plain").c_str();
  if (!storage::writeFile(path, body)) return sendError(500, "Write failed");
  if (!existed) storage::rescan();
  Serial.printf("[web] saved %s (%u bytes)\n", path.c_str(), (unsigned)body.size());
  app::externalChange(path);
  sendJson(200, "{\"path\":" + q(path) + "}");
}

void handleDeleteNote() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isNotePath(path)) return sendError(400, "Bad path");
  if (!storage::remove(path)) return sendError(404, "Delete failed");
  Serial.printf("[web] deleted %s\n", path.c_str());
  app::noteDeleted(path);
  sendJson(200, "{}");
}

void handleRename() {
  if (!requireCard()) return;
  std::string from = arg("from"), to = arg("to");
  if (!isNotePath(from) || !isNotePath(to)) return sendError(400, "Bad path");
  if (storage::exists(to)) return sendError(409, "A note with that name exists");
  int links = 0;
  if (!storage::renameNote(from, to, &links)) return sendError(500, "Rename failed");
  Serial.printf("[web] renamed %s -> %s (%d links)\n", from.c_str(), to.c_str(), links);
  app::notePathChanged(from, to);
  app::externalChange(to);
  sendJson(200, "{\"path\":" + q(to) + ",\"links\":" + std::to_string(links) + "}");
}

void handleBacklinks() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isNotePath(path)) return sendError(400, "Bad path");
  std::string j = "[";
  bool first = true;
  for (auto& b : storage::backlinks(path)) {
    j += std::string(first ? "" : ",") + "{\"path\":" + q(b.first) + ",\"line\":" + q(b.second) + "}";
    first = false;
  }
  sendJson(200, j + "]");
}

void handleFile() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!safePath(path)) return sendError(400, "Bad path");
  int64_t size = storage::fileSize(path);
  if (size < 0) return sendError(404, "Not found");
  server.sendHeader("Cache-Control", "max-age=300");
  server.setContentLength(size);
  server.send(200, mimeFor(path), "");
  storage::streamFile(path, [](const uint8_t* d, size_t n) {
    return server.client().write(d, n) == n;
  });
}

void handleScan() {
  int n = WiFi.scanNetworks();
  std::string j = "[";
  for (int i = 0; i < n; i++) {
    j += std::string(i ? "," : "") + "{\"ssid\":" + q(WiFi.SSID(i).c_str()) + ",\"rssi\":" +
         std::to_string(WiFi.RSSI(i)) + ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  sendJson(200, j + "]");
}

void handleSetWifi() {
  std::string ssid = arg("ssid"), pass = arg("pass");
  if (ssid.empty()) return sendError(400, "SSID required");
  sendJson(200, "{\"ok\":true}");
  delay(200);  // let the response go out before the radio reconfigures
  setNetwork(ssid, pass);
}

void handleNotFound() {
  if (state_ == State::AccessPoint) {
    // Captive portal: send phones' connectivity checks to the setup page
    server.sendHeader("Location", "http://192.168.4.1/#settings");
    server.send(302, "text/plain", "");
    return;
  }
  sendError(404, "Not found");
}

void startServer() {
  if (serverStarted_) return;
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/tree", HTTP_GET, handleTree);
  server.on("/api/note", HTTP_GET, handleGetNote);
  server.on("/api/note", HTTP_PUT, handlePutNote);
  server.on("/api/note", HTTP_DELETE, handleDeleteNote);
  server.on("/api/rename", HTTP_POST, handleRename);
  server.on("/api/backlinks", HTTP_GET, handleBacklinks);
  server.on("/api/file", HTTP_GET, handleFile);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleSetWifi);
  server.onNotFound(handleNotFound);
  static const char* headers[] = {"Content-Type"};
  server.collectHeaders(headers, 1);
  server.begin();
  serverStarted_ = true;
}

void startAccessPoint() {
  Serial.printf("[wifi] starting access point %s\n", apSsid_.c_str());
  WiFi.mode(WIFI_AP_STA);  // STA stays available for scanning from the setup page
  WiFi.softAP(apSsid_.c_str(), apPass_.c_str());
  dns.start(53, "*", WiFi.softAPIP());
  state_ = State::AccessPoint;
  startServer();
}

void startConnect() {
  Serial.printf("[wifi] connecting to '%s'\n", ssid_.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kHostname);
  WiFi.begin(ssid_.c_str(), pass_.c_str());
  connectStarted_ = millis();
  state_ = State::Connecting;
}

}  // namespace

// ---------------------------------------------------------------------------

void begin() {
  if (radio::mode() != radio::Mode::Wifi) return;
  WiFi.persistent(false);
  loadSettings();
  if (ssid_.empty()) startAccessPoint();
  else startConnect();
}

void loop() {
  if (state_ == State::Off) return;
  if (state_ == State::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state_ = State::Connected;
      Serial.printf("[wifi] connected, http://%s/ heap %u\n", WiFi.localIP().toString().c_str(), ESP.getFreeHeap());
      MDNS.begin(kHostname);
      MDNS.addService("http", "tcp", 80);
      startServer();
    } else if (millis() - connectStarted_ > kConnectTimeoutMs) {
      Serial.println("[wifi] connection timed out");
      WiFi.disconnect();
      startAccessPoint();
    }
  } else if (state_ == State::Connected && WiFi.status() != WL_CONNECTED) {
    // Dropped: the WiFi driver keeps retrying on its own
    static uint32_t lostAt = 0;
    if (!lostAt) lostAt = millis();
    if (millis() - lostAt > 2000) {
      Serial.println("[wifi] connection lost, reconnecting");
      WiFi.reconnect();
      lostAt = 0;
    }
  }
  if (state_ == State::AccessPoint) dns.processNextRequest();
  if (serverStarted_) server.handleClient();
}

State state() { return state_; }

const char* stateText() {
  switch (state_) {
    case State::Off: return "WiFi off";
    case State::Connecting: return "Connecting...";
    case State::Connected: return "Connected";
    case State::AccessPoint: return "Setup network";
  }
  return "";
}

std::string ip() {
  if (state_ == State::Connected) return WiFi.localIP().toString().c_str();
  if (state_ == State::AccessPoint) return WiFi.softAPIP().toString().c_str();
  return "";
}

std::string url() { return ip().empty() ? "" : "http://" + ip() + "/"; }

std::string savedSsid() {
  if (ssid_.empty() && state_ == State::Off) {
    Preferences p;
    p.begin("wifi", false);  // read-write so a missing namespace is created, not an error
    std::string s = p.getString("ssid", "").c_str();
    p.end();
    return s;
  }
  return ssid_;
}

std::string apSsid() { return apSsid_; }
std::string apPassword() { return apPass_; }
bool hasNetwork() { return !savedSsid().empty(); }

void setNetwork(const std::string& ssid, const std::string& pass) {
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", ssid.c_str());
  p.putString("pass", pass.c_str());
  p.end();
  ssid_ = ssid;
  pass_ = pass;
  if (radio::mode() != radio::Mode::Wifi) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  startConnect();
}

void forgetNetwork() {
  Preferences p;
  p.begin("wifi", false);
  p.remove("ssid");
  p.remove("pass");
  p.end();
  ssid_.clear();
  pass_.clear();
  if (radio::mode() == radio::Mode::Wifi && state_ != State::AccessPoint) {
    WiFi.disconnect();
    startAccessPoint();
  }
}

}  // namespace web
