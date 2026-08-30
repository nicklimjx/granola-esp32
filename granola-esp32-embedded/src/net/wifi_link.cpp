#include "wifi_link.h"

#include <WiFi.h>

#include "../../include/app_config.h"

namespace {
// Long enough that a slow association (a busy AP can take well over 5 s) is
// never aborted half way by our own retry. WiFi.setAutoReconnect() handles the
// common case; this is the backstop.
constexpr uint32_t kRetryIntervalMs = 15000;
}

void WifiLink::begin(const char* ssid, const char* password) {
  ssid_ = ssid;
  password_ = password;

  WiFi.mode(WIFI_STA);
  // Modem sleep adds tens of milliseconds of latency to inbound frames, which
  // matters when the reaction window is 800 ms.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_, password_);
  lastAttemptMs_ = millis();

  log_i("connecting to Wi-Fi \"%s\"", ssid_);
}

void WifiLink::loop() {
  const bool connected = isConnected();

  if (connected != wasConnected_) {
    wasConnected_ = connected;
    if (connected) {
      log_i("Wi-Fi connected, ip %s", WiFi.localIP().toString().c_str());
    } else {
      log_w("Wi-Fi lost");
    }
  }

  if (connected) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastAttemptMs_ >= kRetryIntervalMs) {
    lastAttemptMs_ = now;
    WiFi.disconnect();
    WiFi.begin(ssid_, password_);
    log_i("retrying Wi-Fi");
  }
}

bool WifiLink::isConnected() const { return WiFi.status() == WL_CONNECTED; }

String WifiLink::ipAddress() const { return WiFi.localIP().toString(); }
