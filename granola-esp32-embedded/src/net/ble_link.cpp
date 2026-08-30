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
    if (subValue & 0x02) owner_->enqueueEvent(EventType::ReadyRequested);
  }
  void onStatus(BLECharacteristic*, Status status, uint32_t) override {
    owner_->enqueueEvent(status == SUCCESS_INDICATE
                             ? EventType::IndicationSucceeded
                             : EventType::IndicationFailed);
  }
 private:
  BleLink* owner_;
};

void BleLink::begin(const Config& config) {
  config_ = config;
  readyJson_ = protocol::encodeBoardReady(config_.boardId);
  inbound_.reserve(kInboundCapacity);

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
  if (overflowed) {
    log_w("BLE event queue overflow; dropped data");
  }

  Event event;
  while (popEvent(event)) {
    if (event.type == EventType::Connected) {
      connected_ = true;
      log_i("BLE client connected");
      if (connectionHandler_) connectionHandler_(true);
      continue;
    }
    if (event.type == EventType::Disconnected) {
      connected_ = false;
      outboundLength_ = 0;
      outboundOffset_ = 0;
      outboundChunkLength_ = 0;
      indicationAwaitingStatus_ = false;
      inbound_.clear();
      BLEDevice::startAdvertising();
      log_i("BLE client disconnected; advertising restarted");
      if (connectionHandler_) connectionHandler_(false);
      continue;
    }
    if (event.type == EventType::ReadyRequested) {
      if (connected_) sendRaw(readyJson_);
      continue;
    }
    if (event.type == EventType::IndicationSucceeded) {
      if (connected_ && indicationAwaitingStatus_) {
        outboundOffset_ += outboundChunkLength_;
        if (outboundOffset_ == outboundLength_) {
          outboundOffset_ = 0;
          outboundLength_ = 0;
        }
      }
      outboundChunkLength_ = 0;
      indicationAwaitingStatus_ = false;
      continue;
    }
    if (event.type == EventType::IndicationFailed) {
      outboundChunkLength_ = 0;
      indicationAwaitingStatus_ = false;
      continue;
    }

    for (size_t i = 0; i < event.length; ++i) {
      const char byte = static_cast<char>(event.data[i]);
      if (byte == '\n') {
        if (!inbound_.isEmpty()) {
          handleLine(reinterpret_cast<const uint8_t*>(inbound_.c_str()), inbound_.length());
          inbound_.clear();
        }
      } else if (inbound_.length() < kInboundCapacity) {
        inbound_ += byte;
      } else {
        log_w("BLE JSON line exceeds %u bytes; dropping", static_cast<unsigned>(kInboundCapacity));
        inbound_.clear();
      }
    }
  }
  pumpIndication();
}

bool BleLink::sendActionDetected(const char* roundId, Action action, uint32_t elapsedMs) {
  return sendRaw(protocol::encodeActionDetected(roundId, action, elapsedMs));
}

bool BleLink::sendRaw(const String& json) {
  if (!connected_) {
    log_w("dropping outbound BLE message, link down");
    return false;
  }
  const size_t length = json.length() + 1;
  if (outboundOffset_ < outboundLength_ || length > kOutboundCapacity) {
    log_w("dropping outbound BLE message, indication queue busy or oversized");
    return false;
  }
  memcpy(outbound_, json.c_str(), json.length());
  outbound_[json.length()] = '\n';
  outboundLength_ = length;
  outboundOffset_ = 0;
  return true;
}

void BleLink::enqueueConnection(bool connected) {
  enqueueEvent(connected ? EventType::Connected : EventType::Disconnected);
}

void BleLink::enqueueEvent(EventType type) {
  Event event;
  event.type = type;
  portENTER_CRITICAL(&eventMux_);
  if (eventCount_ == kEventCapacity) eventOverflow_ = true;
  else {
    events_[eventTail_] = event;
    eventTail_ = (eventTail_ + 1) % kEventCapacity;
    ++eventCount_;
  }
  portEXIT_CRITICAL(&eventMux_);
}

void BleLink::enqueueData(const uint8_t* data, size_t length) {
  while (length > 0) {
    Event event;
    event.type = EventType::Data;
    event.length = static_cast<uint8_t>(min(length, kChunkSize));
    memcpy(event.data, data, event.length);
    portENTER_CRITICAL(&eventMux_);
    if (eventCount_ == kEventCapacity) eventOverflow_ = true;
    else {
      events_[eventTail_] = event;
      eventTail_ = (eventTail_ + 1) % kEventCapacity;
      ++eventCount_;
    }
    portEXIT_CRITICAL(&eventMux_);
    data += event.length;
    length -= event.length;
  }
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

void BleLink::handleLine(const uint8_t* data, size_t length) {
  protocol::Inbound message;
  if (!protocol::parseInbound(data, length, message)) return;
  if (message.type == protocol::InboundType::Instruction) {
    if (message.action == Action::None) {
      log_w("instruction %s names an unsupported action", message.roundId);
    } else if (instructionHandler_) {
      instructionHandler_(message.roundId, message.action, message.timeoutMs);
    }
  } else if (message.type == protocol::InboundType::RoundResult && roundResultHandler_) {
    roundResultHandler_(message.roundId, message.outcome, message.score);
  } else if (message.type == protocol::InboundType::GameStop && stopHandler_) {
    stopHandler_(message.reset);
  }
}

void BleLink::pumpIndication() {
  if (!connected_ || indicationAwaitingStatus_ || outboundOffset_ >= outboundLength_) return;
  outboundChunkLength_ = min(kChunkSize, outboundLength_ - outboundOffset_);
  indicationAwaitingStatus_ = true;
  tx_->setValue(outbound_ + outboundOffset_, outboundChunkLength_);
  tx_->indicate();
}
