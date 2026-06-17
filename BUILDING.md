# Building PinPoint Studio

This document outlines the dependencies and steps required to build PinPoint Studio from source on Linux, macOS, and Windows.

## General Requirements

- **CMake**: 3.16 or higher (3.24+ recommended — `DOWNLOAD_EXTRACT_TIMESTAMP` support).
- **Qt 6.11**: Quick, QuickControls2, Quick3D, SerialPort, Bluetooth, Multimedia, Network, WebSockets, Concurrent, ShaderTools, GuiPrivate, CorePrivate (the Private modules back the RHI Bayer-demosaic video item).
- **C++ Compiler**: C++20 capable (`CMAKE_CXX_STANDARD 20`, required) — GCC 10+, Clang 12+, or MSVC 2022.
- **OpenCV**: 3.0+ (required on all platforms — pose preprocessing, swing export demosaic).
- **FFmpeg** (optional): libavcodec/libavformat/libavutil/libswscale dev libraries with **libx264**, located via `pkg-config`. Powers the swing-export H.264/MP4 encoder. Without it the app still builds and runs, but swing video export is disabled. GPL FFmpeg builds are fine — the project is GPLv2+.

---

## Linux

PinPoint Studio uses `pkg-config` to locate system libraries.

### 1. Install Build Essentials
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config git
```
`git` is required at configure time — CMake clones whisper.cpp, libsamplerate, Eigen, and (if absent on the system) espeak-ng via FetchContent.

### 2. Install Qt 6.11
Use the [Qt Online Installer](https://www.qt.io/download-qt-installer). Select the following modules:
- Qt Quick / Qt Quick Controls 2 / Qt Quick 3D
- Qt Serial Port, Qt Bluetooth, Qt Multimedia
- Qt Network, Qt WebSockets, Qt Concurrent, Qt Shader Tools

### 3. System Dependencies
```bash
# Industrial camera support (Aravis v0.8.x)
sudo apt install libglib2.0-dev libaravis-dev

# Camera device identifier lookup (USB serial numbers for V4L2 cameras)
sudo apt install libudev-dev

# Pose estimation and video pre-processing
sudo apt install libopencv-dev

# Swing video export (H.264 via libx264) — optional but recommended
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev

