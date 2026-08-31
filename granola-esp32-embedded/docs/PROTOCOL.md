# Board/browser BLE protocol

Protocol version 2 uses newline-framed JSON only for board discovery. Gameplay
uses fixed binary packets whose boundaries are GATT operation boundaries.
Protocol v1 JSON gameplay messages are incompatible and must not be sent to a
v2 board.

## GATT service

| Role | UUID | Properties |
| --- | --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | - |
| RX, browser to board | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | write with response |
| TX, board to browser | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | indicate |

Each instruction or stop is exactly one browser GATT write. Each round-ended
event is exactly one 9-byte board indication. These packets are never split,
concatenated, newline-framed, or reconstructed as a stream.

Subscribing to TX requests `board.ready`. That JSON object ends in `\n` and may
span sequential indications of at most 20 bytes. The browser buffers only this
JSON stream until the newline. Board indications use a fixed FIFO and advance a
chunk/message only after indication acknowledgement; a failed indication is
retried.

BLE callbacks only copy bounded connection, packet, subscription, and status
events. `BleLink::loop()` validates packets, dispatches game handlers, pumps
indications, and restarts advertising after disconnect.

## Discovery: `board.ready`

```json
{"type":"board.ready","protocolVersion":2,"boardId":"bopit-01","supportedActions":["tap","swipe","press"]}
```

The browser must reject any other protocol version and only choose an action in
`supportedActions`.

## Integer encoding

All multibyte integers are unsigned little-endian values. Implementations encode
and decode bytes explicitly; packet structs are not cast onto buffers.

### Type/version byte

The high nibble is protocol version 2. Defined packet bytes are:

| Byte 0 | Direction | Packet |
| --- | --- | --- |
| `0x21` | browser to board | instruction |
| `0x22` | board to browser | round ended |
| `0x24` | browser to board | stop |

### Action codes

| Code | Action | Wire name | Current availability |
| --- | --- | --- | --- |
| `0` | no detected action / timeout | - | round-ended only |
| `1` | screen tap | `tap` | advertised |
| `2` | device twist | `twist` | implemented but not advertised or accepted |
| `3` | screen swipe | `swipe` | advertised |
| `4` | side-button press | `press` | advertised |

An instruction action must be both a defined code and present in the firmware's
`kSupportedActions`. The current board rejects code 2 while Twist is disabled.

## Browser to board

### Instruction: 9 bytes

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `0x21` |
| 1 | 4 | session (`runId`) u32 LE |
| 5 | 1 | round u8, 1 through 60 |
| 6 | 1 | supported action code, 1 through 4 |
| 7 | 2 | `timeoutMs` u16 LE, nonzero |

Session zero is valid. The board tracks session presence separately from the
numeric value.

A board with no current session accepts only round 1 while idle. Once accepted,
it rejects every instruction while awaiting that round. From feedback it
accepts only the exact next round for the same session. Duplicate, skipped,
stale, and different-session packets are ignored. Stop or disconnect clears
session presence so a fresh session's round 1 can start. A restart writes stop
for the old session before writing round 1 for the new session.

### Stop: 6 bytes

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `0x24` |
| 1 | 4 | session u32 LE |
| 5 | 1 | flags; bit 0 means reset |

A stop only affects its matching current session. It abandons the active round,
clears board session state, and returns to connected idle. The reset flag marks
a restart; score reset is browser-owned.

## Board to browser

### Round ended: 9 bytes

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `0x22` |
| 1 | 4 | session u32 LE |
| 5 | 1 | round u8, 1 through 60 |
| 6 | 1 | detected action code; 0 means timeout |
| 7 | 2 | `elapsedMs` u16 LE |

The board reports the first queued input only when its captured timestamp is at
or before the deadline. `elapsedMs <= timeoutMs` is in-window. An input queued
after the deadline is reported as timeout (`action=0`), as is no input. If the
outbound FIFO is temporarily full, the game retries the event until it is queued
or a valid next instruction, stop, or disconnect supersedes it.

The browser ignores an event unless its session and round exactly match the
active instruction. This makes duplicate, stale, delayed, and different-session
events harmless. Its watchdog is recovery for a missing indication, not the
normal timeout mechanism.

## Ownership and lifecycle

```text
browser instruction -> board Awaiting
input/deadline       -> board Feedback + round-ended indication
round-ended/watchdog -> browser verdict + score + 350 ms feedback delay
next instruction     -> board replaces retained feedback with next prompt
```

The board owns input timing, immediate local success/wrong/timeout feedback, and
timeout emission. Board feedback remains visible until the next instruction.
The board never displays or computes score.

The browser owns the 60-round sequence, random action selection, timeout tiers,
verdict derivation, score, prompt audio, watchdog recovery, and next-round
timing. A matching round event or watchdog stops the active prompt audio,
including on round 60. There is no `round.result` packet.

Ten-round tiers use 4000, 3400, 2800, 2400, 2000, and 1600 ms. The watchdog
adds 250 ms transport grace and browser feedback lasts 350 ms.

## Browser constraint

Selection must originate from a user gesture in a Web Bluetooth-capable
Chromium browser. Serve the dashboard from `http://localhost`; arbitrary HTTP
LAN origins are not secure contexts and cannot use `navigator.bluetooth`.
