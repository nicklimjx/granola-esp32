#pragma once
//
// JSON codec for the board <-> browser BLE protocol.
//
// One newline-framed JSON object, always with a "type" field. The board sends
// two message types and receives three:
//
//   board.ready     board   -> browser  identity + capabilities
//   instruction     browser -> board    starts one round
//   action.detected board   -> browser  first action detected in the round
//   round.result    browser -> board    authoritative verdict and score
//   game.stop       browser -> board    immediately abandon the active round
//

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "../game/actions.h"

namespace protocol {

constexpr int kProtocolVersion = 1;

// Round IDs are opaque server-side strings that the board only ever echoes.
// Long enough for a UUID; anything longer is rejected rather than truncated,
// because a truncated ID would be silently ignored by the server.
constexpr size_t kRoundIdMaxLen = 40;

enum class InboundType : uint8_t {
  Unknown,      // a type this firmware does not handle
  Instruction,  // {"type":"instruction","roundId":"1:12","action":"tap","timeoutMs":3400}
  RoundResult,  // {"type":"round.result","roundId":"12","result":"success","score":5}
  GameStop,     // {"type":"game.stop","reset":true}
};

struct Inbound {
  InboundType type = InboundType::Unknown;
  char roundId[kRoundIdMaxLen + 1] = {0};

  // Instruction only.
  Action action = Action::None;
  uint32_t timeoutMs = 0;

  // RoundResult only.
  RoundOutcome outcome = RoundOutcome::Unknown;
  int32_t score = 0;

  // GameStop only; omitted means false.
  bool reset = false;
};

// Parses one frame. Returns false if the payload is not JSON, has no "type", or
// is a known type with an unusable payload (missing/oversized round ID). An
// unrecognised "type" parses successfully as InboundType::Unknown.
bool parseInbound(const uint8_t* payload, size_t length, Inbound& out);

// {"type":"board.ready","protocolVersion":1,"boardId":"bopit-01",
//  "supportedActions":["tap","swipe","press"]}
String encodeBoardReady(const char* boardId);

// {"type":"action.detected","roundId":"12","action":"twist","elapsedMs":640}
String encodeActionDetected(const char* roundId, Action action, uint32_t elapsedMs);

}  // namespace protocol
