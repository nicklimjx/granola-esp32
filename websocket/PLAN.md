# Direct Web Bluetooth implementation

The bounded one-board vertical slice is:

```text
Chromium on http://localhost:8080
  -> user clicks Connect board
  -> browser selects Nordic UART BLE service
  -> TX read yields newline-framed board.ready
  -> user starts the 60-step game
  -> browser writes instruction and round.result to RX
  -> board notifies action.detected on TX
```

## Ownership

- Go embeds and serves `dashboard.html`, `app.js`, and `game.mjs` only.
- `game.mjs` owns deterministic transitions, score, timeout tiers, watchdog
  grace, feedback hold, and the 60-step stop condition.
- `app.js` owns Web Bluetooth selection, GATT connection, newline framing,
  sequential 20-byte writes, timers, and tile rendering.
- Firmware owns BLE advertising, queued callback events, framing, hardware
  input, elapsed-time measurement, and panel feedback.

No database, accounts, multiplayer roster, frontend framework, package
manifest, Wi-Fi, WebSocket endpoint, or reconnect/resume game is included.
A disconnect abandons the active board round; the page can select a board again.

## Verification

- `go test ./...`
- `node --test game.test.mjs`
- `pio run -e amoled18`
- Browser and physical-board smoke test in Chromium on localhost
