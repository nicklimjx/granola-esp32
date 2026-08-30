#pragma once
//
// Per-board hardware description. Everything board-specific lives here so that
// src/game and src/net stay portable across the two boards.
//
// Pin values below are taken from Waveshare's own example pin_config.h
// (waveshareteam/ESP32-S3-Touch-AMOLED-1.8, examples/arduino*/libraries/Mylibrary)
// and the board wiki.
//

#include <stdint.h>

#if defined(BOARD_AMOLED_18)

#define BOARD_NAME "ESP32-S3-Touch-AMOLED-1.8"
// Opaque identity string sent in board.ready.
#define BOARD_ID "bopit-01"

// ---- Display: 368x448 AMOLED on QSPI --------------------------------------
// Rev 1 boards use an SH8601 + FT3168; rev 2 boards use a CO5300 + CST816.
// The two revisions share this pin map, so the driver is chosen at runtime by
// probing the touch controller's I2C address (see src/ui/ui.cpp).
#define LCD_QSPI_CS 12
#define LCD_QSPI_SCK 11
#define LCD_QSPI_D0 4
#define LCD_QSPI_D1 5
#define LCD_QSPI_D2 6
#define LCD_QSPI_D3 7
#define LCD_WIDTH 368
#define LCD_HEIGHT 448
#define LCD_BRIGHTNESS 200

// ---- Shared I2C bus: touch, QMI8658 IMU, AXP2101 PMU, PCF85063 RTC --------
#define I2C_SDA_PIN 15
#define I2C_SCL_PIN 14
#define I2C_FREQ_HZ 400000
#define TOUCH_INT_PIN 21

// ---- "press-it" button ----------------------------------------------------
// The board has two side buttons: BOOT (GPIO0, active low, free to poll at
// runtime) and PWR (only readable through the IO expander's EXIO4, active
// high, and a 6 s hold powers the board off). BOOT is the safe choice.
#define PRESS_BUTTON_PIN 0
#define PRESS_BUTTON_ACTIVE_LOW 1

// ---- IMU orientation -----------------------------------------------------
// "Twist" is rotation about the axis normal to the screen. On this board that
// is the QMI8658's Z axis. Change to 0 (X) or 1 (Y) if bench testing shows the
// IMU is mounted differently on your revision.
#define TWIST_GYRO_AXIS 2

#elif defined(BOARD_LCD_146)

#define BOARD_NAME "ESP32-S3-Touch-LCD-1.46"
#define BOARD_ID "bopit-02"

#error "The ESP32-S3-Touch-LCD-1.46 pin map is not filled in yet. \
Take the values from Waveshare's example pin_config.h for that board \
(display SPI/QSPI pins, I2C SDA/SCL, touch INT, button GPIO, panel size), \
add a UI implementation for its round 412x412 LCD, and enable the lcd146 \
env in platformio.ini."

#else
#error "No board selected. Build with -D BOARD_AMOLED_18 (see platformio.ini)."
#endif
