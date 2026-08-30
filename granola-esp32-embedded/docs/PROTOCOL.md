# Board-side protocol notes

The authoritative protocol is the server's (`websocket/protocol.go`, protocol
version 1). This file records how the board implements it and the few points
that need agreement between the two sides.

Codec: [`src/net/protocol.h`](../src/net/protocol.h) /
[`protocol.cpp`](../src/net/protocol.cpp). Socket, reconnect and callbacks:
[`src/net/ws_link.h`](../src/net/ws_link.h).

## What the board sends

### `board.ready`

Sent automatically on every connect, before anything else. The hardware is
initialised in `setup()` before the socket is ever opened, so the board is
always ready by the time the socket comes up.

```json
{"type":"board.ready","protocolVersion":1,"boardId":"bopit-01",
 "supportedActions":["tap","twist","swipe","press"]}
```

| `boardId` | Board |
| --- | --- |
| `bopit-01` | ESP32-S3-Touch-AMOLED-1.8 |
| `bopit-02` | ESP32-S3-Touch-LCD-1.46 (not yet implemented) |

### `action.detected`

Sent once per round, for the first action detected inside the window.

```json
{"type":"action.detected","roundId":"12","action":"twist","elapsedMs":640}
```

`elapsedMs` is measured with `millis()` from the instant the `instruction` frame
is handled. The prompt repaint (~15 ms) happens after that stamp is taken, so
`elapsedMs` includes it — it is time-from-instruction, not time-from-prompt, as
the protocol specifies.

**Nothing is sent when the window closes with no action.** The board stops
accepting input and waits for the server's `round.result` with
`result: "timeout"`.

If the link is down when an action fires, the frame is dropped rather than
queued — the server discards actions for completed rounds anyway.

## What the board expects

### `instruction`

Opens the window. `timeoutMs` is used as given.

An instruction naming an action outside `supportedActions` is logged and
ignored, and no window opens. A missing or over-long (>40 char) `roundId` is
rejected the same way — a truncated ID would make every subsequent
`action.detected` a silent no-op on the server.

### `round.result`

Renders the verdict and score. The board displays `score` and never computes
one; `result` maps to `NICE` / `WRONG` / `TOO SLOW` on the panel.

A `round.result` whose `roundId` is not the round the board is currently in is
ignored, mirroring the server's own rule about unknown or completed round IDs.

If no `round.result` arrives within 5 s the board returns to idle rather than
sitting on a frozen screen (`cfg::kRoundResultTimeoutMs`).

## Round lifecycle on the board

```
Idle            no round in flight, or link down
  | instruction
Awaiting        window open, prompt + countdown bar on screen
  | first action detected -> action.detected sent
  | or window expires     -> nothing sent
AwaitingResult  input ignored, waiting on round.result
  | round.result
Feedback        verdict + score on screen for 350 ms
  | -> Idle
```

Only the first action in a round counts, matching the server's rule: the input
queue is flushed when the window opens, and further input is ignored from the
moment the round closes.

A disconnect returns the board to `Idle` and abandons the round, per the
protocol.

## Three things to agree with the server side

1. **Action names.** The board advertises `tap`, `twist`, `swipe`, `press` —
   `tap` matches the example in the spec, the other three are the board's
   choice. They are defined in one place
   ([`src/game/actions.h`](../src/game/actions.h)) if the server would rather
   use different strings.

2. **`twist` and `swipe` carry no direction.** The detectors fire on rotation
   or travel either way, so the server must not request `twist_left`-style
   variants as things stand. The gyro sign *is* known inside
   [`twist_detector.cpp`](../src/input/twist_detector.cpp), so
   direction-specific twists are cheap to add if the game wants them; swipe
   direction would need a little more work.

3. **The difficulty ramp is now server-side.** The board takes `timeoutMs` at
   face value, so the intended ramp has to be implemented in Go:

   | Round | `timeoutMs` |
   | --- | --- |
   | 1–10 | 2000 |
   | 11–20 | 1700 |
   | 21–30 | 1400 |
   | 31–40 | 1200 |
   | 41–50 | 1000 |
   | 51+ | 800 |

   If an instruction arrives with `timeoutMs` absent or `0`, the board falls
   back to this same table, counting instructions locally, and logs a warning.
   That is a safety net, not the intended path — `roundId` is an opaque string,
   so the board cannot derive the real round number from it.

## Keepalive

The protocol requires no application-level heartbeat, and the board sends none.
It does use RFC6455 ping/pong (5 s interval, 3 s pong timeout, 2 missed pongs
before reconnecting) so that a board attached to an AP that has gone away
notices. Every mainstream Go WebSocket library answers ping frames
automatically; if the server does not, set `cfg::kWsUsePingPong = false` in
[`include/app_config.h`](../include/app_config.h) — otherwise the board will
reconnect every few seconds.

## Multiplayer

Nothing above is single-player-specific. Two boards connect to the same server
and are told apart by `boardId`. Only the board-side pin map and UI need work
for the second board.
