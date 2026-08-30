# WebSocket protocol

The ESP32 opens a WebSocket connection to the local Go server. Messages are JSON.

## Run the Stage 1 server

```sh
cd websocket
go run .
```

The server listens on `:8080`; boards connect to `ws://<server-ip>:8080/board`. Open `http://<server-ip>:8080/` for the browser lobby. Its `/dashboard` WebSocket receives full state snapshots and starts the game with `{"type":"game.start"}`. `GET /state` and `POST /start` remain available for simple clients and tests.

## Messages

### `board.ready`: board to server

Sent once the socket is open and the board hardware is initialized.

```json
{"type":"board.ready","protocolVersion":1,"boardId":"bopit-01","supportedActions":["tap","twist","swipe","press"]}
```

`Action` is a string rather than a closed protocol enum. The server must only request values advertised in `supportedActions`.

### `instruction`: server to board

Starts one round.

```json
{"type":"instruction","roundId":"12","action":"tap","timeoutMs":1500}
```

### `action.detected`: board to server

Reports the first action detected during the active round. `elapsedMs` is measured by the board from receipt of the instruction using its monotonic clock.

```json
{"type":"action.detected","roundId":"12","action":"tap","elapsedMs":640}
```

### `round.result`: server to board

Ends the round and provides the authoritative score.

```json
{"type":"round.result","roundId":"12","result":"success","score":5}
```

`result` is one of `success`, `wrong_action`, or `timeout`.

## Rules

- Each WebSocket belongs to the `boardId` sent in `board.ready`.
- Before start, connected boards wait in the lobby and appear on the dashboard.
- A dashboard `game.start` command (or `POST /start`) locks the connected roster and starts each connected board's game.
- A game has 60 steps. Each step randomly selects one of that board's advertised actions. The timeout is 2000, 1700, 1400, 1200, 1000, then 800 ms for successive groups of 10 steps.
- A successful step increments the board's score. Every result immediately starts the next step; the final result ends the game.
- Dashboard clients receive a full state snapshot when board connection, instruction, result, or score state changes.
- Locked disconnected boards remain on the dashboard. Boards that first connect after start are excluded and receive no round.
- A new connection with the same `boardId` replaces and closes the older connection. A locked board may reconnect and starts again at step 1 with its saved score.
- Round IDs are the step numbers `1` through `60` within one board connection.
- Only the first detected action counts.
- The server ignores actions for unknown or completed round IDs.
- The server only sends actions advertised by the board.
- The server owns round results and score.
- A disconnected board loses its active game. Before start it returns to waiting; after start a locked board starts again at step 1 when it reconnects.
- Invalid JSON, an unsupported protocol version, an empty supported-action list, or an omitted, null, or negative `elapsedMs` closes the socket.
- The no-response watchdog starts after each instruction is written and allows that step's timeout plus 250 ms of network grace.
- WebSocket open/close events represent connection state; no connection message or custom heartbeat is required.
