# NOTES B0.2b · #251: qml_ui on Windows, and qml_ui split into one ctest test per file

## 1. Diagnosis (studio PC, 1d9ada11, build\tests-vs18 Debug; each file alone, offscreen, QT_QPA_FONTDIR, -v2, junitxml)
- **Verdict: slow, not hung. Nothing in product code waits.** SessionReport alone **ends by itself in 293 s**. It is CPU-bound: 248 CPU s in 270 s wall, about 3 s of start-up, and its first row passes at 2.5 s. Its result is 35 pass and 4 fail.
- **The slow file is SessionDiagnostics** (`tst_session_diagnostics.qml`, 80 functions). Alone it takes **1374 s** and every function passes. It is not CPU-bound (about 45% of one core).
  - Longest functions: test_20 nothingOverhangsTheChrome 230 s, test_55 87 s, test_57 70 s, test_51 57 s, test_52 45 s, test_74 37 s.
  - Of the 80 functions, 16 take 20 s or more and 44 take 5–20 s.
- **Run 3 explained.** In run order, test_73 is followed by test_74 and then cleanupTestCase. Run 3's last line was test_73, so the kill came inside SessionDiagnostics::test_74, not inside SessionReport.
  - The arithmetic agrees. Alone, test_22..test_73 (51 functions) take 871 s; run 3 did 51 functions in 930 s. The 27 functions before them take 462 s, which falls before run 3's 900 s mark.
  - NOTES-B0.2 §4.11's "the last of SessionDiagnostics' 78" was wrong: 80 functions, and test_74 comes after test_73.
- **SessionReport's 4 failures** are the page count (`tst_session_report.qml:151`), each one page fewer than the count recorded on the Mac:
  - sixty-swings-three-clubs 15 vs 16
  - longest-names-and-note 13 vs 14
  - long-session 12 vs 13
  - consistency-mixed-clubs 9 vs 10

  This is Windows-only (they pass on the Mac). It was seen only under offscreen with QT_QPA_FONTDIR, so it is no evidence that the real app paginates differently. EDGE: issue to file.
- **The 9 files after it, each alone, all pass.** sessions_bulk 4 s, sessions_selection 4, shot_card 2, shot_picker 2, swing_areas 4, table_card 2, table_row 2, theme_bands 2, theme_tables 2.
- No procdump was needed: nothing hung. The studio's settings ini sha256 was 1ab76d2f… before and after every run.
- No #251 comment was made: the brief asks for one only when product code hangs.

## 2. The split: what changed
- **`src/Gui/CMakeLists.txt:180-233`**: the one `qml_ui` test is gone.
  - `file(GLOB … CONFIGURE_DEPENDS tests/qml/tst_*.qml)` registers each file as `qml_ui.tst_<file>`: `qml_ui_test -input <file>`, with the same ENVIRONMENT, `LABELS qml_ui`, and a TIMEOUT from a table.
  - The TIMEOUT table: default 120; session_diagnostics 2700; session_report 600; charts_panel 600; chart_presets 300; lm_graphics 300. That is about 2x the studio's time, which is 3–10x the Mac's.
  - RUN_SERIAL is dropped, so the label runs at ctest's -j. The comment block says why and gives the old-name mapping.
- **Old name → new.** `-R '^qml_ui$'` → `-L qml_ui`; `-E '^qml_ui$'` → `-LE qml_ui`; one file is `-R '^qml_ui\.tst_<file>$'`.
  - **CALL:** the label replaces the name (the brief: "make it a ctest label"). No whole-suite `qml_ui` test is kept, since it would run every file twice in a full run.
  - Risk: `-R '^qml_ui$'` now finds no test, and ctest exits 0 on that, so an unchanged script silently runs no QML.
- **`tools/studio/studio-build.cmd`** (header lines 13-21; `:tests`):
  - `qml_ui` is taken out of TEXCL.
  - Pass 1 adds `-LE qml_ui`.
  - Pass 2 is `ctest -j%TJ% --timeout 900 -L qml_ui`, at the `jobs` count (default 16).
  - It lists and logs the count (`tests_list_qml.log`, "pass 2 the 39 QML files").
  - The script is still ASCII and CRLF.
  - TEXCL is 48 suites, as before: the brief's "51" does not match B0.2's 49, which counted qml_ui.
