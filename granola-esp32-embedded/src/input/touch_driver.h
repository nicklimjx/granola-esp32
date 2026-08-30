#pragma once
//
// Minimal capacitive touch driver for the AMOLED-1.8's two board revisions.
//
// Rev 1 fits an FT3168 (FocalTech, I2C 0x38), rev 2 a CST816/CST820 (Hynitron,
// I2C 0x15). Their first-point register layout is the same:
//
//   0x02  number of touch points (low nibble)
//   0x03  X high nibble
//   0x04  X low byte
//   0x05  Y high nibble
//   0x06  Y low byte
//
// so one 5-byte burst read starting at 0x02 works on both. The address that
// answers also tells us which display controller is fitted, which ui.cpp uses
// to pick between SH8601 and CO5300.
//

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

class TouchDriver {
 public:
  static constexpr uint8_t kAddrFt3168 = 0x38;
  static constexpr uint8_t kAddrCst816 = 0x15;

  // Probes both addresses on an already-initialised bus. Returns false if
  // neither answers.
  bool begin(TwoWire& bus);

  // 0 when begin() failed.
  uint8_t address() const { return address_; }

  // Reads the first touch point. `pressed` is false when no finger is down;
  // x/y are only meaningful when it is true. Returns false on a bus error.
  bool read(bool& pressed, int16_t& x, int16_t& y);

 private:
  TwoWire* bus_ = nullptr;
  uint8_t address_ = 0;
};
