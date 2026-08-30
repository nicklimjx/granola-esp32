#pragma once
// Tunable game and input parameters. Hardware pins live in board_config.h;
// BLE identity is derived from BOARD_ID and needs no local credentials.

#include <stdint.h>
#include "board_config.h"

#define FIRMWARE_VERSION "0.2.0"

namespace cfg {

// Browser instructions normally carry the action window. This matching table
// is only a firmware fallback for a missing timeoutMs.
constexpr uint16_t kActionWindowsMs[] = {4000, 3400, 2800, 2400, 2000, 1600};
constexpr uint32_t kRoundsPerTier = 10;
constexpr uint32_t kFeedbackHoldMs = 350;
constexpr uint32_t kRoundResultTimeoutMs = 5000;

constexpr uint32_t kTouchPollIntervalMs = 8;
constexpr int16_t kTapMaxTravelPx = 28;
constexpr uint32_t kTapMaxDurationMs = 450;
constexpr int16_t kSwipeMinTravelPx = 70;
constexpr uint32_t kSwipeMaxDurationMs = 900;

constexpr uint32_t kImuPollIntervalMs = 5;
constexpr float kTwistGateDps = 70.0f;
constexpr float kTwistAngleDeg = 45.0f;
constexpr float kTwistDominance = 1.3f;
constexpr uint32_t kTwistMaxDurationMs = 700;
constexpr uint32_t kTwistRefractoryMs = 350;
constexpr uint32_t kTwistGapMs = 130;

constexpr uint32_t kButtonDebounceMs = 25;

}  // namespace cfg
