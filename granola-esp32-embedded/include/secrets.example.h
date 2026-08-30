#pragma once
//
// Copy this file to include/secrets.h (gitignored) and fill in your values.
//

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// The laptop hosting the WebSocket server. Use its LAN IP; mDNS names such as
// "laptop.local" are not resolved by this firmware.
#define WS_HOST "192.168.1.50"
#define WS_PORT 8080
#define WS_PATH "/bopit"
