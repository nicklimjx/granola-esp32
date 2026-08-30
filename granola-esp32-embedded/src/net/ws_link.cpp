#include "ws_link.h"

#include "../../include/app_config.h"

WsLink* WsLink::instance_ = nullptr;

void WsLink::begin(const Config& config) {
  config_ = config;
  instance_ = this;

  socket_.onEvent(&WsLink::eventTrampoline);
  socket_.setReconnectInterval(config_.reconnectIntervalMs);
  if (cfg::kWsUsePingPong) {
    socket_.enableHeartbeat(cfg::kWsPingIntervalMs, cfg::kWsPongTimeoutMs,
                            cfg::kWsMissedPongsBeforeDisconnect);
  }
  socket_.begin(config_.host, config_.port, config_.path);
  started_ = true;

  log_i("WebSocket target ws://%s:%u%s (board %s)", config_.host, config_.port, config_.path,
        config_.boardId);
}

void WsLink::loop() {
  if (!started_) {
    return;
  }
  socket_.loop();
}

bool WsLink::sendActionDetected(const char* roundId, Action action, uint32_t elapsedMs) {
  return sendRaw(protocol::encodeActionDetected(roundId, action, elapsedMs));
}

bool WsLink::sendRaw(const String& json) {
  if (!connected_) {
    log_w("dropping outbound frame, link down: %s", json.c_str());
    return false;
  }
  // sendTXT(String&) wants a mutable reference; the const char* overload does
  // not, and avoids a copy.
  return socket_.sendTXT(json.c_str(), json.length());
}

void WsLink::eventTrampoline(WStype_t type, uint8_t* payload, size_t length) {
  if (instance_ != nullptr) {
    instance_->handleEvent(type, payload, length);
  }
}

void WsLink::handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      connected_ = true;
      log_i("WebSocket connected");
      // The hardware is initialised in setup(), before the socket is ever
      // opened, so the board is always ready by the time we get here.
      const String ready = protocol::encodeBoardReady(config_.boardId);
      socket_.sendTXT(ready.c_str(), ready.length());
      if (connectionHandler_) {
        connectionHandler_(true);
      }
      break;
    }

    case WStype_DISCONNECTED:
      if (connected_) {
        log_w("WebSocket disconnected");
      }
      connected_ = false;
      if (connectionHandler_) {
        connectionHandler_(false);
      }
      break;

    case WStype_TEXT:
      handleText(payload, length);
      break;

    case WStype_ERROR:
      log_e("WebSocket error");
      break;

    default:
      // Binary frames and fragments are not used by this protocol.
      break;
  }
}

void WsLink::handleText(const uint8_t* payload, size_t length) {
  protocol::Inbound msg;
  if (!protocol::parseInbound(payload, length, msg)) {
    return;  // parseInbound already logged the reason
  }

  switch (msg.type) {
    case protocol::InboundType::Instruction:
      if (msg.action == Action::None) {
        // The server should only request actions we advertised in board.ready.
        log_w("instruction for round %s names an unsupported action, ignoring", msg.roundId);
        break;
      }
      if (instructionHandler_) {
        instructionHandler_(msg.roundId, msg.action, msg.timeoutMs);
      }
      break;

    case protocol::InboundType::RoundResult:
      if (roundResultHandler_) {
        roundResultHandler_(msg.roundId, msg.outcome, msg.score);
      }
      break;

    case protocol::InboundType::Unknown:
    default:
      log_d("ignoring unhandled message type");
      break;
  }
}
