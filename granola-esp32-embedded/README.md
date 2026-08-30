# Granola bop-it — board firmware

Firmware for the physical half of a bop-it game. A laptop hosts a WebSocket
server and the GUI/scoreboard (implemented separately); the boards detect what
the player did and time each round.

Currently implemented: **single player on the Waveshare
ESP32-S3-Touch-AMOLED-1.8** (board ID 1). The ESP32-S3-Touch-LCD-1.46 (board
ID 2) is scaffolded for the later multiplayer mode but not built out.

## The four actions

These are the strings the board advertises in `board.ready.supportedActions`;
the server may only request these.

| Action | Wire name | Detected by |
| --- | --- | --- |
| Bop it | `tap` | screen tap (FT3168 / CST816 touch) |
| Twist it | `twist` | rotation about the screen-normal axis (QMI8658 gyro) |
| Swipe it | `swipe` | screen swipe |
| Press it | `press` | BOOT side button (GPIO0) |

## Getting started

1. Copy the settings template and fill in your network and server details:

   ```sh
   cp include/secrets.example.h include/secrets.h
   ```

   `WS_HOST` must be the laptop's LAN IP — `.local` names are not resolved.

2. Build and flash:

   ```sh
   pio run -e amoled18 -t upload -t monitor
   ```

The first build downloads the Arduino-ESP32 3.x toolchain via the pioarduino
platform fork (~250 MB, a few minutes). See the comment in `platformio.ini` for
why the official PlatformIO platform is not used.

## Protocol

The server owns the protocol (version 1). The board sends `board.ready` and
`action.detected`; it receives `instruction` and `round.result`, and displays
the score and verdict the server sends rather than computing them.

[`docs/PROTOCOL.md`](docs/PROTOCOL.md) records the board-side implementation and
the three things that still need agreeing with the server side — action names,
the absence of a twist/swipe direction, and the fact that the difficulty ramp
(2000 ms down to 800 ms) now has to live in the Go server because the board only
sees an opaque `roundId`.

## Layout

```
include/
  board_config.h     pin map and board ID, one block per board
  app_config.h       game timing and gesture thresholds
  secrets.example.h  template for Wi-Fi / server settings
src/
  main.cpp           wiring only
  net/
    wifi_link.*      Wi-Fi lifecycle
    ws_link.*        the WebSocket interface: socket, reconnect, callbacks
    protocol.*       JSON codec for every message in docs/PROTOCOL.md
  game/
    actions.*        the four actions, their wire names, and round outcomes
    game.*           round state machine and the action-window clock
  input/
    input_manager.*  the three devices reduced to one queue of actions
    touch_driver.*   FT3168 / CST816 over I2C
    qmi8658.*        gyroscope
    twist_detector.* gyro samples -> "twist it"
    button_input.*   debounced BOOT button
  ui/
    ui.*             prompt, countdown bar, and the server's verdict and score
```

`src/net` and `src/game` contain no board-specific code, so the second board
only needs a pin map in `board_config.h` and a UI for its panel.

## Tuning on hardware

The gesture thresholds in `include/app_config.h` are starting points. Build with
`-D CORE_DEBUG_LEVEL=4` to log every recognised input and every rejected twist
candidate with its accumulated angle and axis dominance, which is the fastest
way to dial in `kTwistAngleDeg`, `kTwistGateDps` and `kSwipeMinTravelPx`.

If twists are not detected at all, the IMU is likely mounted on a different
axis than assumed — change `TWIST_GYRO_AXIS` in `include/board_config.h`.

## Board revisions

The AMOLED-1.8 ships in two revisions with the same pinout but different
controllers: SH8601 + FT3168 (rev 1) and CO5300 + CST816 (rev 2). The firmware
probes the touch controller's I2C address at boot and picks the matching display
driver, so one binary runs on either.
