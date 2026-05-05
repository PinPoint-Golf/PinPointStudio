# PinPoint
A golf swing analysis app that extracts kinematic metrics using IMUs and Computer vision coupled with an AI coach to diagnose and explain your swing

Initial prototyping will take place H2 of 2026 using 3x Witmotion WT9011DCL MPU9250 IMUs placed on the sacrum, upper thorax T3, and the T12 junction. This will be coupled with spatial data captured via a high speed camera, likely supported via the Pylon SDK..

Built with Qt and C++ the initial dependencies will include OpenSim, OpenPose, OpenCV, Claude AI. STT/TTS via Whisper, MS Azure Speech and ONNX/Kokoro. It is anticipated that once a working prototype is available support for alternatives will be added.

It will be published as an open source desktop application (for use in golf studios) and will be developed as a smartphone app once the basic concepts have been proven (for use on the range).
