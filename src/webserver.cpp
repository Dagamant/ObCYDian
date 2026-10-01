#include "webserver.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include <esp32/rom/crc.h>

#include "battery.h"
#include "clock.h"
#include "dav.h"
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

// Streams a large JSON response in ~1 KB chunks (chunked encoding) instead of building it
// all in RAM first.
class JsonOut {
 public:
  JsonOut() {
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
  }
  ~JsonOut() {
    flush();
    server.sendContent("");  // end of the chunked response
  }
  JsonOut& operator<<(const std::string& s) {
    buf_ += s;
    if (buf_.size() > 1024) flush();
    return *this;
  }
  JsonOut& operator<<(const char* s) { return *this << std::string(s); }

 private:
  void flush() {
    if (!buf_.empty()) server.sendContent(buf_.data(), buf_.size());
    buf_.clear();
  }
  std::string buf_;
};

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

bool syncTimeFromArgs();

void handleTree() {
  if (server.hasArg("utc")) syncTimeFromArgs();
  if (!requireCard()) return;
  JsonOut out;
  out << "{\"folders\":[";
  bool first = true;
  for (auto& f : storage::folders()) {
    out << (first ? "" : ",") << q(f);
    first = false;
  }
  out << "],\"notes\":[";
  first = true;
  for (auto& n : storage::notes()) {
    out << (first ? "" : ",") << q(n);
    first = false;
  }
  out << "],\"aliases\":{";
  std::string lastPath;
  for (auto& a : storage::aliases()) {  // grouped by note: {"/path.md": ["alias", ...]}
    if (a.second != lastPath) {
      out << (lastPath.empty() ? "" : "],") << q(a.second) << ":[";
      lastPath = a.second;
      first = true;
    }
    out << (first ? "" : ",") << q(a.first);
    first = false;
  }
  out << (lastPath.empty() ? "}}" : "]}}");
}

// Browsers send their clock (UTC seconds + offset) so the device knows the date
bool syncTimeFromArgs() {
  long utc = atol(arg("utc").c_str());
  int offset = atoi(arg("offset").c_str());
  if (utc < 1700000000) return false;
  if (!wallclock::valid() || labs((long)time(nullptr) - utc) > 120) wallclock::set(utc, offset);
  else wallclock::setOffset(offset);
  return true;
}

void handleTime() {
  long utc = atol(arg("utc").c_str());
  int offset = atoi(arg("offset").c_str());
  if (utc < 1700000000) return sendError(400, "Bad time");
  // Trust the browser when we have no time yet, or for the timezone offset
  if (!wallclock::valid() || labs((long)time(nullptr) - utc) > 120) wallclock::set(utc, offset);
  else wallclock::setOffset(offset);
  sendJson(200, "{}");
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
  if (!existed) storage::indexAdd(path);
  else storage::indexUpdate(path);  // aliases may have changed
  Serial.printf("[web] saved %s (%u bytes)\n", path.c_str(), (unsigned)body.size());
  app::externalChange(path);
  sendJson(200, "{\"path\":" + q(path) + "}");
}

void handleDeleteNote() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isNotePath(path)) return sendError(400, "Bad path");
  app::saveCurrent();
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
  app::saveCurrent();
  if (!storage::renameNote(from, to, &links)) return sendError(500, "Rename failed");
  Serial.printf("[web] renamed %s -> %s (%d links)\n", from.c_str(), to.c_str(), links);
  app::notePathChanged(from, to);
  sendJson(200, "{\"path\":" + q(to) + ",\"links\":" + std::to_string(links) + "}");
}

void handleBacklinks() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isNotePath(path)) return sendError(400, "Bad path");
  std::string j = "[";
  bool first = true;
  for (auto& b : storage::backlinks(path)) {
    j += std::string(first ? "" : ",") + "{\"path\":" + q(b.path) + ",\"line\":" + q(b.text) + ",\"n\":" + std::to_string(b.line) + "}";
    first = false;
  }
  sendJson(200, j + "]");
}

// Folder paths: absolute, inside the vault, not the root itself
bool isFolderPath(const std::string& p) { return safePath(p) && p.size() > 1 && p.back() != '/'; }

