#pragma once
// Screen navigation, modal dialogs/menus and toasts.

#include <functional>
#include <string>
#include <vector>

#include "input.h"

struct PickerData;

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void draw() = 0;
  virtual void onTap(int x, int y) {}
  virtual void onDrag(int dy) {}
  virtual void onDragEnd() {}
  virtual void onKey(const input::Event& e) {}
  // Called before navigating away (the editor saves here).
  virtual void onLeave() {}
  virtual void tick() {}
  virtual int scroll() const { return 0; }
  // Text cursor to restore when coming back to this screen (-1: none)
  virtual int cursorPos() const { return -1; }
  virtual void setCursorPos(int pos) {}
  // Screens that take typed text get the on-screen keyboard.
  virtual bool acceptsText() const { return false; }
};

namespace app {

enum class SwitcherMode { Open, New, Rename };

void begin();
void loop();
void handle(const input::Event& e);

void openFolder(const std::string& dir, int scroll = 0);
void openNote(const std::string& path, int scroll = 0);  // reading view
// Switches the current note between reading and editing in place, or opens `path` anew.
void editNote(const std::string& path);
void viewNote(const std::string& path);
void openTools();
void openBluetooth();
void openWifi();
void openPower();
void openSearch();
// Opens a note in reading view scrolled to `line` (0-based).
void openNoteAt(const std::string& path, int line);
// Text prompt screen; `done` runs after the prompt closes with the entered text.
void prompt(const std::string& title, const std::string& hint, const std::string& initial, bool secret,
            std::function<void(const std::string&)> done);
// True when a keyboard (Bluetooth or the serial console) can type.
bool keyboardAvailable();
// Quick switcher / name prompt. `dir` is where new notes go; `path` is the note to rename.
void openSwitcher(SwitcherMode mode, const std::string& dir, const std::string& path = "");
// Closes the switcher and opens `path` (in the editor if `edit`).
void finishSwitcher(const std::string& path, bool edit);
void noteMenu(const std::string& path);
void back();
void home();

// Keep navigation history consistent after a note is renamed or deleted.
void notePathChanged(const std::string& from, const std::string& to);
void noteDeleted(const std::string& path);
void folderPathChanged(const std::string& from, const std::string& to);
void folderDeleted(const std::string& dir);
// A note was created or changed outside the device UI (e.g. from the web page): refresh
// whatever is on screen if it shows that note or its folder.
void externalChange(const std::string& path);

// The note on screen ("" if none); *editing tells whether it's in edit mode.
std::string currentNote(bool* editing);
// Saves anything unsaved before power-down.
void prepareSleep();
// Saves the open note (before files are renamed or deleted from elsewhere).
void saveCurrent();

void redraw();
// Re-opens the screen on top of the history (e.g. after its path changed).
void reload();
// Battery/radio quick menu (switch radio, screen off, sleep).
void quickMenu();

// --- Lists and commands
// Shows a list screen (pushed onto the history, so lists can nest).
void pick(PickerData data);
void pickerChose(int index);  // called by the list screen
// Command palette for the current screen (Ctrl+P, or the top bar's menu without a filter).
void commandPalette(bool filter);

// What the current screen shows, for context-aware commands
struct Context {
  enum Where { Folder, Note, Other } where;
  std::string path;  // folder or note
  bool editing;
};
Context context();
// Operations on the open note (no-ops if none)
void insertIntoNote(const std::string& text);
void revealLine(int line);
std::string noteText();  // current editor text (or "")

// Folder/note actions shared by menus and the palette
void newNoteIn(const std::string& dir);
void newFolderIn(const std::string& dir);
void renameFolderPrompt(const std::string& dir);
void deleteFolderConfirm(const std::string& dir);
void deleteNoteConfirm(const std::string& path);
void openDailyNote(int offsetDays);
// Shows/hides the on-screen keyboard and relays out the current screen.
void toggleKeyboard();
void toast(const std::string& msg, uint32_t ms = 2000);
void confirm(const std::string& title, const std::string& message, const char* okLabel,
             uint16_t okColor, std::function<void()> onOk);
// Vertical list of choices; `onPick` gets the chosen index.
void menu(const std::string& title, const std::vector<std::string>& items,
          std::function<void(int)> onPick);

}  // namespace app
