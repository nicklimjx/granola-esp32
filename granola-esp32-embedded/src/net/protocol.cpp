#include "protocol.h"

#include <ArduinoJson.h>
#include <string.h>

namespace protocol {
namespace {

// Copies an opaque server string into the fixed-size field. Fails rather than
// truncates: the server ignores actions for round IDs it does not recognise, so
// a truncated ID would turn every round into a silent no-op.
bool copyRoundId(JsonVariantConst value, char* out, size_t capacity) {
  const char* roundId = value.as<const char*>();
  if (roundId == nullptr) {
    return false;
  }
  const size_t length = strlen(roundId);
  if (length == 0 || length >= capacity) {
    return false;
  }
  memcpy(out, roundId, length + 1);
  return true;
}

}  // namespace

bool parseInbound(const uint8_t* payload, size_t length, Inbound& out) {
  out = Inbound{};

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    log_w("json parse failed: %s", err.c_str());
    return false;
  }

  const char* type = doc["type"].as<const char*>();
  if (type == nullptr) {
    log_w("frame has no type");
    return false;
  }

  if (strcmp(type, "instruction") == 0) {
    if (!copyRoundId(doc["roundId"], out.roundId, sizeof(out.roundId))) {
      log_w("instruction has a missing or oversized roundId");
      return false;
    }
    out.type = InboundType::Instruction;
    out.action = actionFromWire(doc["action"].as<const char*>());
    out.timeoutMs = doc["timeoutMs"].as<uint32_t>();
    return true;
  }

  if (strcmp(type, "game.stop") == 0) {
    out.type = InboundType::GameStop;
    out.reset = doc["reset"].as<bool>();
    return true;
  }

  if (strcmp(type, "round.result") == 0) {
    if (!copyRoundId(doc["roundId"], out.roundId, sizeof(out.roundId))) {
      log_w("round.result has a missing or oversized roundId");
      return false;
    }
    out.type = InboundType::RoundResult;
    out.outcome = outcomeFromWire(doc["result"].as<const char*>());
    out.score = doc["score"].as<int32_t>();
    return true;
  }

  return true;  // parsed fine, just not a type we act on
}

String encodeBoardReady(const char* boardId) {
  JsonDocument doc;
  doc["type"] = "board.ready";
  doc["protocolVersion"] = kProtocolVersion;
  doc["boardId"] = boardId;

  JsonArray actions = doc["supportedActions"].to<JsonArray>();
  for (const Action action : kSupportedActions) {
    actions.add(actionToWire(action));
  }

  String out;
  serializeJson(doc, out);
  return out;
}

String encodeActionDetected(const char* roundId, Action action, uint32_t elapsedMs) {
  JsonDocument doc;
  doc["type"] = "action.detected";
  doc["roundId"] = roundId;
  doc["action"] = actionToWire(action);
  doc["elapsedMs"] = elapsedMs;

  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace protocol
