# REVIEW B1.7 · independent review (AG-21), 26 Nov 2026

**Verdict: SIGNED OFF on the diff, conditional on finding 1.** No code or doc change is needed. Three definition-of-done results (the app build, the design-system lint, the studio PC full suite with parity) did not exist yet when this review was written. They must be in NOTES before the commit. If any of them fails, the verdict becomes CHANGES NEEDED.

Reviewed: `build/wt-37c` against HEAD `008b8eb4`: the whole `git diff HEAD` (17 files) and `B1.7-hand-edits.patch`.

## The definition of done, bullet by bullet

| Bullet | Met? | Evidence |
|---|---|---|
| What 1: new header, the four structs unchanged | met | A Python byte compare of each struct (its comment block through `};`) between `git show HEAD:…/swing_analysis.h` and `capture_time_types.h`: ImuSegmentBinding (42 lines), BallSample2D (24), BallTrack2D (11), BallBaselineRef (24) are IDENTICAL. Namespace `pinpoint::analysis` (`capture_time_types.h:42`). `isValid` is a member and moved with it. `--color-moved=zebra`: 210 lines moved and 49 plain. Every plain line is a licence, comment, include or namespace line, so no line inside a struct changed. |
| What 2: `recording.h` drops `swing_analysis.h`, which includes the new header | met | `recording.h:45`, `swing_analysis.h:44` |
| What 3: the two headers moved by the tool and map | met | Both renames are 100% similarity. The map is `tools/refactor/maps/B1.7-shared.map`. No `Vocabulary/Analysis/{segment_role,trunk_calib_provenance}` path is left in live code or docs (`git grep`; only historical research and audit data). |
| What 4: the root `CMakeLists.txt` | met | `CMakeLists.txt:2940`, beside `recording.h` and `analysis_context.h`. No other CMake list names those two. |
| Check 3: self-contained, Shared includes only Qt | met | `capture_time_types.h:30-40`: Qt, `<cstdint>`, `<vector>`, `Buffer/types.h` (Acquisition), `Common/segment_role.h`, `Common/trunk_calib_provenance.h`. Each name the structs use has its include. `segment_role.h` includes only `<QString>`; `trunk_calib_provenance.h` only `<QJsonObject>` and `<QString>`. |
| Check 4: no includer of `recording.h` loses a type | met (static) | Transitive include closure, old tree against new: 7 files stop reaching `swing_analysis.h`. They are `recording.h`, `recording_integrity.h`, `recording_header.{h,cpp}`, `swing_exporter.cpp`, `swing_store_repository.cpp` and `tools/migrate/backfill_analysis_context.cpp`. None of them names a type from `swing_analysis.h` or its sub-headers (`ClubReview` comes from `effective_context.h`). Each std/Qt type they use (`optional`, `QJsonArray`, `QVariantMap`, `QHash`, `QStringList`) has its own include. `backfill_analysis_context` is built only in `build/app`; see finding 1. |
| Accept: `context_check --list c`; grep 0 | met | Run against a `git archive HEAD` copy, then against the worktree. (c) went from 5 to 3, and both the `recording.h:45` and `trunk_stand_tall.h:45` rows are gone. (The brief expected 7 to 5; the base was already 5.) (b) went from 68 to 66: two deferrals (`imu_instance.h:36`, `imu_role_map.h:27`) became Shared, and no new carve-out appeared. `grep 'Analysis/' src/Vocabulary/Recording/*.h`: 0. |
| Tests: Mac test tree builds; brief's suites and the architecture lint | met | `b17-build-tests.log`: 3480 steps, `== exit 0`, no `FAILED:`. ctest through the lock, `-R 'recording\|swing_doc\|imu_\|trunk\|ball_\|binding\|architecture_lint' -j 3`: **26/26 passed**, including `architecture_lint_test`, `recording_contract_test`, `swing_doc_test` and `trunk_stand_tall_test`. |
| No allowlist line (HARD) | met | `git diff HEAD --stat -- src/Gui/tests/`: empty |
| Docs: IM version, RING-VOCABULARY, 1.12.6; deck | met | See finding 4. NOTES §2 records the deck search ("no deck change"). |
| App build; `design_system_lint_test`; studio PC full suite; parity against `b1-base-2` | **not evidenced** | Finding 1 |

## Findings

1. **BLOCKING until shown: three results not yet evidenced.** At review time, `build/wt-37c/build/studio PC-runs/` held no run newer than `00:01`, which was B1.6's. `b17-build-app.log` was still being written, and NOTES had no results section. `design_system_lint_test` falls outside the ctest selection this review was allowed to run. Not met until NOTES shows all of these:
   - the app build, with `backfill_analysis_context`, which only `build/app` compiles;
   - `design_system_lint_test`;
   - the studio PC full suite (both passes), which also proves the include change on MSVC;
   - parity 0 against `b1-base-2`.

   If each one is green, the item is signed off with no re-review.
2. **EDGE: a stale map comment.** `tools/refactor/maps/B1.7-shared.map:4` says the split goes to "(capture_facts.h)". The header is `capture_time_types.h`; NOTES §2 explains why the name changed. This is a wording minor and can be fixed in the fix round.
3. **EDGE: the rig design's evidence table now mixes two dates.** `docs/design/rig_design.md` dates its code facts to `a5089848` (`:14`). The move tool rewrote A5-8 (`:807`) to `Vocabulary/Common/segment_role.h`, but A5-4 (`:803`) and §6 rule 1 (`:583`) still say `recording.h` includes `swing_analysis.h`. The page says §6 is re-run at Stage B's close, so this is harmless; it is for that re-run.
4. **Information-model edits: accurate, and within the approval cited (no finding).** Version row: `information_model.md:6`. RING-VOCABULARY (`:436`):
   - Shared `Vocabulary/Common` gains the segment roles and the trunk calibration provenance.
   - Acquisition's `Vocabulary/Recording` gains the capture-time types.
   - `Vocabulary/Analysis` drops "the segment roles".
   - The Directories cell names `capture_time_types`, `segment_role` and `trunk_calib_provenance`, with `[^sa24-q27]`.

   Change-log row 1.12.6 (`:2093`) is in order after 1.12.5. The approval it cites holds: Q27 names `trunk_calib_provenance.h`. The plan's Stage B text (`platform_refactor_assessment.md:742-747`) names the three capture-time types with `BallSample2D`, and `SegmentRole` going to `Vocabulary/Common`, with the RING-VOCABULARY edit. PR-9 (`JUDGEMENT-CALLS.md:43`) logs these as consequences of earlier rulings, and the row words them that way, not as a new ruling. Q27's test that two contexts use each type also holds:
   - `SegmentRole`: Acquisition (`Vocabulary/Recording/capture_time_types.h`, and `Gui/imu/imu_role_map.h`) and Diagnostics (`swing_analysis.h`).
   - `TrunkCalibProvenance`: Acquisition (`IMU/trunk_stand_tall.h:45`) and Diagnostics (`swing_analysis.h`).

   The developer-guide edits (`shot_analyzer_developer_guide.md:808-812`, `swinglab_developer_guide.md:678-679`) and `tools/trunk/trunk_hw.py:80` are accurate.
5. **Note, not a finding.** The four structs keep namespace `pinpoint::analysis` inside `Vocabulary/Recording`, as the brief requires, because they are serialised. A later rename into `pinpoint::recording` would be a separate change.
