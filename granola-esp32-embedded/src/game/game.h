#pragma once
// Board-local round execution. The browser owns score and sequencing; the board
// owns timing, immediate verdict display, and one eventual round-ended event.

#include <stdint.h>

#include "../input/input_manager.h"
#include "../net/ble_link.h"
#include "../ui/ui.h"
#include "actions.h"

class Game {
 public:
  enum class State : uint8_t { Idle, Awaiting, Feedback };

  void begin(BleLink* link, InputManager* input, Ui* ui);
  void loop();
  void onInstruction(uint32_t session, uint8_t round, Action action, uint16_t timeoutMs);
  void stop(uint32_t session, bool reset);
  void onConnectionChange(bool connected);

  State state() const { return state_; }

 private:
  void finishRound(Action detected, uint32_t elapsedMs, LocalVerdict verdict);
  void tryReportRound();
  void enterIdle();
  void renderIdle();

  BleLink* link_ = nullptr;
  InputManager* input_ = nullptr;
  Ui* ui_ = nullptr;
  State state_ = State::Idle;

  bool hasSession_ = false;
  uint32_t session_ = 0;
  uint8_t round_ = 0;
  Action expected_ = Action::None;
  uint16_t windowMs_ = 0;
  uint32_t instructionRxMs_ = 0;
  uint32_t lastUiTickMs_ = 0;

  bool reportPending_ = false;
  Action reportedAction_ = Action::None;
  uint16_t reportedElapsedMs_ = 0;
};
