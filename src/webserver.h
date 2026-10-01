#pragma once
// WiFi + HTTP server for the vault (radio mode "WiFi"). Joins the saved network; if there is
// none or it can't connect, opens its own access point with a setup page (captive portal).

#include <string>

namespace web {

enum class State { Off, Connecting, Connected, AccessPoint };

void begin();  // starts WiFi if the radio mode is WiFi
void loop();   // call from the main loop

State state();
const char* stateText();
std::string url();          // e.g. http://192.168.1.23/
std::string ip();
std::string savedSsid();
std::string apSsid();
std::string apPassword();
bool hasNetwork();

// Saves credentials; reconnects right away when in WiFi mode.
void setNetwork(const std::string& ssid, const std::string& pass);
void forgetNetwork();

}  // namespace web
