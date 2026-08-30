# WebSocket protocol

The ESP32 opens a WebSocket connection to the local Go server. Messages are JSON.

## Messages

### `board.ready`: board to server

Sent once the socket is open and the board hardware is initialized.

```json
{"type":"board.ready","protocolVersion":1,"boardId":"bopit-01","supportedActions":["tap","twist_left","shake"]}
```

`Action` is a string rather than a closed protocol enum. The server must only request values advertised in `supportedActions`.

### `instruction`: server to board

Starts one round.

```json
{"type":"instruction","roundId":"12","action":"twist_left","timeoutMs":1800}
```

### `action.detected`: board to server

Reports the first action detected during the active round. `elapsedMs` is measured by the board from receipt of the instruction using its monotonic clock.

```json
{"type":"action.detected","roundId":"12","action":"twist_left","elapsedMs":640}
```

### `round.result`: server to board

Ends the round and provides the authoritative score.

```json
{"type":"round.result","roundId":"12","result":"success","score":5}
```

`result` is one of `success`, `wrong_action`, or `timeout`.

## Rules

- One active round per board.
- Only the first detected action counts.
- The server ignores actions for unknown or completed round IDs.
- The server only sends actions advertised by the board.
- The server owns round results and score.
- A disconnected board loses its active round and starts idle after reconnecting.
- Invalid JSON or an unsupported protocol version is logged and the socket is closed.
- WebSocket open/close events represent connection state; no connection message or custom heartbeat is required.
