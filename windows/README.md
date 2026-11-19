# windows/ — TabInk Windows host

Runs as a normal desktop user-session process, **not** a Session 0 service. Design: [architecture.md §3, §5, §6](../docs/architecture.md#5-native-windows-pen-input).

| Path | Language | Responsibility |
|---|---|---|
| `core/injection/` | C++20 | `CreateSyntheticPointerDevice(PT_PEN)` / `InjectSyntheticPointerInput`, stroke state machine |
| `core/capture/` | C++/WinRT | Windows Graphics Capture of a user-selected window |
| `core/encode/` | C++ | D3D11 → Media Foundation H.264 low-delay encoder |
| `core/mapping/` | C++ | `MappingSnapshot`, physical-pixel transforms, per-monitor DPI v2 |
| `core/transport/` | C++ | TLS listeners on loopback 27180/27181, protocol codec |
| `ui/` | C# .NET 10 (WPF or WinUI 3) | Tray, settings, pairing QR. Talks to the core over an ACL'd named pipe. |
| `tests/pen-diagnostics/` | C++ or C# | P0 synthetic-pen spike and diagnostic target |

No kernel driver in MVP or v1. WinTab is unsupported.

## Status

| Path | State |
|---|---|
| `core/injection/pen_injector.*` | **Started.** `PT_PEN` injector with a strict hover/down/move/up/cancel state machine, automatic eraser flip, never-stuck-down teardown. Unit-tested. |
| `tests/pen-diagnostics/` | **P0.3 tool** `pen_diag`: self-test (PASS), patterns and replay into real apps. See its [README](tests/pen-diagnostics/README.md) for results and findings. |
| `tests/unit/` | Unit tests (minimal harness; framework pending D5) |
| everything else | Not started (MVP) |

Build and test from the repo root:

```bash
windows\build.cmd test
```

**Key P0.3 finding:** the synthetic pen reaches only one display (the built-in panel on this laptop) and takes that display's local pixels. See roadmap decision D7.
