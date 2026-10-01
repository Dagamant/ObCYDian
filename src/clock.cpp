#include "clock.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "radio.h"
#include "webserver.h"

namespace wallclock {

namespace {

RTC_DATA_ATTR bool rtcValid = false;  // survives deep sleep, like the time itself
bool valid_ = false;
int offset_ = 0;  // minutes east of UTC
bool ntpStarted_ = false;
uint32_t lastSave_ = 0;

void saveLastKnown() {
  Preferences p;
  p.begin("clock", false);
  p.putLong64("last", (int64_t)time(nullptr));
  p.putInt("offset", offset_);
  p.end();
}

}  // namespace

void begin() {
  Preferences p;
  p.begin("clock", false);
  offset_ = p.getInt("offset", 0);
  int64_t last = p.getLong64("last", 0);
  p.end();
  valid_ = rtcValid && time(nullptr) > 1700000000;
  if (!valid_ && last > 1700000000 && time(nullptr) < last) {
    // Power was lost: restore the last known time so dates are at least close
    timeval tv{(time_t)last, 0};
    settimeofday(&tv, nullptr);
  }
  Serial.printf("[clock] %s %s\n", format("YYYY-MM-DD HH:mm").c_str(), valid_ ? "" : "(not set)");
}

void loop() {
  // WiFi: ask NTP once connected
  if (!ntpStarted_ && radio::mode() == radio::Mode::Wifi && web::state() == web::State::Connected) {
    ntpStarted_ = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com");
  }
  if (ntpStarted_ && !valid_ && time(nullptr) > 1700000000 && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    valid_ = rtcValid = true;
    Serial.printf("[clock] NTP time %s\n", format("YYYY-MM-DD HH:mm").c_str());
    saveLastKnown();
  }
  if (valid_ && millis() - lastSave_ > 15 * 60 * 1000UL) {
    lastSave_ = millis();
    saveLastKnown();
  }
}

bool valid() { return valid_; }

time_t now() { return time(nullptr) + offset_ * 60; }

void set(time_t utc, int offsetMinutes) {
  timeval tv{utc, 0};
  settimeofday(&tv, nullptr);
  offset_ = offsetMinutes;
  valid_ = rtcValid = true;
  saveLastKnown();
  Serial.printf("[clock] set to %s (UTC%+d min)\n", format("YYYY-MM-DD HH:mm").c_str(), offset_);
}

void setOffset(int offsetMinutes) {
  offset_ = offsetMinutes;
  saveLastKnown();
}

int offsetMinutes() { return offset_; }

bool setDate(const std::string& ymd) {
  int y, m, d;
  if (sscanf(ymd.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 2020 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  time_t local = now();
  tm t;
  gmtime_r(&local, &t);
  if (!valid_) t.tm_hour = 12, t.tm_min = 0, t.tm_sec = 0;  // unknown time of day: use midday
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  // tm is local wall time here; convert back to UTC
  time_t asUtc = mktime(&t);  // TZ is UTC on this device, so mktime == timegm
  set(asUtc - offset_ * 60, offset_);
  return true;
}

std::string format(const std::string& pat, time_t t) {
  tm tm_;
  gmtime_r(&t, &tm_);  // `t` is already local time
  static const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                  "July",    "August",   "September", "October", "November", "December"};
  static const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  std::string out;
  char buf[16];
  size_t i = 0;
  auto take = [&](const char* tok) {
    size_t n = strlen(tok);
    if (pat.compare(i, n, tok) != 0) return false;
    i += n;
    return true;
  };
  while (i < pat.size()) {
    if (pat[i] == '[') {  // [literal text]
      size_t e = pat.find(']', i);
      if (e != std::string::npos) {
        out += pat.substr(i + 1, e - i - 1);
        i = e + 1;
        continue;
      }
    }
    if (take("YYYY")) snprintf(buf, sizeof(buf), "%04d", tm_.tm_year + 1900), out += buf;
    else if (take("YY")) snprintf(buf, sizeof(buf), "%02d", tm_.tm_year % 100), out += buf;
    else if (take("MMMM")) out += kMonths[tm_.tm_mon];
    else if (take("MMM")) out += std::string(kMonths[tm_.tm_mon]).substr(0, 3);
    else if (take("MM")) snprintf(buf, sizeof(buf), "%02d", tm_.tm_mon + 1), out += buf;
    else if (take("M")) out += std::to_string(tm_.tm_mon + 1);
    else if (take("DD")) snprintf(buf, sizeof(buf), "%02d", tm_.tm_mday), out += buf;
    else if (take("D")) out += std::to_string(tm_.tm_mday);
    else if (take("dddd")) out += kDays[tm_.tm_wday];
    else if (take("ddd")) out += std::string(kDays[tm_.tm_wday]).substr(0, 3);
    else if (take("HH")) snprintf(buf, sizeof(buf), "%02d", tm_.tm_hour), out += buf;
    else if (take("H")) out += std::to_string(tm_.tm_hour);
    else if (take("hh")) snprintf(buf, sizeof(buf), "%02d", (tm_.tm_hour + 11) % 12 + 1), out += buf;
    else if (take("h")) out += std::to_string((tm_.tm_hour + 11) % 12 + 1);
    else if (take("mm")) snprintf(buf, sizeof(buf), "%02d", tm_.tm_min), out += buf;
    else if (take("ss")) snprintf(buf, sizeof(buf), "%02d", tm_.tm_sec), out += buf;
    else if (take("A")) out += tm_.tm_hour < 12 ? "AM" : "PM";
    else out += pat[i++];
  }
  return out;
}

}  // namespace wallclock
