# NOTES B0.4 · #187: one test-isolation helper, and the Windows exclusions come back

Worktree `build/wt-5b`, fast-forwarded to origin/main `7bacad21`. Uncommitted; nothing pushed.

## 1. What changed
- **The helper:** `src/Core/tests/isolated_stores.h` (new, header-only, `pinpoint::test::IsolatedStores`). It makes one `QTemporaryDir` per process.
  - `QSettings::setPath` for Ini and Native, User and System scope, so `ppSettings()` is `<tmp>/settings/PinPointStudio/PinPointStudio.ini`.
  - `setTestModeEnabled(true)`.
  - `PINPOINT_DIAGNOSTICS_DIR` and `PINPOINT_STUDIO_DIR` point into `<tmp>`.
  - `libraryRoot()` and `contains(path)`, canonicalised.
  - `qFatal` if no temp dir; nothing is restored on destruction.
  - It sits in a `tests/` dir and is included by relative path: no new library or ring, and `pp_add_test` is unchanged.
- **The guard:** `src/Core/tests/isolated_stores_test.cpp` (`src/Core/tests/CMakeLists.txt:67-75`).
  - It checks through the app's own helpers that `ppSettings()` (file, write, read back), `diagnosticsDataDir()` and the studio folder (an imported logo) lie inside the temp dir.
  - It checks that 7 standard locations and the library's `AppData/swings` fallback are not the real ones.
- **The forget-check is a lint:** `src/Core/tests/store_isolation_lint_test.cpp`, plus a manifest the umbrella writes at configure (`tests/CMakeLists.txt:40-78`; plain CMake, so Ninja and JOM both work).
  - A suite that compiles a real `ppSettings(` call must make an `IsolatedStores`. Comments and string, char and raw literals are blanked, and the rule runs on fixtures first.
  - Lint, not hash: the exclusions were for reads, which a hash can't see, and a hash can't name the suite. Result: 333 suites, 66 reach the settings, 66 isolated.
- **67 suites use it.** It is the first line of `main()`, the first member of a QTest class, or a file-scope static for gtest_main.
  - Hand isolation was replaced where it had the same intent: test mode on/off toggles, per-block app-name tricks, own setPath.
  - Finer per-test setPath calls are kept after the helper.
  - model_browser_test and inspector_refresh_test no longer use `XDG_DATA_HOME` (`src/Analysis/tests/CMakeLists.txt:2493-2523`).
  - model_browser_test now checks that its user layers are in its temp dir (`:633-638`), and closes the user norm file before the save (`:1990`). The open file was its Windows failure.
- **TEXCL** (`tools/studio/studio-build.cmd`, the last edit, ASCII and CRLF): `headless_probe_test|athlete_dump_ids_test|regrade_ledger_new_needs_ini_test`.
- **Docs:** `testing_developer_guide.md` §9 gains "Per-user stores — `IsolatedStores` and `store_isolation_lint_test`", two settings bullets and the file map; the helper's header comment explains how to use it.

## 2. Calls
- **45 of the 47 come back, not 48** (TEXCL held headless + 47; "49" was a miscount). `athlete_dump_ids_test` and `regrade_ledger_new_needs_ini_test` are `cmake -P` scripts driving tools that read the real settings by design. Fixing that is a tool change (an opt-in settings path): EDGE/issue.
- **Qt cannot put the standard app-data folders inside a temp dir on macOS or Windows.** So test mode covers them (`~/.qttest`, `%APPDATA%\qttest`); the app's own folders go through their env seams. The library fallback has no seam; that is product code.
- **The lint rule is `ppSettings()` only.** About 37 suites reach `QStandardPaths` only, into a folder named after the test binary: EDGE.
- **The studio PC runs used a temporary probe:** the 45 were re-registered as `<name>.b04` with the originals' properties, because the script can't be told the names. It is removed, and the final umbrella was configured on the Mac: 397 tests, no `.b04`, lint passes.

## 3. Evidence
- **Mac**, 4 builds:
  - build 1 failed on a private member;
  - build 2 full with `-k 0`;
  - build 3 the lint;
  - build 4 (extended) `model_browser_test` and `folder_series_test`.
- **Mac runs:**
  - the 70 changed suites and both lints pass (69/70 first, then a raw-string fix in the lint);
  - after build 4: folder_series, model_browser and the three lints pass, 5/5;
  - `--repeat until-fail:15` of model_browser_test: twice at once before the fix (A and B rc 0, 15 each), and once after (rc 0, 15/15, 402 s);
  - `-L qml_ui`: only #231.