void handleMakeFolder() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isFolderPath(path)) return sendError(400, "Bad path");
  if (storage::exists(path)) return sendError(409, "Already exists");
  if (!storage::mkdirs(path)) return sendError(500, "Couldn't create folder");
  storage::rescan();
  app::externalChange(path);
  sendJson(200, "{\"path\":" + q(path) + "}");
}

void handleRenameFolder() {
  if (!requireCard()) return;
  std::string from = arg("from"), to = arg("to");
  if (!isFolderPath(from) || !isFolderPath(to)) return sendError(400, "Bad path");
  if (storage::exists(to)) return sendError(409, "Something with that name exists");
  int links = 0;
  app::saveCurrent();
  if (!storage::renameFolder(from, to, &links)) return sendError(500, "Rename failed");
  Serial.printf("[web] renamed folder %s -> %s (%d links)\n", from.c_str(), to.c_str(), links);
  app::folderPathChanged(from, to);
  sendJson(200, "{\"path\":" + q(to) + ",\"links\":" + std::to_string(links) + "}");
}

void handleDeleteFolder() {
  if (!requireCard()) return;
  std::string path = arg("path");
  if (!isFolderPath(path)) return sendError(400, "Bad path");
  app::saveCurrent();
  bool ok = storage::removeFolder(path);
  Serial.printf("[web] deleted folder %s\n", path.c_str());
  app::folderDeleted(path);
  if (!ok) return sendError(500, "Couldn't delete everything");
  sendJson(200, "{}");
}

void handleSearch() {
  if (!requireCard()) return;
  std::string query = arg("q");
  auto hits = storage::searchText(query, 60);
  JsonOut out;
  out << "[";
  bool first = true;
  for (auto& h : hits) {
    out << (first ? "" : ",") << "{\"path\":" << q(h.path) << ",\"line\":" << std::to_string(h.line)
        << ",\"text\":" << q(h.text.size() > 200 ? h.text.substr(0, 200) : h.text) << "}";
    first = false;
  }
  out << "]";
}

void handleRadio() {
  std::string m = arg("mode");
  radio::Mode mode = m == "bluetooth" ? radio::Mode::Bluetooth : m == "wifi" ? radio::Mode::Wifi : m == "off" ? radio::Mode::Off : (radio::Mode)-1;
  if ((int)mode < 0) return sendError(400, "mode must be bluetooth, wifi or off");
  sendJson(200, "{\"ok\":true}");
  delay(300);
  radio::switchTo(mode);
}

// --- Graph: notes, links between them and links to notes that don't exist yet
void handleGraph() {
  if (!requireCard()) return;
  const auto& notes = storage::notes();
  std::vector<std::string> ghosts;
  std::vector<std::pair<int, int>> links;
  for (int i = 0; i < (int)notes.size(); i++) {
    std::string text;
    if (!storage::readFile(notes[i], text)) continue;
    for (size_t a = text.find("[["); a != std::string::npos; a = text.find("[[", a + 2)) {
      size_t b = text.find("]]", a + 2);
      if (b == std::string::npos) break;
      size_t end = std::min(text.find_first_of("|#", a + 2), b);
      std::string target = text.substr(a + 2, end - a - 2);
      if (target.empty() || target.find('\n') != std::string::npos) continue;
      std::string path = storage::resolveLink(target, notes[i]);
      int j = -1;
      if (!path.empty()) {
        // notes are sorted case-insensitively: binary search for the index
        auto it = std::lower_bound(notes.begin(), notes.end(), path, [](const std::string& a, const std::string& b) {
          return strcasecmp(a.c_str(), b.c_str()) < 0;
        });
        if (it != notes.end() && *it == path) j = it - notes.begin();
      } else {
        size_t dot = target.find_last_of('.');
        if (dot != std::string::npos && target.size() - dot <= 5 && strcasecmp(target.c_str() + dot, ".md") != 0)
          continue;  // an attachment (image etc.), not a missing note
        for (int k = 0; k < (int)ghosts.size(); k++)
          if (strcasecmp(ghosts[k].c_str(), target.c_str()) == 0) j = notes.size() + k;
        if (j < 0) {
          ghosts.push_back(target);
          j = notes.size() + ghosts.size() - 1;
        }
      }
      if (j >= 0 && j != i) {
        bool dup = false;
        for (auto& l : links)
          if ((l.first == i && l.second == j) || (l.first == j && l.second == i)) dup = true;
        if (!dup) links.push_back({i, j});
      }
    }
  }
  JsonOut out;
  out << "{\"notes\":[";
  for (size_t i = 0; i < notes.size(); i++) out << (i ? "," : "") << q(notes[i]);
  out << "],\"ghosts\":[";
  for (size_t i = 0; i < ghosts.size(); i++) out << (i ? "," : "") << q(ghosts[i]);
  out << "],\"links\":[";
  for (size_t i = 0; i < links.size(); i++)
    out << (i ? "," : "") << "[" << std::to_string(links[i].first) << "," << std::to_string(links[i].second) << "]";
  out << "]}";
}

