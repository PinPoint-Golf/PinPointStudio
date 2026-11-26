# NOTES B1.18 · dag_layout and corridor_plot to the UI ring (R8 part 2)

Lane 1, `build/wt-37c`, base `fcd5ac53` (origin/main, ff-only: already up to date). Not committed.

## The four checks (before the move)
1. `count_includers.py`: each stem has 2 production includers (itself and `Gui/diagnosticmodel/model_browser.cpp`) and 2 tests (`dag_layout_test`, `model_browser_test`; `corridor_plot_test`, `inspector_refresh_test`). Same after, at the new paths; unresolved: 0.
2. Outward includes: `dag_layout` → `Diagnostics/packs/{characteristic,reference}_pack.h`; `corridor_plot` → `Diagnostics/norms/norm.h`, `Diagnostics/engine/diagnostics_health.h`. All Rules, so Adapters → Rules points inward. No Rules or Domain-model file includes either header: no A1 finding.
3. `cmake/PinPointQmlModule.cmake:757–758, 767–768` and `src/Analysis/tests/CMakeLists.txt:1451` (diagnostics_catalogue_integrity_test), `1522` (dag_layout_test), `2533–2534` (model_browser_test), `2568–2569` (inspector_refresh_test), `2595` (corridor_plot_test).
4. Allowlist: only `dag_layout.cpp | A7` (line 169, not 172); `corridor_plot` has none. Neither file has file access (A5) or a QVariantMap/QVariantList (A6), so in `Gui` they trip neither.

## What changed
- Map `tools/refactor/maps/B1.18-layout.map` (two stems → `src/Gui/diagnosticmodel/layout/`). Tool run: 4 files moved, 10 include lines in 9 files, 14 path mentions.
- Hand edits: `src/Gui/tests/architecture_allowlist.txt:169` deleted (out of A7's scope); `src/Analysis/tests/CMakeLists.txt` 7 lines respelt `${SRC}/Gui/…` (finding F1); `docs/developer/diagnostics_developer_guide.md` (the MODEL/FAÇADE diagram and §14's file map); IM 1.10.8 (below).
- `--verify fcd5ac53 --allow-prose <IM> --allow-prose <dev guide>`: renames 4, files changed 15; the only "other lines" are the ordered allowlist deletion (134 vs 135 entries, line 169) — so it prints FAILED for exactly that, nothing else. Note: verify passed the broken `${GUI}` spelling too (F1); for build-file variables the configure is the proof, not `--verify`.

## Calls
- C1 · Tests stay in `src/Diagnostics/tests` (the brief maps two stems only; `model_browser_test` is already there for a `Gui` file). Their includes are now `../../Gui/diagnosticmodel/layout/…`; the lint does not read tests.
- C2 · Context: the tool flags the four files DIA → HUB (`context_check.py` tags `Gui/` HUB). R8 rules the ring only; the folder's tag follows, no table edit. `context_check.py` before/after: crossings 970 → 972, both "hub composes (Q26)"; rejected counts unchanged; exit 0.
- C3 · IM: RING-RULES and RING-ADAPTERS name both by directory, so no §4.1 row changes. Added changelog row 1.10.8, footnote `[^sa24-r8]`, and the version line. The walkthrough deck shows rings by directory: no refresh needed.
- C4 · No QML touched; `tst_model_browser` / `tst_diagnostic_model` do not exist (no QML test of the model browser), so none run.
- C5 · MOVES.md's "one lane" rule: lane 2 (#186, shot card QML) ran in parallel by the coordinator's plan; it touches none of these files.

