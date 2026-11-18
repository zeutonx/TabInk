# TabInk

**Use a Samsung Galaxy Tab S10 FE + S Pen as a precise pen display for one Windows app window, built for mathematical handwriting.**

TabInk streams a single Windows window you choose (OneNote, Xournal++, a PDF, a whiteboard) to the tablet over USB. S Pen strokes travel back as native Windows Ink pen input, with pressure, eraser and hover. In v1, you lasso a handwritten equation and TabInk turns it into LaTeX locally, ready to paste into Overleaf.

> **Status: pre-alpha. Phase P0 (feasibility spikes).** No usable app yet. See [docs/roadmap.md](docs/roadmap.md).

---

## Contents

- [Why TabInk](#why-tabink)
- [How it works](#how-it-works)
- [Requirements](#requirements)
- [Getting started](#getting-started)
- [Docker](#docker)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Roadmap](#roadmap)
- [Security](#security)
- [Contributing](#contributing)
- [License](#license)

## Why TabInk

Tools like Samsung Second Screen, spacedesk and SuperDisplay already mirror a PC screen to a tablet. TabInk aims at a narrower job they don't combine:

| Goal | What it means |
|---|---|
| **Selected-window precision** | Stream one app window at native-like scale, plus *zoom-to-write* for tiny subscripts |
| **Real Windows Ink** | Synthetic `PT_PEN` input with pressure, eraser and hover, not an emulated mouse |
| **Math-first input** | No aggressive smoothing. Dots, primes, `i`/`j` and 1–2 mm subscripts survive. |
| **Handwriting → LaTeX → Overleaf** | Local OCR (TexTeller/Pix2Text), an editable preview, then paste |

If an existing app already covers your workflow, use it. The design says so too ([architecture.md §0](docs/architecture.md#0-alternatives-reviewed-first)).

## How it works

```text
 Galaxy Tab S10 FE  (Kotlin)                          Windows 11  (C++20 core + C# UI)
 ┌──────────────────────────────┐                     ┌──────────────────────────────────┐
 │ S Pen MotionEvent + history  │──pen/ctrl :27180──▶ │ TLS session ─▶ synthetic PT_PEN  │──▶ target app
 │ palm filter, local ink layer │                     │                                  │      │
 │ MediaCodec H.264 ◀───────────│◀─── video :27181 ───│ H.264 encode ◀─ Windows Graphics │◀─────┘
 └──────────────────────────────┘                     │                 Capture (window) │
          USB · adb reverse · TLS 1.3 on both          │ Python OCR sidecar (v1, loopback)│
                                                      └──────────────────────────────────┘
```

- **Pen and video use separate channels**, so a slow video frame never delays a pen-up.
- The tablet draws a **local ink preview** immediately, then fades it out once the real stroke appears in the video.
- Coordinates map through a versioned `MappingSnapshot` in physical pixels. If the window moves mid-stroke, the stroke is canceled rather than redrawn in the wrong place.

The full design is in [docs/architecture.md](docs/architecture.md).

## Requirements

### Hardware

| Item | Requirement | Notes |
|---|---|---|
| Tablet | Samsung Galaxy Tab S10 FE + included S Pen | 90 Hz, 2304×1440, USB 2.0. Other Android stylus tablets may work but are untested. |
| PC | Windows 11 x64 with a GPU that has hardware H.264 encode | NVIDIA NVENC, Intel Quick Sync or AMD AMF |
| Cable | USB-C data cable (not charge-only) | USB 2.0 speed is enough |

### Software: to run TabInk (once it exists)

| Component | Requirement |
|---|---|
| Windows | Windows 11, desktop session (not a service), DPI-aware target apps |
| Android | Android 14+ on the tablet; **USB debugging enabled** (MVP only) |
| Target apps | Windows Ink apps: OneNote, Xournal++, Edge PDF… WinTab-only apps are **not** supported. |
| OCR (v1, optional) | Python 3.12 **or** Docker |

### Software: to develop TabInk

| Tool | Version | Used for |
|---|---|---|
| Git | any recent | |
| Android Studio | current stable (bundles JDK 21, SDK, `adb`) | Tablet app |
| Android SDK | platform `android-36`, build-tools `36.1.0` | Matches the Docker build image |
| Visual Studio 2026 or Build Tools | MSVC v14.5x, "Desktop development with C++" | Windows core |
| Windows SDK | 10.0.26100+ | Pointer injection, WGC, Media Foundation |
| CMake + Ninja | 3.30+ | Windows core build (pending decision D5) |
| .NET SDK | 10.0 | Windows tray/settings UI |
| Python | **3.12** | OCR sidecar and tooling. 3.13/3.14 are not supported by every OCR dependency. |
| Docker (optional) | Docker Desktop or Docker Engine in WSL 2 | OCR sidecar and reproducible Android builds |

Per-component Python requirements are pinned in [`recognition/requirements/`](recognition/requirements/). Full setup steps, including what this dev PC still lacks, are in [docs/dev-setup.md](docs/dev-setup.md).

## Getting started

> Only the OCR sidecar stub and the Docker images can be built today. The Android and Windows parts start in P0.

**1. Clone**

```bash
git clone https://github.com/zeutonx/TabInk.git
```

**2. Prepare the tablet.** Enable Developer options and USB debugging, connect over USB, accept the RSA prompt, then check that it's listed:

```bash
adb devices
```

**3. OCR sidecar, native Python** (from `recognition/`):

```bash
py -3.12 -m venv .venv
```

```bash
.venv\Scripts\pip install -r requirements/dev.txt
```

```bash
.venv\Scripts\python -m pytest -q
```

```bash
.venv\Scripts\uvicorn tabink_ocr.server:app --host 127.0.0.1 --port 8765
```

**4. Android app / Windows host.** Not yet. See [P0 in the roadmap](docs/roadmap.md#p0--feasibility-spikes-2-weeks-throwaway-code-is-fine).

## Docker

Docker covers the parts of TabInk that can run in a container:

| Service | What | Command |
|---|---|---|
| `ocr` | OCR sidecar on **127.0.0.1:8765** only | `docker compose up -d ocr` |
| `ocr` + engine | Same, with TexTeller / Pix2Text / pix2tex baked in | `OCR_ENGINE=texteller docker compose up -d --build ocr` |
| `android-build` | JDK 21 + Android SDK for clean, reproducible APK builds | `docker compose --profile android run --rm android-build ./gradlew assembleDebug` |

**The Windows host can't run in Docker.** Synthetic pen injection, Windows Graphics Capture and the hardware encoder all need your real Windows desktop session and GPU. Windows containers have no interactive desktop.

Notes:
- The OCR port is published on loopback only, the container runs as a non-root user with a read-only root filesystem, and models are cached in the `ocr-models` volume.
- Engine images are large (PyTorch CPU + models). Copy [`.env.example`](.env.example) to `.env` to set defaults.
- On Windows, Docker Desktop and Docker Engine inside WSL 2 both work. Run commands from the repo root.

## Repository layout

```text
TabInk/
├── android/              Kotlin tablet client (Gradle project created in P0)
├── windows/              C++20 core (capture, encode, injection, mapping, transport) + C# UI
│   └── tests/pen-diagnostics/   P0 synthetic-pen spike
├── recognition/          Python OCR sidecar
│   ├── tabink_ocr/       Service (stub: /health, /v1/recognize → 501)
│   ├── requirements/     *.in sources + pinned *.txt lock files, one set per engine
│   ├── tests/            pytest
│   └── Dockerfile
├── protocol/             Shared schema + golden test vectors (Kotlin and C++ must both pass)
├── tools/                Latency benchmark, packet replay, calibration
├── docker/android-build/ Android build image
├── docs/                 Design, protocol, threat model, test matrix, roadmap, setup
├── docker-compose.yml
└── .github/workflows/ci.yml
```

## Documentation

| Doc | Contents |
|---|---|
| [architecture.md](docs/architecture.md) | Full system design, the source of truth |
| [roadmap.md](docs/roadmap.md) | Phases, gates, open decisions (D1–D6) |
| [protocol-v1.md](docs/protocol-v1.md) | Wire format, the 32-byte pen packet, golden vector |
| [threat-model.md](docs/threat-model.md) | Threats, mitigations, security acceptance tests |
| [test-matrix.md](docs/test-matrix.md) | App compatibility, latency, calibration, precision, OCR results |
| [dev-setup.md](docs/dev-setup.md) | Toolchain install and tablet setup |

## Roadmap

| Phase | Scope | Gate |
|---|---|---|
| **P0** (now) | Baseline existing tools; Android pen logger; Windows `PT_PEN` diagnostic | Two apps accept real pen input. 30 min of math with no lost pen-ups. |
| **MVP** | USB, pairing, one window, H.264, mapping, pen/eraser/undo | 30-min proof session, p95 alignment ≤2 px, zero unauthorized injection |
| **v1** | Ink preview, zoom-to-write, Wi-Fi, OCR → Overleaf | 100-equation OCR benchmark |
| **v2** | AOA USB, WebRTC/QUIC, extensions, installers | Evidence-driven |

## Security

TabInk can inject input into your desktop, so it's built to fail closed. Every channel uses TLS 1.3 with paired keys, even over USB. Injection is limited to the selected window and pauses when focus changes. Screen content and LaTeX aren't logged by default. Details: [docs/threat-model.md](docs/threat-model.md). Please report security issues privately to the maintainer rather than in a public issue.

## Contributing

Early-stage, single-maintainer project. Before writing code, read [architecture.md](docs/architecture.md), [roadmap.md](docs/roadmap.md) and the hard rules in [CLAUDE.md](CLAUDE.md). The wire format changes only through [protocol-v1.md](docs/protocol-v1.md) and its golden vectors.

## License

Not chosen yet. It will be MIT or Apache-2.0 after a dependency and model-weight license audit (roadmap decision D4). Until then, all rights reserved.
