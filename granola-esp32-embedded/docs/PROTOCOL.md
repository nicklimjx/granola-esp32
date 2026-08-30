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
{"type":"board.ready","protocolVersion":1,"boardId":"bopit-01","supportedActions":["tap","twist","swipe","press"]}
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
{"type":"instruction","roundId":"12","action":"tap","timeoutMs":1700}
```

The board rejects unsupported actions and unusable round IDs. A missing/zero
`timeoutMs` uses the firmware's matching fallback table.

### `round.result`

```json
{"type":"round.result","roundId":"12","result":"success","score":5}
```

Results are `success`, `wrong_action`, or `timeout`. The board renders the
browser-owned score and ignores results for a different round.

## Round lifecycle

```text
instruction -> Awaiting -> action.detected or local window close
            -> AwaitingResult -> round.result -> Feedback (350 ms) -> Idle
```

The browser chooses one advertised action for each of 60 steps. Ten-step tiers
use 2000/1700/1400/1200/1000/800 ms. Its watchdog resolves no-response rounds
after the window plus 250 ms grace, waits 350 ms after every result, and stops
after round 60. A disconnect abandons the active round.

## Browser constraint

Selection must originate from a user gesture in a Web Bluetooth-capable
Chromium browser. Serve the dashboard from `http://localhost`; arbitrary HTTP
LAN origins are not secure contexts and cannot use `navigator.bluetooth`.
