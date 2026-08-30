#include "input_manager.h"

#include <math.h>

#include "../../include/app_config.h"

bool InputManager::begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

  const bool touchOk = touch_.begin(Wire);
  const bool imuOk = imu_.begin(Wire);

  button_.begin(PRESS_BUTTON_PIN, PRESS_BUTTON_ACTIVE_LOW != 0);
  twist_.begin(TWIST_GYRO_AXIS);

  if (imuOk) {
    // The board must be still for this; it runs before the game starts.
    imu_.calibrateGyroBias();
  }

  flush();
  return touchOk && imuOk;
}

void InputManager::loop() {
  const uint32_t now = millis();

  if (button_.update(now)) {
    push(Action::Press, now);
  }

  if (now - lastTouchPollMs_ >= cfg::kTouchPollIntervalMs) {
    lastTouchPollMs_ = now;
    pollTouch(now);
  }

  if (now - lastImuPollMs_ >= cfg::kImuPollIntervalMs) {
    lastImuPollMs_ = now;
    pollImu(now);
  }
}

void InputManager::pollTouch(uint32_t nowMs) {
  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
  if (!touch_.read(pressed, x, y)) {
    return;
  }

  if (pressed) {
    lastX_ = x;
    lastY_ = y;
  }

  if (pressed && !fingerDown_) {
    fingerDown_ = true;
    gestureSpent_ = false;
    startX_ = x;
    startY_ = y;
    startMs_ = nowMs;
    return;
  }

  if (pressed && fingerDown_) {
    if (gestureSpent_) {
      return;
    }
    const int32_t dx = x - startX_;
    const int32_t dy = y - startY_;
    const float travel = sqrtf(static_cast<float>(dx * dx + dy * dy));
    const uint32_t duration = nowMs - startMs_;

    if (travel >= cfg::kSwipeMinTravelPx && duration <= cfg::kSwipeMaxDurationMs) {
      push(Action::Swipe, nowMs);
      gestureSpent_ = true;
    } else if (travel >= cfg::kSwipeMinTravelPx) {
      // Travelled far enough but too slowly to be a swipe, and too far to be a
      // tap: a drag. Discard it rather than scoring it as something.
      gestureSpent_ = true;
    }
    return;
  }

  if (!pressed && fingerDown_) {
    fingerDown_ = false;
    if (gestureSpent_) {
      return;
    }
    const int32_t dx = lastX_ - startX_;
    const int32_t dy = lastY_ - startY_;
    const float travel = sqrtf(static_cast<float>(dx * dx + dy * dy));
    const uint32_t duration = nowMs - startMs_;

    if (travel <= cfg::kTapMaxTravelPx && duration <= cfg::kTapMaxDurationMs) {
      push(Action::Bop, nowMs);
    }
  }
}

void InputManager::pollImu(uint32_t nowMs) {
  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
  if (!imu_.readGyro(gx, gy, gz)) {
    return;
  }
  if (twist_.update(gx, gy, gz, nowMs)) {
    push(Action::Twist, nowMs);
  }
}

void InputManager::push(Action action, uint32_t nowMs) {
  log_d("input: %s", actionToWire(action));

  if (count_ == kQueueSize) {
    // Full queue means the game layer is not draining; the newest input is the
    // one that matters, so drop the oldest.
    head_ = (head_ + 1) % kQueueSize;
    --count_;
  }
  const uint8_t tail = (head_ + count_) % kQueueSize;
  queue_[tail].action = action;
  queue_[tail].timestampMs = nowMs;
  ++count_;
}

bool InputManager::pop(InputEvent& out) {
  if (count_ == 0) {
    return false;
  }
  out = queue_[head_];
  head_ = (head_ + 1) % kQueueSize;
  --count_;
  return true;
}

void InputManager::flush() {
  head_ = 0;
  count_ = 0;
  // fingerDown_ deliberately keeps the real state of the glass. Marking the
  // gesture spent means a finger already down when the round opened has to lift
  // and press again, so a mid-flight swipe or tap cannot score.
  gestureSpent_ = true;
  twist_.reset();
}
