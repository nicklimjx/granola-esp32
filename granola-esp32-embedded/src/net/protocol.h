#pragma once
//
// JSON codec for the board <-> Go server WebSocket protocol.
//
// One JSON object per text frame, always with a "type" field. Four message
// types exist; the board sends two and receives two:
//
//   board.ready     board  -> server   identity + capabilities, on connect
//   instruction     server -> board    starts one round
//   action.detected board  -> server   the first action detected in the round
//   round.result    server -> board    authoritative verdict and score
//
// The server closes the socket on invalid JSON or an unsupported protocol
// version, so everything emitted here is fixed-shape.
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
  Instruction,  // {"type":"instruction","roundId":"12","action":"twist","timeoutMs":1800}
  RoundResult,  // {"type":"round.result","roundId":"12","result":"success","score":5}
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
};

// Parses one frame. Returns false if the payload is not JSON, has no "type", or
// is a known type with an unusable payload (missing/oversized round ID). An
// unrecognised "type" parses successfully as InboundType::Unknown.
bool parseInbound(const uint8_t* payload, size_t length, Inbound& out);

// {"type":"board.ready","protocolVersion":1,"boardId":"bopit-01",
//  "supportedActions":["tap","twist","swipe","press"]}
String encodeBoardReady(const char* boardId);

// {"type":"action.detected","roundId":"12","action":"twist","elapsedMs":640}
String encodeActionDetected(const char* roundId, Action action, uint32_t elapsedMs);

}  // namespace protocol
