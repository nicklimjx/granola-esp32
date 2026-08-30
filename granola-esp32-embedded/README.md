# Granola bop-it - board firmware

Firmware for one Waveshare ESP32-S3-Touch-AMOLED-1.8 bop-it board. A Chromium
page connects directly through Web Bluetooth, chooses actions, and owns score
and game timing. The firmware needs no Wi-Fi credentials or intermediary
WebSocket server.

## Actions

| Prompt | Wire name | Detected by |
| --- | --- | --- |
| Bop it | `tap` | screen tap |
| Twist it | `twist` | QMI8658 gyroscope (implemented, temporarily unadvertised) |
| Swipe it | `swipe` | screen swipe |
| Press it | `press` | BOOT side button (GPIO0) |

## Build and use

```sh
pio run -e amoled18 -t upload -t monitor
cd ../websocket && go run .
```

Open `http://localhost:8080` in a Web Bluetooth-capable Chromium browser, click
**Connect board**, select **Granola Bop-It**, then start. Web Bluetooth requires
a secure context; Chromium treats localhost as secure. A LAN-IP HTTP URL and
non-supporting browsers do not work.

Arduino-ESP32 3.3.x supplies the BLE API. The first PlatformIO build downloads
the pioarduino toolchain described in `platformio.ini`.

## Protocol and layout

The board is a Nordic UART BLE GATT server. JSON semantics remain protocol v1;
objects are newline-framed and transported in conservative 20-byte chunks.
The browser sends `game.stop` with `reset:false` to preserve score or
`reset:true` before restart to clear score and fallback-tier progress.
See [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

```
include/
  board_config.h     pins and BOARD_ID
  app_config.h       game fallback and input thresholds
src/
  main.cpp           hardware/game/BLE wiring
  net/
    ble_link.*        GATT server, event queues, framing and indications
    protocol.*        JSON codec
  game/               existing board round state and action names
  input/              existing hardware detectors
  ui/                 existing panel UI
```

BLE callbacks enqueue connection, subscription, status, and data events.
`BleLink::loop()` performs JSON dispatch and indication retries, so callbacks
never enter `Game` or `Ui`. Advertising restarts after disconnect.

## Hardware tuning

Gesture thresholds remain in `include/app_config.h`. Build with
`-D CORE_DEBUG_LEVEL=4` for detector diagnostics. If twist detection fails,
check `TWIST_GYRO_AXIS` in `include/board_config.h`.
