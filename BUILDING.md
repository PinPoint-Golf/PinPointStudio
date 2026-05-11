# Building PinPoint

This document outlines the dependencies and steps required to build PinPoint from source on Linux, macOS, and Windows.

## General Requirements

- **CMake**: 3.16 or higher (3.24+ recommended — `DOWNLOAD_EXTRACT_TIMESTAMP` support).
- **Qt 6.11**: Quick, QuickControls2, Quick3D, SerialPort, Bluetooth, Multimedia, Network, WebSockets, Concurrent, ShaderTools.
- **C++ Compiler**: C++17/20 capable — GCC 9+, Clang 10+, or MSVC 2022.

---

## Linux

PinPoint uses `pkg-config` to locate system libraries.

### 1. Install Build Essentials
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
```

### 2. Install Qt 6.11
Use the [Qt Online Installer](https://www.qt.io/download-qt-installer). Select the following modules:
- Qt Quick / Qt Quick Controls 2 / Qt Quick 3D
- Qt Serial Port, Qt Bluetooth, Qt Multimedia
- Qt Network, Qt WebSockets, Qt Concurrent, Qt Shader Tools

### 3. System Dependencies
```bash
# Industrial camera support (Aravis v0.8.x)
sudo apt install libglib2.0-dev libaravis-0.8-dev

# Pose estimation and video pre-processing
sudo apt install libopencv-dev

# Optional: espeak-ng (built from source if absent)
sudo apt install libespeak-ng-dev
```

### 4. GPU Acceleration (Optional)

#### Whisper (STT)
- **Vulkan** (recommended — works on NVIDIA, AMD, Intel):
  ```bash
  sudo apt install libvulkan-dev shaderc
  ```
- **CUDA**: Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit). Vulkan is preferred and checked first; CUDA is the fallback when Vulkan headers are absent.

#### ONNX Runtime (TTS / pose estimation)
- **CUDA 12**: ORT 1.26.0 uses the `linux-x64-gpu` package — requires matching CUDA 12 runtime.
- **CUDA 13**: Not yet wired up in the Linux CMake path (only Windows); falls back to CPU for ORT.
- **CPU**: Used automatically when no matching CUDA is found.

---

## macOS

Dependencies are best managed via [Homebrew](https://brew.sh/).

### 1. Install Dependencies
```bash
brew install cmake espeak-ng aravis opencv
```

### 2. Install Qt 6.11
Use the [Qt Online Installer](https://www.qt.io/download-qt-installer) or:
```bash
brew install qt@6
export PATH="$(brew --prefix qt@6)/bin:$PATH"
```

### 3. GPU Acceleration

| Platform | Whisper (STT) | ONNX Runtime (TTS / pose) |
|---|---|---|
| Apple Silicon (M1/M2/M3/M4) | Metal (via ggml) | CoreML (ORT 1.26.0) |
| Intel Mac | CPU only | CPU only (ORT pinned to 1.20.1 — last Intel release) |

No additional GPU SDK installation is needed on Apple Silicon — CoreML and Metal are part of the OS.

---

## Windows

### 1. Visual Studio
Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) with the **"Desktop development with C++"** workload.

### 2. Qt 6.11
Install via the [Qt Online Installer](https://www.qt.io/download-qt-installer). Select the **MSVC 2022 64-bit** component.

### 3. GPU Acceleration

#### Whisper (STT) — ggml backends
Vulkan is detected and preferred over CUDA. Install whichever you have:
- **Vulkan SDK**: [vulkan.lunarg.com](https://vulkan.lunarg.com/) — works on NVIDIA, AMD, and Intel Arc.
- **CUDA Toolkit**: [developer.nvidia.com/cuda-toolkit](https://developer.nvidia.com/cuda-toolkit) — CUDA fallback when Vulkan headers are absent. Any recent CUDA version works.

#### ONNX Runtime (TTS / pose estimation) — CUDA EP
ORT 1.26.0 ships two Windows GPU packages. CMake selects automatically based on the installed CUDA Toolkit major version:

| Installed CUDA | ORT package selected | EP in app |
|---|---|---|
| CUDA 12.x | `onnxruntime-win-x64-gpu-1.26.0.zip` | CUDA |
| CUDA 13.x | `onnxruntime-win-x64-gpu_cuda13-1.26.0.zip` | CUDA |
| Other / none | `onnxruntime-win-x64-1.26.0.zip` | CPU |

> **Note:** DirectML was discontinued from ORT packages after version 1.24.4 and is no longer supported.

#### cuDNN 9 (required for ORT CUDA EP)
The ORT CUDA EP depends on **cuDNN 9** at runtime. Install it from the [NVIDIA cuDNN download page](https://developer.nvidia.com/cudnn) (free developer account required). Use the **Windows installer** — it places the DLLs under `C:\Program Files\NVIDIA\CUDNN\v9.x\bin\<cuda_major>.<minor>\x64\`.

CMake automatically copies the matching cuDNN DLLs next to the executable at POST_BUILD, so no PATH changes are needed after installation.

> **cuDNN must be installed before running CMake configure** — the glob that finds the DLLs runs at configure time.

### 4. Spinnaker SDK (Optional)
For Teledyne/FLIR industrial cameras, install the [Spinnaker SDK](https://www.teledyneflir.com/products/spinnaker-sdk/) to the default location: `C:\Program Files\Teledyne\Spinnaker`.

### 5. Aravis (Optional)
Set the `ARAVIS_ROOT` environment variable to your Aravis installation directory.

### 6. OpenCV (Optional but recommended)
Download from [opencv.org](https://opencv.org/releases/). CMake probes `C:\opencv\build` and `C:\tools\opencv\build` automatically. For a custom location: `-DOpenCV_DIR=C:\path\to\opencv\build`.

---

## Build Instructions

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt/6.11.x/compiler_arch
cmake --build . --config Release
```

