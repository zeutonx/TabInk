# TabInk Test Matrix

Source: [architecture.md §18](architecture.md#18-testing-and-acceptance-plan). Record honest results: `pass`, `partial`, `unsupported`, or `—` (not run yet). Each result needs a date, TabInk commit, and app version.

## 1. App compatibility (Windows 11, synthetic PT_PEN)

| App | Version | Pressure | Down/up | Hover | Eraser | Side button | Shortcuts | Capture | Notes |
|---|---|---|---|---|---|---|---|---|---|
| OneNote desktop | | — | — | — | — | — | — | — | MVP target |
| Xournal++ | | — | — | — | — | — | — | — | MVP target |
| Edge PDF ink | | — | — | — | — | — | — | — | |
| Acrobat | | — | — | — | — | — | — | — | |
| Goodnotes for Windows | | — | — | — | — | — | — | — | |
| Excalidraw (browser) | | — | — | — | — | — | — | — | |
| tldraw (browser) | | — | — | — | — | — | — | — | |
| Obsidian ink | | — | — | — | — | — | — | — | |
| Whiteboard web / Teams | | — | — | — | — | — | — | — | Not the retired standalone app |
| Samsung Notes Windows | | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Only on supported Samsung PCs |
| WinTab-only apps | | unsupported | | | | | | | Out of scope for MVP and v1 |

## 2. Existing-tool baselines (P0)

Run these on **this** tablet and PC before building anything.

| Tool | Pressure | Hover | Side button | Palm rejection | Small subscripts | Approx. latency | Notes |
|---|---|---|---|---|---|---|---|
| Samsung Second Screen | — | — | — | — | — | — | |
| spacedesk | — | — | — | — | — | — | |
| SuperDisplay (installed) | — | — | — | — | — | — | |
| Samsung Notes (native, tablet) | — | — | — | — | — | — | Reference for precision tests |

## 3. Latency (report p50 / p95 / p99 / max, ≥500 strokes)

| Measurement | Transport | Page state | p50 | p95 | p99 | max | Target |
|---|---|---|---|---|---|---|---|
| Local preview | — | static | | | | | p50 <15 ms, p95 <25 ms |
| Remote visible ink | USB | static | | | | | p50 <45 ms, p95 <75 ms |
| Remote visible ink | USB | scrolling | | | | | |
| Remote visible ink | Wi-Fi | static | | | | | |
| Remote visible ink | Wi-Fi | scrolling | | | | | |

Method: high-speed camera or photodiode, plus monotonic timestamps at every stage with a calibrated clock offset. Never use network RTT in place of pen-to-ink time.

## 4. Calibration (9×9 grid; target median ≤1 px, p95 ≤2 tablet px)

| Windows DPI | Monitor layout | Tablet rotation | Zoom | Median px | p95 px | Teleports on change? |
|---|---|---|---|---|---|---|
| 100% | primary | 0° | 1× | | | |
| 125% | | | | | | |
| 150% | | | | | | |
| 175% | | | | | | |
| | monitor left of primary (negative origin) | | | | | |
| | monitor right of primary | | | | | |
| | | 90° | | | | |
| | | 180° | | | | |
| | | | 2× | | | |
| | | | 4× | | | |
| window drag/resize mid-stroke | | | | | | must cancel, never teleport |

## 5. Precision (30 repetitions each, normal and 4× zoom)

Glyphs: `i`, `j`, `\alpha`, `\varepsilon`, `\partial`, `\int_0^1`, `x_{n+1}`, `\sum`, 2×2/3×3 matrices, proof lines.
Count: dropped dots, endpoint hooks, crossed symbols, discontinuities, mispositioned subscripts. Baseline: Samsung Notes directly on the tablet. A packet-replay test confirms the raw sample count is preserved.

## 6. Palm and gestures (100+ trials each)

Full palm, wrist edge, several fingers while writing, two-finger scroll, hovering near the screen, release after hover, button presses, leaving and re-entering the screen. Record false marks and accidental gestures.

## 7. OCR benchmark (v1)

100 self-written, labeled crops: 20 fractions/integrals, 20 Greek/subscript, 20 matrices, 20 multi-line derivations, 20 complex expressions. Split 20 for tuning and 80 held out.
Metrics: exact normalized match, render match, valid render rate, symbol error rate, structure accuracy, median/p95 inference time, user correction seconds. Engines: TexTeller 3.x, Pix2Text MFR, pix2tex. Results go to a reproducible CSV under `recognition/benchmarks/`.

## 8. Security

See the checklist in [threat-model.md](threat-model.md#security-acceptance-tests).
