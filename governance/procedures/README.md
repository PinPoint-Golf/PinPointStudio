# Procedures

Step-by-step procedures. Each step names the control it carries out; a procedure adds no rule of its own.

| Procedure | For |
|---|---|
| [`commit.md`](commit.md) | Landing a finished change |
| [`cut-over.md`](cut-over.md) | Replacing and deleting code in a refactor |
| [`sweep.md`](sweep.md) | A run over many swings, on the primary platform's release build |
| [`imu-driver-tests.md`](imu-driver-tests.md) | The IMU and analysis unit tests, built standalone |
| [`qml-render.md`](qml-render.md) | Rendering one QML component without the app |

Procedures already written in `docs/`, which move here later:

- the release runbooks, including the Windows installer and the Linux AppImage: `docs/implementation/macos_release_runbook.md`, `windows_release_runbook.md` (its "Local release" section is the local test installer), `linux_release_runbook.md`;
- the code-signing guide: `docs/developer/macos_code_signing_developer_guide.md`;
- the corpus collection protocols: `docs/validation/corpus1_collection_protocol.md`, `corpus2_collection_protocol.md`, `corpus3_collection_protocol.md`, `two_camera_capture_protocol.md`, `instrumented_club_protocol.md`, `shaft_validation_protocol.md`.
