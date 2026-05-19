# PinPoint

PinPoint is a free, open source and cross-platform desktop application for serious golf swing analysis. It combines high-speed industrial cameras, Bluetooth IMUs, and on-device AI to build a complete picture of the swing — without sending data to the cloud unless you configure it to.

The app is currently in active prototyping. The core capture and analysis pipeline is functional; the coaching and session-history layers are in development.

![PinPoint — Editorial light](docs/aesthetic/editorial_opening_light.png)

The long term goal is to exploit computer vision and wearables to analyse golf movements and mechanistically determine your kinematic sequence aka Lateral-Rock-Twist-Jump, extract key golf swing metrics like X-Factor and tilt, working with the full swing or specialist shots such as pitching and in the sand, wrist angles to examine cupping, cocking and flipping, estimated ground forces to support the kinematic sequence analysis. 

Our ambition is to be a platform that can be used by golfers, coaches and researchers to improve everyone's golfing ability and understanding of the golf swing.

## Documentation
- [Building Instructions](BUILDING.md) — How to resolve dependencies and build PinPoint.
- [UX Design](docs/pinpoint-ux-design.md) — UI structure, navigation, and interaction design rationale.
- [Persona UX Assessment](docs/pinpoint-persona-assessment.md) — UX evaluation against three user archetypes; identifies gaps and design priorities.
- [EventBuffer Design](docs/event_buffer_design.md) — Architecture and design rationale for the lock-free EventBuffer.
- [EventBuffer Developer Guide](docs/event_buffer_developer_guide.md) — Tutorial covering usage, threading model, and integration patterns.
- [WT901BLE67 Protocol Reference](docs/WT901BLE67_Protocol.md) — Packet formats, register map, and BLE transport details for the Witmotion IMU.
- [Aesthetic Design Concepts](docs/aesthetic/pinpoint-aesthetic-concepts.md) — Three visual design directions (Editorial, Instrument, Studio) across light and dark themes.
- [QML Design System](docs/PINPOINT_QML_DESIGN_SYSTEM.md) — Token system, typography rules, and component patterns; read before writing any QML.

---

## UI shell

The interface uses a left-side navigation rail with five mode buttons and an athlete avatar at the top.

| Mode | Status | Description |
|---|---|---|
| **Swing** | Active | Multi-camera capture, pose estimation, and ¼-speed swing replay |
| **Wrist** | Placeholder | IMU-based wrist kinematics (requires an athlete) |
| **GRF** | Placeholder | Ground reaction force analysis (requires an athlete) |
| **Coach** | Placeholder | AI coaching output (requires an athlete) |

Wrist, GRF, and Coach redirect to the Welcome screen until at least one athlete has been created.

The **Settings** button (bottom of the rail) cycles through all six visual themes — three aesthetics (Editorial, Instrument, Studio) × two modes (light, dark). See [Aesthetic Design Concepts](docs/aesthetic/pinpoint-aesthetic-concepts.md).

| Editorial | Instrument | Studio |
|---|---|---|
| ![Editorial light](docs/aesthetic/editorial_summary_light.png) | ![Instrument light](docs/aesthetic/instrument_summary_light.png) | ![Studio light](docs/aesthetic/studio_summary_light.png) |

---

## Features

### Athlete management

Every session belongs to an athlete. The athlete management flow is the entry point to the app.

- **Create athlete** — Required fields: name, handedness. Recommended: height, weight, handicap, primary club. Optional: driver speed target, notes/tags.
- **Athlete picker** — Shows the three most-recently-active athletes as cards, plus a full searchable list. The selected athlete's initials appear in the rail avatar.
- **Delete athlete** — Destructive action available in the picker with a single click on the highlighted athlete.
- **Persistence** — All athlete records stored in `QSettings` (INI format); survives restarts. Heights stored in ft, weights in lb regardless of entry unit.
- **Navigation guard** — Wrist, GRF, and Coach modes require at least one athlete; they redirect to the Welcome screen if the roster is empty.

### Swing — multi-camera video analysis

- **Multi-camera support** — Select any combination of discovered cameras; each gets its own side-by-side view with independent pose estimation, stats, and model selector. Start/Stop controls all cameras simultaneously.
- **Camera backends** — UVC webcams, Aravis (GenICam industrial cameras), Spinnaker (Teledyne/FLIR).
- **Spinnaker pipeline** — Raw Bayer bytes captured with no CPU demosaic on the hot path; a custom `QQuickRhiItem` runs a bilinear GPU Bayer demosaic shader at display rate while the pose estimator receives OpenCV-demosaiced frames at its already-throttled rate.
- **Pose estimation** — MoveNet SinglePose Lightning and Thunder via ONNX Runtime — real-time skeleton overlay on each live feed, switchable per camera.
- **Swing replay** — On ball-lost, the last 5 seconds of captured footage replay automatically at ¼ speed with a `REPLAY ¼×` overlay; a pulsing badge marks the replaying camera view. Ball detection drives buffer capture so the replay always covers the full swing.
- **GPU acceleration** — CoreML (Apple Silicon), CUDA 12/13 (NVIDIA on Linux/Windows).

### IMU — wrist motion capture

- **Device** — Witmotion WT901BLE67 BLE 6-axis IMU (accelerometer, gyroscope, Euler angles, quaternion).
- **3D orientation visualiser** — Labelled cube driven by the corrected quaternion; matches the physical device orientation in real time.
- **Auto-initialisation** — Sets vertical mounting, 6-axis algorithm, 100 Hz output rate, and zeros orientation to current position on every connect.
- **Zero button** — Re-zeroes orientation on demand for mid-session repositioning.
- **Rate selector** — Adjustable output rate (10 / 20 / 50 / 100 / 200 Hz).
- **Live data rate** — 2-second rolling Hz average shown in the UI.
- **Battery indicator** — Colour-coded BAT: N% badge, polled via register 0x64 every 60 s.
- **Auto-retry** — One automatic retry after a 45-second cooldown on failed connections (device requires ~40 s to exit cooldown after a rejected attempt).
- **Session log** — Timestamped per-record diagnostics; Save Log writes the full session to `~/imu_log_<timestamp>.txt`.

