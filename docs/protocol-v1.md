# TabInk Protocol v1

Status: **draft**, taken from [architecture.md §4](architecture.md#4-transport-protocol-pairing). This file is the wire contract. If it disagrees with the code, fix the code or update this file. Don't let them drift apart.

## 1. Channels

| Channel | MVP port (PC loopback) | Direction | Contents | Priority |
|---|---|---|---|---|
| Control/pen | TCP 27180 | both | Pen samples, control messages, acks, heartbeats | Highest. `TCP_NODELAY`. Never shares a thread with video. |
| Video | TCP 27181 | PC → tablet | Length-delimited encoded H.264 frames + frame metadata | Lower. Old frames may be dropped. |

MVP transport is `adb reverse`:

```bash
adb reverse tcp:27180 tcp:27180
```

```bash
adb reverse tcp:27181 tcp:27181
```

The Android app connects to `127.0.0.1:27180` and `127.0.0.1:27181` on the tablet. ADB tunnels both to the PC listener. Both streams use authenticated TLS 1.3, even over USB (see [threat-model.md](threat-model.md)).

**Queue policy:** queues are bounded. Under pressure, drop old video frames first, then superseded hover moves. Never drop pen down, pen up, cancel, or contact moves.

## 2. Encoding rules

- Use little-endian for all integers.
- Fields are **packed with no padding**. `timestamp_us` sits at offset 12, which is not 8-byte aligned. Parsers must read fields with explicit byte reads or `memcpy`. Don't cast the buffer to a struct pointer.
- Unknown `version` → close the connection with a protocol error. Don't guess.
- Unknown `kind`, or unknown `flags`/`buttons` bits → log and ignore the bits. Reject the packet if it's malformed.

## 3. Pen packet (32 bytes)

| Offset | Size | Type | Name | Meaning |
|---:|---:|---|---|---|
| 0 | 2 | u16 | `magic` | `0x5449` |
| 2 | 1 | u8 | `version` | `1` |
| 3 | 1 | u8 | `kind` | `1` = sample, `2` = control, `3` = ack |
| 4 | 4 | u32 | `session_id` | Random for each session |
| 8 | 4 | u32 | `seq` | Monotonic in each direction. Gaps are logged. |
| 12 | 8 | u64 | `timestamp_us` | Android monotonic clock, µs |
| 20 | 2 | u16 | `x_norm` | 0..65535 across the active tablet content rect |
| 22 | 2 | u16 | `y_norm` | 0..65535 across the active tablet content rect |
| 24 | 2 | u16 | `pressure` | 0..1024 (Windows `POINTER_PEN_INFO` range). 0 while hovering. |
| 26 | 2 | u16 | `flags` | See below |
| 28 | 2 | u16 | `buttons` | bit0 primary, bit1 secondary |
| 30 | 2 | u16 | `map_epoch` | Mapping version the sample was taken under |

### `flags`

| Bit | Name | Rule |
|---:|---|---|
| 0 | `CONTACT` | Pen tip is touching |
| 1 | `IN_RANGE` | Pen is hovering or touching |
| 2 | `RELEASE` | Final sample of a contact (pen up) |
| 3 | `ERASER` | Eraser end / eraser tool type |
| 4 | `PREDICTED` | Predicted point. **The host must never inject it.** It's for preview only. |
| 5 | `CANCELED` | The stroke was canceled. The host must end the pointer without committing more ink. |
| 6–15 | reserved | Send as 0 |

### Ordering

1. Send every historical `MotionEvent` sample, in order, before the current sample. Never collapse them to the latest point.
2. A stroke is `down (CONTACT|IN_RANGE)` → `move…` → `RELEASE` or `CANCELED`. The host injects in exactly this order.
3. On disconnect, reconnect, or a `map_epoch` change mid-stroke, the host sends a synthetic pen-up/cancel itself. It never resumes an old pointer-down.

### Worked example (golden vector candidate)

Sample with session `0x12345678`, seq `1`, t = 1 000 000 µs, x = 32768, y = 16384, pressure 512, `CONTACT|IN_RANGE`, no buttons, epoch 1:

```text
49 54 01 01 78 56 34 12  01 00 00 00 40 42 0F 00
00 00 00 00 00 80 00 40  00 02 03 00 00 00 01 00
```

When code exists, add this and edge cases (max values, every flag, release, cancel) to `protocol/golden-test-vectors/`. Both the Kotlin and C++ codecs must pass them.

## 4. Video frame messages

The design fixes only these fields: `frame_id` (monotonic), capture timestamp, `map_epoch`, crop rect, content width/height, followed by the encoded payload. The design also requires length-delimited framing, keyframe requests on reconnect, and that video frame rate is never confused with pen sampling.

**Byte layout: TBD in P0/MVP.** Define it here before implementing it.

## 5. Control messages

Also **TBD**. The design requires these:

- Version/feature/codec negotiation
- Heartbeat and ping/pong for clock-offset estimation (several low-RTT rounds)
- Mapping snapshot push (`MappingSnapshot{epoch, source_rect, view_rect, scale, rotation, monitor_id, capture_size}`)
- Keyframe request
- Abstract toolbar actions, e.g. `TOOL_SELECT(ERASER)`, `UNDO`, `ZOOM(2x)`. Never raw keystrokes.
- Settings changes with `settings_revision` and an explicit ack (JSON or CBOR)
- OCR request/response (v1)

## 6. Pairing and handshake

See [architecture.md §4](architecture.md#4-transport-protocol-pairing) and [threat-model.md](threat-model.md). In short:

- A QR code on the PC holds a one-use, short-lived, high-entropy token and the pinned host key fingerprint.
- TLS 1.3 with fingerprint validation. The tablet sends its key and proof. The PC user approves, and the pair is stored.
- Later connections use mutual auth. Every session gets a new `session_id`.

## 7. Open questions

1. **Magic byte order.** `0x5449` written little-endian puts the bytes `49 54` on the wire, which reads as ASCII "IT", not "TI". Pick one: keep the value and accept "IT" in hex dumps, or use `0x4954` so the bytes read "TI".
2. **`kind` for non-sample messages.** The design says other messages are "separately framed". Decide whether control and ack traffic shares the 32-byte frame or gets its own length-prefixed envelope on port 27180.
3. **Max message size and rate limits** for each channel (required by the threat model).
