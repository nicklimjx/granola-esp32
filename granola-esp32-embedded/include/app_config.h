#pragma once
//
// Tunable game / input parameters. Hardware pins live in board_config.h,
// network credentials in secrets.h.
//

#include <stdint.h>

#include "board_config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found - using placeholder Wi-Fi/WebSocket settings. Copy include/secrets.example.h."
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#define WS_HOST "192.168.1.50"
#define WS_PORT 8080
#define WS_PATH "/bopit"
#endif

#define FIRMWARE_VERSION "0.1.0"

namespace cfg {

// ---- Network -------------------------------------------------------------
constexpr uint32_t kWsReconnectIntervalMs = 2000;

// The protocol needs no application-level heartbeat, but a board on Wi-Fi can
// otherwise sit "connected" to an AP that has gone away. RFC6455 ping/pong
// costs nothing and every mainstream Go WebSocket library answers pings
// automatically. Set to false if the server turns out not to.
constexpr bool kWsUsePingPong = true;
constexpr uint32_t kWsPingIntervalMs = 5000;
constexpr uint32_t kWsPongTimeoutMs = 3000;
constexpr uint8_t kWsMissedPongsBeforeDisconnect = 2;

// ---- Round timing --------------------------------------------------------
// The action window comes from the server as instruction.timeoutMs. This table
// is only a fallback for an instruction that arrives without one, and encodes
// the intended ramp: first 10 rounds 2000 ms, then one step shorter every 10,
// holding at 800 ms from round 51.
constexpr uint16_t kActionWindowsMs[] = {2000, 1700, 1400, 1200, 1000, 800};
constexpr uint32_t kRoundsPerTier = 10;

// How long the server's verdict stays on screen before the board goes back to
// waiting. Kept short so it never eats into the next round's window.
constexpr uint32_t kFeedbackHoldMs = 350;

// The server ends every round with round.result, including timeouts. If one
// never arrives the board goes idle rather than sitting on a dead screen.
constexpr uint32_t kRoundResultTimeoutMs = 5000;

// ---- Touch gestures ------------------------------------------------------
constexpr uint32_t kTouchPollIntervalMs = 8;   // ~125 Hz
// A press that stays inside this radius and is released quickly is a tap.
constexpr int16_t kTapMaxTravelPx = 28;
constexpr uint32_t kTapMaxDurationMs = 450;
// Travel that fires a swipe. Reported as soon as the threshold is crossed
// rather than on release, which feels much better in a reaction game.
constexpr int16_t kSwipeMinTravelPx = 70;
constexpr uint32_t kSwipeMaxDurationMs = 900;

// ---- Twist (gyroscope) ---------------------------------------------------
constexpr uint32_t kImuPollIntervalMs = 5;     // ~200 Hz
// Rotation rate on the twist axis that starts accumulating an angle.
constexpr float kTwistGateDps = 70.0f;
// Accumulated rotation that counts as a twist.
constexpr float kTwistAngleDeg = 45.0f;
// The twist axis must dominate the other two by this factor, so that shaking
// or a hard tap does not register as a twist.
constexpr float kTwistDominance = 1.3f;
// A twist must complete within this long, and consecutive twists are ignored
// for this long afterwards.
constexpr uint32_t kTwistMaxDurationMs = 700;
constexpr uint32_t kTwistRefractoryMs = 350;
// Rotation must stay above the gate with no gap longer than this.
constexpr uint32_t kTwistGapMs = 130;

// ---- Button --------------------------------------------------------------
constexpr uint32_t kButtonDebounceMs = 25;

}  // namespace cfg
