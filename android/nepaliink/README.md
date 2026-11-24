# nepaliink: P0.4 Nepali handwriting test

Throwaway spike ([roadmap P0.4](../../docs/roadmap.md#p04-nepali-handwriting-recognition-androidnepaliink)). It answers one question: **is ML Kit's `ne-NP` model accurate enough on your S Pen handwriting?** It doesn't send anything to Windows.

## What was checked (24 Nov 2026)

| Question | Finding |
|---|---|
| Nepali supported? | Yes: `ne`, `ne-IN` and `ne-NP` (Devanagari). [Model list](https://developers.google.com/ml-kit/vision/digital-ink-recognition/base-models) |
| Where does it run? | On the device. Google states that input and results aren't sent to its servers. [Terms](https://developers.google.com/ml-kit/terms) |
| Network use | One model download (about 20 MB). The library also **sends usage and performance metrics to Google**, and users must be told where the law requires it. Offline use after the download isn't tested yet. |
| Licence | Proprietary ML Kit Terms of Service, no reverse engineering. Not open source. |
| Trainable? | Not through this API |
| APK cost | The debug APK is 40 MB (all ABIs) |

## Build and install

From the repo root:

```bash
docker compose --profile android run --rm android-build ./gradlew :nepaliink:testDebugUnitTest :nepaliink:assembleDebug
```

```bash
adb install -r android/nepaliink/build/outputs/apk/debug/nepaliink-debug.apk
```

## Test

1. Tap **Download model** once, while online.
2. Write the prompt with the S Pen on one line (fingers and palm are ignored), tap **Recognize**, then **Next**. Don't practise: only the first attempt counts.
3. After all 100 prompts, tap **Copy report** and fill in the table below. You can close the app and continue later; **Restart test** erases the scores.
4. Turn on airplane mode, restart the app and recognise one word.

Ink and recognised text are never logged or saved; only score counts and progress are stored. **Copy text** uses the Android clipboard, and other apps can sync that.

CER counts Unicode code points after NFC, without ZWJ/ZWNJ, over all scored prompts. Skipped prompts are shown but not scored, so report them.

The report also splits scores by words, lines and numbers, and errors by character type as `edits/expected`. An inserted character counts under its own type, so that ratio can exceed 100%. Where several alignments are equally short, the split is one of them; the overall CER is not affected.

Each first attempt is also recognised with two other settings on the same ink: `FITTED_AREA` (writing area as tall as your ink) and `NO_CONTEXT` (no writing area at all). These `alt` lines are exploratory and never change the gate. If one clearly wins, confirm it on a fresh run, because picking the best of three on the same data flatters it.

| Mostly errors in | Try next |
|---|---|
| `matra`, `sign` | Larger marks, or check whether `alt FITTED_AREA` does better |
| `halant`, `consonant` | Check conjuncts; compare `ne-NP` with `ne` |
| `space` | Write word by word, with pre-context |
| `digit` only | Number input may need its own mode |

**Gate:** CER under 10% on all 100 prompts with none skipped. One writer is a feasibility signal, not a general accuracy claim.

## Results

| Writer | Set | Scored / skipped | CER | WER | Exact | In candidates | Works offline |
|---|---|---|---|---|---|---|---|
| | v1 | | | | | | |
