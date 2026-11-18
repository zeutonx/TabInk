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

**P0:** a standalone pen-sample logger comes first (see [roadmap.md](../docs/roadmap.md#p02-android-pen-sample-logger-android)). Create the Gradle project with Android Studio.
