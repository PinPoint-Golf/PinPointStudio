# PinPoint

A golf swing analysis app that extracts kinematic metrics using IMUs and computer vision, coupled with an AI coach to diagnose and explain your swing.

## Documentation
- [Building Instructions](BUILDING.md) — How to resolve dependencies and build PinPoint.

## Overview

Initial prototyping is underway using IMUs and high-speed industrial cameras. The app currently has four main tabs:

### IMU
- **Witmotion WT901BLE67** — BLE 6-axis IMU capture (accelerometer, gyroscope, Euler angles)

### Audio
- **Speech-to-text**: Whisper.cpp (local, GPU-accelerated) with Azure Speech fallback for CPU-only systems
- **Text-to-speech**: Kokoro TTS (local, ONNX Runtime) with Azure Neural Voice fallback for CPU-only systems

### Video
- **Camera backends**: UVC webcams, Aravis (GenICam industrial cameras), Spinnaker (Teledyne/FLIR)
- **Pose estimation**: MoveNet SinglePose Lightning and Thunder via ONNX Runtime — real-time skeleton overlay on the live feed
- **GPU acceleration**: CoreML (Apple Silicon), CUDA (NVIDIA), DirectML (Windows)

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
| GPU acceleration | Vulkan, CUDA, CoreML, DirectML |
| Image processing | OpenCV |
| IMU | Witmotion WT901BLE67 via Qt Bluetooth LE |

## Roadmap

- Two-camera 3D pose reconstruction (triangulate occluded joints from a second viewpoint)
- Kinematic metric extraction from pose sequences
- AI coach integration
- Smartphone app (once core concepts proven on desktop)

It will be published as an open-source desktop application for use in golf studios.
