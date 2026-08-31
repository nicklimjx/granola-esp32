#pragma once
// BLE Nordic UART link. Callbacks enqueue bounded events only; loop() parses
// complete gameplay writes and invokes game handlers.

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
  using InstructionHandler = std::function<void(uint32_t, uint8_t, Action, uint16_t)>;
  using StopHandler = std::function<void(uint32_t, bool)>;

  void onConnection(ConnectionHandler handler) { connectionHandler_ = std::move(handler); }
  void onInstruction(InstructionHandler handler) { instructionHandler_ = std::move(handler); }
  void onStop(StopHandler handler) { stopHandler_ = std::move(handler); }

  void begin(const Config& config);
  void loop();
  bool isConnected() const { return connected_; }
  bool sendRoundEnded(uint32_t session, uint8_t round, Action action, uint16_t elapsedMs);
  bool sendRaw(const String& json);

 private:
  static constexpr size_t kChunkSize = 20;
  static constexpr size_t kEventCapacity = 24;
  static constexpr size_t kOutboundMessageCapacity = 512;
  static constexpr size_t kOutboundQueueCapacity = 6;

  enum class EventType : uint8_t {
    Connected,
    Data,
    ReadyRequested,
  };
  struct Event {
    EventType type;
    uint32_t generation = 0;
    uint8_t length = 0;
    uint8_t data[protocol::kMaxInboundLength] = {0};
  };
  struct OutboundMessage {
    uint16_t length = 0;
    uint16_t offset = 0;
    bool chunked = false;
    uint8_t data[kOutboundMessageCapacity] = {0};
  };

  class ServerCallbacks;
  class RxCallbacks;
  class TxCallbacks;

  void enqueueConnection(bool connected);
  void enqueueData(const uint8_t* data, size_t length);
  void enqueueReadyRequested();
  void recordIndicationStatus(bool succeeded);
  bool popEvent(Event& event);
  bool takeIndicationStatus(bool& succeeded, uint32_t& generation);
  bool takePendingDisconnect(uint32_t& generation);
  void snapshotCallbackConnection(bool& connected, uint32_t& generation);
  bool enqueueOutbound(const uint8_t* data, size_t length, bool chunked);
  void clearOutbound();
  void pumpIndication();

  Config config_;
  BLEServer* server_ = nullptr;
  BLECharacteristic* tx_ = nullptr;
  bool connected_ = false;
  String readyJson_;

  Event events_[kEventCapacity];
  size_t eventHead_ = 0;
  size_t eventTail_ = 0;
  size_t eventCount_ = 0;
  bool eventOverflow_ = false;
  bool callbackConnected_ = false;
  uint32_t callbackGeneration_ = 0;
  bool disconnectPending_ = false;
  uint32_t disconnectGeneration_ = 0;
  bool indicationStatusPending_ = false;
  bool indicationStatusSucceeded_ = false;
  uint32_t indicationStatusGeneration_ = 0;
  portMUX_TYPE eventMux_ = portMUX_INITIALIZER_UNLOCKED;

  uint32_t activeGeneration_ = 0;
  OutboundMessage outbound_[kOutboundQueueCapacity];
  size_t outboundHead_ = 0;
  size_t outboundTail_ = 0;
  size_t outboundCount_ = 0;
  size_t outboundChunkLength_ = 0;
  bool indicationAwaitingStatus_ = false;

  ConnectionHandler connectionHandler_;
  InstructionHandler instructionHandler_;
  StopHandler stopHandler_;
};
