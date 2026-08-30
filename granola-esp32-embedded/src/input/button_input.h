#pragma once
//
// Debounced edge detection for the "press it" button (the BOOT side button on
// the AMOLED-1.8, GPIO0, active low).
//

#include <stdint.h>

class ButtonInput {
 public:
  void begin(uint8_t pin, bool activeLow);

  // Call every loop(). Returns true once per press.
  bool update(uint32_t nowMs);

  bool isDown() const { return stableState_; }

 private:
  uint8_t pin_ = 0;
  bool activeLow_ = true;
  bool stableState_ = false;   // true == pressed
  bool lastRawState_ = false;
  uint32_t lastChangeMs_ = 0;
};
