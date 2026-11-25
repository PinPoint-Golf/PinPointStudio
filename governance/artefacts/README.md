# Artefacts

The governing documents: the specifications that decide whether a change is acceptable. They moved here with `git mv` in one scripted commit at the end of the platform refactor's Stage B Part 1, paths only and no content change; `artefact_paths_check` (ctest) fails if a tracked file still cites an old path[^move].

| Artefact | Owner | Path |
|---|---|---|
| The information model and its decision register | Solution Architect | `governance/artefacts/information_model.md` |
| The design system (README, component pages, variability page, generated `tokens.json`) | Design Lead | `governance/artefacts/design_system/` |
| The sign conventions | Solution Architect | `governance/artefacts/sign_conventions.md` |
| The normative reference | Coaching Lead | `governance/artefacts/normative_reference.md` |
| The IMU frame contract | Solution Architect | `governance/artefacts/imu_frame_contract.md` |

The edits each needs are drafted outside the repository, for their owners. The typeset rules in §15 of `docs/design/pinpoint_qml_design_system.md` are still to be folded into the design system's pages, a separate item (D13; Design Lead)[^move].

[^move]: Product Owner, 25 Nov 2026 (#195; decision D13 for the last three).
