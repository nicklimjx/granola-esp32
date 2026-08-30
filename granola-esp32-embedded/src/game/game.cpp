#include "game.h"

#include <Arduino.h>
#include <string.h>

#include "../../include/app_config.h"

namespace {
constexpr uint32_t kUiTickIntervalMs = 33;  // ~30 fps countdown redraw
constexpr size_t kWindowTiers = sizeof(cfg::kActionWindowsMs) / sizeof(cfg::kActionWindowsMs[0]);
}  // namespace

uint32_t Game::fallbackWindowMs(uint32_t instructionIndex) {
  size_t tier = instructionIndex / cfg::kRoundsPerTier;
  if (tier >= kWindowTiers) {
    tier = kWindowTiers - 1;
  }
  return cfg::kActionWindowsMs[tier];
}

void Game::begin(WsLink* link, InputManager* input, Ui* ui) {
  link_ = link;
  input_ = input;
  ui_ = ui;
  enterIdle();
}

void Game::onInstruction(const char* roundId, Action action, uint32_t timeoutMs) {
  // Stamp the clock first: the protocol defines elapsedMs from receipt of the
  // instruction, and everything below (including a ~15 ms full repaint) happens
  // after receipt.
  const uint32_t receivedMs = millis();

  if (action == Action::None || roundId == nullptr) {
    return;
  }

  windowMs_ = timeoutMs;
  if (windowMs_ == 0) {
    // The server owns the timeout ramp. Falling back keeps the board playable
    // rather than leaving a window that never closes.
    windowMs_ = fallbackWindowMs(instructionIndex_);
    log_w("instruction %s had no timeoutMs, falling back to %u ms", roundId, windowMs_);
  }
  ++instructionIndex_;

  strncpy(roundId_, roundId, sizeof(roundId_) - 1);
  roundId_[sizeof(roundId_) - 1] = '\0';
  expected_ = action;
  instructionRxMs_ = receivedMs;
  state_ = State::Awaiting;

  log_i("round %s: %s, timeout %u ms", roundId_, actionToWire(expected_), windowMs_);

  ui_->showPrompt(expected_, windowMs_, score_, roundId_);
  lastUiTickMs_ = millis();

  // Anything the player did before the prompt was on screen does not count.
  input_->flush();
}

void Game::onRoundResult(const char* roundId, RoundOutcome outcome, int32_t score) {
  if (roundId == nullptr || strcmp(roundId, roundId_) != 0) {
    // Mirrors the server's own rule about unknown or completed round IDs.
    log_w("ignoring round.result for round %s, current round is %s",
          roundId == nullptr ? "(null)" : roundId, roundId_);
    return;
  }

  score_ = score;
  log_i("round %s: %s, score %ld", roundId_, outcomeToWire(outcome), static_cast<long>(score_));

  ui_->showResult(outcome, score_);
  state_ = State::Feedback;
  phaseStartMs_ = millis();
}

void Game::onConnectionChange(bool connected) {
  if (!connected) {
    // The protocol says a disconnected board loses its active round and starts
    // idle after reconnecting, so there is nothing to preserve. The score is
    // kept on screen until the server sends a fresh one.
    enterIdle();
  } else {
    renderIdle();
  }
}

void Game::loop() {
  const uint32_t now = millis();

  switch (state_) {
    case State::Awaiting: {
      InputEvent event;
      if (input_->pop(event)) {
        // flush() dropped everything queued before the window opened, so this is
        // the first action of the round — the only one that counts.
        const uint32_t elapsedMs = event.timestampMs - instructionRxMs_;
        log_i("round %s: detected %s after %u ms", roundId_, actionToWire(event.action), elapsedMs);
        link_->sendActionDetected(roundId_, event.action, elapsedMs);
        closeWindow();
        return;
      }

      if (now - instructionRxMs_ >= windowMs_) {
        // Nothing to send: the server runs its own timeout and will tell us.
        log_i("round %s: window closed with no action", roundId_);
        ui_->showPending("TIME");
        closeWindow();
        return;
      }

      if (now - lastUiTickMs_ >= kUiTickIntervalMs) {
        lastUiTickMs_ = now;
        ui_->updateCountdown(windowMs_, now - instructionRxMs_);
      }
      break;
    }

    case State::AwaitingResult:
      // Don't hang if round.result never arrives; the next instruction would
      // recover anyway, but an unresponsive screen is worse than a reset.
      if (now - phaseStartMs_ >= cfg::kRoundResultTimeoutMs) {
        log_w("round %s: no round.result within %u ms, going idle", roundId_,
              cfg::kRoundResultTimeoutMs);
        enterIdle();
      }
      break;

    case State::Feedback:
      // Subtraction form throughout, so millis() rollover is a non-event.
      if (now - phaseStartMs_ >= cfg::kFeedbackHoldMs) {
        enterIdle();
      }
      break;

    case State::Idle:
    default: {
      // Drain input so a stray gesture cannot sit in the queue between rounds.
      InputEvent discarded;
      while (input_->pop(discarded)) {
      }
      break;
    }
  }
}

void Game::closeWindow() {
  state_ = State::AwaitingResult;
  phaseStartMs_ = millis();
}

void Game::enterIdle() {
  state_ = State::Idle;
  expected_ = Action::None;
  roundId_[0] = '\0';
  renderIdle();
}

void Game::renderIdle() {
  if (ui_ == nullptr) {
    return;
  }
  if (link_ != nullptr && link_->isConnected()) {
    ui_->showWaiting(score_);
  } else {
    ui_->showStatus("BOP IT", "connecting...");
  }
}
