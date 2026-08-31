# Granola bop-it - board firmware

Firmware for one Waveshare ESP32-S3-Touch-AMOLED-1.8 bop-it board. A Chromium
page connects directly through Web Bluetooth, chooses actions, owns score and
sequencing, and plays prompt audio. The board detects and times actions and
shows immediate local feedback. It needs no Wi-Fi credentials or intermediary
WebSocket server.

## Actions

| Prompt | Wire name/code | Detected by |
| --- | --- | --- |
| Bop it | `tap` / 1 | screen tap |
| Twist it | `twist` / 2 | QMI8658 gyroscope, implemented but temporarily disabled |
| Swipe it | `swipe` / 3 | screen swipe |
| Press it | `press` / 4 | BOOT side button (GPIO0) |

Current `board.ready.supportedActions` advertises tap, swipe, and press. The
instruction parser rejects Twist while it is absent from that list.

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

Protocol v2 uses a mixed transport:

- `board.ready` is newline-framed JSON and may be split into 20-byte indications.
- Each browser instruction is one exact 9-byte GATT write.
- Each board round event is one exact 9-byte indication.
- Each browser stop is one exact 6-byte GATT write.
- There is no `round.result` packet.

Multibyte fields are little-endian. Session is the browser's u32 `runId`; round
is 1 through 60. The board accepts only round 1 without a session, then the
exact next round for that same session from feedback. Stop or disconnect clears
the session so a fresh round 1 can start. See
[`docs/PROTOCOL.md`](docs/PROTOCOL.md) for byte layouts and compatibility rules.

The browser is the only scoreboard. The board shows prompt/countdown and then
retains its local verdict until the next instruction.

```
include/
  board_config.h     pins and BOARD_ID
  app_config.h       firmware version and input thresholds
src/
  main.cpp           hardware/game/BLE wiring
  net/
    ble_link.*        GATT server, event/FIFO queues and indications
    protocol.*        v2 binary codec and board.ready JSON encoder
  game/               board round state and action names/codes
  input/              hardware detectors
  ui/                 prompt, countdown and local feedback
```

BLE callbacks enqueue connection, subscription, status, and complete-write
events. `BleLink::loop()` validates gameplay packets and retries indications, so
callbacks never enter `Game` or `Ui`. Advertising restarts after disconnect.

## Hardware tuning

Gesture thresholds remain in `include/app_config.h`. Build with
`-D CORE_DEBUG_LEVEL=4` for detector diagnostics. If twist detection fails,
check `TWIST_GYRO_AXIS` in `include/board_config.h` after Twist is re-enabled.
