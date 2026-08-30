#pragma once
//
// InputManager — turns the three input devices into a single queue of Actions.
//
// Touch handling recognises two gestures from the same finger-down: a swipe
// fires the moment the travel threshold is crossed (waiting for release would
// cost 100+ ms of a 1600 ms window), and a tap fires on release if the finger
// never travelled far. A gesture that has already produced a swipe is marked
// spent so its release cannot also produce a tap.
//

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

#include "../game/actions.h"
#include "button_input.h"
#include "qmi8658.h"
#include "touch_driver.h"
#include "twist_detector.h"

struct InputEvent {
  Action action = Action::None;
  uint32_t timestampMs = 0;
};

class InputManager {
 public:
  // Initialises the I2C bus and all three devices. Returns false if any device
  // is missing; the game still runs on whatever did come up.
  bool begin();

  // Polls the devices and appends to the queue. Call every loop().
  void loop();

  // Oldest queued event, or false if the queue is empty.
  bool pop(InputEvent& out);

  // Drops queued events and abandons gestures in progress. Called when a round
  // opens so that input from before the instruction cannot score.
  void flush();

  bool hasTouch() const { return touch_.address() != 0; }
  bool hasImu() const { return imu_.isPresent(); }
  uint8_t touchAddress() const { return touch_.address(); }

 private:
  static constexpr uint8_t kQueueSize = 8;

  void push(Action action, uint32_t nowMs);
  void pollTouch(uint32_t nowMs);
  void pollImu(uint32_t nowMs);

  TouchDriver touch_;
  Qmi8658 imu_;
  ButtonInput button_;
  TwistDetector twist_;

  // Touch gesture state.
  bool fingerDown_ = false;
  bool gestureSpent_ = false;
  int16_t startX_ = 0;
  int16_t startY_ = 0;
  // Last coordinates while the finger was down. The controller reports no
  // coordinates on the sample where the finger lifts, so the tap/swipe decision
  // at release has to use these.
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  uint32_t startMs_ = 0;

  uint32_t lastTouchPollMs_ = 0;
  uint32_t lastImuPollMs_ = 0;

  InputEvent queue_[kQueueSize];
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};
