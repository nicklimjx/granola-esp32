#include "ble_link.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <string.h>

namespace {
constexpr char kServiceUuid[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kRxUuid[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kTxUuid[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
}

class BleLink::ServerCallbacks : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleLink* owner) : owner_(owner) {}
  void onConnect(BLEServer*) override { owner_->enqueueConnection(true); }
  void onDisconnect(BLEServer*) override { owner_->enqueueConnection(false); }
 private:
  BleLink* owner_;
};

class BleLink::RxCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit RxCallbacks(BleLink* owner) : owner_(owner) {}
  void onWrite(BLECharacteristic* characteristic) override {
    const String value = characteristic->getValue();
    owner_->enqueueData(reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
  }
 private:
  BleLink* owner_;
};

class BleLink::TxCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit TxCallbacks(BleLink* owner) : owner_(owner) {}
  void onSubscribe(BLECharacteristic*, ble_gap_conn_desc*, uint16_t subValue) override {
    if (subValue & 0x02) owner_->enqueueReadyRequested();
  }
  void onStatus(BLECharacteristic*, Status status, uint32_t) override {
    owner_->recordIndicationStatus(status == SUCCESS_INDICATE);
  }
 private:
  BleLink* owner_;
};

void BleLink::begin(const Config& config) {
  config_ = config;
  readyJson_ = protocol::encodeBoardReady(config_.boardId);

  BLEDevice::init(config_.deviceName);
  server_ = BLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(this));
  BLEService* service = server_->createService(kServiceUuid);

  tx_ = service->createCharacteristic(kTxUuid, BLECharacteristic::PROPERTY_INDICATE);
  tx_->setCallbacks(new TxCallbacks(this));

  BLECharacteristic* rx = service->createCharacteristic(kRxUuid, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new RxCallbacks(this));

  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();
  log_i("BLE advertising as %s (board %s)", config_.deviceName, config_.boardId);
}

void BleLink::loop() {
  portENTER_CRITICAL(&eventMux_);
  const bool overflowed = eventOverflow_;
  eventOverflow_ = false;
  portEXIT_CRITICAL(&eventMux_);
  if (overflowed) log_w("BLE event queue overflow; dropped event");

  uint32_t disconnectedGeneration = 0;
  if (takePendingDisconnect(disconnectedGeneration)) {
    bool callbackConnected = false;
    uint32_t callbackGeneration = 0;
    snapshotCallbackConnection(callbackConnected, callbackGeneration);
    if (!callbackConnected && disconnectedGeneration == callbackGeneration) {
      const bool retiredActiveConnection = connected_;
      connected_ = false;
      clearOutbound();
      BLEDevice::startAdvertising();
      log_i("BLE generation %lu disconnected; advertising restarted",
            static_cast<unsigned long>(disconnectedGeneration));
      if (retiredActiveConnection && connectionHandler_) connectionHandler_(false);
    }
  }

  bool latestConnected = false;
  uint32_t latestGeneration = 0;
  snapshotCallbackConnection(latestConnected, latestGeneration);
  if (connected_ && (!latestConnected || latestGeneration != activeGeneration_)) {
    connected_ = false;
    clearOutbound();
    log_i("BLE connection generation retired");
    if (connectionHandler_) connectionHandler_(false);
  }

  bool indicationSucceeded = false;
  uint32_t indicationGeneration = 0;
  if (takeIndicationStatus(indicationSucceeded, indicationGeneration) &&
      indicationGeneration == activeGeneration_ && connected_ && indicationAwaitingStatus_) {
    if (indicationSucceeded && outboundCount_ > 0) {
      OutboundMessage& message = outbound_[outboundHead_];
      message.offset += outboundChunkLength_;
      if (message.offset == message.length) {
        outboundHead_ = (outboundHead_ + 1) % kOutboundQueueCapacity;
        --outboundCount_;
      }
    }
    // Failure keeps the same queue head and offset for retry.
    outboundChunkLength_ = 0;
    indicationAwaitingStatus_ = false;
  }

  Event event;
  while (popEvent(event)) {
    bool callbackConnected = false;
    uint32_t callbackGeneration = 0;
    snapshotCallbackConnection(callbackConnected, callbackGeneration);

    switch (event.type) {
      case EventType::Connected:
        if (!callbackConnected || event.generation != callbackGeneration) break;
        if (connected_ && event.generation == activeGeneration_) break;
        if (connected_) {
          connected_ = false;
          clearOutbound();
          if (connectionHandler_) connectionHandler_(false);
        }
        activeGeneration_ = event.generation;
        connected_ = true;
        log_i("BLE client connected (generation %lu)",
              static_cast<unsigned long>(activeGeneration_));
        if (connectionHandler_) connectionHandler_(true);
        break;
      case EventType::ReadyRequested:
        if (connected_ && callbackConnected && event.generation == activeGeneration_ &&
            event.generation == callbackGeneration) {
          sendRaw(readyJson_);
        }
        break;
      case EventType::Data: {
        if (!connected_ || !callbackConnected || event.generation != activeGeneration_ ||
            event.generation != callbackGeneration) {
          log_w("ignoring stale BLE data from generation %lu",
                static_cast<unsigned long>(event.generation));
          break;
        }
        protocol::Inbound message;
        if (!protocol::parseInbound(event.data, event.length, message)) {
          log_w("rejecting invalid atomic gameplay packet (%u bytes)", event.length);
        } else if (message.type == protocol::InboundType::Instruction && instructionHandler_) {
          instructionHandler_(message.session, message.round, message.action, message.timeoutMs);
        } else if (message.type == protocol::InboundType::GameStop && stopHandler_) {
          stopHandler_(message.session, message.reset);
        }
        break;
      }
    }
  }
  pumpIndication();
}

