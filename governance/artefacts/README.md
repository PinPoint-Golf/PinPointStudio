# Artefacts

The reference documents a change is checked against, read on demand (never whole when large: `python3 tools/method/im_excerpt.py ID…` gives an excerpt of the information model). Changes to the information model, the lifecycle model and the design system are agreed briefly with the owner first ([`../controls/decide.md`](../controls/decide.md)), and the document is updated when ownership, an on-disk format or a lifecycle actually changes: no version bump or change-log row for a small edit; git keeps the history.

| Artefact | What it governs |
|---|---|
| [`requirements.md`](requirements.md) | What the app must do for golfers and coaches: each need, with why, its status and what it rests on. Hand-edited; a row that points to a rule only traces the need; the rule decides it |
| [`use_cases.md`](use_cases.md) and [`uc-map.csv`](uc-map.csv) | The requirements organised into epics and use cases, and the mapping of each requirement to its use cases |
| [`information_model.md`](information_model.md) | The domain's objects, ids, files, ownership, rings and contexts, and the decision register |
| [`design_system/`](design_system/README.md) | Every colour, font, size and space (`Theme.qml` and the generated `tokens.json`), and the shared components |
| [`sign_conventions.md`](sign_conventions.md) | The sign of every measured angle and direction |
| [`normative_reference.md`](normative_reference.md) | The swing norms the diagnostics grade against |
| [`imu_frame_contract.md`](imu_frame_contract.md) | The orientation frames every motion-sensor consumer depends on |
| [`lifecycle_model.md`](lifecycle_model.md) | The lifecycles of the application, the athlete, the session, the shot and the Rig: their states, their transitions and what each guarantees |
| [`test_strategy.md`](test_strategy.md) | What tests are for, where each level runs, "seen to fail" for bug fixes, and the on-demand traceability report |

How the artefacts sit beside the controls is in [`../README.md`](../README.md).

`artefact_paths_check` (ctest) fails if a tracked file cites an artefact by a path it no longer has.