- **Real stores, Mac:** the ini 385bcee0…, the three pinpoint plists and the Application Support entries are the same before (14:46) and after. Nothing is newer than the marker (find to depth 4).
- **Cabin run 1:** exit 11, a build error. An include of mine sat inside `#ifdef Q_OS_MACOS` (`folder_series_test.cpp`); fixed.
- **Cabin run 2** (`studio PC-wt-5b-20261124-150925`): exit 12; stores unchanged (settings 1ab76d2f…, Recycle Bin 87, Credential Manager 0); checkout clean.
  - Pass 1 is 321/335: 41 of the 45 pass, and the other 10 failures are already filed (#243, #244, #246–#250, #255).
  - Pass 2 is 41/43: #231 and #253.
  - **#245 is fixed:** lm_session_model_test passes.
- **B0.3's condition:** `cabin_cmds` (extracted verbatim) returned **1** at 15:07:26, while run 1's wrapper was live.
- **Unproven: the TEXCL edit** (it would be exit 21). After the commit: `tools/method/cabin_run.sh build/<clean worktree at main>`.
  - Expect the 45 to run under their own names.
  - Expect exit 12, from the three findings in A1 plus the filed ones.
  - model_browser_test should now pass.
- **Real stores my change can reach, and their guards:**
  - settings and pairings: setPath plus the lint;
  - diagnostics and studio folders: the env seams;
  - app-data, cache and config: test mode.
  - **On Windows, Home, Documents and the Desktop are not moved, and are not reached.** No suite calls `ModelBrowser::exportRoadmap`/`exportReferences`, and `premier_monitor.cpp:41` only builds a path.
  - Found on the way: `~/Library/Application Support/ppcp_host_service_test/` (last written 14:20) is now under the helper; the folder is left alone.
- **Clean-up:** `~/Library/Application Support/model_browser_test/` (listing in `build/wt-5b/build/b04/model_browser_test-listing.txt`; two empty dirs, unchanged by my runs) was removed with `rmdir`.

## 4. Paths, EDGE
- **New:** `src/Core/tests/isolated_stores.h`, `isolated_stores_test.cpp`, `store_isolation_lint_test.cpp`.
- **Changed:**
  - `src/Core/tests/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/Analysis/tests/CMakeLists.txt`;
  - `tools/studio/studio-build.cmd`, `docs/developer/testing_developer_guide.md`;
  - 67 test sources (`git -C build/wt-5b status`); this file.
- **EDGE:**
  - the 2 tool-driven tests;
  - the `QStandardPaths`-only suites;
  - test-mode folders shared by concurrent runs of one suite;
  - the lint misses an inline `ppSettings()` in an unlisted header.

---
## Appendix A1 · Issue texts for the three new Windows failures
Run `studio PC-wt-5b-20261124-150925`, `logs/tests_ctest.log` (kept on the share). On the Mac all three pass.

**1. Tests: session_diagnostics_test's unreadable-file cases (UNR-b, UNR-c) cannot fail on Windows.** This is #244's cause: add it there.
- The test makes the file and folder unreadable or read-only with `QFile::setPermissions` (`src/DomainModel/tests/session_diagnostics_test.cpp:1482, 1506`). Windows ignores that.
- Evidence (log lines 1431–1435):
  `[FAIL] UNR-b1 the file made unopenable (scratch)` · `[FAIL] UNR-b2 read-only: nothing is written this load, the file is not set aside` · `[FAIL] UNR-b3 after a fire, a flush and the release the file is byte for byte as it was` · `[FAIL] UNR-c2 the rename failed: read-only, the file left as it was and nothing set aside`.

**2. Tests: reduction_rule_golden_test's athleteWorkOns output differs from the golden on Windows.**
- Only `athleteWorkOns` differs; its rule versions match the golden.
- Evidence (lines 800–802):
  `[PASS] athleteWorkOns: its rule versions are the golden's` · `the output is at C:<studio PC checkout>/build/tests-vs18/Analysis/reduction_rule_golden_athleteWorkOns.json` · `[FAIL] athleteWorkOns: the output is the golden's — if it moved on purpose, bump the version…`.
- The output is kept at `build/wt-5b/build/b04/studio PC-reduction_rule_golden_athleteWorkOns.json` (80 rows, LF, sha1 f4c29509…). The golden (`src/Diagnostics/tests/data/rule_version_golden/reductions.json`) holds only a sha1, so the next step is to record the Mac's output and diff it.
- Cause not traced.

**3. Tests: charts_delivery_presenter_test golfersDay uses a drive-less fixture path, which Windows makes `C:/…`.**
- The fixture's folder is `/fixture/Golfer` (`src/Gui/tests/charts_delivery_presenter_test.cpp:403`). The presenter takes `QFileInfo(swingDir).absolutePath()` (`src/Gui/charts/charts_delivery_presenter.cpp:191`), which on Windows adds the drive, so the prefix check fails. This is test-only, traced by reading.
- Fix: build the fixture's folder from a `QTemporaryDir` path or `QDir::rootPath()`.
- Evidence (lines 847–848):
  `FAIL!  : ChartsDeliveryPresenterTest::golfersDay() 'm.value(QStringLiteral("swingDir")).toString().startsWith(golfer + QStringLiteral("/2026-10-20_Golfer_Wrist_02/swing_"))' returned FALSE.` · `charts_delivery_presenter_test.cpp(462) : failure location`.

## Appendix A2 · #187 closing text
- Fixed in platform refactor B0.4. `pinpoint::test::IsolatedStores` (`src/Core/tests/isolated_stores.h`) gives each process its own temp dir: setPath, test mode, and `PINPOINT_DIAGNOSTICS_DIR`/`PINPOINT_STUDIO_DIR`. It is in all 67 suites that reach the settings.
- `isolated_stores_test` and `store_isolation_lint_test` guard it.
- model_browser_test no longer relies on `XDG_DATA_HOME`, and checks that its user layers lie in its temp dir. Two concurrent `--repeat until-fail:15` runs pass.
- Other `XDG`/`HOME` users are app launches, not suites: `headless_probe_check.cmake` (still excluded on Windows) and the probe and typeset scripts.
- The stale `~/Library/Application Support/model_browser_test/` has been removed.
- On Windows, 45 of the 47 suites are back. Three fail with filed reasons (A1); athlete_dump_ids_test and regrade_ledger_new_needs_ini_test stay excluded (their tools read the real settings).
