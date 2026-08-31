#pragma once
//
// The four bop-it actions and their wire names.
//
// The board advertises wire names in board.ready and protocol v2 maps those
// actions to fixed byte codes for gameplay packets.
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

// Actions currently advertised to the browser, in selection order. The Twist
// enum and detector remain, but inbound parsing rejects it while unadvertised.
constexpr Action kSupportedActions[] = {Action::Bop, Action::Swipe, Action::Press};
constexpr size_t kSupportedActionCount = sizeof(kSupportedActions) / sizeof(kSupportedActions[0]);

const char* actionToWire(Action action);
Action actionFromWire(const char* wire);
uint8_t actionToCode(Action action);
Action actionFromCode(uint8_t code);

// Human-readable prompt shown on the board, e.g. "TWIST IT!".
const char* actionToPrompt(Action action);

enum class LocalVerdict : uint8_t {
  Success,
  WrongAction,
  Timeout,
};
