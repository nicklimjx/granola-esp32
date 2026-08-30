#include "touch_driver.h"

namespace {

bool probe(TwoWire& bus, uint8_t address) {
  bus.beginTransmission(address);
  return bus.endTransmission() == 0;
}

}  // namespace

bool TouchDriver::begin(TwoWire& bus) {
  bus_ = &bus;
  address_ = 0;

  for (const uint8_t candidate : {kAddrFt3168, kAddrCst816}) {
    if (probe(bus, candidate)) {
      address_ = candidate;
      log_i("touch controller at 0x%02X (%s)", address_,
            address_ == kAddrFt3168 ? "FT3168" : "CST816");
      return true;
    }
  }

  log_e("no touch controller found on I2C");
  return false;
}

bool TouchDriver::read(bool& pressed, int16_t& x, int16_t& y) {
  pressed = false;
  if (address_ == 0 || bus_ == nullptr) {
    return false;
  }

  bus_->beginTransmission(address_);
  bus_->write(static_cast<uint8_t>(0x02));
  if (bus_->endTransmission(false) != 0) {
    return false;
  }

  uint8_t buf[5] = {0};
  if (bus_->requestFrom(address_, sizeof(buf)) != sizeof(buf)) {
    return false;
  }
  for (uint8_t& byte : buf) {
    byte = static_cast<uint8_t>(bus_->read());
  }

  const uint8_t points = buf[0] & 0x0F;
  if (points == 0) {
    return true;
  }

  pressed = true;
  x = static_cast<int16_t>(((buf[1] & 0x0F) << 8) | buf[2]);
  y = static_cast<int16_t>(((buf[3] & 0x0F) << 8) | buf[4]);
  return true;
}
