# Browser Bluetooth dashboard

The Go program is only a static localhost server. The Chromium page connects
directly to one ESP32 over Web Bluetooth and owns the 60-round sequence, score,
verdicts, timing recovery, and prompt audio. There are no WebSocket or Wi-Fi
endpoints.

```sh
cd websocket
go run .
# open http://localhost:8080
```

Web Bluetooth requires a compatible Chromium browser and a secure context;
`http://localhost` is treated as secure. The board picker must be opened by the
**Connect board** button gesture. Other browsers and LAN-IP HTTP origins are not
supported.

## Prompt recordings

Add `tap.mp3`, `swipe.mp3`, and `press.mp3` under `audio/`. The cue starts only
after the corresponding instruction's single GATT write succeeds. A matching
round event or watchdog completion stops it, including on the final round;
stale events do not affect the active cue. Restart `go run .` after replacing a
recording so the embedded assets are rebuilt.

## Protocol v2 transport

The board advertises the Nordic UART service:

- Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX, browser writes: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- TX, board indicates: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

`board.ready` remains newline-framed JSON and can arrive in multiple 20-byte
indications. It must declare `protocolVersion: 2`; v1 gameplay is incompatible.

Gameplay is binary and packet boundaries are GATT operation boundaries:

| Byte 0 | Packet | Length | Direction |
| --- | --- | --- | --- |
| `0x21` | instruction | 9 | browser to board, one write |
| `0x22` | round ended | 9 | board to browser, one indication |
| `0x24` | stop | 6 | browser to board, one write |

Instruction and round-ended packets contain session u32 LE at bytes 1-4 and
round 1-60 at byte 5. Instruction byte 6 is action and bytes 7-8 are nonzero
timeout ms u16 LE. Round-ended byte 6 is detected action, where zero means
timeout, and bytes 7-8 are elapsed ms u16 LE. Stop byte 5 contains flags; bit 0
means reset.

Action codes are tap=1, twist=2, swipe=3, press=4. The browser chooses only from
`board.ready.supportedActions`; current firmware advertises tap, swipe, and
press. Full layouts and sequencing rules are in
[`../granola-esp32-embedded/docs/PROTOCOL.md`](../granola-esp32-embedded/docs/PROTOCOL.md).

There is no `round.result` write. The board owns immediate local verdict display
and retains it until the next instruction. The board never displays score.

## Game rules

The browser runs exactly 60 sequential rounds under one u32 session/run ID. It
randomly chooses an advertised action. Groups of ten use 4000, 3400, 2800,
2400, 2000, then 1600 ms windows. An expected action with
`elapsedMs <= timeoutMs` succeeds; wrong or late actions and action code zero do
not score. The dashboard shows score, last verdict, and reaction time.

The board normally emits timeout. A browser watchdog at timeout plus 250 ms is
recovery for a missing event. Browser feedback lasts 350 ms before it sends the
next instruction. Duplicate, stale, wrong-round, and different-session events
are ignored.

Stop sends a matching-session stop and preserves browser score. Restart first
writes a reset stop for the old session, then writes round 1 under a fresh
session and resets browser score. Disconnect abandons the run; reconnect starts
a fresh round-1 session.
