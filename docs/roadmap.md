# TabInk Roadmap

Source: [architecture.md §17](architecture.md#17-project-organization-and-roadmap). Work moves phase by phase. The next phase starts only after the current phase's gate passes.

## P0 — Feasibility spikes (~2 weeks). Throwaway code is fine.

### P0.1 Existing-tool baseline
- [ ] Test Samsung Second Screen, spacedesk and SuperDisplay on this tablet and PC (pressure, hover, button, palm, tiny subscripts, rough latency)
- [ ] Record results in [test-matrix.md §2](test-matrix.md#2-existing-tool-baselines-p0)
- [ ] **Decision:** does an existing tool already cover the real use case? If yes, stop here.

### P0.2 Android pen-sample logger (`android/`)
- [x] Minimal Kotlin app with one custom `View`: log `onTouchEvent`/`onHoverEvent`, tool type, historical samples (with timestamps), pressure, `AXIS_DISTANCE`, button state ([`android/penlogger`](../android/penlogger/README.md))
- [ ] Run the [test protocol](../android/penlogger/README.md#test-protocol-20-min) on the tablet and fill in the results table
- [ ] Measure the real S10 FE sample rate, pressure distribution, hover distance and side-button delivery. Don't assume 240 Hz.
- [ ] Check whether Samsung Air Command swallows side-button events

### P0.3 Windows synthetic-pen diagnostic (`windows/tests/pen-diagnostics/`)
- [x] `CreateSyntheticPointerDevice(PT_PEN)` + `InjectSyntheticPointerInput`: draw a pressure ramp, eraser strokes, tiny glyphs, hover, side button ([`pen_diag`](../windows/tests/pen-diagnostics/README.md))
- [x] Automated self-test against our own window: **PASS**. 1914/1914 contact frames exact, pressure 32..1024, hover/barrel/eraser delivered, injection→window p95 0.86 ms
- [ ] Try it in OneNote, Xournal++ and Edge PDF (`pen_diag patterns`, needs you at the keyboard)
- [x] Replay a P0.2 log through the injector (`pen_diag replay`). Built; waiting on a tablet log.

**P0 gate:** at least two target apps accept meaningful pen input (pressure, clean down/up), with no unexplained input hooks.

**Go/no-go (architecture.md end):** synthetic pen draws 30 consecutive minutes of small math notation without systematic loss, offsets, or broken pen-up. If not, fall back to existing apps or treat a signed HID driver as a separate research task.

## MVP — Smallest usable math-note system

USB via `adb reverse`, authenticated pairing, one selected WGC window, H.264 to `MediaCodec`, coordinate mapping, ordered pen injection, palm suppression, pen/eraser/undo, clean frame handling. Local preview is optional. First supported target: OneNote or Xournal++.

**Acceptance:** a 30-minute two-page proof (integrals, Greek, matrices, superscripts) with no lost-up artifacts, alignment p95 ≤2 tablet px, and zero unauthorized injection.
**Not required:** OCR, Wi-Fi, WinTab, multi-monitor, smart preview.

## v1

Local-ink reconciliation, pressure curves, tool profiles, zoom-to-write, multi-monitor, hardened pairing, Wi-Fi TLS, and **select → TexTeller/Pix2Text → KaTeX preview/edit → paste into Overleaf**.
**Acceptance:** 100-equation OCR benchmark run. OCR never blocks writing.

## v2

AOA USB without developer mode, WebRTC/QUIC, browser extension, WinTab/virtual HID only with evidence, installers, multi-device.

## Open decisions (resolve before or during P0)

| # | Decision | Options | Notes |
|---|---|---|---|
| D1 | Windows P0 language | C# + P/Invoke **or** C++20 directly | The design suggests a C# proof of concept if C++ is new. The production core stays C++. |
| D2 | Pen magic byte order | `0x5449` ("IT" on the wire) or `0x4954` ("TI") | See [protocol-v1.md §7](protocol-v1.md#7-open-questions) |
| D3 | Control-message framing | Shared 32-byte frame or a separate length-prefixed envelope | protocol-v1.md §7 |
| D4 | License | MIT or Apache-2.0 | Choose only after auditing dependencies (architecture.md §19). No LICENSE file until then. |
| D5 | Windows build system | CMake + vcpkg/NuGet, or an MSBuild `.sln` | |
| D6 | GPU for capture + encode | NVIDIA NVENC or Intel Quick Sync | This laptop is hybrid (Intel UHD + RTX 5060). The WGC texture and MF encoder must share a D3D11 adapter. |
| D7 | Multi-monitor pen reach | (a) Only target windows on the pen-mapped display; tell the user to move the window. (b) Ask the user to remap pen input in Tablet PC Settings. (c) Virtual HID digitizer (kernel driver, v2+). | **P0.3 finding:** the synthetic pen is bound to one display (the built-in panel here) and uses its local pixels. Off-display points are silently ignored. This changes architecture.md §5/§6 "virtual-desktop pixels". Leaning (a) for MVP, measured at runtime as `pen_diag` does. Check whether a PC without a built-in digitizer maps to the primary display. |
