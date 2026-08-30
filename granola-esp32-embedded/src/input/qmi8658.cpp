#include "qmi8658.h"

namespace {

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;  // accelerometer range + ODR
constexpr uint8_t kRegCtrl3 = 0x04;  // gyroscope range + ODR
constexpr uint8_t kRegCtrl5 = 0x06;  // low-pass filters
constexpr uint8_t kRegCtrl7 = 0x08;  // sensor enable
constexpr uint8_t kRegGyroX = 0x3B;

constexpr uint8_t kCtrl1AddrAutoIncrement = 0x40;
constexpr uint8_t kCtrl2Accel8g250Hz = 0x25;   // range 8 g (2 << 4) | ODR 250 Hz (5)
constexpr uint8_t kCtrl3Gyro512dps = 0x55;     // range 512 dps (5<<4) | ODR 224.2 Hz (5)
constexpr uint8_t kCtrl5LpfEnable = 0x11;      // accel + gyro LPF on, mode 0
constexpr uint8_t kCtrl7EnableAccelGyro = 0x03;

constexpr float kGyroFullScaleDps = 512.0f;

}  // namespace

bool Qmi8658::begin(TwoWire& bus) {
  bus_ = &bus;
  address_ = 0;

  for (const uint8_t candidate : {kAddrLow, kAddrHigh}) {
    address_ = candidate;
    uint8_t who = 0;
    if (readRegs(kRegWhoAmI, &who, 1) && who == kWhoAmIValue) {
      break;
    }
    address_ = 0;
  }

  if (address_ == 0) {
    log_e("QMI8658 not found on I2C");
    return false;
  }

  gyroScaleDps_ = kGyroFullScaleDps / 32768.0f;

  const bool ok = writeReg(kRegCtrl1, kCtrl1AddrAutoIncrement) &&
                  writeReg(kRegCtrl2, kCtrl2Accel8g250Hz) &&
                  writeReg(kRegCtrl3, kCtrl3Gyro512dps) && writeReg(kRegCtrl5, kCtrl5LpfEnable) &&
                  writeReg(kRegCtrl7, kCtrl7EnableAccelGyro);
  if (!ok) {
    log_e("QMI8658 configuration failed");
    address_ = 0;
    return false;
  }

  // Give the gyro time to spin up before the bias measurement.
  delay(60);
  log_i("QMI8658 ready at 0x%02X", address_);
  return true;
}

void Qmi8658::calibrateGyroBias(uint16_t samples) {
  if (address_ == 0 || samples == 0) {
    return;
  }

  double sumX = 0.0;
  double sumY = 0.0;
  double sumZ = 0.0;
  uint16_t taken = 0;

  for (uint16_t i = 0; i < samples; ++i) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (readGyroRaw(x, y, z)) {
      sumX += x;
      sumY += y;
      sumZ += z;
      ++taken;
    }
    delay(5);
  }

  if (taken == 0) {
    return;
  }

  biasX_ = static_cast<float>(sumX / taken);
  biasY_ = static_cast<float>(sumY / taken);
  biasZ_ = static_cast<float>(sumZ / taken);
  log_i("gyro bias dps: x=%.2f y=%.2f z=%.2f", biasX_, biasY_, biasZ_);
}

bool Qmi8658::readGyro(float& x, float& y, float& z) {
  if (!readGyroRaw(x, y, z)) {
    return false;
  }
  x -= biasX_;
  y -= biasY_;
  z -= biasZ_;
  return true;
}

bool Qmi8658::readGyroRaw(float& x, float& y, float& z) {
  uint8_t buf[6] = {0};
  if (!readRegs(kRegGyroX, buf, sizeof(buf))) {
    return false;
  }

  const int16_t rawX = static_cast<int16_t>((static_cast<uint16_t>(buf[1]) << 8) | buf[0]);
  const int16_t rawY = static_cast<int16_t>((static_cast<uint16_t>(buf[3]) << 8) | buf[2]);
  const int16_t rawZ = static_cast<int16_t>((static_cast<uint16_t>(buf[5]) << 8) | buf[4]);

  x = rawX * gyroScaleDps_;
  y = rawY * gyroScaleDps_;
  z = rawZ * gyroScaleDps_;
  return true;
}

bool Qmi8658::writeReg(uint8_t reg, uint8_t value) {
  if (bus_ == nullptr || address_ == 0) {
    return false;
  }
  bus_->beginTransmission(address_);
  bus_->write(reg);
  bus_->write(value);
  return bus_->endTransmission() == 0;
}

bool Qmi8658::readRegs(uint8_t reg, uint8_t* out, size_t count) {
  if (bus_ == nullptr || address_ == 0) {
    return false;
  }
  bus_->beginTransmission(address_);
  bus_->write(reg);
  if (bus_->endTransmission(false) != 0) {
    return false;
  }
  if (bus_->requestFrom(address_, count) != count) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    out[i] = static_cast<uint8_t>(bus_->read());
  }
  return true;
}
