# Minimal implementation plan

## 1. Prove the protocol with the Go server

**Stage 1 implemented:** one `tap` round per connection, with board-reported elapsed-time validation and a no-response watchdog.

Add a small Go server that:

- Exposes a `GET /board` WebSocket endpoint.
- Accepts board connections.
- Receives `board.ready` and stores each connection by `boardId`.
- Sends a hard-coded `instruction`.
- Receives `action.detected`.
- Validates the round ID, action, and timeout.
- Sends `round.result`.
- Keeps one in-memory game state per board.
- Closes the older socket when the same `boardId` reconnects.

No database, authentication, shared multiplayer game, or mid-round reconnect recovery.

## 2. Prove ESP32 communication

The board should:

- Join the laptop's 2.4 GHz hotspot.
- Connect to the server's `/board` endpoint.
- Send `board.ready` after its hardware is initialized.
- Display each received instruction.
- Initially use one button as a fake detected action.
- Send `action.detected`.
- Display the received round result.
- Reconnect after losing the socket.

This is the first end-to-end vertical slice. Complete it before implementing sensor recognition.

## 3. Add hardware actions individually

Implement actions in this order:

1. Screen tap.
2. Short and long button presses.
3. Twist.
4. Swipe.
5. Shake, push, and pull.
6. Microphone actions.

Every detector should produce the same internal event:

```text
DetectedAction {
    action
    elapsedMs
}
```

Only the board's networking task should convert this event to protocol JSON.

## 4. Add the game loop

The server runs this loop:

```text
board ready
  -> choose a supported action
  -> send instruction
  -> start timeout
  -> receive action
  -> send result
  -> update score
  -> repeat
```

Add one small state-transition test covering:

- Correct action.
- Wrong action.
- Timeout.
- Action from a stale round.

## 5. Add a minimal browser page

**Implemented:** a responsive tile-grid lobby at `GET /`.

- Connected boards wait as tiles before start.
- The dashboard receives full snapshots over `/dashboard`.
- Start locks the current roster and sends one shared `tap` instruction to every locked board.
- Locked disconnected boards remain visible; late boards are excluded.
- Tiles show connection/round status, current instruction, and score.

Reset, game replay, board selection, and frontend frameworks remain out of scope.

## Work split

### Server

- WebSocket endpoint.
- In-memory board registry, game state, and timeout.
- Action validation.
- Minimal browser page.

### Board

- Wi-Fi and WebSocket client.
- Display and audio feedback.
- Sensor detectors.
- FreeRTOS event queue.

Integrate after step 2 instead of waiting for every sensor to be implemented.

## Definition of done

```text
Open browser
  -> start game
  -> board displays "Bop it"
  -> player taps screen
  -> server validates the action
  -> board displays success
  -> score increments
```

## Not included

- Database.
- Accounts or leaderboards.
- Shared multiplayer games between boards.
- Bluetooth LE.
- Mid-round reconnection recovery.