bool BleLink::sendRoundEnded(uint32_t session, uint8_t round, Action action, uint16_t elapsedMs) {
  uint8_t packet[protocol::kRoundEndedLength];
  protocol::encodeRoundEnded(session, round, action, elapsedMs, packet);
  return enqueueOutbound(packet, sizeof(packet), false);
}

bool BleLink::sendRaw(const String& json) {
  if (json.length() + 1 > kOutboundMessageCapacity) return false;
  uint8_t framed[kOutboundMessageCapacity];
  memcpy(framed, json.c_str(), json.length());
  framed[json.length()] = '\n';
  return enqueueOutbound(framed, json.length() + 1, true);
}

bool BleLink::enqueueOutbound(const uint8_t* data, size_t length, bool chunked) {
  if (!connected_ || length == 0 || length > kOutboundMessageCapacity ||
      (!chunked && length > kChunkSize) || outboundCount_ == kOutboundQueueCapacity) {
    log_w("outbound BLE queue unavailable");
    return false;
  }
  OutboundMessage& message = outbound_[outboundTail_];
  memcpy(message.data, data, length);
  message.length = static_cast<uint16_t>(length);
  message.offset = 0;
  message.chunked = chunked;
  outboundTail_ = (outboundTail_ + 1) % kOutboundQueueCapacity;
  ++outboundCount_;
  return true;
}

void BleLink::enqueueConnection(bool connected) {
  portENTER_CRITICAL(&eventMux_);
  if (!connected) {
    callbackConnected_ = false;
    disconnectGeneration_ = callbackGeneration_;
    disconnectPending_ = true;
    portEXIT_CRITICAL(&eventMux_);
    return;
  }

  ++callbackGeneration_;
  callbackConnected_ = true;
  Event event;
  event.type = EventType::Connected;
  event.generation = callbackGeneration_;
  if (eventCount_ == kEventCapacity) eventOverflow_ = true;
  else {
    events_[eventTail_] = event;
    eventTail_ = (eventTail_ + 1) % kEventCapacity;
    ++eventCount_;
  }
  portEXIT_CRITICAL(&eventMux_);
}