# Optional: espeak-ng (built from source if absent)
sudo apt install libespeak-ng-dev
```

### 4. GPU Acceleration (Optional)

#### Whisper (STT)
- **Vulkan** (recommended — works on NVIDIA, AMD, Intel):
  ```bash
  sudo apt install libvulkan-dev glslc
  ```
  `glslc` is the shader compiler ggml's Vulkan backend invokes at build time (CMake's `Vulkan_GLSLC_EXECUTABLE`).
- **CUDA**: Install the [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit). Vulkan is preferred and checked first; CUDA is the fallback when Vulkan headers are absent.

#### ONNX Runtime (TTS / pose estimation)
`WITH_CUDA` defaults ON. **Unlike the Windows path, the Linux CMake does not probe the installed CUDA version.** When `WITH_CUDA=ON` it unconditionally downloads `onnxruntime-linux-x64-gpu-1.26.0.tgz`, which is built against **CUDA 12.x + cuDNN 9**. There is no CUDA-13 Linux package selection (the `gpu_cuda13` variant exists only for Windows).

- **CUDA 12.x runtime + cuDNN 9 installed** → the CUDA EP loads and ORT runs on the GPU.
- **CUDA 13.x installed (no CUDA 12 alongside)** → the CUDA-12-built EP fails to load at runtime because it imports CUDA-12-versioned shared libraries (e.g. `libcublasLt.so.12`, `libcudnn.so.9`), so ORT **silently falls back to CPU**. To get GPU ORT under CUDA 13, install a CUDA 12.x runtime alongside it (the EP only needs the CUDA 12 `.so`s present, not as the default toolkit), or wait for a CUDA-13 Linux package to be wired into CMake.
- **No CUDA, incompatible CUDA, or `-DWITH_CUDA=OFF`** → ORT runs on CPU. Passing `-DWITH_CUDA=OFF` also avoids the larger GPU download, fetching the CPU-only `onnxruntime-linux-x64-1.26.0.tgz` instead.

> cuDNN 9 is a separate install from the CUDA Toolkit. On Linux install the cuDNN 9 runtime matching CUDA 12 from NVIDIA's apt repository (e.g. `libcudnn9-cuda-12`) or the [cuDNN download page](https://developer.nvidia.com/cudnn).

> **Intel integrated GPU (including Linux on MacBook Pro Intel):**
> ORT's only GPU execution provider on Linux is CUDA. Intel UHD and Iris integrated graphics cannot use it.
> OpenVINO would be the natural alternative EP for Intel hardware, but it requires building ORT from source
> (`-DONNXRUNTIME_USE_OPENVINO=ON`) — Intel stopped bundling their own ORT build in the OpenVINO toolkit
> after the 2024.x series. Pre-built `libonnxruntime_providers_openvino.so` is not available for download.
> ORT therefore runs on CPU on any Linux machine without an NVIDIA GPU.

---

## macOS

Dependencies are best managed via [Homebrew](https://brew.sh/).

### 1. Install Dependencies
```bash
brew install cmake espeak-ng aravis opencv ffmpeg
```
CMake injects the Homebrew prefix into `PKG_CONFIG_PATH` automatically, so `ffmpeg` is found without any extra configuration.

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

### 6. OpenCV (Required, 3.0+)
Download from [opencv.org](https://opencv.org/releases/). CMake probes `C:\opencv\build` and `C:\tools\opencv\build` automatically. For a custom location: `-DOpenCV_DIR=C:\path\to\opencv\build`.

### 7. FFmpeg (Optional — swing video export)
The swing-export H.264 encoder needs FFmpeg dev libraries with **libx264**, found via `pkg-config`:

1. **pkg-config**: install one if not present, e.g. `choco install pkgconfiglite` (or [pkgconf](https://github.com/pkgconf/pkgconf)). It must be on PATH at configure time.
2. **FFmpeg**: download a `win64-gpl-shared` build from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases) (e.g. `ffmpeg-n7.1-latest-win64-gpl-shared-7.1.zip`) and extract it so that `C:\ffmpeg\lib\pkgconfig` exists. CMake probes `C:\ffmpeg` and `C:\tools\ffmpeg`; for a custom location pass `-DFFMPEG_DIR=`. The BtbN shared builds include MSVC import libraries and relocatable `.pc` files — no patching needed. The **gpl** variant is required for libx264 (the project is GPLv2+, so GPL FFmpeg is fine).

The linked FFmpeg DLLs (`avcodec`, `avformat`, `avutil`, `swscale`, `swresample`) are copied next to the executable at POST_BUILD. If FFmpeg was installed after a previous configure, clear the cached probe before reconfiguring:
```
cmake -U "FFMPEG*" -U "__pkg_config_checked_FFMPEG" <builddir>
```

> **windeployqt caveat:** Qt Multimedia bundles its own LGPL FFmpeg with the **same DLL names** (`avcodec-61.dll`, …) but *without* libx264. Running `windeployqt` overwrites the copied DLLs and breaks swing export ("libx264 encoder not available"). After deploying, re-copy the five DLLs from `C:\ffmpeg\bin` over the deployed ones — Qt Multimedia works fine against the GPL build (same ABI, superset of features).

---

## Build Instructions

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt/6.11.x/compiler_arch
cmake --build . --config Release
```

On first configure, CMake downloads all required models and binaries (see table below). Subsequent configures reuse the cache in `build/_deps/` — only a full directory wipe triggers re-downloads.

---

## Packaging & Release (Linux — AppImage with in-app update)

Linux ships as a single self-updating **AppImage** — the native analogue of the
macOS (Sparkle) / Windows (WinSparkle) update story. Design:
[`docs/design/linux_update.md`](docs/design/linux_update.md); build plan:
[`docs/implementation/linux_update_impl.md`](docs/implementation/linux_update_impl.md).

**Build the AppImage:**
```bash
# Host tools (once): linuxdeploy, linuxdeploy-plugin-qt, appimagetool,
# appimageupdatetool, zsync (zsyncmake). All from the AppImageCommunity releases.
export SIGN_KEY=<your-release-gpg-key-fpr>
export CMAKE_PREFIX=~/Qt/6.11.0/gcc_64
export CUDA_LIB_DIR=/usr/local/cuda-12/lib64   # for GPU ORT
export CUDNN_LIB_DIR=/usr/lib/x86_64-linux-gnu # cuDNN 9
tools/package_appimage.sh                      # → dist/PinPointStudio-<ver>-x86_64.AppImage{,.zsync,.sig}
tools/package_appimage.sh --no-sign            # unsigned dev build (no in-app update trust)
```

