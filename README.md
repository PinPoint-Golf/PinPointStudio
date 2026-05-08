# PinPoint
A golf swing analysis app that extracts kinematic metrics using IMUs and Computer vision coupled with an AI coach to diagnose and explain your swing

## Documentation
- [Building Instructions](BUILDING.md) - How to resolve dependencies and build PinPoint.

Initial prototyping is underway using IMUs and high-speed industrial cameras. Support is currently implemented for:
- **Witmotion IMUs** WT9011DCL IMUs (BLE/Serial)
- **Aravis**: Generic GenICam support for industrial cameras (Linux/macOS/Windows).
- **Spinnaker SDK**: Teledyne/FLIR industrial cameras (Windows only).
- **Standard Cameras**: UVC-compliant webcams and OS-native camera backends.

Built with Qt 6.10 and C++20, the project currently utilizes:
- **Whisper (via whisper.cpp)**: Local high-performance speech-to-text.
- **ONNX Runtime & Kokoro**: Local text-to-speech.
- **Azure Speech Services**: Cloud fallback for STT and TTS.
- **Espeak-ng & libsamplerate**: Phoneme tokenization and audio processing.
- **Vulkan/CUDA**: GPU acceleration for AI models.

It will be published as an open source desktop application (for use in golf studios) and will be developed as a smartphone app once the basic concepts have been proven (for use on the range).
