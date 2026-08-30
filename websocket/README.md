# Browser Bluetooth dashboard

The Go program is only a static localhost server. The Chromium page connects
directly to one ESP32 with Web Bluetooth and owns the complete 60-step game.
There are no WebSocket or Wi-Fi endpoints.

```sh
cd websocket
go run .
# open http://localhost:8080
```

Web Bluetooth requires a compatible Chromium browser and a secure context;
`http://localhost` is treated as secure. The board picker must be opened by the
**Connect board** button gesture. Other browsers and opening the page through a
LAN IP are not supported.

## BLE transport

The board advertises the Nordic UART service:

- Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX, browser writes: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- TX, board indicates: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

Each message is one JSON object followed by `\n`. Both directions preserve
stream framing across BLE packets. Browser writes and board indications are
split into sequential chunks of at most 20 bytes. Subscribing to TX requests a
`board.ready` indication, so readiness cannot be lost during connection setup.

## Semantic messages

- `board.ready`: board to browser
- `action.detected`: board to browser
- `instruction`: browser to board
- `round.result`: browser to board
- `game.stop`: browser to board

Protocol version remains 1. The browser only chooses actions from
`board.ready.supportedActions`; current firmware advertises tap, swipe, and
press while Twist remains implemented but temporarily unavailable.

## Game rules

The browser starts one board for exactly 60 sequentially numbered steps. It
randomly chooses an advertised action. Groups of ten use 4000, 3400, 2800,
2400, 2000, then 1600 ms windows. A correct in-window action increments score;
a wrong action or late/no action does not. The dashboard shows the last verdict
and reaction time when the board reported one. The no-response watchdog adds
250 ms transport grace, and each result remains visible for 350 ms before the
next instruction. Stop sends `game.stop` with `reset:false` and preserves the
displayed score. Restart serializes `game.stop` with `reset:true` before the
first instruction, resetting browser and firmware score/tier state for a fresh
60-step run; it is also available after completion.
