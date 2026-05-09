# Building PinPoint

This document outlines the dependencies and steps required to build PinPoint from source on Linux, macOS, and Windows.

## General Requirements

- **CMake**: Version 3.16 or higher.
- **Qt 6.11**: Required components include Quick, QuickControls2, Quick3D, SerialPort, Bluetooth, Multimedia, Network, WebSockets, Concurrent, and ShaderTools.
- **C++ Compiler**: A compiler supporting C++17/20 (GCC 9+, Clang 10+, or MSVC 2022).

---

## Linux

PinPoint uses `pkg-config` to locate system libraries.

### 1. Install Build Essentials
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
```

### 2. Install Qt 6.11
Use the [Qt Online Installer](https://www.qt.io/download-qt-installer) to install Qt 6.11 or higher. Select the following modules:
- Qt Quick / Qt Quick Controls 2 / Qt Quick 3D
- Qt Serial Port
- Qt Bluetooth
- Qt Multimedia
- Qt Network / Qt WebSockets
- Qt Shader Tools
- Qt Concurrent

### 3. System Dependencies
```bash
# Required for Aravis (Industrial Camera support) v0.8.x
sudo apt install libglib2.0-dev libaravis-0.8-dev

# Optional: espeak-ng (if not found, CMake will build it from source)
sudo apt install libespeak-ng-dev

# Required for pose estimation and video pre-processing
sudo apt install libopencv-dev
```

### 4. GPU Acceleration (Optional)
For hardware acceleration in Whisper (STT) and ONNX Runtime (TTS/pose estimation):
- **Vulkan (Recommended)**:
  ```bash
  sudo apt install libvulkan-dev shaderc
  ```
- **CUDA**: Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) for NVIDIA GPUs.

---

## macOS

On macOS, dependencies are best managed via [Homebrew](https://brew.sh/).

### 1. Install Homebrew and Dependencies
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake qt@6 espeak-ng aravis opencv
```

### 2. Qt Path
```bash
export PATH="$(brew --prefix qt@6)/bin:$PATH"
```

### 3. Architecture Note
- **Apple Silicon (M1/M2/M3)**: ONNX Runtime uses CoreML acceleration.
- **Intel Macs**: ONNX Runtime falls back to CPU.

---

## Windows

### 1. Visual Studio
Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) with the **"Desktop development with C++"** workload.

### 2. Qt 6.11
Install Qt 6.11+ via the [Qt Online Installer](https://www.qt.io/download-qt-installer). Select the MSVC 2022 64-bit component.

### 3. GPU Acceleration (Optional)
- **Vulkan**: Install the [Vulkan SDK](https://vulkansdk.lunarg.com/).
- **CUDA**: Install the [CUDA Toolkit 12.x](https://developer.nvidia.com/cuda-toolkit).
  - CUDA and DirectML are mutually exclusive in the ONNX Runtime package. If both are enabled, CUDA takes priority.

### 4. Spinnaker SDK (Optional)
For Teledyne/FLIR industrial cameras, install the [Spinnaker SDK](https://www.teledyneabbott.com/products/spinnaker-sdk/) to the default location: `C:\Program Files\Teledyne\Spinnaker`.

### 5. Aravis (Optional)
Set the `ARAVIS_ROOT` environment variable to your Aravis installation directory.

### 6. OpenCV (Optional but recommended)
Download from [opencv.org](https://opencv.org/releases/). The build system probes `C:\opencv\build` and `C:\tools\opencv\build` automatically. For a custom location: `-DOpenCV_DIR=C:\path\to\opencv\build`.

---

## Build Instructions

```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt/6.11.x/compiler_arch
cmake --build . --config Release
```

### Automatically Downloaded Dependencies

The following are fetched automatically during `cmake ..` — no manual installation required:

| Dependency | What | Size | When |
|---|---|---|---|
| ONNX Runtime | Prebuilt shared library (platform-matched) | ~10 MB | Always |
| Whisper.cpp | Built from source | — | Always |
| espeak-ng | Built from source | — | Only if not found on system |
| MoveNet Lightning | ONNX pose model (Hugging Face) | ~9 MB | When OpenCV present |
| MoveNet Thunder | ONNX pose model (Hugging Face) | ~30 MB | When OpenCV present |
| u2netp | ONNX person segmentation model | ~4.7 MB | When OpenCV present |
| yt-dlp | Platform binary for YouTube download | ~15 MB | Always |

All models are cached in `build/_deps/` and are not re-downloaded on subsequent `cmake` runs unless the build directory is wiped. If a download fails, the affected feature is disabled but everything else builds normally.

### Film Tab — YouTube Download

The Film tab uses a bundled **yt-dlp** binary to download YouTube videos to a local cache (`~/.local/share/PinPoint/film-cache/` on Linux). No separate yt-dlp installation is needed.

To use YouTube Premium quality, log into YouTube in your browser before downloading. The app reads your browser's cookies automatically. On Linux, Brave is the recommended browser — Chrome and Chromium require the `secretstorage` Python module which is not available in the bundled binary.

### Build Options

| Option | Default | Description |
|---|---|---|
| `-DWITH_CUDA=ON/OFF` | ON | Enable CUDA support for ONNX Runtime and Whisper |
| `-DWITH_DIRECTML=ON/OFF` | ON | Enable DirectML on Windows |
| `-DWITH_COREML=ON/OFF` | ON | Enable CoreML on macOS ARM64 |
| `-DASSEMBLYAI_API_KEY=<key>` | — | Seed AssemblyAI API key into settings |
| `-DOpenCV_DIR=<path>` | — | Path to OpenCV CMake config (Windows, non-standard location) |
