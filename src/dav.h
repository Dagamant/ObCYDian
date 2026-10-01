#pragma once
// WebDAV server (port 8080) so Obsidian can sync with the device using the Remotely Save
// plugin. Runs in WiFi mode alongside the web app.
//
// Remotely Save keeps a vault under /<vault name>/ on the server, so the first path segment
// is mapped onto the root of the card: /MyVault/Daily/x.md is /Daily/x.md here. Generic
// WebDAV clients see the vault as /vault/.

namespace dav {

constexpr int kPort = 8080;

void begin();
void loop();

}  // namespace dav
