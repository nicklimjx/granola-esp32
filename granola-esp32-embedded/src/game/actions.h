#pragma once
//
// The four bop-it actions and their wire names.
//
// The server protocol treats an action as an open string, not an enum: the
// board advertises what it can detect in board.ready.supportedActions and the
// server may only ask for those values. The constants below are therefore the
// authoritative list — change them here and the advertisement, the outbound
// action.detected messages and the parser all follow.
//

#include <stddef.h>
#include <stdint.h>

enum class Action : uint8_t {
  None = 0,  // no action (nothing detected yet)
  Bop,       // screen tap
  Twist,     // device twisted about the screen-normal axis (IMU)
  Swipe,     // screen swiped
  Press,     // physical button pressed
};

// Wire names advertised in supportedActions. Note that neither `twist` nor
// `swipe` carries a direction: the board detects rotation and travel either
// way, so the server must not request direction-specific variants.
constexpr const char* kWireBop = "tap";
constexpr const char* kWireTwist = "twist";
constexpr const char* kWireSwipe = "swipe";
constexpr const char* kWirePress = "press";

// Everything this board can detect, in advertisement order.
constexpr Action kSupportedActions[] = {Action::Bop, Action::Twist, Action::Swipe, Action::Press};
constexpr size_t kSupportedActionCount = sizeof(kSupportedActions) / sizeof(kSupportedActions[0]);

const char* actionToWire(Action action);
Action actionFromWire(const char* wire);

// Human-readable prompt shown on the board, e.g. "TWIST IT!".
const char* actionToPrompt(Action action);

// The authoritative per-round verdict, which the server owns and sends in
// round.result.
enum class RoundOutcome : uint8_t {
  Unknown = 0,
  Success,
  WrongAction,
  Timeout,
};

RoundOutcome outcomeFromWire(const char* wire);
const char* outcomeToWire(RoundOutcome outcome);
