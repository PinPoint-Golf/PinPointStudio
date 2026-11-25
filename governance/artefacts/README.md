# Artefacts

The governing specifications: the documents a change is checked against. Each has one owning role, who approves any change to it before it is built ([`../controls/decide.md`](../controls/decide.md)); each records its own changes in its own change log or history.

| Artefact | Owner | What it governs |
|---|---|---|
| [`information_model.md`](information_model.md) | Solution Architect | The domain's objects, ids, files, ownership, rings and contexts, and the decision register |
| [`design_system/`](design_system/README.md) | Design Lead | Every colour, font, size and space (`Theme.qml` and the generated `tokens.json`), and the shared components |
| [`sign_conventions.md`](sign_conventions.md) | Solution Architect | The sign of every measured angle and direction |
| [`normative_reference.md`](normative_reference.md) | Coaching Lead | The swing norms the diagnostics grade against |
| [`imu_frame_contract.md`](imu_frame_contract.md) | Solution Architect | The orientation frames every motion-sensor consumer depends on |

`artefact_paths_check` (ctest) fails if a tracked file cites an artefact by a path it no longer has.
