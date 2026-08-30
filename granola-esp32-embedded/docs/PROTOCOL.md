# Board/browser BLE protocol

Protocol version 1 keeps the existing JSON message shapes while replacing each
WebSocket frame with a newline-delimited byte stream over Nordic UART BLE.

## GATT service

| Role | UUID | Properties |
| --- | --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | - |
| RX, browser to board | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | write |
| TX, board to browser | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | indicate |

Every JSON object ends in `\n`; receivers buffer arbitrary chunks until that
delimiter. Browser writes and firmware indications are sequential chunks of at
most 20 bytes. Subscribing to TX enqueues `board.ready`; each indicated chunk is
advanced only after the browser acknowledges it.

BLE stack callbacks only copy connection/data events into `BleLink`'s bounded
queue. `BleLink::loop()` changes connection state, parses JSON, dispatches game
handlers, sends indications, and restarts advertising after disconnect.

## Board to browser

### `board.ready`

```json
{"type":"board.ready","protocolVersion":1,"boardId":"bopit-01","supportedActions":["tap","swipe","press"]}
```

### `action.detected`

The first action in an active window is reported. `elapsedMs` is measured with
`millis()` from instruction receipt. Nothing is sent when no action occurs.

```json
{"type":"action.detected","roundId":"12","action":"twist","elapsedMs":640}
```

## Browser to board

### `instruction`

```json
{"type":"instruction","roundId":"1:12","action":"tap","timeoutMs":3400}
```

The board rejects unsupported actions and unusable round IDs. A missing/zero
`timeoutMs` uses the firmware's matching fallback table.

### `round.result`

```json
{"type":"round.result","roundId":"12","result":"success","score":5}
```

Results are `success`, `wrong_action`, or `timeout`. The board renders the
browser-owned score and ignores results for a different round.

### `game.stop`

```json
{"type":"game.stop","reset":false}
```

The board immediately abandons any active round and returns to its connected
idle UI without rendering a timeout. The optional `reset` defaults to `false`,
which preserves score and fallback-tier progress. Restart sends `reset:true`
before its first instruction, clearing both values so that prompt shows zero.

## Round lifecycle

```text
instruction -> Awaiting -> action.detected or local window close
            -> AwaitingResult -> round.result -> Feedback (350 ms) -> Idle
```

The browser chooses one advertised action for each of 60 steps. Current
firmware advertises and accepts tap, swipe, and press. The Twist enum and
detector remain implemented, but wire parsing rejects Twist while unadvertised. Ten-step tiers use
4000/3400/2800/2400/2000/1600 ms. Its watchdog resolves no-response rounds
after the window plus 250 ms grace, waits 350 ms after every result, and
completes after round 60. Stop preserves score; restart serializes a resetting
stop before its first instruction, resets score, step, and fallback tier, uses
fresh opaque round IDs, and invalidates stale browser timers. A disconnect
abandons the active round.

## Browser constraint

Selection must originate from a user gesture in a Web Bluetooth-capable
Chromium browser. Serve the dashboard from `http://localhost`; arbitrary HTTP
LAN origins are not secure contexts and cannot use `navigator.bluetooth`.