// --- Vault backup: an uncompressed .zip streamed straight from the card. Every size is
// known up front (local headers + data + data descriptors + central directory), so the
// response has a Content-Length and nothing is buffered; CRCs go in the data descriptors.

void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

void dosTime(uint32_t utc, uint16_t* date, uint16_t* time) {
  time_t t = utc ? (time_t)utc + wallclock::offsetMinutes() * 60 : wallclock::now();
  tm tm_;
  gmtime_r(&t, &tm_);
  *date = ((tm_.tm_year - 80) << 9) | ((tm_.tm_mon + 1) << 5) | tm_.tm_mday;
  *time = (tm_.tm_hour << 11) | (tm_.tm_min << 5) | (tm_.tm_sec / 2);
}

void handleBackup() {
  if (!requireCard()) return;
  app::saveCurrent();
  auto files = storage::listAll("/");
  struct Entry {
    std::string name;
    uint32_t size, crc, offset;
    uint16_t date, time;
  };
  std::vector<Entry> entries;
  uint32_t total = 0, central = 0;
  for (auto& f : files) {
    Entry e{f.path.substr(1), f.size, 0, total, 0, 0};
    dosTime(f.mtime, &e.date, &e.time);
    total += 30 + e.name.size() + e.size + 16;
    central += 46 + e.name.size();
    entries.push_back(e);
  }
  const uint32_t length = total + central + 22;
  std::string fname = "obcydian-vault-" + wallclock::format("YYYY-MM-DD") + ".zip";
  server.sendHeader("Content-Disposition", ("attachment; filename=\"" + fname + "\"").c_str());
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(length);
  server.send(200, "application/zip", "");
  WiFiClient client = server.client();
  uint8_t h[46];
  for (auto& e : entries) {
    memset(h, 0, 30);
    put32(h, 0x04034b50);
    put16(h + 4, 20);
    put16(h + 6, 0x0808);  // sizes/CRC in a data descriptor; UTF-8 names
    put16(h + 10, e.time);
    put16(h + 12, e.date);
    put16(h + 26, e.name.size());
    client.write(h, 30);
    client.write((const uint8_t*)e.name.data(), e.name.size());
    uint32_t crc = 0, sent = 0;
    storage::streamFile("/" + e.name, [&](const uint8_t* d, size_t n) {
      n = std::min<size_t>(n, e.size - sent);  // never send more than announced
      crc = crc32_le(crc, d, n);
      sent += n;
      return client.write(d, n) == n;
    });
    while (sent < e.size) {  // file shrank while streaming: pad so the archive stays well-formed
      static const uint8_t zeros[64] = {0};
      size_t n = std::min<size_t>(sizeof(zeros), e.size - sent);
      crc = crc32_le(crc, zeros, n);
      client.write(zeros, n);
      sent += n;
    }
    e.crc = crc;
    put32(h, 0x08074b50);
    put32(h + 4, crc);
    put32(h + 8, e.size);
    put32(h + 12, e.size);
    client.write(h, 16);
    power::keepAwake();
  }
  for (auto& e : entries) {
    memset(h, 0, 46);
    put32(h, 0x02014b50);
    put16(h + 4, 20);
    put16(h + 6, 20);
    put16(h + 8, 0x0808);
    put16(h + 12, e.time);
    put16(h + 14, e.date);
    put32(h + 16, e.crc);
    put32(h + 20, e.size);
    put32(h + 24, e.size);
    put16(h + 28, e.name.size());
    put32(h + 42, e.offset);
    client.write(h, 46);
    client.write((const uint8_t*)e.name.data(), e.name.size());
  }
  memset(h, 0, 22);
  put32(h, 0x06054b50);
  put16(h + 8, entries.size());
  put16(h + 10, entries.size());
  put32(h + 12, central);
  put32(h + 16, total);
  client.write(h, 22);
  Serial.printf("[web] backup: %u files, %u bytes\n", (unsigned)entries.size(), (unsigned)length);
}

