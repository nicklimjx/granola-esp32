// Granola bop-it board firmware. Chromium owns the game and score over a
// direct Web Bluetooth connection; this board detects and times actions.

#include <Arduino.h>

#include "../include/app_config.h"
#include "game/game.h"
#include "input/input_manager.h"
#include "net/ble_link.h"
#include "ui/ui.h"

namespace {
BleLink bleLink;
InputManager input;
Ui ui;
Game game;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  log_i("%s firmware %s (board ID %s)", BOARD_NAME, FIRMWARE_VERSION, BOARD_ID);

  if (!input.begin()) log_w("one or more input devices missing; continuing with what came up");
  if (!ui.begin(input.touchAddress())) log_w("running headless; browser remains the scoreboard");
  ui.showStatus("BOP IT", "connecting...");

  bleLink.onConnection([](bool connected) { game.onConnectionChange(connected); });
  bleLink.onInstruction([](uint32_t session, uint8_t round, Action action, uint16_t timeoutMs) {
    game.onInstruction(session, round, action, timeoutMs);
  });
  bleLink.onStop([](uint32_t session, bool reset) { game.stop(session, reset); });
  game.begin(&bleLink, &input, &ui);

  BleLink::Config config;
  config.deviceName = "Granola Bop-It";
  config.boardId = BOARD_ID;
  bleLink.begin(config);
}

void loop() {
  bleLink.loop();
  input.loop();
  game.loop();
}
