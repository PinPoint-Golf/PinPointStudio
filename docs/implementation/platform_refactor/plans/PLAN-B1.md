# Stage B, Part 1: the plan (24 Nov 2026)

From `docs/implementation/platform_refactor_assessment.md`, "Stage B", Part 1 only. Premises checked on origin/main `bdd04541`: `NOTES-B1-plan.md`. Every brief reads the standing block (#402); scripted moves also read `docs/implementation/platform_refactor/moves.md`. Each agent first regenerates its IM excerpt from its own tree (`make_im_excerpts.sh <worktree>`). Lane 1 is `build/wt-37c`, lane 2 `build/wt-5b`.

**Three things the plan's text gets wrong** (NOTES §2): the Vocabulary move is 279 files, not 268; two of Stage B's four allowlist lines can't reach 0 while the corridor scan's forwarder stays in Diagnostics, so "34 → 30" needs R11 or a new figure from the Product Owner (and it starts from 37 once the three stale tags are re-tagged, B1.T); and thirteen placements and names are still only proposed, so B1.0 puts them to the Solution Architect before the items that need them.

| # | Item (one agent run each) | After | Runs beside | Tier | Accept | h |
|---|---|---|---|---|---|---|
| B1.0 | The placements proposal, R1–R13, for the Solution Architect (docs, no build) | now | P-3b, B1.1 | coordinator reads | 13 questions, each with counts and a recommendation | 1.5 |
| B1.1 | A9's two deletions: `video_overlay_pose` and `_base`; the `hm_frame.h` include | now | P-3b, B1.0 | independent | 2 of the 9 rejected includes gone (9 → 7) | 1 |
| B1.2 | The move tool, `tools/refactor/move_files.py`, with `--dry-run`, `--verify`, a self-test | B1.1 | P-3b, B1.3 | independent | self-test in ctest; dry run of the Vocabulary map: 279 files, 402 includes, 0 unresolved | 2.5 |
| B1.T | The three stale allowlist tags re-tagged and named on #224 (the coordinator's step) | P-3b | any | self-check | `# step 1`/`# step 4` tags 3 → 0; `#224` lines 34 → 37 | 0.25 |
| B1.3 | Build-time and parity baselines, Mac and studio PC (no source change) | P-3b | B1.2 | self-check | 3 times on the studio PC recorded (PR-26: build timings on the studio PC only); parity label `b1-base` | 1.5 |
| B1.4 | The Platform library pilot, linked under MSVC | B1.3 | B1.8 | independent | 1 static library; Core's tests link it; hand-listed Platform sources in them → 0 | 2.5 |
| B1.5 | **The Vocabulary root move** (scripted, alone) | B1.2, B1.3 | nothing | self-check | lint's per-file Vocabulary entries 10 → 7 (Buffer's); Vocabulary directory rules 5 → 2 | 3 |
| B1.6 | **Today's `src/Coaching` → `src/Diagnostics/derived/`** (scripted, alone) | B1.5, P-3b | nothing | self-check | files in `src/Coaching` → 0; B-08's non-wrist half closed | 2.5 |
| B1.7 | `recording.h`'s capture-time types to `Vocabulary/Recording`; `SegmentRole`, `TrunkCalibProvenance` to `Vocabulary/Common` | B1.5 | B1.10 | independent | rejected includes 7 → 5 | 2 |
| B1.8 | The priorities' type renames, and the panel's heading | P-3b | B1.4 | independent | `WorkOn*` type names in Diagnostics' rules → 0; persisted keys changed: 0 | 3 |
| B1.9 | The studio profile to `src/App/` (scripted) | B1.5 | B1.15a | self-check | `Core`'s Adapters entries 4 → 3 | 1 |
| B1.10 | `modelFileFingerprint` as a direct call; the hook deleted | B1.5 | B1.7 | independent | install sites 3 → 0; the stamp's text unchanged | 1.5 |
| B1.11 | Core's placements: three Platform headers, `pp_settings`, `kf3_smoother`, `app_info`; the `Core/` rule becomes Platform | **R1–R4**, B1.9 | nothing with the lint | independent | `Core`'s files outside Platform → 1 (`device_enumerator`, Stage D); rejected includes 5 → 2 | 2 |
| B1.12 | LaunchMonitor's inferred reads to a Rules directory (scripted) | **R5**, B1.5 | nothing | self-check | LaunchMonitor's per-file lint entries 8 → 0 | 1.5 |
| B1.13 | Re-analysis' A1 line | **R10** | B1.15a | independent | `swing_reanalyzer.cpp` A1: 1 → 0; rejected includes 2 → 1 | 1.5 |
| B1.14 | The corridor forwarder out of Diagnostics | **R11**, P-3b | nothing with the allowlist | independent | `measure_sample.cpp` A1 and A5 lines → 0; rejected includes 1 → 0 | 1.5 |
| B1.15a | The ten chart rule files: duties and includes inventory (no code) | now | any | coordinator reads | every outward include placed | 1.5 |
| B1.15b | The ten chart rule files out of `src/Gui` (8,529 lines) | B1.15a, **R7**, B1.6 | nothing | independent | the ten in `src/Gui`: 10 → 0, with no new lint line | 3 |
| B1.16 | **`src/Export` renamed** (scripted, alone) | **R6**, B1.15b | nothing | self-check | files in `src/Export` → 0 | 2.5 |
| B1.17 | The store doors' three A5 lines (J-88, J-110) | **R9**, B1.16 | nothing with the lint | independent | 2 lines → 0; `rule_data_reads` counted and at 0 | 1.5 |
| B1.18 | `layoutDag` and the corridor plot to the UI ring (B-10) | **R8** | nothing with the allowlist | independent | `dag_layout.cpp`'s A7 line gone | 1.5 |
| B1.19 | **Diagnostics re-cut by layer** (B-09; scripted, alone) | **R8**, B1.6, B1.18 | nothing | self-check | includes from a lower Diagnostics folder into a higher one → 0, by a script | 2.5 |
| B1.20 | **(Moved after Stage B Part 2 by P1, 24 Nov.)** **One namespace per context** (scripted, alone; a run per namespace) | **R12**, every move | nothing | independent | files in `pinpoint::analysis` outside Analysis → 0; duplicate type names in one namespace: 0 | 6 |
| B1.20a | A test that fails on two types of one name in one namespace (P1's guard while B1.20 waits) | now | any | independent | the test in ctest, red on a planted duplicate, green on main | 1 |
| B1.21 | Lint A9 | **R13**, B1.7, B1.11–B1.14, B1.19 | B1.20's review | independent | A9 findings 0; allowlist lines for A9: 0; `check_includes.py` (c) = 0 | 2.5 |
| B1.22 | Part 1's close: counts re-taken by an agent that built none of it | all | nothing | n/a | the stage's Accept row, each figure with its command | 2 |

**Order and lanes.** Now, beside P-3b (lane 1): B1.0 (docs), then lane 2 runs B1.1 and B1.2. When P-3b lands: B1.T (three tags), then B1.3 with B1.2 finishing; then B1.4 beside B1.8. Then the two ruled moves, each alone: **B1.5, B1.6**. Then pairs: B1.7 ∥ B1.10; B1.9 ∥ B1.15a. From here the Solution Architect's ruling on B1.0 is needed: B1.11, B1.12, B1.13, B1.14, B1.15b, B1.16, B1.17, B1.18, B1.19, mostly one at a time because each touches the lint, the allowlist or the root build file (NOTES C-6). Then B1.20, B1.21, B1.22. If the ruling is late, everything up to B1.10 and B1.15a still runs.

**P-3b** lands before B1.3. It adds `src/Coaching/session_gates.h` (B1.6 moves it) and re-tags `measure_sample.cpp`'s A1 line `# kept` (B1.14 removes it).

**Review tier.** Self-check: the five moves that are a map and the tool only (B1.5, B1.6, B1.12, B1.16, B1.19) and B1.9, proven by `--verify`, both lints with a planted violation, a clean build, the studio PC's full suite and parity 0. Independent: everything that edits code by hand, deletes, renames a type, changes the lint or is seen by a golfer.

**Effort.** About **52 h** (37–65 h) of effort for 25 items, against ORDER's 17–23 h for the same scope. The difference: 25 items where ORDER counted about ten, each tree-wide move rebuilds both trees on the Mac, and the namespace change alone is 511 files. Measured so far: 2.4 h and 370k tokens an item (13 items, `LEDGER.csv`). Because the moves run alone, about 40 h of running: **3 days** at 14–16 h a day, plus the wait for B1.0's ruling. Without B1.20 and B1.4 it is about 43 h. **Recommendation to the Product Owner:** move B1.20 (namespaces) out of Part 1, to after Part 2 or to #224-H; no library depends on it (NOTES §2, 11).

**Cabin.** Every code item runs the full suite on the studio PC through `tools/method/cabin_run.sh` (🟡, studio idle, no hands). B1.3 and B1.4 also need a clean Windows build there (`studio-build.cmd`, about 35 min each). **No item needs the Product Owner at the studio PC.** B1.4's commit waits for visit 1's live check (it changes the Windows build), and B1.5's does if the visit is on 25 Nov as planned (NOTES C-3).
