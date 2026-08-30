#pragma once
//
// Game — the single-player round loop.
//
// The server picks the moves, sets the timeout and owns the score; the board
// detects actions and measures reaction time. One round looks like:
//
//   instruction         -> window opens, prompt on screen, clock starts
//   first action        -> action.detected sent, board stops accepting input
//   (or window expires) -> board stops accepting input, sends nothing; the
//                          server is the one that declares a timeout
//   round.result        -> verdict and score on screen
//
// Only the first action in a round counts, matching the server's rule. The
// board never decides success or failure itself; it renders what round.result
// says.
//
// Multiplayer needs no change here: each board runs its own instance and the
// board ID in board.ready keeps the two players apart.
//

#include <stdint.h>

#include "../input/input_manager.h"
#include "../net/protocol.h"
#include "../net/ws_link.h"
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

  void begin(WsLink* link, InputManager* input, Ui* ui);

  // Advances the clock and drains the input queue. Call every loop().
  void loop();

  // Wired to WsLink. Safe to call from the socket callback.
  void onInstruction(const char* roundId, Action action, uint32_t timeoutMs);
  void onRoundResult(const char* roundId, RoundOutcome outcome, int32_t score);
  void onConnectionChange(bool connected);

  // Fallback window used only when the server omits timeoutMs: 2000 ms for the
  // first 10 instructions, then one step shorter every 10, holding at 800 ms.
  // The server normally owns this; see docs/PROTOCOL.md.
  static uint32_t fallbackWindowMs(uint32_t instructionIndex);

  State state() const { return state_; }

 private:
  void closeWindow();
  void enterIdle();
  void renderIdle();

  WsLink* link_ = nullptr;
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

  // Last score the server told us. Displayed, never computed here.
  int32_t score_ = 0;
};
