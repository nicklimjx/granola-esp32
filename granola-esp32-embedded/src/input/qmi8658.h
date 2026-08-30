#pragma once
//
// Minimal QMI8658 (6-axis IMU) driver — just enough to stream gyroscope rates,
// which is all the twist detector needs. Register map per the QST QMI8658A
// datasheet; the board wires it to the shared I2C bus.
//
// Kept in-tree rather than pulling in a full sensor library so the twist path
// has no dependency that can shift under it.
//

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

class Qmi8658 {
 public:
  // AD0 low on this board, so 0x6B. 0x6A is the alternate address.
  static constexpr uint8_t kAddrLow = 0x6B;
  static constexpr uint8_t kAddrHigh = 0x6A;
  static constexpr uint8_t kWhoAmIValue = 0x05;

  // Enables the gyroscope at +/-512 dps, 224 Hz, and (for completeness) the
  // accelerometer at +/-8 g. Returns false if WHO_AM_I does not answer.
  bool begin(TwoWire& bus);

  bool isPresent() const { return address_ != 0; }

  // Rotation rates in degrees/second, bias-corrected.
  bool readGyro(float& x, float& y, float& z);

  // Averages samples at rest to measure the zero-rate offset. The board must be
  // still while this runs. Safe to call again later.
  void calibrateGyroBias(uint16_t samples = 128);

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool readRegs(uint8_t reg, uint8_t* out, size_t count);
  bool readGyroRaw(float& x, float& y, float& z);

  TwoWire* bus_ = nullptr;
  uint8_t address_ = 0;
  float gyroScaleDps_ = 512.0f / 32768.0f;
  float biasX_ = 0.0f;
  float biasY_ = 0.0f;
  float biasZ_ = 0.0f;
};
