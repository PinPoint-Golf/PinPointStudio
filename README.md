# PinPoint

A golf swing analysis app that extracts kinematic metrics using IMUs and computer vision, coupled with an AI coach to diagnose and explain your swing.

## Documentation
- [Building Instructions](BUILDING.md) — How to resolve dependencies and build PinPoint.

## Overview

Initial prototyping is underway using IMUs and high-speed industrial cameras. The app currently has four main tabs:

### IMU
- **Witmotion WT901BLE67** — BLE 6-axis IMU capture (accelerometer, gyroscope, Euler angles)
- **3D orientation visualiser** — labelled cube driven by corrected quaternion; matches physical device orientation
- **Auto-initialisation on connect** — sets vertical mounting, 6-axis algorithm, 100 Hz output rate, and zeros orientation to current position
- **Zero button** — re-zeroes orientation on demand for mid-session repositioning
- **Live data rate** — 2-second rolling Hz average shown in the UI
- **Battery indicator** — colour-coded BAT: N% badge in the toolbar, polled via register 0x64 every 60 s

### Audio
- **Speech-to-text**: Whisper.cpp (local, GPU-accelerated) with Azure Speech fallback for CPU-only systems
- **Text-to-speech**: Kokoro TTS (local, ONNX Runtime) with Azure Neural Voice fallback for CPU-only systems

### Video
- **Camera backends**: UVC webcams, Aravis (GenICam industrial cameras), Spinnaker (Teledyne/FLIR)
- **Pose estimation**: MoveNet SinglePose Lightning and Thunder via ONNX Runtime — real-time skeleton overlay on the live feed
- **GPU acceleration**: CoreML (Apple Silicon), CUDA 12/13 (NVIDIA on Linux/Windows)

### Film
- **YouTube download**: Bundled yt-dlp fetches videos from YouTube (Premium quality, browser cookie auth) to a local cache — no re-download on repeat analysis
- **On-demand annotation**: Pause on a frame, click Annotate — runs a person segmentation model (u2netp) to isolate the golfer, blurs the background to suppress interference, then runs MoveNet for a clean pose estimate
- **Skeleton overlay**: Background-blurred frame displayed with the MoveNet skeleton drawn on top
- **Scrubbing**: Live frame preview while dragging the seek slider

## Technology

Built with **Qt 6.11** and **C++20**.

| Component | Technology |
|---|---|
| UI | Qt Quick / QML |
| Speech-to-text | whisper.cpp (Vulkan/CUDA) + Azure Speech REST |
| Text-to-speech | Kokoro (ONNX Runtime) + Azure Neural Voice |
| Pose estimation | MoveNet Lightning/Thunder (ONNX Runtime) |
| Person segmentation | u2netp (ONNX Runtime) |
| Video download | yt-dlp (bundled binary) |
| GPU acceleration | Vulkan, CUDA (12 + 13), CoreML |
| Image processing | OpenCV |
| IMU | Witmotion WT901BLE67 via Qt Bluetooth LE |

## Local Files

PinPoint reads and writes files in several locations. Platform paths shown for Linux; macOS and Windows equivalents are noted in brackets.

### Application data directory
`~/.local/share/PinPoint/` (macOS: `~/Library/Application Support/PinPoint/`, Windows: `%APPDATA%\PinPoint\`)

| Path | What | When |
|---|---|---|
| `models/whisper/<model>.bin` | Whisper STT model | Copied automatically from the CMake build cache at build time |
| `models/kokoro/` | Kokoro TTS ONNX model + voice data | Downloaded automatically from HuggingFace on first launch when a GPU is available |
| `film-cache/<video_id>.mp4` | Downloaded YouTube videos | Written by yt-dlp on demand; never auto-deleted |

### Application settings
`~/.config/PinPoint/PinPoint.conf` (macOS: `~/Library/Preferences/PinPoint.plist`, Windows: Registry `HKCU\Software\PinPoint`)

| Key | What | Written when |
|---|---|---|
| `secrets/assemblyaiApiKey` | AssemblyAI API key | First launch if `ASSEMBLYAI_API_KEY` env var is set |
| `secrets/azureTtsApiKey` | Azure TTS key | First launch if `AZURE_TTS_API_KEY` env var is set |
| `secrets/azureSttApiKey` | Azure STT key | First launch if `AZURE_STT_API_KEY` env var is set |
| `stt/modelPath` | Override path to Whisper model | Only if set manually |

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
| `~/imu_log_<timestamp>.txt` | IMU session log | Save Log button in Capture tab |

### External network activity
The app only contacts external services when explicitly configured:
- **Azure Speech** (STT/TTS) — if an Azure API key is present
- **AssemblyAI** (STT) — if an AssemblyAI API key is present
- **YouTube** (via yt-dlp) — when downloading a video in the Film tab, using your browser's stored cookies

## Roadmap

- Two-camera 3D pose reconstruction (triangulate occluded joints from a second viewpoint)
- Kinematic metric extraction from pose sequences
- AI coach integration
- Smartphone app (once core concepts proven on desktop)

It will be published as an open-source desktop application for use in golf studios.
