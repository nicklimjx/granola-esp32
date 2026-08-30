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

RoundOutcome outcomeFromWire(const char* wire) {
  if (wire == nullptr) {
    return RoundOutcome::Unknown;
  }
  if (strcmp(wire, "success") == 0) {
    return RoundOutcome::Success;
  }
  if (strcmp(wire, "wrong_action") == 0) {
    return RoundOutcome::WrongAction;
  }
  if (strcmp(wire, "timeout") == 0) {
    return RoundOutcome::Timeout;
  }
  return RoundOutcome::Unknown;
}

const char* outcomeToWire(RoundOutcome outcome) {
  switch (outcome) {
    case RoundOutcome::Success:
      return "success";
    case RoundOutcome::WrongAction:
      return "wrong_action";
    case RoundOutcome::Timeout:
      return "timeout";
    case RoundOutcome::Unknown:
    default:
      return "unknown";
  }
}
