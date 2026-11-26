# Artefacts

The governing specifications: the documents a change is checked against. Each has one owning role, who approves any change to it before it is built ([`../controls/decide.md`](../controls/decide.md)); each records its own changes in its own change log or history.

| Artefact | Owner | What it governs |
|---|---|---|
| [`requirements.md`](requirements.md) | Product Owner | What the app must do for golfers and coaches: each need, with why, its status and what it rests on. Hand-edited; the Coaching Lead approves the golf in the rows marked CL, and a row that points to a rule is decided by that rule's owner |
| [`use_cases.md`](use_cases.md) and [`uc-map.csv`](uc-map.csv) | Product Owner | The requirements organised into epics and use cases, and the mapping of each requirement to its use cases, which the traceability matrix reads |
| [`information_model.md`](information_model.md) | Solution Architect | The domain's objects, ids, files, ownership, rings and contexts, and the decision register |
| [`design_system/`](design_system/README.md) | Design Lead | Every colour, font, size and space (`Theme.qml` and the generated `tokens.json`), and the shared components |
| [`sign_conventions.md`](sign_conventions.md) | Solution Architect | The sign of every measured angle and direction |
| [`normative_reference.md`](normative_reference.md) | Coaching Lead | The swing norms the diagnostics grade against |
| [`imu_frame_contract.md`](imu_frame_contract.md) | Solution Architect | The orientation frames every motion-sensor consumer depends on |
| [`lifecycle_model.md`](lifecycle_model.md) | Solution Architect | The lifecycles of the application, the athlete, the session, the shot and the Rig: their states, their transitions and what each guarantees |
| [`test_strategy.md`](test_strategy.md) | Solution Architect | What counts as evidence that a requirement is met: the kinds, what each can prove, how long each lasts, what makes a test proof, and how the RTM reads it |
| [`rtm_observations.csv`](rtm_observations.csv) | Solution Architect | The observations register: one line per requirement an observation (a run, a probe, a visit, a release check) bears on, against a named build ([`test_strategy.md`](test_strategy.md) §7) |

`artefact_paths_check` (ctest) fails if a tracked file cites an artefact by a path it no longer has.
