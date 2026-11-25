# Artefacts

The governing documents: the specifications that decide whether a change is acceptable. They move here with `git mv` in one scripted commit at the end of the platform refactor's Stage B Part 1, with a check that no old path is still cited[^move]. Until then they stay at their current paths.

| Artefact | Owner | Current path |
|---|---|---|
| The information model and its decision register | Solution Architect | `docs/developer/information_model.md` |
| The design system (README, component pages, variability page, generated `tokens.json`) | Design Lead | `docs/design/system/` |
| The sign conventions | Solution Architect | `docs/design/pinpoint_sign_conventions.md` |
| The normative reference | Coaching Lead | `docs/reference/golf_swing_normative_reference.md` |
| The IMU frame contract | Solution Architect | `docs/design/imu_frame_contract.md` |

The edits each needs before or at the move are drafted outside the repository, for their owners.

[^move]: Product Owner, 25 Nov 2026 (#195; decision D13 for the last three).
