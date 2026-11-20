# pen-diagnostics — P0.3 synthetic pen test

Answers the project's go/no-go question ([roadmap P0.3](../../../docs/roadmap.md#p03-windows-synthetic-pen-diagnostic-windowstestspen-diagnostics)): **can a user-mode synthetic `PT_PEN` draw small mathematical notation cleanly in real Windows apps?**

The injector it drives, [`windows/core/injection/pen_injector.*`](../../core/injection/), is the seed of the production `core/injection` module.

## Build

```bash
windows\build.cmd test
```

This builds `windows\out\pen_diag.exe` and `windows\out\unit_tests.exe` with MSVC (C++20, `/W4 /WX`) and runs the unit tests. No CMake yet (roadmap D5).

## Commands

| Command | What it does |
|---|---|
| `pen_diag info` | OS, monitors and DPI, which monitor is the built-in panel, desktop/session/integrity, whether a synthetic pen can be created |
| `pen_diag self-test` | Opens its own window, injects every pattern, reads back what Windows delivered (`WM_POINTER*` + history) and checks counts, positions, pressure, hover, barrel and eraser. Automated **PASS/FAIL**. |
| `pen_diag soak [--minutes 30 \| --rounds N] [--log soak.csv]` | The **go/no-go endurance run**: repeats the math-notation patterns (ramp, taps, tiny, glyphs, barrel, rapid, eraser) in its own window, shifted each round, and checks every round for lost frames, offsets, pressure errors and down/up balance. PASS needs the full duration with zero failed rounds. `--log` writes per-round counts only (no coordinates). It moves the cursor on the pen's display the whole time, so start it and step away. |
| `pen_diag patterns [--only …] [--delay 5] [--rate 240]` | After a countdown, draws the patterns into **the app you switch to** |
| `pen_diag replay <pen.csv> [--speed 1] [--delay 5]` | Replays an [`android/penlogger`](../../../android/penlogger/README.md) CSV into the app you switch to: real S Pen strokes with no streaming |
| `pen_diag probe` | Low-level check: does the cursor follow injected hover, and do ordinary mouse events arrive? |

Patterns (`--only` takes any of these):

| Section | Content | What to look for in the app |
|---|---|---|
| `ramp` | Two lines, pressure 32→1024 and 1024→32 | Width/opacity changes smoothly with pressure |
| `taps` | 20 single-sample taps | Dots appear, not dropped (`i`/`j` dots, decimal points) |
| `tiny` | Dashes 1–12 px; circles r = 2–12 px | Tiny strokes survive, circles close, no hooks at the ends |
| `glyphs` | `i j x′ − = + ∫ x_{n+1}` at ~10, 20, 40 px | Readable at 10 px; the subscript sits where it was drawn |
| `hover` | In-range path with no contact | Hover cursor follows; nothing is drawn |
| `barrel` | Stroke with the side button held | The app's barrel behaviour (often select or erase) |
| `rapid` | 60 strokes at 1 kHz | Every stroke present: no joined or missing strokes, so pen-up is never lost |
| `eraser` | Eraser stroke across the ramps | Ramps are erased where crossed (in apps that support the pen eraser) |

**Safety:** injects only into the window that was foreground when the countdown ended. Refuses elevated, minimized and lock-screen targets. Stops on **Esc**, **Ctrl+C**, a focus change, or the pen leaving the target. Always ends with pen-up and leaving range.

## Real-app procedure

1. Put the app window on the **display the synthetic pen reaches**. The tool prints it, and on this laptop it's the built-in panel, `\\.\DISPLAY1` (see the findings below).
2. Open a blank page and select a **pen tool**: OneNote's Draw tab, Xournal++'s pen, or Edge's PDF "Draw".
3. Run the patterns, then click the app within 5 s:
   ```bash
   windows\out\pen_diag.exe patterns
   ```
4. Compare the result with the table above. Record pass, partial or fail in [docs/test-matrix.md §1](../../../docs/test-matrix.md#1-app-compatibility-windows-11-synthetic-pt_pen).
5. With a tablet log, replay real handwriting:
   ```bash
   windows\out\pen_diag.exe replay p0-logs\pen-YYYYMMDD-HHMMSS.csv
   ```

## Results

### Self-test, 19 Nov 2026 (this dev PC: Windows 11 build 26300, 2× 1920×1080 at 125%)

`pen_diag self-test`: **PASS**, 11 of 11 checks.

| Measure | Result |
|---|---|
| Frames injected / failed | 2351 / 0 |
| Strokes (down / up / cancel) | 154 / 154 / 0, all delivered |
| Contact frames compared | 1914 sent, 1914 received, **0 position errors, 0 pressure errors** |
| Coalescing | None: every injected frame arrived as its own `WM_POINTER` message |
| Pressure range delivered | 32..1024 |
| Hover / barrel / eraser | Delivered (eraser after the flip fix below) |
| Injection → window procedure latency | **p50 0.36 ms, p95 0.86 ms**, max 3.5 ms (n = 1631) |

### Findings that affect the design

1. **The synthetic pen is bound to one display and uses that display's pixel coordinates.** On this laptop, Windows maps pen digitizers to the built-in panel (DISPLAY1, at x = −1920). `InjectSyntheticPointerInput` treats `ptPixelLocation` as pixels *of that display*. Device (0,0) landed at screen (−1920,0), and points outside it are **silently ignored** while the call still returns success. That contradicts the design's "physical virtual-desktop pixels" (architecture.md §5, §6). `pen_diag` now measures the mapping at startup (`pen_space.*`) and refuses targets the pen can't reach. → Roadmap decision **D7**.
2. **Flipping to the eraser while in range is ignored.** Like a physical pen, the device must leave range and re-enter with `PEN_FLAG_INVERTED`. The injector now does that automatically (`endFlips` counter). Eraser contact frames carry `INVERTED | ERASER`, matching hardware.
3. **The success return value proves nothing about delivery.** Every call "succeeded" while all input went to the wrong monitor. This confirms the design's rule (§7) that local preview must not treat an inject result as proof of ink.
4. **The mapping is not stable: it changed overnight with no TabInk involvement (20 Nov 2026).** After a sleep/resume and a power-source change (EL Display Hub was also running), every real pen and touch digitizer moved to DISPLAY2. The synthetic pen ended up with **no monitor**: its range spanned the whole virtual desktop, and every injected point parked the cursor at the built-in panel's centre (−960, 544). `InjectSyntheticPointerInput` still returned success for every frame. Calibration caught it and refused to draw. `pen_diag info` and failed calibrations now print each pointer device's monitor and `GetPointerDeviceRects` mapping. **For the production host:** read the synthetic device's mapping at start, and again after display changes and power resume. Run the hover calibration before every session. Never assume yesterday's mapping. To fix it, the user picks the pen screen in Control Panel > Tablet PC Settings > Setup.

### Real apps

Not run yet. Needs you at the keyboard: see the procedure above. Results go in [test-matrix.md §1](../../../docs/test-matrix.md#1-app-compatibility-windows-11-synthetic-pt_pen).