void BleLink::enqueueData(const uint8_t* data, size_t length) {
  Event event;
  event.type = EventType::Data;
  if (length <= sizeof(event.data)) {
    event.length = static_cast<uint8_t>(length);
    memcpy(event.data, data, length);
  } else {
    event.length = 0;  // loop() rejects oversized writes without partial parsing.
  }
  portENTER_CRITICAL(&eventMux_);
  event.generation = callbackGeneration_;
  if (eventCount_ == kEventCapacity) eventOverflow_ = true;
  else {
    events_[eventTail_] = event;
    eventTail_ = (eventTail_ + 1) % kEventCapacity;
    ++eventCount_;
  }
  portEXIT_CRITICAL(&eventMux_);
}

void BleLink::enqueueReadyRequested() {
  Event event;
  event.type = EventType::ReadyRequested;
  portENTER_CRITICAL(&eventMux_);
  event.generation = callbackGeneration_;
  if (eventCount_ == kEventCapacity) eventOverflow_ = true;
  else {
    events_[eventTail_] = event;
    eventTail_ = (eventTail_ + 1) % kEventCapacity;
    ++eventCount_;
  }
  portEXIT_CRITICAL(&eventMux_);
}

void BleLink::recordIndicationStatus(bool succeeded) {
  portENTER_CRITICAL(&eventMux_);
  // NimBLE 3.3.11 reports a confirmed indication, then its blocking wrapper can
  // report a timeout for the same send. Confirmation is terminal and must win.
  if (!indicationStatusPending_ || succeeded) indicationStatusSucceeded_ = succeeded;
  indicationStatusGeneration_ = callbackGeneration_;
  indicationStatusPending_ = true;
  portEXIT_CRITICAL(&eventMux_);
}

bool BleLink::popEvent(Event& event) {
  bool available = false;
  portENTER_CRITICAL(&eventMux_);
  if (eventCount_ > 0) {
    event = events_[eventHead_];
    eventHead_ = (eventHead_ + 1) % kEventCapacity;
    --eventCount_;
    available = true;
  }
  portEXIT_CRITICAL(&eventMux_);
  return available;
}

bool BleLink::takePendingDisconnect(uint32_t& generation) {
  bool pending = false;
  portENTER_CRITICAL(&eventMux_);
  if (disconnectPending_) {
    generation = disconnectGeneration_;
    disconnectPending_ = false;
    pending = true;
  }
  portEXIT_CRITICAL(&eventMux_);
  return pending;
}

bool BleLink::takeIndicationStatus(bool& succeeded, uint32_t& generation) {
  bool pending = false;
  portENTER_CRITICAL(&eventMux_);
  if (indicationStatusPending_) {
    succeeded = indicationStatusSucceeded_;
    generation = indicationStatusGeneration_;
    indicationStatusPending_ = false;
    pending = true;
  }
  portEXIT_CRITICAL(&eventMux_);
  return pending;
}

void BleLink::snapshotCallbackConnection(bool& connected, uint32_t& generation) {
  portENTER_CRITICAL(&eventMux_);
  connected = callbackConnected_;
  generation = callbackGeneration_;
  portEXIT_CRITICAL(&eventMux_);
}

void BleLink::clearOutbound() {
  outboundHead_ = 0;
  outboundTail_ = 0;
  outboundCount_ = 0;
  outboundChunkLength_ = 0;
  indicationAwaitingStatus_ = false;
}

void BleLink::pumpIndication() {
  if (!connected_ || indicationAwaitingStatus_ || outboundCount_ == 0) return;
  OutboundMessage& message = outbound_[outboundHead_];
  const size_t remaining = message.length - message.offset;
  outboundChunkLength_ = message.chunked ? min(kChunkSize, remaining) : remaining;
  indicationAwaitingStatus_ = true;
  tx_->setValue(message.data + message.offset, outboundChunkLength_);
  tx_->indicate();
}
