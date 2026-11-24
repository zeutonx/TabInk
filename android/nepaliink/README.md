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
3. After all 100 prompts, copy the stats line into the table below. Stay in the app: scores aren't saved.
4. Turn on airplane mode, restart the app and recognise one word.

Nothing is logged or saved. **Copy text** uses the Android clipboard, and other apps can sync that.

CER counts Unicode code points after NFC, without ZWJ/ZWNJ, over all scored prompts. Skipped prompts are shown but not scored, so report them.

**Gate:** CER under 10% on all 100 prompts with none skipped. One writer is a feasibility signal, not a general accuracy claim.

## Results

| Writer | Set | Scored / skipped | CER | WER | Exact | In candidates | Works offline |
|---|---|---|---|---|---|---|---|
| | v1 | | | | | | |