## Findings (new items, for the coordinator)
- F1 · BLOCKING tool defect, `tools/refactor/move_files.py` `CMakeVars` (≈ lines 443–459, shared by the rewriter and `--verify`): any variable set to one value anywhere is treated as global. `GUI` is set only in `src/Gui/tests/CMakeLists.txt:34`, so the tool respelt `src/Analysis/tests/CMakeLists.txt` paths as `${GUI}/…`, empty there; the test tree failed to configure. Fixed here by hand (`${SRC}/Gui/…`, the file's own spelling for `model_browser.cpp`). A replay of this map from the base reproduces `${GUI}` until the tool is fixed. Fix before B1.19.
- #224 B-10: done by B1.18 — `dag_layout` and `corridor_plot` are in `src/Gui/diagnosticmodel/layout/` (Adapters), their A7 line gone.
- #222: `layoutDag` (852 lines, now `src/Gui/diagnosticmodel/layout/dag_layout.cpp`) is still to split; it is no longer on the allowlist, since A7 reads Vocabulary and Rules only.

## Real stores
None reachable: a file move with include-path changes; no code changed.

## Tests
- Mac build 1: app tree 0 (`build/b118-b1-app.log`); test tree first failed to configure (F1), then 0 after the fix (`build/b118-b1b-tests.log`). Nothing had compiled before the configure failed, so this is build 1 finished, not build 2.
- Mac ctest (`build/b118-ctest.log`): `dag_layout_test`, `corridor_plot_test`, `model_browser_test`, `inspector_refresh_test`, `diagnostics_catalogue_integrity_test`, `diagnostics_health_test`, `architecture_lint_test`, `design_system_lint_test`, `im_excerpt_selftest`, `move_files_selftest`: 10/10 passed. The test sources are unchanged but for the include line, so their expected coordinates are unchanged.
- Parity Mac half: `parity_baseline.py b118-layout`, `parity_diff.py b1-base b118-layout`: **0 differences** (diagnostics.json, athlete.json; 8 sessions).
- Cabin: the first launch (`build/b118-studio PC.log`) was made before the Mac configure was known good, so it carried the broken `${GUI}` lines; stopped while still waiting for wt-5b's studio PC lock, before launch (no run dir, the lock never ours). Second run `studio PC-wt-37c-20261124-223622` over fcd5ac53 (summary `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261124-223622.txt`, logs `the share build exit 0; pass 1 326/338, pass 2 41/43; stores unchanged. Against this lane's last run (`-215411`, B1.14): the same set plus `diag_stage_panel_test` (`ARCHIVEe archived`). It links neither moved file (`_diag_parallel_live_sources`), failed at another ARCHIVE step in `-201422` and passed in `-215411`: an intermittent archive-timing failure, not this move; it passes on the Mac alone (ctest -R, through the lock). EDGE: file it as flaky on Windows. **No new failure attributable to the move**; the other 11 + 2 are the known Windows set (PR-22, #187). 2 launches against a budget of 1, 1 real run.

## Paths changed
`cmake/PinPointQmlModule.cmake`, `src/Analysis/analysis_dag.h` (comment), `src/Analysis/tests/CMakeLists.txt`, `src/Diagnostics/tests/{corridor_plot,dag_layout,inspector_refresh,model_browser}_test.cpp`, `src/Diagnostics/{engine/dag_layout,norms/corridor_plot}.{h,cpp}` → `src/Gui/diagnosticmodel/layout/`, `src/Gui/diagnosticmodel/model_browser.cpp`, `src/Gui/tests/architecture_allowlist.txt`, `docs/developer/diagnostics_developer_guide.md`, `governance/artefacts/information_model.md`, `tools/refactor/maps/B1.18-layout.map` (new).

## EDGE
- Stale header comments name `src/Diagnostics/dag_layout.*` and `src/Diagnostics/corridor_plot.{h,cpp}` (already wrong before the move: `dag_layout_test.cpp:1`, `corridor_plot_test.cpp:1`); left, since the tool cannot map a path that never existed.
- `docs/design/diagnostics_norms.md:142` lists `dag_layout` under a planned `src/Gui/characteristics/` (a historic brief); left.


**Correction (coordinator, after review):** the studio PC run's extra failure was diag_stage_panel_test ARCHIVEe and ARCHIVEg (the archive's copy 0 bytes, 0 writes), now #264, not #261's ARCHIVEh2, which passed. It's not caused by this move.
