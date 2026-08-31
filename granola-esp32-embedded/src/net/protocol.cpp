#include "protocol.h"

#include <ArduinoJson.h>

namespace protocol {
namespace {

uint16_t readU16Le(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readU32Le(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

void writeU16Le(uint8_t* bytes, uint16_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32Le(uint8_t* bytes, uint32_t value) {
  bytes[0] = static_cast<uint8_t>(value);
  bytes[1] = static_cast<uint8_t>(value >> 8);
  bytes[2] = static_cast<uint8_t>(value >> 16);
  bytes[3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace

bool parseInbound(const uint8_t* payload, size_t length, Inbound& out) {
  out = Inbound{};
  if (payload == nullptr || length == 0) return false;

  if (payload[0] == kInstructionType) {
    if (length != kInstructionLength) return false;
    const Action action = actionFromCode(payload[6]);
    const uint16_t timeoutMs = readU16Le(payload + 7);
    if (payload[5] < 1 || payload[5] > 60 || action == Action::None || timeoutMs == 0) return false;
    out.type = InboundType::Instruction;
    out.session = readU32Le(payload + 1);
    out.round = payload[5];
    out.action = action;
    out.timeoutMs = timeoutMs;
    return true;
  }

  if (payload[0] == kStopType) {
    if (length != kStopLength) return false;
    out.type = InboundType::GameStop;
    out.session = readU32Le(payload + 1);
    out.reset = (payload[5] & 0x01) != 0;
    return true;
  }

  return false;
}

String encodeBoardReady(const char* boardId) {
  JsonDocument doc;
  doc["type"] = "board.ready";
  doc["protocolVersion"] = kProtocolVersion;
  doc["boardId"] = boardId;

  JsonArray actions = doc["supportedActions"].to<JsonArray>();
  for (const Action action : kSupportedActions) actions.add(actionToWire(action));

  String out;
  serializeJson(doc, out);
  return out;
}

void encodeRoundEnded(uint32_t session, uint8_t round, Action action, uint16_t elapsedMs,
                      uint8_t out[kRoundEndedLength]) {
  out[0] = kRoundEndedType;
  writeU32Le(out + 1, session);
  out[5] = round;
  out[6] = actionToCode(action);
  writeU16Le(out + 7, elapsedMs);
}

}  // namespace protocol
