#include "game.h"

#include <Arduino.h>

namespace {
constexpr uint32_t kUiTickIntervalMs = 33;

uint16_t clampElapsed(uint32_t elapsedMs) {
  return elapsedMs > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(elapsedMs);
}
}  // namespace

void Game::begin(BleLink* link, InputManager* input, Ui* ui) {
  link_ = link;
  input_ = input;
  ui_ = ui;
  enterIdle();
}

void Game::onInstruction(uint32_t session, uint8_t round, Action action, uint16_t timeoutMs) {
  const uint32_t receivedMs = millis();
  if (action == Action::None || round < 1 || round > 60 || timeoutMs == 0) return;
  const bool startsSession = !hasSession_ && state_ == State::Idle && round == 1;
  const bool continuesSession = hasSession_ && state_ == State::Feedback &&
                                session == session_ && round_ < 60 && round == round_ + 1;
  if (!startsSession && !continuesSession) {
    log_w("ignoring out-of-sequence instruction session %lu round %u",
          static_cast<unsigned long>(session), round);
    return;
  }

  if (reportPending_) {
    log_w("superseding unsent round event for session %lu round %u",
          static_cast<unsigned long>(session_), round_);
    reportPending_ = false;
  }
  hasSession_ = true;
  session_ = session;
  round_ = round;
  expected_ = action;
  windowMs_ = timeoutMs;
  instructionRxMs_ = receivedMs;
  state_ = State::Awaiting;

  log_i("session %lu round %u: %s, timeout %u ms",
        static_cast<unsigned long>(session_), round_, actionToWire(expected_), windowMs_);
  ui_->showPrompt(expected_, windowMs_, round_);
  lastUiTickMs_ = millis();

  // Gestures completed before the prompt became visible cannot count.
  input_->flush();
}

void Game::stop(uint32_t session, bool reset) {
  if (!hasSession_ || session != session_) {
    log_w("ignoring stop for stale session %lu", static_cast<unsigned long>(session));
    return;
  }
  log_i("session %lu stopped%s", static_cast<unsigned long>(session), reset ? " with reset" : "");
  input_->flush();
  reportPending_ = false;
  hasSession_ = false;
  session_ = 0;
  round_ = 0;
  enterIdle();
}

void Game::onConnectionChange(bool connected) {
  if (!connected) {
    hasSession_ = false;
    session_ = 0;
    round_ = 0;
    reportPending_ = false;
    enterIdle();
  } else {
    renderIdle();
  }
}

void Game::loop() {
  tryReportRound();
  const uint32_t now = millis();

  if (state_ == State::Awaiting) {
    InputEvent event;
    if (input_->pop(event)) {
      const uint32_t elapsedMs = event.timestampMs - instructionRxMs_;
      if (elapsedMs <= windowMs_) {
        const LocalVerdict verdict = event.action == expected_
                                         ? LocalVerdict::Success
                                         : LocalVerdict::WrongAction;
        finishRound(event.action, elapsedMs, verdict);
      } else {
        // Input can be queued before loop() runs. A timestamp after the exact
        // deadline is still a timeout, not a late action.
        finishRound(Action::None, elapsedMs, LocalVerdict::Timeout);
      }
      return;
    }

    // elapsed == timeout is still live; timeout starts strictly after it.
    if (now - instructionRxMs_ > windowMs_) {
      finishRound(Action::None, windowMs_, LocalVerdict::Timeout);
      return;
    }

    if (now - lastUiTickMs_ >= kUiTickIntervalMs) {
      lastUiTickMs_ = now;
      ui_->updateCountdown(windowMs_, now - instructionRxMs_);
    }
    return;
  }

  // Drain stray gestures while idle or while feedback remains on screen.
  InputEvent discarded;
  while (input_->pop(discarded)) {
  }
}

void Game::finishRound(Action detected, uint32_t elapsedMs, LocalVerdict verdict) {
  reportedAction_ = detected;
  reportedElapsedMs_ = clampElapsed(elapsedMs);
  reportPending_ = true;
  state_ = State::Feedback;

  log_i("session %lu round %u ended: %s after %u ms",
        static_cast<unsigned long>(session_), round_,
        detected == Action::None ? "timeout" : actionToWire(detected), reportedElapsedMs_);
  ui_->showResult(verdict);
  tryReportRound();
}

void Game::tryReportRound() {
  if (!reportPending_ || link_ == nullptr) return;
  if (link_->sendRoundEnded(session_, round_, reportedAction_, reportedElapsedMs_)) {
    reportPending_ = false;
  }
}

void Game::enterIdle() {
  state_ = State::Idle;
  expected_ = Action::None;
  renderIdle();
}

void Game::renderIdle() {
  if (ui_ == nullptr) return;
  if (link_ != nullptr && link_->isConnected()) ui_->showWaiting();
  else ui_->showStatus("BOP IT", "connecting...");
}
