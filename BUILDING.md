# Building PinPoint

This document outlines the dependencies and steps required to build PinPoint from source on Linux, macOS, and Windows.

## General Requirements

- **CMake**: Version 3.16 or higher.
- **Qt 6.10**: Required components include Quick, QuickControls2, SerialPort, Bluetooth, Multimedia, Network, WebSockets, Concurrent, and ShaderTools.
- **C++ Compiler**: A compiler supporting C++17/20 (GCC 9+, Clang 10+, or MSVC 2022).

---

## Linux

PinPoint uses `pkg-config` to locate system libraries.

### 1. Install Build Essentials
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
```

### 2. Install Qt 6.10
It is recommended to use the [Qt Online Installer](https://www.qt.io/download-qt-installer) to ensure you have version 6.10 or higher. Ensure the following modules are selected:
- Qt Quick
- Qt Quick Controls 2
- Qt Serial Port
- Qt Bluetooth
- Qt Multimedia
- Qt Network
- Qt WebSockets
- Qt Shader Tools

### 3. System Dependencies
```bash
# Required for Aravis (Industrial Camera support)
sudo apt install libglib2.0-dev libaravis-0.8-dev

# Optional: espeak-ng (if not found, CMake will build it from source)
sudo apt install libespeak-ng-dev
```

### 4. GPU Acceleration (Optional)
For hardware acceleration in Whisper (STT) and ONNX Runtime (TTS):
- **Vulkan (Recommended)**: Install the `vulkan-sdk` or `libvulkan-dev`.
- **CUDA**: Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) if you have an NVIDIA GPU and prefer CUDA over Vulkan.

---

## macOS

On macOS, dependencies are best managed via [Homebrew](https://brew.sh/).

### 1. Install Homebrew and Dependencies
```bash
# Install Homebrew if not already present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install build tools and libraries
brew install cmake qt@6 espeak-ng aravis
```

### 2. Qt Path
Ensure the Qt 6.10+ binaries are in your PATH or provided to CMake via `-DCMAKE_PREFIX_PATH`.
```bash
export PATH="$(brew --prefix qt@6)/bin:$PATH"
```

### 3. Architecture Note
- **Apple Silicon (M1/M2/M3)**: PinPoint uses CoreML for ONNX Runtime acceleration.
- **Intel Macs**: ONNX Runtime will fall back to CPU for better compatibility and performance with certain models.

---

## Windows

### 1. Visual Studio
Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) with the **"Desktop development with C++"** workload.

### 2. Qt 6.10
Install Qt 6.10+ via the [Qt Online Installer](https://www.qt.io/download-qt-installer). Make sure to install the MSVC 2022 64-bit component.

### 3. GPU Acceleration (Optional)
- **Vulkan**: Install the [Vulkan SDK](https://vulkansdk.lunarg.com/).
- **CUDA**: Install the [CUDA Toolkit 12.x](https://developer.nvidia.com/cuda-toolkit).
    - *Note: PinPoint is configured to use CUDA 12 for ONNX Runtime. If a different version is detected, it may fall back to DirectML.*

### 4. Aravis (Optional)
If you require industrial camera support on Windows, you will need to provide the Aravis library and headers. You can set the `ARAVIS_ROOT` environment variable to the directory containing Aravis.

---

## Build Instructions

Once dependencies are installed, you can build PinPoint using the following commands:

```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt/6.10.x/compiler_arch
cmake --build . --config Release
```

### Build Options
You can toggle certain features at configure time:
- `-DWITH_CUDA=ON/OFF`: Enable/disable CUDA support (Default: ON).
- `-DWITH_DIRECTML=ON/OFF`: Enable/disable DirectML on Windows (Default: ON).
- `-DWITH_COREML=ON/OFF`: Enable/disable CoreML on macOS ARM64 (Default: ON).
