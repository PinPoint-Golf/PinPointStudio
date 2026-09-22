<p align="left">
    <img src="src/Resources/icons/pinpointstudio_1024.png" width="150" />
  </p>

# PinPoint Studio

PinPoint Studio is a free, open-source, cross-platform desktop application for golf swing
analysis. It brings together high-speed cameras, phones, wearable sensors and launch monitors,
measures the swing with on-device computer vision, and explains what it found — without
sending your data anywhere unless you choose to.

It is built for golfers, coaches and researchers, and runs on Windows, macOS and Linux.

> **Status:** beta (v0.1). Capture, analysis, review and diagnostics work end to end; the
> ground-forces and AI-coach sessions are still in development.

<img width="2560" height="1440" alt="beta3-screenshot" src="https://github.com/user-attachments/assets/c060ba3b-2927-4380-8c58-a34cc4c424a2" />

## Key features

- **Multi-camera capture** — USB webcams, GenICam industrial cameras (Aravis) and Teledyne FLIR
  high-speed cameras (Spinnaker), with a dedicated close-up impact camera.
- **Phone as a camera** — an iPhone running PinPoint Capture joins a session over WiFi or a USB
  cable, on an encrypted link.
- **Markerless swing tracking** — whole-body pose estimation (ViTPose), club shaft and clubhead
  tracking with no tape or markers, and ball detection.
- **Automatic shot detection** — shots are detected from IMU impact, impact sound or the launch
  monitor, and each one is analysed and saved as it happens.
- **Swing metrics** — the ten P-positions (address to finish), tempo, club delivery, body
  rotation, posture, wrist angles and the kinematic sequence, each shown with how much it can be
  trusted.
- **Diagnostics** — swing faults and strengths are named against normative ranges, with the
  likely causes and drills, for each shot and across a session.
- **Wrist analysis** — Witmotion IMUs, or a HackMotion wG3 sensor, for lead-wrist flexion,
  extension and cocking through the swing.
- **Launch monitors** — Foresight GCQuad (via FSX2020), plus any device that has a GSPro Open
  Connect bridge (Garmin R10, Rapsodo MLM2PRO, SkyTrak+ and others). Readings are paired to the
  swing that produced them.
- **Review** — synchronised replay of every camera with overlays, charts and a phase timeline,
  plus a swing library that keeps every session on disk.
- **Ground-truth markup** — frame-accurate labelling of the P-positions in the app, for
  validating and tuning the analysis.
- **Voice** — local speech-to-text (whisper.cpp) and text-to-speech (Kokoro), with optional cloud
  fallbacks.
- **Local-first and private** — analysis runs on the machine, using GPU acceleration where
  available (CUDA, Vulkan, CoreML, Metal). There is no telemetry, and cloud services are off unless
  you configure them.
- **In-app updates** — signed installers for Windows and macOS that update themselves in place.
  A self-updating Linux AppImage is in progress; until then, Linux builds from source.

## Getting it

Installers are published on the
[Releases page](https://github.com/PinPoint-Golf/PinPointStudio/releases). To build from source,
see **[BUILDING.md](BUILDING.md)**.

## Repository layout

| Path | What's there |
|---|---|
| `src/` | The application, one folder per subsystem (see below) |
| `tests/` | The umbrella test build. Each suite lives beside its code in `src/<area>/tests/` |
| `tools/` | Offline and lab tools: SwingLab (replays the analysis pipeline over recorded swings), shaft/ball/impact labs, launch-monitor fakes, packaging scripts |
| `packaging/` | Windows installer and appcast scripts, macOS packaging assets |
| `cmake/` | Build modules: Qt discovery, the QML module, fonts, OpenSSL, code signing |
| `third_party/` | Small vendored sources (the ESKF orientation filter) |
| `docs/` | Documentation, organised by audience (see below) |

Inside `src/`:

| Folder | Subsystem |
|---|---|
| `Gui/` | The Qt Quick / QML interface and its C++ models |
| `Video/`, `Pose/` | Camera backends; pose, ball and person-segmentation models |
| `Analysis/`, `Metrics/`, `Diagnostics/` | Swing analysis pipeline, metric catalogue, fault diagnostics |
| `Buffer/` | The lock-free event buffer that time-aligns every sensor |
| `IMU/`, `LaunchMonitor/`, `Ppcp/`, `Audio/` | Sensors, launch monitors, phone capture, microphones and shot sound |
| `Export/` | Swing video and the `swing.json` document |
| `STT/`, `TTS/`, `LLM/`, `Secrets/` | Speech, the AI coach, API-key storage |
| `Update/`, `Core/`, `Resources/`, `Shaders/` | In-app updates, shared infrastructure, bundled assets, GPU shaders |

## Documentation

| Where | For |
|---|---|
| [BUILDING.md](BUILDING.md) | Dependencies, building on each platform, packaging, running the tests |
| [`docs/user/`](docs/user) | Using the app: [diagnostics guide](docs/user/pinpoint-diagnostics-guide.md), [wrist calibration](docs/user/wristcalibration.md), [phone capture](docs/user/phone_capture.md), UX design |
| [`docs/developer/`](docs/developer) | Developer guides: [analysis pipeline](docs/developer/analysis_pipeline_developer_guide.md), [testing](docs/developer/testing_developer_guide.md), [SwingLab](docs/developer/swinglab_developer_guide.md), event buffer, diagnostics, metrics |
| [`docs/design/`](docs/design) | Design records for each subsystem, and the [QML design system](docs/design/pinpoint_qml_design_system.md) |
| [`docs/reference/`](docs/reference) | File formats ([`swing.json`](docs/reference/swing_json_schema.md), [swing folders](docs/reference/swing_folder_layout.md)), sign conventions, device protocols, normative data |
| [`docs/validation/`](docs/validation), [`docs/research/`](docs/research) | Validation protocols, corpus results and research notes |
| `docs/implementation/` | Working plans and release runbooks |

## Related projects

These sister repositories are used as dependencies:

- [libppcp](https://github.com/PinPoint-Golf/libppcp) — the PinPoint Capture Protocol, shared with the phone app
- [PinPoint Capture](https://github.com/PinPoint-Golf/PinPointCapture) — the iPhone capture app
- [libwrist](https://github.com/PinPoint-Golf/libwrist) — HackMotion wG3 sensor driver
- [libgspro](https://github.com/PinPoint-Golf/libgspro) — GSPro Open Connect server

## Licence

PinPoint Studio is licensed under the GNU General Public License, version 2 or later. The
distributed binaries are conveyed under GPL-3.0 because of the components they link. See
[LICENSE](LICENSE), and [LICENSEDEPS.md](LICENSEDEPS.md) for every third-party component and its
terms.
