# REVIEW B1.18 · dag_layout and corridor_plot to the UI ring (R8 part 2)

Independent review of the uncommitted change in `build/wt-37c` (HEAD `fcd5ac53`; origin/main is two commits ahead at review time, `46a87e2e` and `f1885b7c`: QML, two design-system docs, `design_system_allowlist.txt`, `tst_shot_card.qml` and `docs/implementation/platform_d_capture_seam.md`. None touches the IM, `architecture_allowlist.txt` or a CMake file, so nothing overlaps this item's paths). Diff read with `git diff HEAD -M` (a diff against origin/main also shows those two commits, reversed; they are not this item's).

## Verdict: SIGN OFF

No blocking finding. Two record fixes (R1, R2) and one note for B1.19 (N1) are below; none needs a code change in this item.

## 1. Built what R8 part 2 ruled
- R8 part 2 (ruled, the Solution Architect, 24 Nov 2026): the two stems to `src/Gui/diagnosticmodel/layout/`, Rules to Adapters, the A7 line leaving scope, the split staying with #222. Built exactly that; the map names the two stems and that folder.
- Pure rename: `git diff -M` shows 4 renames at 99% similarity. Each moved file changes one `#include` line only (re-pathed to `../../../Diagnostics/...`). The other edits are include re-paths (model_browser.cpp ×2, the 4 tests ×1 each), source lists (PinPointQmlModule.cmake ×4, src/Analysis/tests/CMakeLists.txt ×7), one comment path (analysis_dag.h:94), and docs. No other content change.
- Every includer re-pathed: a grep of `src cmake tools CMakeLists.txt` for `dag_layout`/`corridor_plot` in code and build files finds only new paths and prose comments. The old paths survive only in the frozen audit data under `docs/implementation/swe-audit/` and in R8's own text, which is right.
- A7 scope: the lint gates A7 on `ring == Vocabulary || ring == Rules` (architecture_lint_test.cpp:769); `dirRings()` maps `Gui/` to Adapters and `fileRings()` names neither file, so both are Adapters and out of A7. Allowlist line `Diagnostics/engine/dag_layout.cpp | A7` deleted, no line added (the diff has one `-` line and no `+` in the allowlist). The lint passes.
- A1: what the moved files include (characteristic_pack, reference_pack, norm, diagnostics_health) is all Rules, so Adapters to Rules points inward; nothing in Rules or the Domain model includes them. 0 findings.

## 2. The hand-fixed CMake lines
- All 7 lines in `src/Analysis/tests/CMakeLists.txt` (1451, 1522, 2533, 2534, 2568, 2569, 2595) use `${SRC}/Gui/diagnosticmodel/layout/...`. `SRC` is set at line 23 and `DIAG` at line 1264, both before the first use; `DIAG_PACK` (1314) is unchanged. The variables on the diff's changed CMake lines are only `${SRC}`, `${DIAG}`, `${DIAG_PACK}`.
- No other file has the wrong variable: `${GUI}` appears only in `src/Gui/tests/CMakeLists.txt` and the `.cmake` files beside it, where `GUI` is set (line 34). `PinPointQmlModule.cmake` uses repo-relative paths like its neighbours.
- The test tree configures: `build/b118-b1-tests.log` has the two `CMake Error ... add_executable` lines (the `${GUI}` defect); `build/b118-b1b-tests.log` reads "Configuring done / Generating done" with no error. `build/tests/build.ninja` (22:29) holds 13 references to `Gui/diagnosticmodel/layout/dag_layout.cpp`, 0 to the old path, and object files for all 5 test targets plus qml_ui_test at the new paths.

## 3. Behaviour unchanged
- Parity: `build/b118-layout-parity-diff.log`: 0 differences (diagnostics.json, athlete.json).
- My run (through the lock): `dag_layout_test`, `corridor_plot_test`, `model_browser_test`, `architecture_lint_test`, `move_files_selftest`: 5/5 passed. The implementer's 10/10 run (`build/b118-ctest.log`) matches.
- **Should the tests move?** No, not in this item. R8's "still plain C++ with their tests" I read as "they keep their tests" (they stay plain C++ and stay tested), not "the tests move": the brief maps the two stems only, and its own text says "names, namespaces and code don't change". If the coordinator reads the phrase the other way, it is a question for the Solution Architect, not a defect here. R8 moves the two stems; `model_browser_test` and `inspector_refresh_test` already test a `Gui` file from `src/Diagnostics/tests`, so these join an existing pattern. Moving them into `src/Gui/tests` would move their `pp_add_test` into another CMake scope, the same hazard F1 just hit, for no gain while the lint does not read tests. See N1 for when they should.

