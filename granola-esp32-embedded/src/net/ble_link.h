#pragma once
// BLE Nordic UART link. BLE callbacks only enqueue events; loop() owns JSON
// dispatch and therefore is the only place that invokes game handlers.

#include <Arduino.h>
#include <functional>

#include "../game/actions.h"
#include "protocol.h"

class BLECharacteristic;
class BLEServer;

class BleLink {
 public:
  struct Config {
    const char* deviceName = "Granola Bop-It";
    const char* boardId = "bopit-01";
  };

  using ConnectionHandler = std::function<void(bool)>;
  using InstructionHandler = std::function<void(const char*, Action, uint32_t)>;
  using RoundResultHandler = std::function<void(const char*, RoundOutcome, int32_t)>;

  void onConnection(ConnectionHandler handler) { connectionHandler_ = std::move(handler); }
  void onInstruction(InstructionHandler handler) { instructionHandler_ = std::move(handler); }
  void onRoundResult(RoundResultHandler handler) { roundResultHandler_ = std::move(handler); }

  void begin(const Config& config);
  void loop();
  bool isConnected() const { return connected_; }
  bool sendActionDetected(const char* roundId, Action action, uint32_t elapsedMs);
  bool sendRaw(const String& json);

 private:
  static constexpr size_t kChunkSize = 20;
  static constexpr size_t kEventCapacity = 48;
  static constexpr size_t kOutboundCapacity = 512;
  static constexpr size_t kInboundCapacity = 1024;

  enum class EventType : uint8_t {
    Connected,
    Disconnected,
    Data,
    ReadyRequested,
    IndicationSucceeded,
    IndicationFailed,
  };
  struct Event {
    EventType type;
    uint8_t length = 0;
    uint8_t data[kChunkSize] = {0};
  };

  class ServerCallbacks;
  class RxCallbacks;
  class TxCallbacks;

  void enqueueConnection(bool connected);
  void enqueueData(const uint8_t* data, size_t length);
  void enqueueEvent(EventType type);
  bool popEvent(Event& event);
  void handleLine(const uint8_t* data, size_t length);
  void pumpIndication();

  Config config_;
  BLEServer* server_ = nullptr;
  BLECharacteristic* tx_ = nullptr;
  bool connected_ = false;
  String readyJson_;
  String inbound_;

  Event events_[kEventCapacity];
  size_t eventHead_ = 0;
  size_t eventTail_ = 0;
  size_t eventCount_ = 0;
  bool eventOverflow_ = false;
  portMUX_TYPE eventMux_ = portMUX_INITIALIZER_UNLOCKED;

  uint8_t outbound_[kOutboundCapacity] = {0};
  size_t outboundLength_ = 0;
  size_t outboundOffset_ = 0;
  size_t outboundChunkLength_ = 0;
  bool indicationAwaitingStatus_ = false;

  ConnectionHandler connectionHandler_;
  InstructionHandler instructionHandler_;
  RoundResultHandler roundResultHandler_;
};
