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
  void folderMenu();
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
  ui::Rect btnFormat_, btnSample_, btnRemount_, btnPower_, btnBluetooth_, btnWifi_;
  bool needFree_ = false;
  uint64_t free_ = 0;
  bool freeKnown_ = false;
};

class SwitcherScreen : public Screen {
 public:
  void open(app::SwitcherMode mode, const std::string& dir, const std::string& path);
  bool acceptsText() const override { return true; }
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
  bool acceptsText() const override { return true; }
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

class PowerScreen : public Screen {
 public:
  void draw() override;
  void onTap(int x, int y) override;
  void onKey(const input::Event& e) override;

 private:
  void drawRows();
  ui::Rect btnDim_, btnBright_, btnScreen_, btnSleep_, btnOffNow_, btnSleepNow_, btnCalibrate_;
};

// Full-text search across all notes.
class SearchScreen : public Screen {
 public:
  void draw() override;
  void onTap(int x, int y) override;
  void onDrag(int dy) override;
  void onKey(const input::Event& e) override;
  void tick() override;
  bool acceptsText() const override { return true; }

 private:
  void drawInput();
  void drawResults();
  void run();
  int rowsVisible() const;
  std::string query_, ran_;
  size_t cursor_ = 0;
  std::vector<storage::Hit> hits_;
  int sel_ = 0, first_ = 0;
  uint32_t dueAt_ = 0;
};

// One row of a PickerScreen list
struct PickItem {
  std::string label;
  std::string detail;  // small, right-aligned (ASCII)
  std::string sub;     // optional second line
  int indent = 0;      // pixels
  int check = -1;      // -1 none, 0 open checkbox, 1 ticked
};

// Everything a picker shows; kept per history entry so lists can be nested.
struct PickerData {
  std::string title, empty;
  std::vector<PickItem> items;
  bool filterable = false;
  bool closeOnPick = false;  // menus/palette: leave before running the action
  std::function<void(int)> onPick;
  std::function<int(int)> onToggle;  // checkbox tapped: returns the new state
};

// Generic list: command palette, outline, backlinks, tags, tasks, recent, bookmarks...
class PickerScreen : public Screen {
 public:
  void load(PickerData* data);
  void draw() override;
  void onTap(int x, int y) override;
  void onDrag(int dy) override;
  void onKey(const input::Event& e) override;
  bool acceptsText() const override { return d_ && d_->filterable; }

 private:
  void refilter();
  void drawInput();
  void drawList();
  int listY() const;
  int rowH() const;
  int rows() const;
  void pick(int visibleIndex);
  PickerData* d_ = nullptr;
  std::string query_;
  std::vector<int> shown_;  // indices into d_->items
  int sel_ = 0, first_ = 0, dragAcc_ = 0;
};