On first configure, CMake downloads all required models and binaries (see table below). Subsequent configures reuse the cache in `build/_deps/` — only a full directory wipe triggers re-downloads.

---

## Automatically Downloaded Dependencies

The following are fetched at `cmake ..` time — no manual steps required:

| Dependency | What | Cached size | Condition |
|---|---|---|---|
| ONNX Runtime 1.26.0 | Prebuilt shared library (platform/CUDA matched) | ~10–80 MB | Always |
| whisper.cpp v1.7.2 | Built from source | — | Always |
| espeak-ng 1.52.0 | Built from source | — | Only if not found on system |
| libsamplerate 0.2.2 | Built from source | — | Always |
| `ggml-base.en.bin` | Whisper STT model (Hugging Face) | ~148 MB | Always |
| MoveNet Lightning | ONNX pose model (Hugging Face) | ~9 MB | When OpenCV present |
| MoveNet Thunder | ONNX pose model (Hugging Face) | ~30 MB | When OpenCV present |
| u2netp | ONNX person segmentation model | ~4.7 MB | When OpenCV present |
| yt-dlp | Platform binary for YouTube download | ~15 MB | Always |

If a download fails, the affected feature is disabled but the rest of the build continues normally. Re-run CMake to retry failed downloads.

---

## GPU Acceleration Summary by Platform

| Feature | Windows CUDA 12 | Windows CUDA 13 | Linux CUDA 12 | macOS ARM64 | macOS Intel |
|---|---|---|---|---|---|
| Whisper STT | Vulkan or CUDA | Vulkan or CUDA | Vulkan or CUDA | Metal | CPU |
| Kokoro TTS (ORT) | CUDA | CUDA | CUDA | CoreML | CPU |
| MoveNet pose (ORT) | CUDA | CUDA | CUDA | CoreML | CPU |
| Person segmenter (ORT) | CUDA | CUDA | CUDA | CoreML | CPU |

Vulkan is preferred over CUDA for Whisper because it works across GPU vendors (NVIDIA, AMD, Intel Arc) without needing the CUDA Toolkit.

---

## Build Options

| Option | Default | Description |
|---|---|---|
| `-DWITH_CUDA=ON/OFF` | ON | Enable CUDA EP for ONNX Runtime (auto-disabled on version mismatch) |
| `-DWITH_COREML=ON/OFF` | ON | Enable CoreML EP on macOS ARM64 |
| `-DASSEMBLYAI_API_KEY=<key>` | — | Seed AssemblyAI API key into settings at build time |
| `-DOpenCV_DIR=<path>` | — | Path to OpenCV CMake config (Windows, non-standard location) |

---

## Film Tab — YouTube Download

The Film tab uses a bundled **yt-dlp** binary to download YouTube videos to a local cache. No separate yt-dlp installation is needed.

To use YouTube Premium quality, log into YouTube in your browser before downloading — the app reads your browser's cookies. On Linux, Brave is the recommended browser; Chrome and Chromium require the `secretstorage` Python module which is not available in the bundled binary.
