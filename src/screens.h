#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app.h"
#include "storage.h"
#include "textfont.h"
#include "ui.h"

class BrowserScreen : public Screen {
 public:
  void open(const std::string& dir, int scroll);
  const std::string& dir() const { return dir_; }
  void draw() override;
  void onTap(int x, int y) override;
  void onDrag(int dy) override;
  void onKey(const input::Event& e) override;
  int scroll() const override { return scroll_; }

 private:
  void drawList();
  int maxScroll() const;
  void activate(int i);
  void newNote();
  std::string dir_ = "/";
  std::vector<storage::Entry> entries_;
  int scroll_ = 0;
  int sel_ = -1;  // keyboard selection
  ui::Rect action_;  // empty-state button
};

class ToolsScreen : public Screen {
 public:
  void draw() override;
  void onTap(int x, int y) override;
  void tick() override;

 private:
  void drawInfo();
  void format();
  ui::Rect btnFormat_, btnSample_, btnRemount_, btnCalibrate_, btnBluetooth_, btnWifi_;
  bool needFree_ = false;
  uint64_t free_ = 0;
  bool freeKnown_ = false;
};

class SwitcherScreen : public Screen {
 public:
  void open(app::SwitcherMode mode, const std::string& dir, const std::string& path);
  void draw() override;
  void onTap(int x, int y) override;
  void onKey(const input::Event& e) override;

 private:
  void refresh();
  void drawInput();
  void drawList();
  void commit(int index);
  std::string target() const;  // path the query would create / rename to

  app::SwitcherMode mode_ = app::SwitcherMode::Open;
  std::string dir_, path_, query_;
  size_t cursor_ = 0;
  std::vector<std::string> results_;
  bool offerCreate_ = false;
  int sel_ = 0;
};

class BluetoothScreen : public Screen {
 public:
  void draw() override;
  void onTap(int x, int y) override;
  void onKey(const input::Event& e) override;
  void tick() override;

 private:
  void drawBody();
  int lastState_ = -1;
  size_t lastFound_ = 0;
  std::vector<ui::Rect> rows_;
  ui::Rect btnScan_, btnForget_, btnPower_;
};

class WifiScreen : public Screen {
 public:
  void draw() override;
  void onTap(int x, int y) override;
  void onKey(const input::Event& e) override;
  void tick() override;

 private:
  void drawBody();
  void setNetwork();
  int lastState_ = -1;
  ui::Rect btnNetwork_, btnMode_, btnForget_;
};

// Single-line text input (used for WiFi credentials); needs a keyboard.
class PromptScreen : public Screen {
 public:
  void open(const std::string& title, const std::string& hint, const std::string& initial, bool secret,
            std::function<void(const std::string&)> done);
  void draw() override;
  void onTap(int x, int y) override;
  void onKey(const input::Event& e) override;

 private:
  void drawInput();
  std::string title_, hint_, text_;
  size_t cursor_ = 0;
  bool secret_ = false, reveal_ = false;
  std::function<void(const std::string&)> done_;
};
