#pragma once
//
// Recognises a deliberate "twist it" from gyroscope samples.
//
// A twist is a fast rotation about the axis normal to the screen. Peak rate
// alone is a poor test — a hard tap or a shake spikes the gyro too — so this
// integrates the rate on the twist axis while it stays above a gate and fires
// once the accumulated angle passes a threshold, subject to two extra tests:
//
//   * the twist axis must dominate the other two, which rejects shaking and
//     the wrist flick that comes with tapping the screen;
//   * the whole gesture must complete inside a time limit, which rejects the
//     slow rotation of somebody simply turning the device over.
//
// Thresholds live in app_config.h and are worth tuning on real hardware; run
// the firmware with CORE_DEBUG_LEVEL=4 to see the accumulated angles.
//

#include <stdint.h>

class TwistDetector {
 public:
  // `axis`: 0 = X, 1 = Y, 2 = Z. Comes from TWIST_GYRO_AXIS.
  void begin(uint8_t axis);

  // Feed every gyro sample. Returns true exactly once per recognised twist.
  bool update(float gyroX, float gyroY, float gyroZ, uint32_t nowMs);

  // Abandons any gesture in progress, so rotation that started before a round
  // opened cannot complete a twist inside it.
  void reset();

 private:
  uint8_t axis_ = 2;

  bool active_ = false;
  float sign_ = 0.0f;
  float angleDeg_ = 0.0f;
  float peakPrimary_ = 0.0f;
  float peakOther_ = 0.0f;
  uint32_t startMs_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastAboveGateMs_ = 0;
  uint32_t lastFireMs_ = 0;
};
