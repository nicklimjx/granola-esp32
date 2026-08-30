#pragma once
//
// Thin Wi-Fi lifecycle helper: connect at boot, notice drops, retry. Kept
// separate from WsLink so the WebSocket layer only deals with the socket.
//

#include <Arduino.h>
#include <stdint.h>

class WifiLink {
 public:
  void begin(const char* ssid, const char* password);

  // Non-blocking; call every loop(). Re-issues WiFi.begin() after a drop.
  void loop();

  bool isConnected() const;
  String ipAddress() const;

 private:
  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  uint32_t lastAttemptMs_ = 0;
  bool wasConnected_ = false;
};
