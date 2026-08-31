#pragma once
// Protocol v2 codec. Browser-to-board gameplay messages are exact, atomic
// binary packets. board.ready remains newline-framed JSON for discovery.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "../game/actions.h"

namespace protocol {

constexpr int kProtocolVersion = 2;
constexpr size_t kInstructionLength = 9;
constexpr size_t kRoundEndedLength = 9;
constexpr size_t kStopLength = 6;
constexpr size_t kMaxInboundLength = kInstructionLength;

constexpr uint8_t kInstructionType = 0x21;
constexpr uint8_t kRoundEndedType = 0x22;
constexpr uint8_t kStopType = 0x24;

enum class InboundType : uint8_t {
  Unknown,
  Instruction,
  GameStop,
};

struct Inbound {
  InboundType type = InboundType::Unknown;
  uint32_t session = 0;
  uint8_t round = 0;
  Action action = Action::None;
  uint16_t timeoutMs = 0;
  bool reset = false;
};

// Requires one complete GATT value with exactly the packet's specified length.
bool parseInbound(const uint8_t* payload, size_t length, Inbound& out);

// Newline is appended by BleLink before chunked transmission.
String encodeBoardReady(const char* boardId);

// [0]=0x22, [1..4]=session LE, [5]=round, [6]=action (0 means timeout),
// [7..8]=elapsedMs LE.
void encodeRoundEnded(uint32_t session, uint8_t round, Action action, uint16_t elapsedMs,
                      uint8_t out[kRoundEndedLength]);

}  // namespace protocol
