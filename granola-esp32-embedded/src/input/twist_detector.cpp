#include "twist_detector.h"

#include <Arduino.h>
#include <math.h>

#include "../../include/app_config.h"

void TwistDetector::begin(uint8_t axis) {
  axis_ = axis > 2 ? 2 : axis;
  reset();
  lastFireMs_ = 0;
}

void TwistDetector::reset() {
  active_ = false;
  sign_ = 0.0f;
  angleDeg_ = 0.0f;
  peakPrimary_ = 0.0f;
  peakOther_ = 0.0f;
  startMs_ = 0;
  lastSampleMs_ = 0;
  lastAboveGateMs_ = 0;
}

bool TwistDetector::update(float gyroX, float gyroY, float gyroZ, uint32_t nowMs) {
  const float axes[3] = {gyroX, gyroY, gyroZ};
  const float primary = axes[axis_];
  const float other = fmaxf(fabsf(axes[(axis_ + 1) % 3]), fabsf(axes[(axis_ + 2) % 3]));
  const float magnitude = fabsf(primary);

  if (magnitude < cfg::kTwistGateDps) {
    // Rotation has died down. Keep the accumulator alive across brief dips so a
    // twist that momentarily slows still counts, but drop it after a real gap.
    if (active_ && (nowMs - lastAboveGateMs_) > cfg::kTwistGapMs) {
      reset();
    }
    lastSampleMs_ = nowMs;
    return false;
  }

  const float sign = primary >= 0.0f ? 1.0f : -1.0f;

  if (!active_ || sign != sign_) {
    // New gesture, or a reversal — twisting back is itself a twist.
    reset();
    active_ = true;
    sign_ = sign;
    startMs_ = nowMs;
    lastSampleMs_ = nowMs;
  }

  const uint32_t dtMs = nowMs - lastSampleMs_;
  lastSampleMs_ = nowMs;
  lastAboveGateMs_ = nowMs;

  angleDeg_ += magnitude * (static_cast<float>(dtMs) / 1000.0f);
  peakPrimary_ = fmaxf(peakPrimary_, magnitude);
  peakOther_ = fmaxf(peakOther_, other);

  if (angleDeg_ < cfg::kTwistAngleDeg) {
    return false;
  }

  const bool inTime = (nowMs - startMs_) <= cfg::kTwistMaxDurationMs;
  const bool dominates = peakPrimary_ >= cfg::kTwistDominance * peakOther_;
  const bool outOfRefractory =
      lastFireMs_ == 0 || (nowMs - lastFireMs_) >= cfg::kTwistRefractoryMs;

  log_d("twist candidate: angle=%.1f peak=%.1f other=%.1f inTime=%d dominates=%d", angleDeg_,
        peakPrimary_, peakOther_, inTime, dominates);

  // Either way the gesture is spent; only report it if it passed every test.
  const bool accepted = inTime && dominates && outOfRefractory;
  reset();
  if (accepted) {
    lastFireMs_ = nowMs;
  }
  return accepted;
}
