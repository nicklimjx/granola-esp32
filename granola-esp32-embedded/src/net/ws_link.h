#pragma once
//
// WsLink — the board's connection to the externally hosted WebSocket server.
//
// This is the whole board-side network surface. It owns the socket, the
// reconnect policy and the JSON codec, and exposes the link as callbacks in and
// one send call out. Nothing above it touches WebSockets or JSON, so the same
// link works for the second board and for multiplayer later on.
//
// Usage:
//   WsLink link;
//   link.onInstruction([](const char* id, Action a, uint32_t t) { ... });
//   link.onRoundResult([](const char* id, RoundOutcome o, int32_t s) { ... });
//   link.begin({WS_HOST, WS_PORT, WS_PATH, BOARD_ID});
//   ...
//   link.loop();                                    // every iteration
//   link.sendActionDetected(roundId, action, elapsedMs);
//
// board.ready is sent automatically on every connect, so the server learns the
// board's identity and its supported actions without the game layer caring.
// Per the protocol there is no application-level heartbeat; RFC6455 ping/pong
// is used instead and can be turned off in app_config.h.
//

#include <Arduino.h>
#include <WebSocketsClient.h>

#include <functional>

#include "../game/actions.h"
#include "protocol.h"

class WsLink {
 public:
  struct Config {
    const char* host = nullptr;
    uint16_t port = 0;
    const char* path = "/";
    const char* boardId = "bopit-01";
    uint32_t reconnectIntervalMs = 2000;
  };

  using ConnectionHandler = std::function<void(bool connected)>;
  using InstructionHandler =
      std::function<void(const char* roundId, Action action, uint32_t timeoutMs)>;
  using RoundResultHandler =
      std::function<void(const char* roundId, RoundOutcome outcome, int32_t score)>;

  // Handlers must be installed before begin(); they are invoked from loop().
  void onConnection(ConnectionHandler handler) { connectionHandler_ = std::move(handler); }
  void onInstruction(InstructionHandler handler) { instructionHandler_ = std::move(handler); }
  void onRoundResult(RoundResultHandler handler) { roundResultHandler_ = std::move(handler); }

  void begin(const Config& config);

  // Pumps the socket. Must be called often; inbound handlers fire from here.
  void loop();

  bool isConnected() const { return connected_; }
  const char* boardId() const { return config_.boardId; }

  // Reports the first action detected in a round. Returns false (and drops the
  // message) if the link is down — a reaction time delivered late is worse than
  // none, and the server discards actions for completed rounds anyway.
  bool sendActionDetected(const char* roundId, Action action, uint32_t elapsedMs);

  // Escape hatch for anything not covered above (e.g. multiplayer lobby
  // messages). `json` must be a complete JSON object.
  bool sendRaw(const String& json);

 private:
  static void eventTrampoline(WStype_t type, uint8_t* payload, size_t length);
  void handleEvent(WStype_t type, uint8_t* payload, size_t length);
  void handleText(const uint8_t* payload, size_t length);

  static WsLink* instance_;

  WebSocketsClient socket_;
  Config config_;
  bool connected_ = false;
  bool started_ = false;

  ConnectionHandler connectionHandler_;
  InstructionHandler instructionHandler_;
  RoundResultHandler roundResultHandler_;
};
