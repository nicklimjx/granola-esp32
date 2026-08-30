//
// Granola bop-it — board firmware.
//
// Single-player mode for the Waveshare ESP32-S3-Touch-AMOLED-1.8 (board ID 1).
// The laptop hosts the WebSocket server, generates the moves and keeps score;
// this board detects the four actions, times each round, and reports outcomes.
// See docs/PROTOCOL.md for the message contract.
//

#include <Arduino.h>

#include "../include/app_config.h"
#include "game/game.h"
#include "input/input_manager.h"
#include "net/wifi_link.h"
#include "net/ws_link.h"
#include "ui/ui.h"

namespace {

WifiLink wifi;
WsLink wsLink;
InputManager input;
Ui ui;
Game game;

bool wsStarted = false;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);  // let the USB CDC port come up before the first log line
  log_i("%s firmware %s (board ID %s)", BOARD_NAME, FIRMWARE_VERSION, BOARD_ID);

  // Inputs first: the touch controller's I2C address tells the UI which display
  // controller this board revision has.
  if (!input.begin()) {
    log_w("one or more input devices missing; continuing with what came up");
  }

  if (!ui.begin(input.touchAddress())) {
    log_w("running headless; the laptop GUI is still the scoreboard");
  }
  ui.showStatus("BOP IT", "connecting...");

  wsLink.onConnection([](bool connected) { game.onConnectionChange(connected); });
  wsLink.onInstruction([](const char* roundId, Action action, uint32_t timeoutMs) {
    game.onInstruction(roundId, action, timeoutMs);
  });
  wsLink.onRoundResult([](const char* roundId, RoundOutcome outcome, int32_t score) {
    game.onRoundResult(roundId, outcome, score);
  });

  game.begin(&wsLink, &input, &ui);

  wifi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  wifi.loop();

  // WebSocketsClient needs a working IP stack before begin(), so the socket is
  // started on the first successful association and left to its own reconnect
  // logic from then on.
  if (!wsStarted && wifi.isConnected()) {
    WsLink::Config config;
    config.host = WS_HOST;
    config.port = WS_PORT;
    config.path = WS_PATH;
    config.boardId = BOARD_ID;
    config.reconnectIntervalMs = cfg::kWsReconnectIntervalMs;
    wsLink.begin(config);
    wsStarted = true;
  }

  if (wsStarted) {
    wsLink.loop();
  }

  input.loop();
  game.loop();
}
