#pragma once
// Note screen with two modes, like Obsidian:
//  - editing ("live preview"): formatting is rendered as you type and the markdown syntax
//    is only revealed on the line holding the cursor;
//  - reading: everything rendered, no cursor; taps follow links and toggle checkboxes.
// Only the lines on screen are ever laid out, so large notes need little memory.

#include <string>
#include <vector>

#include "app.h"
#include "display.h"

class EditorScreen : public Screen {
 public:
  // Loads `path`; `scroll` positions the view (carried over when switching modes).
  bool open(const std::string& path, int scroll, bool reading);
  const std::string& path() const { return path_; }
  bool save();

  void draw() override;
  void onTap(int x, int y) override;
  void onDrag(int dy) override;
  void onKey(const input::Event& e) override;
  void onLeave() override;
  void tick() override;
  int scroll() const override { return scroll_; }

  // Layout structures (public so the implementation's helpers can use them)
  struct Seg {
    uint32_t a, b;   // byte range within the line
    uint16_t style;  // emphasis / link / code bits
    uint8_t kind;    // text, syntax marker, bullet, checkbox, rule
  };
  struct Row {
    uint32_t a, b;  // byte range within the line
    int16_t top, asc, h;
  };
  struct Layout {
    int line = -1;
    bool revealed = false;
    uint8_t block = 0, heading = 0, quote = 0;
    int16_t height = 0;
    std::vector<Seg> segs;
    std::vector<int16_t> x;   // per byte (and one past the end): glyph x position
    std::vector<uint8_t> row; // per byte: row index
    std::vector<Row> rows;
    std::vector<int16_t> rowEnd;  // x just past the last glyph of each row
  };

 private:
  struct Line {
    uint32_t start;
    int16_t height;
    uint8_t stateIn;
    bool dirty;
  };
  struct Op {
    uint32_t pos;
    std::string removed, inserted;
    uint32_t cursorBefore, cursorAfter;
    uint32_t time;
  };

  // Text model
  uint32_t lineLen(int i) const;
  int lineOf(uint32_t pos) const;
  void rebuildLines();
  void replace(uint32_t a, uint32_t b, const std::string& s, bool record = true);
  bool relayout();  // returns true if any line height changed
  uint8_t stateOut(uint8_t stateIn, int line) const;
  bool resolved(const std::string& target);

  // Layout & drawing
  void layoutLine(int i, bool revealed, Layout& out);
  void drawContent();
  void drawRegion(int y, int h);  // content-relative strip
  void drawLineOnly(int line);
  void layoutVisible();
  int lineAtY(int32_t y) const;
  void drawLine(LGFX_Sprite& s, const Layout& L, int y0);
  void drawStatus();
  void drawPopup(LGFX_Sprite& s, int bandTop);
  int viewH() const;

  // Cursor & editing helpers
  void setCursor(uint32_t pos, bool extend);
  bool ensureCursorVisible();
  uint32_t prevChar(uint32_t p) const;
  uint32_t nextChar(uint32_t p) const;
  uint32_t wordLeft(uint32_t p) const;
  uint32_t wordRight(uint32_t p) const;
  void moveVertical(int dir, bool extend);
  uint32_t hitTest(int line, int x, int y);
  static uint32_t hitTestRow(const Layout& L, int r, int x);
  bool hasSelection() const { return anchor_ != cursor_; }
  void deleteSelection();
  void insertText(const std::string& s);
  void newline(bool plain);
  void indentLines(bool outdent);
  void wrapSelection(const char* marker);
  void toggleCheckbox(int line);
  void followLinkAtCursor();
  // Link under a tap on a rendered (non-cursor) line; returns false if none.
  bool linkAtTap(int line, int x, int ry, uint32_t* pos);
  bool linkTargetAt(uint32_t pos, std::string& target, bool& external);
  void followLink(const std::string& target, bool external);
  void readingKey(const input::Event& e);
  void scrollTo(int s);
  void drawTitle();
  bool revealedLine(int i) const { return !reading_ && i == cursorLine_; }
  void undo(bool redo);
  void updatePopup();
  void acceptPopup();
  void afterChange(int prevCursorLine, int editedLine, bool structural);

  std::string path_, text_;
  std::vector<Line> lines_;
  std::vector<int32_t> tops_;
  int32_t totalH_ = 0;
  uint32_t cursor_ = 0, anchor_ = 0;
  int cursorLine_ = 0;
  int goalX_ = -1;
  int scroll_ = 0;
  bool dirty_ = false, readOnly_ = false, reading_ = false;
  uint32_t words_ = 0;
  std::string loadError_;
  uint32_t lastEdit_ = 0;
  std::vector<Op> undo_, redo_;
  std::string clipboard_;
  std::vector<Layout> visible_;

  // [[link autocomplete
  bool popup_ = false;
  uint32_t popupStart_ = 0;
  std::vector<std::string> popupItems_;
  int popupSel_ = 0;
  int popupX_ = 0, popupY_ = 0, popupW_ = 0, popupH_ = 0;  // content coordinates
  void placePopup();
  bool titleDirty_ = false;

  // resolved-link cache
  uint32_t linkGen_ = 0;
  std::vector<std::pair<std::string, bool>> linkCache_;
};