### Audio — speech interface

- **Speech-to-text** — Whisper.cpp (local, Vulkan/CUDA GPU-accelerated) with Azure Speech REST fallback for CPU-only systems. Backend badge in the UI shows GPU / Cloud / Apple; clickable to toggle when cloud fallback is available.
- **Text-to-speech** — Kokoro TTS (local, ONNX Runtime) with Azure Neural Voice fallback for CPU-only systems. Same badge/toggle pattern.
- **Latency display** — Per-request latency shown next to each badge (e.g. `523 ms`).

### Film — video annotation

- **YouTube download** — Bundled yt-dlp fetches videos from YouTube (Premium quality, browser cookie auth) to a local cache; no re-download on repeat analysis.
- **On-demand annotation** — Pause on a frame, click Annotate: runs a person segmentation model (u2netp) to isolate the golfer, blurs the background, then runs MoveNet for a clean pose estimate.
- **Skeleton overlay** — Background-blurred frame displayed with the MoveNet skeleton drawn on top.
- **Scrubbing** — Live frame preview while dragging the seek slider.

---

## Technology

Built with **Qt 6.11** and **C++20**.

| Component | Technology |
|---|---|
| UI | Qt Quick / QML (Qt 6.11) |
| Speech-to-text | whisper.cpp (Vulkan / CUDA) + Azure Speech REST |
| Text-to-speech | Kokoro ONNX Runtime + Azure Neural Voice |
| Pose estimation | MoveNet Lightning / Thunder (ONNX Runtime) |
| Person segmentation | u2netp (ONNX Runtime) |
| Video download | yt-dlp (bundled binary) |
| GPU acceleration | Vulkan, CUDA 12 + 13, CoreML (Apple Silicon) |
| Image processing | OpenCV 3.0+ |
| IMU | Witmotion WT901BLE67 via Qt Bluetooth LE |
| Athlete data | QSettings (INI format, `~/.config/Pinpoint/Pinpoint.ini`) |

---

## Local files

PinPoint reads and writes files in several locations. Platform paths shown for Linux; macOS and Windows equivalents are noted in brackets.

### Application data directory
`~/.local/share/PinPoint/` (macOS: `~/Library/Application Support/PinPoint/`, Windows: `%APPDATA%\PinPoint\`)

| Path | What | When |
|---|---|---|
| `models/whisper/<model>.bin` | Whisper STT model | Copied from the CMake build cache at build time |
| `models/kokoro/` | Kokoro TTS ONNX model + voice data | Downloaded from HuggingFace on first launch when a GPU is detected |
| `film-cache/<video_id>.mp4` | Downloaded YouTube videos | Written by yt-dlp on demand; never auto-deleted |

### Application settings
`~/.config/Pinpoint/Pinpoint.ini` (macOS: `~/Library/Preferences/Pinpoint.plist`, Windows: Registry `HKCU\Software\Pinpoint`)

| Key | What | Written when |
|---|---|---|
| `athletes/<uuid>/name` | Athlete name | On athlete creation |
| `athletes/<uuid>/…` | Full athlete record | On creation or update |
| `currentAthleteUuid` | UUID of the selected athlete | On athlete selection |
| `secrets/assemblyaiApiKey` | AssemblyAI API key | First launch if `ASSEMBLYAI_API_KEY` env var is set |
| `secrets/azureTtsApiKey` | Azure TTS key | First launch if `AZURE_TTS_API_KEY` env var is set |
| `secrets/azureSttApiKey` | Azure STT key | First launch if `AZURE_STT_API_KEY` env var is set |

> **Note:** API keys written to settings persist even after the env var is removed. To clear a key, delete the relevant entry from the settings file directly.

### Next to the executable
`<install dir>/models/`

| File | What |
|---|---|
| `movenet_singlepose_lightning.onnx` | MoveNet Lightning pose model (~9 MB) |
| `movenet_singlepose_thunder.onnx` | MoveNet Thunder pose model (~30 MB) |
| `u2netp.onnx` | Person segmentation model (~4.7 MB) |
| `yt-dlp` / `yt-dlp.exe` | Bundled yt-dlp binary for YouTube download |

These are copied from the CMake build cache automatically — no manual placement needed.

### User home directory (on demand)

| File | What | Trigger |
|---|---|---|
| `~/pinpoint_audio_<timestamp>.wav` | Recorded audio session | Save Audio button |
| `~/imu_log_<timestamp>.txt` | IMU session log | Save Log button |

### External network activity
The app only contacts external services when explicitly configured:
- **Azure Speech** (STT / TTS) — if an Azure Cognitive Services key is present
- **AssemblyAI** (STT) — if an AssemblyAI API key is present
- **YouTube** (via yt-dlp) — when downloading a video in the Film tab, using your browser's stored cookies

---

## Roadmap

- **Session recording** — attach captured swing data to the selected athlete; build session history
- **Two-camera 3D pose reconstruction** — triangulate occluded joints from a second viewpoint (multi-camera capture is already in place)
- **Kinematic metric extraction** — derive club head speed, hip/shoulder rotation, lag angle from pose sequences and IMU data
- **AI coach integration** — session-aware coaching output in the Coach mode
- **Wrist / GRF modes** — connect IMU and force plate data to the athlete and session model
- **Smartphone companion** — once core concepts are proven on desktop

It will be published as an open-source desktop application for use in golf studios and coaching facilities.
