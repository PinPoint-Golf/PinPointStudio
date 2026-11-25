# Procedures

Step-by-step procedures. Each step names the control it carries out; a procedure adds no rule of its own.

| Procedure | For |
|---|---|
| [`commit.md`](commit.md) | Landing a finished change |
| [`cut-over.md`](cut-over.md) | Replacing and deleting code in a refactor |
| [`sweep.md`](sweep.md) | A run over many swings, on the primary platform's release build |
| [`imu-driver-tests.md`](imu-driver-tests.md) | The IMU and analysis unit tests, built standalone |
| [`qml-render.md`](qml-render.md) | Rendering one QML component without the app |

## Release

The controls these carry out are in [`controls/release.md`](../controls/release.md).

| Procedure | For |
|---|---|
| [`release-macos.md`](release-macos.md) | A macOS release: signing, notarizing, both architectures' DMGs and appcasts |
| [`release-windows.md`](release-windows.md) | A Windows release: the `-core` installer, its appcast and the `-cuda` runtime; its "Local release" section is the local test installer |
| [`release-linux.md`](release-linux.md) | A Linux AppImage release |
| [`code-signing-macos.md`](code-signing-macos.md) | macOS code signing: the development and release identities, keychains and the signing order |

## Capture and corpus collection

The controls these carry out are in [`controls/kinds-of-work.md`](../controls/kinds-of-work.md), "Data and analysis work".

| Procedure | For |
|---|---|
| [`corpus1_collection_protocol.md`](corpus1_collection_protocol.md) | Corpus 1: one golfer, wrist sensors only |
| [`corpus2_collection_protocol.md`](corpus2_collection_protocol.md) | Corpus 2: wrist sensors verified against the HackMotion as criterion (a verification process under AD-10's exception) |
| [`corpus3_collection_protocol.md`](corpus3_collection_protocol.md) | Corpus 3: calibrated cameras and several golfers |
| [`two_camera_capture_protocol.md`](two_camera_capture_protocol.md) | A calibrated face-on and down-the-line session with the launch monitor |
| [`instrumented_club_protocol.md`](instrumented_club_protocol.md) | Taping a club with retroreflective bands, and its measurement record |
| [`shaft_validation_protocol.md`](shaft_validation_protocol.md) | Validating and tuning the shaft tracker from vision alone |
