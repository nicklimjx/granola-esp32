#include "actions.h"

#include <string.h>

const char* actionToWire(Action action) {
  switch (action) {
    case Action::Bop:
      return kWireBop;
    case Action::Twist:
      return kWireTwist;
    case Action::Swipe:
      return kWireSwipe;
    case Action::Press:
      return kWirePress;
    case Action::None:
    default:
      return "none";
  }
}

Action actionFromWire(const char* wire) {
  if (wire == nullptr) {
    return Action::None;
  }
  for (const Action action : kSupportedActions) {
    if (strcmp(wire, actionToWire(action)) == 0) {
      return action;
    }
  }
  return Action::None;
}

uint8_t actionToCode(Action action) {
  return static_cast<uint8_t>(action);
}

Action actionFromCode(uint8_t code) {
  for (const Action action : kSupportedActions) {
    if (code == actionToCode(action)) return action;
  }
  return Action::None;
}

const char* actionToPrompt(Action action) {
  switch (action) {
    case Action::Bop:
      return "BOP IT!";
    case Action::Twist:
      return "TWIST IT!";
    case Action::Swipe:
      return "SWIPE IT!";
    case Action::Press:
      return "PRESS IT!";
    case Action::None:
    default:
      return "WAIT...";
  }
}