- **Docs**:
  - `docs/developer/testing_developer_guide.md` §6 has a new section on the label, running one file, a direct run, and the old-name mapping; §3's comment line is updated.
  - `BUILDING.md:588` now names the label.

## 3. Proof
- **Mac, `ctest -L qml_ui -j 3`** (the lock, 1 build): **39/39 files ran.** 38 pass; ChartsPanel fails on test_15 only (#231). The summed per-file `Totals:` are **555 passed, 1 failed**, the same as the one-process run (555/1).
  - Wall time **432 s against 581 s** for the one-process run. The floor is SessionDiagnostics (432 s); the other 38 files take 179 s.
  - The first label run had a 300 s default and SessionDiagnostics timed out there at test_54. That is what set its row in the table.
- **Mac, `ctest -LE qml_ui -j 3`**: 350/351. Both lints pass. `ppcp_host_service_test` failed at -j3 (63 s) and passed alone (21 s); it is not one of #228's two. EDGE.
- **Cabin, the edited script run from scratch with `tests nopull patch wip.patch`** (one full run, exit 12):
  - Pass 1: 288 run, 12 failed. That is B0.2 §4's 11 plus `shared_norm_provider_test`: "the user norm set is written inside this run's own temporary directory", the same AppData-vs-GenericData cause as §4.6. It was re-admitted in B0.2's fix round. EDGE.
  - **Pass 2: 39/39 files reported, in 1382 s**, where before nothing finished inside 1830 s. SessionDiagnostics passed in 1381 s. SessionReport failed alone (page count, 292 s) with every other file still reported. ChartsPanel failed on test_15 (#231) and test_15b ("scrolled to 650"), as before.
  - Per file at -j16: charts_panel 268 s, chart_presets 84, lm_graphics 58, variability_funnel 25, variability_mark 23, home_tabs 14, every other file under 9 s.
  - Stores before and after: settings 1ab76d2f…, Recycle Bin 87, Credential Manager 0. The patch was restored and the checkout left clean (`== status after` empty).
  - The run's TIMEOUTs were the provisional ones (SessionDiagnostics 2400, default 300). The final table above was set from these times; that change needs only a reconfigure, and the label was not re-run after it.

## 4. For the coordinator: the command COMMON should give
- Full run: `ctest --test-dir build/<wt>/build/tests -LE qml_ui -j 3`, then `ctest --test-dir build/<wt>/build/tests -L qml_ui -j 3`.
- QML files touched: `ctest … -R '^qml_ui\.tst_<file>$'`, one per file.
- ⚠ COMMON's current `ctest -E '^qml_ui$' -j 3` now silently includes all 39 QML files beside the compute suites with no RUN_SERIAL. That is the starvation case the old comment warned about. Change COMMON before the next lane runs it; lane 1 uses it today.

## 5. Calls, paths, EDGE
- **Calls**:
  - The studio PC checkout was fast-forwarded by hand (43980100 → 1d9ada11, clean, 4 behind).
  - `qml_ui_test` was built for the diagnosis by an ad-hoc .cmd on the share (vcvars, `cmake .`, `jom -j16 qml_ui_test`), as B0.2 did from scratch\b02.
  - SessionDiagnostics was added to the diagnosis, because run 3's last line was in it.
  - The edited studio-build.cmd ran as a copy from the share, since a patch may not touch it.
- **Real stores**: `qml_ui_test` redirects QSettings before RuleData (B0.2 B1). No new store is reached, and the studio's settings hash was unchanged on every run.
- **Changed**: `src/Gui/CMakeLists.txt`, `tools/studio/studio-build.cmd`, `docs/developer/testing_developer_guide.md`, `BUILDING.md`, this file.
- **Cleaned**: scratch\b02b (scripts, runs, patch) and the studio-build tests_* logs were deleted. The studio PC checkout is left at 1d9ada11, clean.
- **EDGE**:
  - SessionReport's Windows page counts (above).
  - `shared_norm_provider_test` on Windows (above).
  - `ppcp_host_service_test` is flaky at -j3 on the Mac.
  - SessionDiagnostics' 23 min on the studio's Debug build is the label's floor; test_20 alone is 230 s.
  - `:putback` in studio-build.cmd has dead lines after its `goto :eof )`, which are harmless.