## 4. The context tag
- `context_check.py` tags by directory: `Gui/` is the hub (Domain + Application, line 45). R13's placements table lists only the "to place" directories and files; `Gui/` is not one of them and neither file is named, so the files take their new folder's tag and no table edit is due. Right.
- `context_check.py` excludes tests (line 2, and line 171 drops `tests` folders), so the tests' new `../../Gui/...` includes are not counted. The crossings: the four outward includes become hub to Diagnostics (characteristic_pack, reference_pack, norm, diagnostics_health), and model_browser.cpp's two includes stop crossing (hub to hub). Net +2, which matches 970 to 972. The source is the hub, so `classify` returns "hub composes (Q26)" (line 141), which is allowed. Rejected counts unchanged (the implementer's run; I did not re-run the script).

## 5. The IM
- Version header 1.10.8, the change-log row 1.10.8 after 1.10.7, and `[^sa24-r8]` beside `[^sa24-r11]`. origin/main's last is 1.10.7 and no other lane claims 1.10.8 (only ORDER.md and these NOTES name it). Consistent.
- Roles, not names: "the Solution Architect, 24 Nov 2026 (R8, part 2)". The footnote is referenced from the row and defined once. Provenance is in the footnote and the "who" column, not inline. RING-RULES and RING-ADAPTERS name `Diagnostics` and `Gui` by directory and the IM names neither file anywhere else, so "no row changes" is true.
- The walkthrough deck shows rings by directory, so no refresh: agreed.

## 6. The studio PC's extra failure: NOT #261's message
- #261 is `ARCHIVEh2 after it lands: shot 5 judged again, the session written exactly once`: a write count, the 300 ms timer racing the reduction. Run `-201422` failed that check, so that one is #261.
- Run `-223622` (this item's) failed **`ARCHIVEe archived`** and **`ARCHIVEg`**, and ARCHIVEh2 **passed**. The log (`the share "kept 0, writes 0", "the archive's 0" bytes. The archive job itself returned not-ok (`r.ok` false) and wrote nothing. Zero writes means no write timer was involved: a different failure from #261's.
- What the log shows: ARCHIVEe (`r.ok && !s->archiving()`) and ARCHIVEg failed, the archive's copy is 0 bytes, 0 writes, and ARCHIVEh2 passed. That points to the archive job's result, not the write count. The cause is unknown; the test ran 297 s under `-j16`, so load may still be involved.
- It is not this move: in the test tree's `build.ninja`, only `dag_layout_test`, `corridor_plot_test`, `model_browser_test`, `inspector_refresh_test`, `diagnostics_catalogue_integrity_test` and `qml_ui_test` compile the moved files, and `diag_stage_panel_test` is not among them. None of its CMake lines changed. The FAIL lines of `-215411` and `-223622` differ by exactly this one test (checked by diff). Not blocking.

## Fixes (records only; no code)
- **R1** · #261's comment of 24 Nov ("Seen again ... run studio PC-wt-37c-20261124-223622") is wrong: that run failed ARCHIVEe/ARCHIVEg (the archive job failed, its copy 0 bytes, 0 writes), not ARCHIVEh2. Correct the comment, and open a new issue: "diag_stage_panel_test ARCHIVEe/ARCHIVEg fail on the studio PC: the archive's copy is 0 bytes, 0 writes; cause unknown", with the log lines above. The check does not print why, so the first step is to print the archive result's reason in ARCHIVEe.
- **R2** · NOTES-B1.18's Tests section calls it "an intermittent archive-timing failure". Say "ARCHIVEe/g failed (archive copy 0 bytes, 0 writes; cause unknown); not #261's ARCHIVEh2", and link the new issue.

## Note for B1.19 (not this item)
- **N1** · R8 part 1's folder-order check ("a file may include its own folder and those before it"). The four tests, and `model_browser_test`/`inspector_refresh_test` before them, include `../../Gui/...` from `src/Diagnostics/tests`. If that check reads `src/Diagnostics/tests`, it must either skip tests (as A1–A8 do) or these six tests move first, as one group, to a test folder beside `src/Gui/diagnosticmodel/`. B1.19's dry run should settle which, before its file list is ruled. The same holds for lint A9 (B1.21): `context_check.py` excludes tests today, and A9 must keep doing so, or these tests become "hub past its contracts" findings.
- F1 (the move tool's global `CMakeVars`) is already scheduled before B1.19 (ORDER.md B1.2a). This review confirms it: `--verify` passed the broken `${GUI}` lines, so until the tool is fixed, the configure is the only proof for build-file variables.
