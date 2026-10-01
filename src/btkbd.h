#pragma once
// Bluetooth LE keyboard (HID over GATT) using NimBLE. A background task scans, pairs and
// reconnects; reports are queued and decoded into input events on the main loop by poll().

#include <stdint.h>

#include <string>
#include <vector>

#include "input.h"

namespace btkbd {

enum class State {
  Off,           // Bluetooth disabled
  NoKeyboard,    // enabled, nothing paired
  Scanning,      // looking for keyboards to pair
  ScanDone,      // scan results ready, waiting for a choice
  Connecting,
  Pairing,       // connected, securing: the passkey may need typing on the keyboard
  Connected,
  Reconnecting,  // paired keyboard not in range / asleep
};

struct Found {
  std::string name, addr;
  uint8_t addrType;
  int rssi;
};

// Starts the Bluetooth stack and task if Bluetooth is enabled in settings.
void begin();
bool enabled();
// Persists the setting; takes effect after restart (radios are switched at boot).
void setEnabled(bool on);

State state();
const char* stateText();
std::string keyboardName();
uint32_t passkey();  // shown during Pairing

void startScan();
std::vector<Found> scanResults();
void pair(const Found& f);
void cancelPairing();
void forget();

// Converts queued HID reports to key events (with auto-repeat). Call from the main loop.
bool poll(input::Event& e);

void setRawLogging(bool on);

}  // namespace btkbd