The script resolves the version from `src/Core/version.h`, builds Release, bundles
Qt/QML + the heavy native deps (ORT, the **x264-capable** FFmpeg — same trap as the
`windeployqt` x264 note in the Windows section, OpenCV, Aravis, espeak-ng,
CUDA/cuDNN, optional Spinnaker) + the `appimageupdatetool` binary the app shells out
to, then
seals with `appimagetool` embedding the `gh-releases-zsync` update information and a
GPG signature.

> **Not yet validated end-to-end.** As of this commit the script encodes the recipe
> but has not been run on a clean VM (the dev host lacks the AppImage tooling). The
> P0 acceptance gate (clean-Ubuntu launch with BLE + cameras + GPU + x264 export) is
> still open — see the impl plan.

**Releasing:** tag exactly the `version.h` string (e.g. `v0.1-alpha1`) and push;
CI (`.github/workflows/release.yml`, P2) builds, signs, and attaches the three
artifacts to the GitHub Release. The in-app updater polls that release feed.

---

## Testing

PinPoint Studio has six standalone unit-test suites. **None are part of the application build** — the root `CMakeLists.txt` forces `BUILD_TESTING OFF`, so building the app never compiles them. Each suite is configured against its own source root and is wired into CTest.

### EventBuffer suite (`src/Buffer/tests`)

Lock-free ring, timeline merger, swing window, wait-flag, watchdog and thread-policy coverage, plus an adversarial producer/consumer fuzz test (8 tests). Uses GoogleTest (fetched automatically) and links the standalone `pinpoint_buffer` library. Tests are included only when `src/Buffer` is the top-level project.

```bash
cmake -S src/Buffer -B build/buffer-tests
cmake --build build/buffer-tests -j
ctest --test-dir build/buffer-tests --output-on-failure
```

Sanitizers are opt-in cache options on the `src/Buffer` project — use a separate build dir for each:

```bash
cmake -S src/Buffer -B build/buffer-asan -DPINPOINT_ENABLE_ASAN=ON   # (also -DPINPOINT_ENABLE_UBSAN=ON)
cmake -S src/Buffer -B build/buffer-tsan -DPINPOINT_ENABLE_TSAN=ON   # ThreadSanitizer
```

`latency_benchmark` is built but intentionally **not** registered with CTest — run it by hand: `./build/buffer-tests/tests/latency_benchmark`.

### Analysis suite (`src/Analysis/tests`)

Covers the shot-analyzer chain (phase signals, phase segmenter, metric extractor, banded swing scorer, full pipeline), wrist-angle math (static + live), IMU anatomical calibration, the host-side orientation filters (Madgwick + ESKF), the WT9011DCL / WT901BLE67 driver frame-parse and `eulerToQuat` override, the 3D-viz binding chain, the stored IMU sample frame, the shaft tracker (math + track assembly), the multi-modal shot arbiter, the per-session summary, the raw-frame decode, and the unified `swing.json` writer/reader round-trip from the Export subsystem (17 tests). Self-contained — own `main()` + `CHECK_NEAR`, no GoogleTest — compiling the handful of needed `.cpp` directly rather than linking an analysis library.

```bash
cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.x/gcc_64
cmake --build build/analyzer-tests -j
ctest --test-dir build/analyzer-tests --output-on-failure
```

Requires Qt6 Core/Gui (and Bluetooth, for the driver test) plus Eigen. Eigen is resolved automatically — first from an explicit `-DEIGEN_INCLUDE_DIR=…`, then from the app build's FetchContent copy (`build/*/_deps/eigen-src`), and finally by fetching 3.4.0 if neither is present. Configuring the app at least once first lets the tests reuse its Eigen with no extra download.

### Shot impact-detector suite (`src/IMU/tests`)

Truth-table coverage for the IMU impact detector (shot detection P1): a real strike fires exactly once; mat/ground taps, waggles, and slow swells are rejected; the refractory collapses double-hits; the orientation gate holds; `est_t` is the back-dated peak; 100 Hz and 200 Hz traces behave identically. Self-contained — own `main()` + printf `CHECK` macros, no GoogleTest. `impact_detector.h` is pure math, so the suite needs neither BLE nor an app build (Qt is linked only for parity with the other suites).

