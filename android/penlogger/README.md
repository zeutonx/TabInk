# penlogger — P0.2 S Pen sample logger

Throwaway feasibility spike ([roadmap P0.2](../../docs/roadmap.md#p02-android-pen-sample-logger-android)). It answers one question before any streaming code exists: **what does the Galaxy Tab S10 FE actually deliver for the S Pen?**

Write on the white area. The panel at the top updates live. **Record** saves every raw sample to CSV.

| Stats line | What it tells you | Design question it answers |
|---|---|---|
| `CONTACT rate` | Median interval between consecutive pen-contact samples, as Hz, plus p95/max interval | Real sample rate; don't assume 240 Hz (architecture.md §§2, 4) |
| `HOVER rate / dist` | Hover sample rate and `AXIS_DISTANCE` range | Is hover distance reported? Is the hover rate lower? |
| `DELIVERY lag` | Time from the newest sample to our callback; samples per event; % arriving as historical | How much batching there is, and the "pen sampling → app delivery" stage of the latency budget (§9) |
| `PRESSURE` | Min..max and a 10-bin histogram of contact pressure | The pressure curve and Windows 0..1024 mapping |
| `PEN … non-monotonic` | Samples whose timestamp goes backwards | Must be 0, or the protocol's ordering assumption fails |
| `BUTTONS` | Every `buttonState` bit seen, plus `BUTTON_PRESS` events | Does the side button reach apps, or does Air Command take it? |
| `TOOLS / other-touch-during-pen` | Tool types seen; finger/palm samples while the pen is down | Input for the palm-rejection heuristic (§2) |

On the canvas, black ink is the pen, red is the eraser, faint blue blobs are finger or palm contacts, green dots are the individual raw samples, and the grey circle is the hover cursor. Strokes aren't smoothed, so the ink shows exactly what Android delivered.

**Unbuffered** turns on `requestUnbufferedDispatch()`, so you can compare batched delivery with per-sample delivery. Stats reset when you toggle it.

## Build

From the repo root, in Docker (works on any machine):

```bash
docker compose --profile android run --rm android-build ./gradlew :penlogger:assembleDebug
```

The APK is written to `android/penlogger/build/outputs/apk/debug/penlogger-debug.apk`. You can also open `android/` in Android Studio and run the `penlogger` configuration.

Unit tests (pure JVM, cover the statistics):

```bash
docker compose --profile android run --rm android-build ./gradlew :penlogger:testDebugUnitTest
```

## Install and record

Needs `adb` on Windows: Android Studio, or just the platform-tools (`winget install Google.PlatformTools`). Enable USB debugging on the tablet first ([dev-setup.md](../../docs/dev-setup.md#tablet-galaxy-tab-s10-fe)).

```bash
adb install -r android/penlogger/build/outputs/apk/debug/penlogger-debug.apk
```

```bash
adb shell am start -n io.github.zeutonx.tabink.penlogger/.MainActivity
```

CSV files go to the app's external files folder. List them and pull them to the PC:

```bash
adb shell ls /sdcard/Android/data/io.github.zeutonx.tabink.penlogger/files/
```

```bash
adb pull /sdcard/Android/data/io.github.zeutonx.tabink.penlogger/files/ p0-logs/
```

Keep pulled logs out of git (`p0-logs/` is local).

## Test protocol (≈20 min)

Run each step with **Unbuffered: off**, then repeat steps 1–3 with it **on**. Press **Reset stats** between steps and screenshot the panel.

1. **Fast long strokes**: 30 s of fast loops. Note the contact rate, batch size and lag.
2. **Slow writing**: write `x_{n+1} = \int_0^1 \alpha\, dt` small, about 5 mm high. Check that dots on `i`/`j` and primes appear as separate strokes.
3. **Hover**: hover from about 2 cm down to the screen and back, several times. Note the distance range and hover rate.
4. **Pressure ramp**: drag from the lightest touch to a full press. Note min and max pressure.
5. **Side button**: press the S Pen button while hovering, then while touching. Check `BUTTONS`, with Air Command on and then off (Settings → Advanced features → S Pen).
6. **Palm**: rest your hand on the screen and write. Note `other-touch-during-pen` and whether blue blobs show up.
7. **Eraser**: check whether the S Pen reports `eraser` in `TOOLS`. The S10 FE pen has no eraser end, so expect no.
8. Record one 2-minute session of normal math writing with **Record** and pull the CSV.

## Analyse a recording

Computes the results-table rows over a whole CSV (the panel keeps only the last 4096 intervals). Prints statistics only, never coordinates. Python 3.12, no dependencies:

```bash
py -3.12 android/penlogger/tools/analyze_log.py p0-logs/pen-YYYYMMDD-HHMMSS.csv
```

Tests: `py -3.12 -m unittest discover -s android/penlogger/tools`

## Results

Fill this in after running the protocol. It feeds decisions in [architecture.md](../../docs/architecture.md) §§2, 4, 8 and 9.

| Measurement | Unbuffered off | Unbuffered on | Notes |
|---|---|---|---|
| Contact sample rate (p50) | | | |
| Contact interval p95 / max | | | |
| Hover sample rate | | | |
| Hover distance range (units) | | | |
| Samples per event (p50 / max) | | | |
| Delivery lag p50 / p95 | | | |
| % historical samples | | | |
| Pressure min..max | | | |
| Non-monotonic timestamps | | | must be 0 |
| Side button bits seen (Air Command on) | | | |
| Side button bits seen (Air Command off) | | | |
| Palm contacts while writing | | | |
| Eraser tool type reported | | | |
| Device / Android build | | | from the CSV header |
