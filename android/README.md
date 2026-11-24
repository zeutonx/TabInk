# android/ — TabInk tablet client

Kotlin app for the Galaxy Tab S10 FE. Design: [architecture.md §2](../docs/architecture.md#2-android-client).

Planned packages under `app/src/main/java/.../`:

| Package | Responsibility |
|---|---|
| `input/` | Custom `View`: `onTouchEvent`/`onHoverEvent`, historical samples, tool type, buttons, palm filter |
| `transport/` | TLS sockets to `127.0.0.1:27180/27181`, pen packet codec, high-priority sender |
| `video/` | `MediaCodec` H.264 decode → `SurfaceView` |
| `overlay/` | Front-buffered local ink preview (`androidx.graphics`) |
| `ui/` | Compose toolbar and settings, kept outside the ink surface |

Hot-path rules: no per-sample allocations if profiling shows them; no work on the UI thread beyond enqueueing; never drop historical samples.

## Gradle project

`android/` is the Gradle root: AGP 9.5.1 with built-in Kotlin, Gradle 9.9.1 (wrapper with a pinned checksum), compileSdk 36, minSdk 34.

| Module | Status |
|---|---|
| [`penlogger/`](penlogger/) | **P0.2 spike.** S Pen sample logger with live stats and CSV export. Throwaway. |
| [`nepaliink/`](nepaliink/) | **P0.4 spike.** Nepali handwriting recognition test (ML Kit). Throwaway. |
| `app/` | The real client. Not started; it begins in the MVP. |

Build everything in Docker from the repo root:

```bash
docker compose --profile android run --rm android-build ./gradlew assembleDebug
```
