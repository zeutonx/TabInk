# Development Setup

Toolchain status of the main dev PC, checked 18 Nov 2026 (Windows 11 build 26300).

## Already installed

| Tool | Version | Used for |
|---|---|---|
| Git | — | |
| VS Build Tools 2026 | 18.9, MSVC 14.51 | C++ host (`vcvars64.bat` present) |
| Windows SDK | 10.0.26100.0 | Pointer injection, WGC, Media Foundation |
| .NET **runtime** | 10.0.10 | Running only. **No SDK yet.** |
| Python | 3.12.10, 3.13.15, 3.14.7 (`py` launcher) | OCR sidecar. Use **3.12**. |
| Java | 25 (Oracle) | Too new for Android Gradle. Use Android Studio's bundled JBR instead. |
| Node.js | — | Optional (KaTeX tooling, browser tests) |
| Docker Engine | 29.8 **inside WSL 2 Ubuntu** (not on the Windows PATH) | OCR sidecar and Android build images. Run `docker` via `wsl`. |

## Still needed

| Install | Why | Phase |
|---|---|---|
| **Android Studio** (includes SDK, platform-tools/`adb`, JBR 21) | Android app and `adb reverse` | P0 |
| **.NET 10 SDK** | C# tray/UI, and the C# P/Invoke pen spike if D1 = C# | P0 |
| **CMake** (+ Ninja), or the "C++ CMake tools for Windows" Build Tools component | C++ core build (if D5 = CMake) | P0/MVP |
| C++/WinRT | WGC from C++. Comes as the `Microsoft.Windows.CppWinRT` NuGet package, not a separate install. | MVP |
| GitHub CLI `gh` (optional) | PRs and issues from the terminal | any |
| Xournal++, OneNote desktop | P0 pen-injection targets | P0 |
| Samsung Second Screen (on tablet) / spacedesk | P0 baselines | P0 |

The winget package IDs below are the common ones. Check them with `winget search` before installing.

```bash
winget install Google.AndroidStudio
```

```bash
winget install Microsoft.DotNet.SDK.10
```

```bash
winget install Kitware.CMake Ninja-build.Ninja
```

```bash
winget install GitHub.cli
```

## Tablet (Galaxy Tab S10 FE)

1. Settings → About tablet → Software information → tap **Build number** 7 times.
2. Developer options → enable **USB debugging**.
3. Connect over USB and accept the RSA prompt. `adb devices` should list the tablet.
4. Check S Pen settings, Air Command in particular, in case it takes the side button away from apps.

ADB is an MVP-only requirement (see [threat-model.md](threat-model.md)).

## Things on this PC that can interfere with tests

- **SuperDisplay Virtual Adapter** and **Easy&Light Display HUB Virtual Display** are installed. SuperDisplay is useful as a P0 baseline. Turn both off when testing TabInk pen injection and capture, so another virtual pen or display isn't mixed into the results.
- **Hybrid GPU** (Intel UHD + NVIDIA RTX 5060 Laptop). Capture and encode must use the same adapter. Record which one each test used.

## OCR environment (v1, not needed yet)

```bash
py -3.12 -m venv recognition/.venv
```

Pin dependencies in `recognition/requirements.lock`. Don't use the global interpreter.
