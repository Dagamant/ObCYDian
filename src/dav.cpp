#include "dav.h"

#include <WiFi.h>

#include <string>
#include <vector>

#include "app.h"
#include "clock.h"
#include "power.h"
#include "storage.h"

namespace dav {

namespace {

WiFiServer server(kPort);
bool started_ = false;
uint32_t lastToast_ = 0;

struct Request {
  std::string method, uri, base, path;  // path: vault path ("/" = root)
  std::string depth = "infinity", destination, overwrite = "T";
  long contentLength = -1;
  bool chunked = false, expectContinue = false;
};

// --- Small helpers -----------------------------------------------------------

bool readLine(WiFiClient& c, std::string& out, uint32_t timeoutMs = 4000) {
  out.clear();
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (c.available()) {
      char ch = c.read();
      if (ch == '\n') {
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
      }
      if (out.size() < 2048) out += ch;
    }
    if (!c.connected()) return false;
    delay(1);
  }
  return false;
}

std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

std::string urlEncodePath(const std::string& s) {
  std::string o;
  char b[4];
  for (unsigned char c : s) {
    if (isalnum(c) || strchr("/-_.~", c)) o += c;
    else {
      snprintf(b, sizeof(b), "%%%02X", c);
      o += b;
    }
  }
  return o;
}

std::string xmlEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

std::string httpDate(uint32_t utc) {
  time_t t = utc ? utc : time(nullptr);
  tm tm_;
  gmtime_r(&t, &tm_);
  char b[40];
  strftime(b, sizeof(b), "%a, %d %b %Y %H:%M:%S GMT", &tm_);
  return b;
}

// /<base>/<rest> -> vault path "/<rest>"; the bare root "/" is the vault too
void mapPath(Request& r, const std::string& rawPath) {
  std::string p = urlDecode(rawPath);
  while (p.find("//") != std::string::npos) p.erase(p.find("//"), 1);
  if (p.empty() || p[0] != '/') p = "/" + p;
  size_t slash = p.find('/', 1);
  if (p == "/") {
    r.base = "";
    r.path = "/";
  } else {
    r.base = p.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
    std::string rest = slash == std::string::npos ? "/" : p.substr(slash);
    while (rest.size() > 1 && rest.back() == '/') rest.pop_back();
    r.path = rest;
  }
}

bool allowed(const std::string& path) {
  return path.find("..") == std::string::npos && path.find('\\') == std::string::npos &&
         path.compare(0, 10, "/.obcydian") != 0;  // device state stays out of sync
}

std::string href(const Request& r, const std::string& path, bool dir) {
  std::string h = (r.base.empty() ? "" : "/" + r.base) + (path == "/" ? "/" : path);
  if (dir && h.back() != '/') h += "/";
  return urlEncodePath(h);
}

void status(WiFiClient& c, int code, const char* text, const std::string& extraHeaders = "", const std::string& body = "") {
  std::string h = "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\n" + extraHeaders +
                  "Content-Length: " + std::to_string(body.size()) + "\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n\r\n" + body;
  c.write((const uint8_t*)h.data(), h.size());
}

const char* mime(const std::string& p) {
  auto ends = [&](const char* e) {
    size_t n = strlen(e);
    return p.size() >= n && strcasecmp(p.c_str() + p.size() - n, e) == 0;
  };
  if (ends(".md") || ends(".txt")) return "text/markdown; charset=utf-8";
  if (ends(".png")) return "image/png";
  if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
  if (ends(".json")) return "application/json";
  if (ends(".css")) return "text/css";
  if (ends(".js")) return "application/javascript";
  return "application/octet-stream";
}

void syncToast(const std::string& path) {
  power::keepAwake();
  if (millis() - lastToast_ > 2500 && power::screenOn()) {
    lastToast_ = millis();
    app::toast("Syncing " + storage::baseName(path), 1500);
  }
}

// --- Methods -----------------------------------------------------------------

void propEntry(std::string& out, const std::string& h, const std::string& name, bool dir, uint32_t size, uint32_t mtime) {
  out += "<d:response><d:href>" + xmlEscape(h) + "</d:href><d:propstat><d:prop>";
  out += "<d:displayname>" + xmlEscape(name) + "</d:displayname>";
  out += dir ? "<d:resourcetype><d:collection/></d:resourcetype>" : "<d:resourcetype/>";
  if (!dir) {
    out += "<d:getcontentlength>" + std::to_string(size) + "</d:getcontentlength>";
    out += "<d:getcontenttype>" + std::string(mime(name)) + "</d:getcontenttype>";
    out += "<d:getetag>\"" + std::to_string(mtime) + "-" + std::to_string(size) + "\"</d:getetag>";
  }
  out += "<d:getlastmodified>" + httpDate(mtime) + "</d:getlastmodified>";
  out += "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
}

void propfind(WiFiClient& c, Request& r) {
  // The DAV root lists one collection, "vault"; everything below a base maps to the card
  if (r.base.empty() && r.path == "/") {
    std::string body = "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:multistatus xmlns:d=\"DAV:\">";
    propEntry(body, "/", "", true, 0, 0);
    if (r.depth != "0") propEntry(body, "/vault/", "vault", true, 0, 0);
    body += "</d:multistatus>";
    return status(c, 207, "Multi-Status", "Content-Type: application/xml; charset=utf-8\r\n", body);
  }
  if (!storage::isDir(r.path)) {
    int64_t size = storage::fileSize(r.path);
    if (size < 0) return status(c, 404, "Not Found");
    std::string body = "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:multistatus xmlns:d=\"DAV:\">";
    propEntry(body, href(r, r.path, false), storage::baseName(r.path + ".md"), false, size, storage::modifiedTime(r.path));
    body += "</d:multistatus>";
    return status(c, 207, "Multi-Status", "Content-Type: application/xml; charset=utf-8\r\n", body);
  }
  // Directory: stream the listing (no Content-Length; the connection closes at the end)
  const char* head = "HTTP/1.1 207 Multi-Status\r\nContent-Type: application/xml; charset=utf-8\r\n"
                     "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n"
                     "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:multistatus xmlns:d=\"DAV:\">";
  c.write((const uint8_t*)head, strlen(head));
  std::string out;
  propEntry(out, href(r, r.path, true), r.path == "/" ? (r.base.empty() ? "vault" : r.base) : storage::baseName(r.path + ".md"), true, 0,
            r.path == "/" ? 0 : storage::modifiedTime(r.path));
  if (r.depth != "0") {
    std::vector<std::string> dirs;
    auto files = storage::listRaw(r.path, &dirs);
    for (auto& d : dirs) {
      if (!allowed(d)) continue;
      propEntry(out, href(r, d, true), storage::baseName(d + ".md"), true, 0, storage::modifiedTime(d));
      if (out.size() > 2048) c.write((const uint8_t*)out.data(), out.size()), out.clear();
    }
    for (auto& f : files) {
      if (!allowed(f.path)) continue;
      propEntry(out, href(r, f.path, false), f.path.substr(f.path.rfind('/') + 1), false, f.size, f.mtime);
      if (out.size() > 2048) c.write((const uint8_t*)out.data(), out.size()), out.clear();
    }
  }
  out += "</d:multistatus>";
  c.write((const uint8_t*)out.data(), out.size());
}

void get(WiFiClient& c, Request& r, bool head) {
  if (storage::isDir(r.path)) return status(c, 405, "Method Not Allowed");
  int64_t size = storage::fileSize(r.path);
  if (size < 0) return status(c, 404, "Not Found");
  uint32_t mtime = storage::modifiedTime(r.path);
  std::string h = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(mime(r.path)) + "\r\nContent-Length: " +
                  std::to_string(size) + "\r\nLast-Modified: " + httpDate(mtime) + "\r\nETag: \"" +
                  std::to_string(mtime) + "-" + std::to_string(size) +
                  "\"\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
  c.write((const uint8_t*)h.data(), h.size());
  if (head) return;
  storage::streamFile(r.path, [&](const uint8_t* d, size_t n) { return c.write(d, n) == n; });
}

void put(WiFiClient& c, Request& r) {
  if (r.path == "/" || storage::isDir(r.path)) return status(c, 405, "Method Not Allowed");
  if (r.chunked || r.contentLength < 0) return status(c, 411, "Length Required");
  const bool existed = storage::fileSize(r.path) >= 0;
  if (r.expectContinue) c.print("HTTP/1.1 100 Continue\r\n\r\n");
  if (!storage::writeBegin(r.path)) return status(c, 409, "Conflict");
  static uint8_t buf[2048];
  long left = r.contentLength;
  uint32_t last = millis();
  bool ok = true;
  while (left > 0 && ok) {
    int avail = c.available();
    if (avail > 0) {
      int n = c.read(buf, std::min<long>(sizeof(buf), left));
      if (n > 0) {
        ok = storage::writeChunk(buf, n);
        left -= n;
        last = millis();
      }
    } else if (!c.connected() || millis() - last > 8000) {
      ok = false;
    } else {
      delay(1);
    }
  }
  if (!ok || !storage::writeFinish()) {
    storage::writeAbort();
    return status(c, 500, "Internal Server Error");
  }
  Serial.printf("[dav] PUT %s (%ld bytes)\n", r.path.c_str(), r.contentLength);
  syncToast(r.path);
  storage::indexAdd(r.path);
  app::externalChange(r.path);
  status(c, existed ? 204 : 201, existed ? "No Content" : "Created");
}

void del(WiFiClient& c, Request& r) {
  if (r.path == "/") return status(c, 403, "Forbidden");
  bool ok;
  if (storage::isDir(r.path)) {
    app::saveCurrent();
    ok = storage::removeFolder(r.path);
    if (ok) app::folderDeleted(r.path);
  } else {
    if (storage::fileSize(r.path) < 0) return status(c, 404, "Not Found");
    app::saveCurrent();
    ok = storage::remove(r.path);
    if (ok) app::noteDeleted(r.path);
  }
  Serial.printf("[dav] DELETE %s: %s\n", r.path.c_str(), ok ? "ok" : "failed");
  syncToast(r.path);
  status(c, ok ? 204 : 500, ok ? "No Content" : "Internal Server Error");
}

void mkcol(WiFiClient& c, Request& r) {
  if (r.contentLength > 0) return status(c, 415, "Unsupported Media Type");
  if (storage::exists(r.path) || r.path == "/") return status(c, 405, "Method Not Allowed");
  if (!storage::isDir(storage::parentDir(r.path))) return status(c, 409, "Conflict");
  bool ok = storage::mkdirs(r.path);
  if (ok) storage::rescan();
  status(c, ok ? 201 : 500, ok ? "Created" : "Internal Server Error");
}

void move(WiFiClient& c, Request& r) {
  // Destination is a full URL; keep its path part and map it the same way
  std::string d = r.destination;
  size_t scheme = d.find("://");
  if (scheme != std::string::npos) d = d.substr(d.find('/', scheme + 3) == std::string::npos ? d.size() : d.find('/', scheme + 3));
  Request to;
  mapPath(to, d);
  if (r.path == "/" || to.path == "/" || !allowed(to.path)) return status(c, 403, "Forbidden");
  bool existed = storage::exists(to.path);
  if (existed && r.overwrite == "F") return status(c, 412, "Precondition Failed");
  app::saveCurrent();
  if (existed) storage::isDir(to.path) ? storage::removeFolder(to.path) : storage::remove(to.path);
  bool wasDir = storage::isDir(r.path);
  bool ok = storage::rename(r.path, to.path);
  if (ok) wasDir ? app::folderPathChanged(r.path, to.path) : app::notePathChanged(r.path, to.path);
  Serial.printf("[dav] MOVE %s -> %s: %s\n", r.path.c_str(), to.path.c_str(), ok ? "ok" : "failed");
  status(c, ok ? (existed ? 204 : 201) : 500, ok ? (existed ? "No Content" : "Created") : "Internal Server Error");
}

void handle(WiFiClient& c) {
  Request r;
  std::string line;
  if (!readLine(c, line)) return;
  size_t a = line.find(' '), b = line.rfind(' ');
  if (a == std::string::npos || b <= a) return status(c, 400, "Bad Request");
  r.method = line.substr(0, a);
  r.uri = line.substr(a + 1, b - a - 1);
  while (readLine(c, line) && !line.empty()) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string k = line.substr(0, colon), v = line.substr(colon + 1);
    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
    for (auto& ch : k) ch = tolower(ch);
    if (k == "content-length") r.contentLength = atol(v.c_str());
    else if (k == "depth") r.depth = v;
    else if (k == "destination") r.destination = v;
    else if (k == "overwrite") r.overwrite = v;
    else if (k == "transfer-encoding") r.chunked = strcasestr(v.c_str(), "chunked") != nullptr;
    else if (k == "expect") r.expectContinue = strcasestr(v.c_str(), "100-continue") != nullptr;
  }
  mapPath(r, r.uri.substr(0, r.uri.find('?')));
  if (storage::state() != storage::State::Mounted) return status(c, 503, "Service Unavailable");
  if (!allowed(r.path)) return status(c, 403, "Forbidden");
  power::keepAwake();