// --- Uploads (multipart, one file per request): streamed to the card
std::string uploadPath_;
bool uploadOk_ = false;

void handleUploadChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    // ?path= gives the full destination (folder uploads keep their structure)
    uploadPath_ = arg("path");
    uploadOk_ = safePath(uploadPath_) && uploadPath_.size() > 1 && storage::writeBegin(uploadPath_);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadOk_ && !storage::writeChunk(up.buf, up.currentSize)) uploadOk_ = false;
    power::keepAwake();
  } else if (up.status == UPLOAD_FILE_END) {
    uploadOk_ = uploadOk_ && storage::writeFinish();
    if (!uploadOk_) storage::writeAbort();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    storage::writeAbort();
    uploadOk_ = false;
  }
}

void handleUploadDone() {
  if (!uploadOk_) return sendError(500, "Upload failed");
  Serial.printf("[web] uploaded %s\n", uploadPath_.c_str());
  storage::indexAdd(uploadPath_);
  app::externalChange(uploadPath_);
  sendJson(200, "{\"path\":" + q(uploadPath_) + "}");
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
  server.on("/api/folder", HTTP_POST, handleMakeFolder);
  server.on("/api/folder", HTTP_DELETE, handleDeleteFolder);
  server.on("/api/folder/rename", HTTP_POST, handleRenameFolder);
  server.on("/api/search", HTTP_GET, handleSearch);
  server.on("/api/radio", HTTP_POST, handleRadio);
  server.on("/api/time", HTTP_ANY, handleTime);  // GET from the web app (see index.html)
  server.on("/api/backup.zip", HTTP_GET, handleBackup);
  server.on("/api/graph", HTTP_GET, handleGraph);
  server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleSetWifi);
  server.onNotFound(handleNotFound);
  static const char* headers[] = {"Content-Type"};
  server.collectHeaders(headers, 1);
  server.begin();
  serverStarted_ = true;
  dav::begin();
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
  } else if (state_ == State::Connected) {
    // Dropped: the driver auto-reconnects; only nudge it occasionally (calling reconnect()
    // too often restarts the attempt before it can finish), and start over after a minute.
    static uint32_t lostAt = 0, nudgedAt = 0;
    if (WiFi.status() == WL_CONNECTED) {
      if (lostAt) Serial.printf("[wifi] reconnected after %u s\n", (unsigned)((millis() - lostAt) / 1000));
      lostAt = 0;
    } else if (!lostAt) {
      lostAt = nudgedAt = millis();
      Serial.println("[wifi] connection lost");
    } else if (millis() - lostAt > 60000) {
      Serial.println("[wifi] still offline, reconnecting from scratch");
      WiFi.disconnect();
      WiFi.begin(ssid_.c_str(), pass_.c_str());
      lostAt = nudgedAt = millis();
    } else if (millis() - nudgedAt > 20000) {
      WiFi.reconnect();
      nudgedAt = millis();
    }
  }
  if (state_ == State::AccessPoint) {
    dns.processNextRequest();
    // With a saved network, keep retrying it in the background (the AP stays up meanwhile)
    static uint32_t lastTry = 0;
    if (!ssid_.empty()) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] reached '%s' after all, closing the setup network\n", ssid_.c_str());
        dns.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        state_ = State::Connected;
        MDNS.begin(kHostname);
        MDNS.addService("http", "tcp", 80);
      } else if (millis() - lastTry > 30000) {
        lastTry = millis();
        WiFi.begin(ssid_.c_str(), pass_.c_str());
      }
    }
  }
  if (serverStarted_) {
    server.handleClient();
    dav::loop();
  }
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