```bash
cmake -S src/IMU/tests -B build/imu-tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.x/gcc_64
cmake --build build/imu-tests -j
ctest --test-dir build/imu-tests --output-on-failure
```

### Acoustic onset-detector suite (`src/Audio/tests`)

Truth-table coverage for the acoustic onset detector (shot detection P2 / P4): a click fires sample-accurately; speech bursts, a sustained tone, tone cutoff, and ambient noise are rejected; the refractory holds; the onset → `est_t` back-dating math is checked; reverberant strikes still confirm (case G); and the absolute amplitude gate rejects quiet sounds while passing loud impacts and still rejecting a loud sustained tone (case H). Same self-contained harness — `onset_detector.h` is pure math, no audio device needed.

```bash
cmake -S src/Audio/tests -B build/audio-tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.x/gcc_64
cmake --build build/audio-tests -j
ctest --test-dir build/audio-tests --output-on-failure
```

### Calibrated ball-detection suite (`src/Pose/tests`)

Covers the OpenCV-only calibrated ball-detection core (`ball_model.h`): background/ball model fitting, theta derivation, multi-cue scoring (dim-studio golden case), gain invariance, drift severity; the calibration protocol (consecutive-clean round bookkeeping, failed-round evidence retention, profile save/load round-trip with schema-version rejection); and the `BallDetector` throttle contract (exactly one `ballDetected`/`detectionSkipped` per `detect()` on every path). Requires OpenCV (core/imgproc/features2d) and Qt6 Core/Gui — no app build.

```bash
cmake -S src/Pose/tests -B build/pose-tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.x/gcc_64
cmake --build build/pose-tests -j
ctest --test-dir build/pose-tests --output-on-failure
```

### Gui model/helper suite (`src/Gui/tests`)

Covers the review-screen model logic: `TimelineLabels` (the train-map 1-D label distribution solver, nearest/active searches, phase names) and `SwingSeriesModel` (playhead → table-row mapping). Self-contained — own `main()` + printf asserts, no GoogleTest — compiling the handful of Gui `.cpp` directly. Requires Qt6 Core/Gui/Qml (Qml because the Gui headers use `QML_ELEMENT`).

```bash
cmake -S src/Gui/tests -B build/gui-tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.x/gcc_64
cmake --build build/gui-tests -j
ctest --test-dir build/gui-tests --output-on-failure
```

---

## Automatically Downloaded Dependencies

The following are fetched at `cmake ..` time — no manual steps required:

| Dependency | What | Cached size | Condition |
|---|---|---|---|
| ONNX Runtime 1.26.0 | Prebuilt shared library (platform/CUDA matched) | ~10–80 MB | Always |
| ONNX Runtime GenAI 0.13.1 | Prebuilt shared library (local LLM engine; CUDA or CPU variant) | ~10–60 MB | `WITH_ORTGENAI=ON` (default; auto-off on Intel macOS) |
| whisper.cpp v1.7.2 | Built from source | — | Always |
| espeak-ng 1.52.0 | Built from source | — | Only if not found on system |
| libsamplerate 0.2.2 | Built from source | — | Always |
| Eigen 3.4.0 | Header-only (IMU EKF) | ~few MB | Always |
| `ggml-base.en.bin` | Whisper STT model (Hugging Face) | ~148 MB | Always |
| MoveNet Lightning | ONNX pose model (Hugging Face) | ~9 MB | Always |
| MoveNet Thunder | ONNX pose model (Hugging Face) | ~30 MB | Always |
| u2netp | ONNX person segmentation model | ~4.7 MB | Always |
| ViTPose-B wholebody | ONNX whole-body pose model (HuggingFace JunkyByte/easy_ViTPose) | ~330 MB | `WITH_VITPOSE=ON` (default) |
| yt-dlp | Platform binary for YouTube download | ~15 MB | Always |

If a download fails, the affected feature is disabled but the rest of the build continues normally. Re-run CMake to retry failed downloads.

> **Not downloaded at build time:** the local LLM model (Phi-4-mini) is fetched by the app itself on first run — into the per-user app-data directory, and only when a compatible GPU is present.