  const std::string& m = r.method;
  if (m == "OPTIONS") {
    status(c, 200, "OK",
           "DAV: 1\r\nMS-Author-Via: DAV\r\nAllow: OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE\r\n"
           "Access-Control-Allow-Methods: OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE\r\n"
           "Access-Control-Allow-Headers: *\r\n");
  } else if (m == "PROPFIND") {
    // Request bodies (which properties) are ignored: we always return the same set
    for (long n = r.contentLength; n > 0 && c.connected(); n--) {
      uint32_t t0 = millis();
      while (!c.available() && millis() - t0 < 2000) delay(1);
      c.read();
    }
    propfind(c, r);
  } else if (m == "GET" || m == "HEAD") {
    get(c, r, m == "HEAD");
  } else if (m == "PUT") {
    put(c, r);
  } else if (m == "DELETE") {
    del(c, r);
  } else if (m == "MKCOL") {
    mkcol(c, r);
  } else if (m == "MOVE") {
    move(c, r);
  } else {
    status(c, 501, "Not Implemented");
  }
}

}  // namespace

void begin() {
  if (started_) return;
  server.begin();
  started_ = true;
  Serial.printf("[dav] WebDAV on port %d\n", kPort);
}

void loop() {
  if (!started_) return;
  WiFiClient c = server.available();
  if (!c) return;
  c.setTimeout(4);
  handle(c);
  c.flush();
  c.stop();
}

}  // namespace dav
