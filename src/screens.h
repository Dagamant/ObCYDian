#pragma once

#include <string>
#include <vector>

#include "app.h"
#include "markdown.h"
#include "storage.h"
#include "ui.h"

class BrowserScreen : public Screen {
 public:
  void open(const std::string& dir, int scroll);
  const std::string& dir() const { return dir_; }
  void draw() override;
  void onTap(int x, int y) override;
  void onDrag(int dy) override;
  int scroll() const override { return scroll_; }

 private:
  void drawList();
  int maxScroll() const;
  std::string dir_ = "/";
  std::vector<storage::Entry> entries_;
  int scroll_ = 0;
  ui::Rect action_;  // empty-state button
};

class ViewerScreen : public Screen {
 public:
  void open(const std::string& path, int scroll);
  const std::string& path() const { return path_; }
  void draw() override;
  void onTap(int x, int y) override;
  void onDrag(int dy) override;
  int scroll() const override { return scroll_; }

 private:
  void drawContent();
  int maxScroll() const;
  std::string path_;
  md::Doc doc_;
  bool loaded_ = false;
  int scroll_ = 0;
};

class ToolsScreen : public Screen {
 public:
  void draw() override;
  void onTap(int x, int y) override;
  void tick() override;

 private:
  void drawInfo();
  void format();
  ui::Rect btnFormat_, btnSample_, btnRemount_, btnCalibrate_;
  bool needFree_ = false;
  uint64_t free_ = 0;
  bool freeKnown_ = false;
};