---

## GPU Acceleration Summary by Platform

| Feature | Windows CUDA 12 | Windows CUDA 13 | Linux CUDA 12 | Linux (Intel GPU) | macOS ARM64 | macOS Intel |
|---|---|---|---|---|---|---|
| Whisper STT | Vulkan or CUDA | Vulkan or CUDA | Vulkan or CUDA | Vulkan (Intel ANV) | Metal | CPU |
| Kokoro TTS (ORT) | CUDA | CUDA | CUDA | CPU | CoreML | CPU |
| MoveNet pose (ORT) | CUDA | CUDA | CUDA | CPU | CoreML | CPU |
| Person segmenter (ORT) | CUDA | CUDA | CUDA | CPU | CoreML | CPU |
| Local LLM (ORT GenAI) | CUDA | CUDA | CUDA | Gemini cloud¹ | built-in | Gemini cloud¹ |

¹ Without a local GPU (or on Intel macOS, where ORT GenAI has no prebuilt) the app skips the local Phi-4-mini download and uses the Gemini cloud backend instead.

Vulkan is preferred over CUDA for Whisper because it works across GPU vendors (NVIDIA, AMD, Intel Arc) without needing the CUDA Toolkit. The Intel ANV Vulkan driver covers integrated graphics on Linux (UHD, Iris, Xe); Intel Arc discrete cards also work via Vulkan.

ORT on Linux with Intel integrated graphics runs on CPU — see the note above on why OpenVINO EP is not supported.

---

## Build Options

| Option | Default | Description |
|---|---|---|
| `-DWITH_CUDA=ON/OFF` | ON | Enable CUDA EP for ONNX Runtime. Windows: auto-disabled to the CPU package when no CUDA 12/13 toolkit matches. Linux: always fetches the CUDA-12 GPU package (no version probe — see [Linux GPU notes](#4-gpu-acceleration-optional)) |
| `-DWITH_COREML=ON/OFF` | ON | Enable CoreML EP on macOS ARM64 |
| `-DWITH_VITPOSE=ON/OFF` | ON | Download and build ViTPose-B wholebody pose estimator (~330 MB model) |
| `-DWITH_MEDIAPIPE=ON/OFF` | OFF | Build MediaPipe BlazePose estimator (requires Python 3.10–3.13 + `qai-hub-models`, `torch`, `onnxscript`) |
| `-DWITH_ORTGENAI=ON/OFF` | ON | ONNX Runtime GenAI local LLM engine (auto-off on Intel macOS — falls back to Gemini cloud) |
| `-DPINPOINT_BUILD_TOOLS=ON/OFF` | OFF | Build the SwingLab offline analysis tools (`swinglab_run`) — see below |
| `-DPINPOINT_DEBUG_LEVEL=<n>` | 1 | Log verbosity (2 = startup info + warnings/errors) |
| `-DASSEMBLYAI_API_KEY=<key>` | — | Seed AssemblyAI API key into settings at build time |
| `-DOpenCV_DIR=<path>` | — | Path to OpenCV CMake config (Windows, non-standard location) |
| `-DFFMPEG_DIR=<path>` | — | FFmpeg root for swing export (Windows, non-standard location; must contain `lib/pkgconfig`) |

---

## SwingLab Offline Tools (Optional)

`swinglab_run` replays a recorded swing directory through the **production** analysis pipeline (same sources, same configs) with JSON-injectable tuning parameters and per-frame trace dumps — used for parameter sweeps and regression triage. It is **not built by default**; enable it with `-DPINPOINT_BUILD_TOOLS=ON`:

```bash
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt/6.11.x/compiler_arch -DPINPOINT_BUILD_TOOLS=ON
cmake --build . --target swinglab_run
```

It shares the app's heavy dependency set (OpenCV incl. videoio, ONNX Runtime, the ViTPose model), so configure the app at least once first to ensure those are present.

---

## Film Tab — YouTube Download

The Film tab uses a bundled **yt-dlp** binary to download YouTube videos to a local cache. No separate yt-dlp installation is needed.

To use YouTube Premium quality, log into YouTube in your browser before downloading — the app reads your browser's cookies. On Linux, Brave is the recommended browser; Chrome and Chromium require the `secretstorage` Python module which is not available in the bundled binary.
