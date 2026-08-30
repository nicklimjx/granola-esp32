#pragma once
//
// Game — the single-player round loop.
//
// The browser picks the moves, sets the timeout and owns the score; the board
// detects actions and measures reaction time. One round looks like:
//
//   instruction         -> window opens, prompt on screen, clock starts
//   first action        -> action.detected sent, board stops accepting input
//   (or window expires) -> board stops accepting input, sends nothing; the
//                          browser is the one that declares a timeout
//   round.result        -> verdict and score on screen
//
// Only the first action in a round counts, matching the browser's rule. The
// board never decides success or failure itself; it renders what round.result
// says.
//

#include <stdint.h>

#include "../input/input_manager.h"
#include "../net/protocol.h"
#include "../net/ble_link.h"
#include "../ui/ui.h"
#include "actions.h"

class Game {
 public:
  enum class State : uint8_t {
    Idle,            // no round in flight (also: link down)
    Awaiting,        // window open, watching for the first action
    AwaitingResult,  // round is over for the player, waiting on round.result
    Feedback,        // showing the verdict for the round just finished
  };

  void begin(BleLink* link, InputManager* input, Ui* ui);

  // Advances the clock and drains the input queue. Call every loop().
  void loop();

  // Wired to BleLink; handlers run from BleLink::loop(), never BLE callbacks.
  void onInstruction(const char* roundId, Action action, uint32_t timeoutMs);
  void onRoundResult(const char* roundId, RoundOutcome outcome, int32_t score);
  void stop(bool reset);
  void onConnectionChange(bool connected);

  // Fallback window used only when the browser omits timeoutMs: 4000 ms for the
  // first 10 instructions, then one step shorter every 10, holding at 1600 ms.
  // The browser normally owns this; see docs/PROTOCOL.md.
  static uint32_t fallbackWindowMs(uint32_t instructionIndex);

  State state() const { return state_; }

 private:
  void closeWindow();
  void enterIdle();
  void renderIdle();

  BleLink* link_ = nullptr;
  InputManager* input_ = nullptr;
  Ui* ui_ = nullptr;

  State state_ = State::Idle;

  char roundId_[protocol::kRoundIdMaxLen + 1] = {0};
  Action expected_ = Action::None;
  uint32_t windowMs_ = 0;
  // Receipt of the instruction. The protocol defines elapsedMs relative to this
  // instant, so it is also what the local window is measured from.
  uint32_t instructionRxMs_ = 0;
  uint32_t phaseStartMs_ = 0;
  uint32_t lastUiTickMs_ = 0;

  // Counts instructions since boot, only to drive fallbackWindowMs().
  uint32_t instructionIndex_ = 0;

  // Last score the browser told us. Displayed, never computed here.
  int32_t score_ = 0;
};
