#include "button_input.h"

#include <Arduino.h>

#include "../../include/app_config.h"

void ButtonInput::begin(uint8_t pin, bool activeLow) {
  pin_ = pin;
  activeLow_ = activeLow;
  pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT_PULLDOWN);

  const bool raw = digitalRead(pin_) == (activeLow_ ? LOW : HIGH);
  stableState_ = raw;
  lastRawState_ = raw;
  lastChangeMs_ = millis();
}

bool ButtonInput::update(uint32_t nowMs) {
  const bool raw = digitalRead(pin_) == (activeLow_ ? LOW : HIGH);

  if (raw != lastRawState_) {
    lastRawState_ = raw;
    lastChangeMs_ = nowMs;
    return false;
  }

  if (raw == stableState_ || (nowMs - lastChangeMs_) < cfg::kButtonDebounceMs) {
    return false;
  }

  stableState_ = raw;
  return stableState_;  // report the press, not the release
}
