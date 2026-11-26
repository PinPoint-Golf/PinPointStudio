# NOTES B1.14: the corridor scan's forwarder leaves Diagnostics (lane 1, `build/wt-37c`, built on `12992896`, merged to `50ba450b`)

Built as R11 ruled (the Solution Architect, 24 Nov 2026): the forwarder and its exception comment sit beside the model
browser in `src/Gui/diagnosticmodel/`; `model_browser.cpp`'s existing A5 line takes the include (2 → 3, reason corrected);
no line added. The scan's behaviour is unchanged: the same `PhaseGridStore::read` with the same policy and config.

## Checks (before the move)
1. `measure_sample.cpp:28` included `Export/phase_grid_store.h`; allowlist lines `measure_sample.cpp | A1 | 1` (`# kept`) and `| A5 | 1` (`#224`).
2. **Callers of `readPhaseGrid(`:** production: `model_browser.cpp:8517` (the corridor scan) and `gate_count_reads.h:45` (the gate count's fallback; R11's `:8461` has since moved there in P-3b). Tests: `measure_sample_test.cpp` (15 calls). **Not through the forwarder:** `readLivePhaseGrid` no longer exists (comments only); `LiveMeasureSource` reads through `PhaseGridStore::readForMeasures`; `tools/diagnostics/measure_dump_core.cpp` calls `PhaseGridStore::read` directly; `live_measure_source_test.cpp`, `src/Analysis/tests/CMakeLists.txt:1640` and `measure_dump_core.{h,cpp}` name it in comments only (left: they describe the door's behaviour).
3. **Duties kept:** the sidecar write (FILE-SHOT-07) through `PhaseGridStore::read` with `sidecarPolicyOf(writeSidecar)` (`ParseAndWrite` for both callers); the `cfg` argument (the scan's millisecond spans); the exception comment (moved whole to the new header).
4. **A5 lands per file that spells the include** (lint ~686; allowlist keyed per file, exact count). A header that included the door would need its own line. **So the call:** the header only declares; `model_browser.cpp` holds the include and the definition.

## What changed
- `src/Gui/diagnosticmodel/corridor_grid_read.h` (new): `pinpoint::diagmodel::readPhaseGrid(dir, writeSidecar = true, cfg = {})`, declaration only, with the exception comment (from `measure_sample.h:269`). Includes only `Vocabulary/Analysis/phase_grid.h`.
- `model_browser.cpp:29-32` includes it and the door; `:52-58` defines it; `:8529` the scan calls `pinpoint::diagmodel::readPhaseGrid`.
- `gate_count_reads.h:34,46` uses the new header in place of `measure_sample.h`.
- `measure_sample.{h,cpp}`: the forwarder, its declaration and the door include are gone. Three comments now point at `PhaseGridStore::read`, and one at the new header.
- **Tests re-pointed, none deleted:** `measure_sample_test.cpp:57` gets a file-local `readPhaseGrid`, the same one line onto the door, so its 15 call sites are unchanged. `session_gates_test.cpp:76` defines `diagmodel::readPhaseGrid` onto the door, because it compiles `gate_count_reads.h` without `model_browser.cpp`.
- Comments: `gate_counts.h:29`, `shot_grid_door.h:24`, `phase_grid_store.h:30-34`, `architecture_lint_test.cpp` (`isRepositoryHeader`).
- Build lists: `cmake/PinPointQmlModule.cmake` and `SESSION_GATES_TEST_SOURCES` list the new header. `phase_grid_store.cpp` stays on every test link line: targets that only compiled it for the sampler could now drop it (EDGE, not pruned).
- **Allowlist (my lines only):** `measure_sample.cpp` A1 and A5 deleted; `model_browser.cpp | A5 | 2 → 3`, its reason naming the door include, FILE-SHOT-07, PR-21/PR-23 and R11, tag unchanged (`# not domain (§8.3)`).
- **IM (v1.10.7, after the merge):** FILE-SHOT-07's exception sentence says the read is the model browser's (`corridor_grid_read.h`, defined in `model_browser.cpp`), footnote `[^sa24-r11]`; one changelog row; version line. §11 names no forwarder. The walkthrough deck is not affected (it names the file only).

## Tests
- Mac, both trees built (tests: 948 steps, exit 0; app: see below).
- `ctest -R 'measure_sample|model_browser|session_gates|inspector_refresh|phase_grid|architecture_lint_test|design_system_lint_test|design_system_tokens_check'`: 7/7 passed. No suite matches `phase_grid`. No QML touched.
- Lint: A5 212 allowed in 66 files (the total is unchanged because one finding moved, `measure_sample.cpp` → `model_browser.cpp`; a 4th finding would have failed as over). R11's corrected count supersedes the brief's "no count raised" for this one line. Debt 421 findings in 136 lines. `platform refactor (#224)`: 35 (36 → 35); `kept`: 2 (the A1 line went).
- `context_check.py --src build/wt-37c/src --list c`: 7 → 6 rows; `measure_sample.cpp:28` is gone and the new include adds none. Left: `swing_reanalyzer.cpp:20` (B1.13), `trunk_stand_tall.h:45` and `recording.h:45` (B1.7), Ppcp ×3 (B1.11). B1.1 is in (48387973); not yet in: B1.7, B1.11, B1.13.
- App build: exit 0 (signed dev bundle; not launched).
- Parity: `parity_baseline.py b114-corridor` (wt-37c's tests tree), then `parity_diff.py b1-base b114-corridor`: **0 differences** (diagnostics.json, athlete.json).
- **Cabin** (`studio PC-wt-37c-20261124-215411`, exit 12, over 631a5282; summary `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261124-215411.txt`, logs `the share build exit 0. Pass 1: 327/338. Pass 2: 41/43. Stores unchanged. **No new failure:** the failing set is this lane's previous run (`-201422`) minus `diag_stage_panel_test` and `session_list_test`, both of which now pass. The 11 + 2 are the known Windows set (PR-22, #187). `session_gates_test`, `measure_sample_test`, `model_browser_test` and `architecture_lint_test` pass on Windows.

## Real stores
**The library is in reach, unchanged:** in the app, the corridor scan and the gate count's fallback read each swing and write `swing_phasegrid.json` beside it (FILE-SHOT-07, `ParseAndWrite`), through the door as before. The guards in the tests: `model_browser_test` (`IsolatedStores` at :621, `QTemporaryDir`), `session_gates_test` (`IsolatedStores`, main()'s first line), and `measure_sample_test` (`QTemporaryDir` copies of the fixtures, :1091, :1386). Parity reads the library read-only, into copies under the parity scratch root. No app launch.

## Calls and EDGE
- CALL: declaration-only header, defined in `model_browser.cpp`: the only shape where A5's finding lands on model_browser's existing line (R11).
- CALL: `measure_sample_test` gets a file-local helper onto the door, which is neither of the brief's options ("include the new header" or `RecordingReader::phaseGrid`). The reason: the header is declaration-only and the suite does not link `model_browser.cpp`, so the helper keeps the 15 cases byte-for-byte. `session_gates_test` likewise defines the declared function.
- CALL: the name stays `readPhaseGrid` (now `pinpoint::diagmodel`), so `[^sa24-p3b-corridor]`'s spelling stays true. ODR check: `analysis::readPhaseGrid` is gone; no binary links both `model_browser.cpp` and `session_gates_test.cpp`.
- **Merged origin/main (`50ba450b`, B1.13 in), at the coordinator's note.** My patch was saved, re-applied with `git apply -3`, and the patch file deleted. Only the IM conflicted (the footnote block and the changelog): both are kept (`[^sa24-r10]` and `[^sa24-r11]`). My row is now 1.10.7, and the Version header was set to 1.10.7 by hand. `im_changelog_merge.py` reports "unresolved: 0". After the merge, the allowlist has **135 entries**, and `context_check --list c` has **5 rows** (B1.13 cleared `swing_reanalyzer.cpp`). The studio PC ran before the merge, over 631a5282 (B1.13 not in). BUDGET: a 3rd incremental tests-tree build, plus the item's suites and both lints, re-checked the merged tree. The build exited 0, and `ctest -R 'measure_sample|model_browser|session_gates|inspector_refresh|architecture_lint_test|design_system_lint_test'` passed 6/6.
- EDGE: `phase_grid_store.cpp` could leave the link lines of targets that no longer need it.
- EDGE: `docs/developer/{swing_export,diagnostics}_developer_guide.md` say "`readPhaseGrid` / `readLivePhaseGrid` take the same policy". This was already stale (`readLivePhaseGrid` is gone) and is left.
- For the coordinator: #224's tagged count is now 35. Stage B's `measure_sample.cpp` lines are at 0.

## Paths
Source: the 15 files in `git -C build/wt-37c status` (one new, `src/Gui/diagnosticmodel/corridor_grid_read.h`; among them `cmake/PinPointQmlModule.cmake`, `src/Analysis/tests/CMakeLists.txt`, `governance/artefacts/information_model.md`, `src/Gui/tests/architecture_allowlist.txt`). Logs: `build/platform/B1/b114-{build-tests,build-tests2,build-app,parity,studio PC}.log`. Parity run: `the share
