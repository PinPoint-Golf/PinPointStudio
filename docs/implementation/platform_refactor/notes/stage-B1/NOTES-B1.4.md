# NOTES B1.4 · The Platform library pilot (lane 2, build/wt-5b at 4d80463b)

**Check first.** RING-PLATFORM's 16 stems: none includes a project file outside Platform. `pp_debug.cpp` includes
`whisper.h`, `ggml.h` and `QMediaPlayer`, so it is left out (stays in the app's list). Header-only, nothing to
compile: `atomic_write.h`, `post_shot_trace.h`, `cpu_topology.h`, `pp_model_path.h`, `ort_log.h` (needs ORT's header),
left where they were listed. `pp_log_stub.cpp` has no user in any test list (Ppcp's mention is a comment saying it
uses the real log); its only other `PpLogStream` definer is `pp_log_stream.cpp`. Hand-listed Platform sources in the
other test lists (grep -c, untouched, Part 2): Analysis 41, Gui 17, Ppcp 9, Pose 5, LaunchMonitor 5, Update 2, IMU 1. No
library source reads `PINPOINT_DEBUG_LEVEL`/`PINPOINT_PROFILE*` (those change callers' macros only), so one library
build serves the compile-out test and both debug-level tests. No `Q_OBJECT`, no `ppSettings`/`QSettings`, no
self-registering statics in the library.

## 1. What changed
- `src/Core/CMakeLists.txt` (new): `pp_platform` STATIC = pp_log_stream, PpMessageLog, PpStatsLog, AnalysisProfileLog,
  pp_profiler, pp_os_metrics, pp_gpu_metrics, folder_lock, work_pool (+ `pp_gpu_metrics_mac.mm`, `-fobjc-arc`, on
  Apple). PUBLIC Qt6::Core, Threads; Metal/Foundation (Apple), dxgi (Windows), `${CMAKE_DL_LIBS}` (other). Include
  dir PRIVATE src/Core + INTERFACE src/Core. MSVC: `_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES` (the app's). Sanitizers
  when `pp_apply_sanitizers` exists. No source file moved or changed.
- `CMakeLists.txt:711-714` `add_subdirectory(src/Core)`; `:949-953` the app links `pp_platform`, its list keeps only
  `pp_debug.cpp` of Platform; `:748`, `:2980`, `:3099` folder_lock, work_pool, the `.mm` dropped from the app's lists;
  `:3103` the `-fobjc-arc` property kept for the offline tools that still list the `.mm`. The offline tools and every
  other test list are unchanged (Part 2).
- `src/Core/tests/CMakeLists.txt:35-37` adds the library `if(NOT TARGET pp_platform)` (standalone or umbrella; no
  `tests/CMakeLists.txt` change); every test links `pp_platform`. Hand-listed Platform sources: 0 (the 3 grep hits
  left are comments). `pp_gpu_metrics_test` stays `NO_QT` for its own file, but Qt Core is now on its link line.
- `BUILDING.md` Testing: one paragraph, the first ring library and how a test links it.

## 2. Tests
- Mac (both trees built: 2 of 2 builds): `ctest --test-dir build/tests/Core` 16/16 pass;
  `architecture_lint_test`, `design_system_lint_test` pass; `store_isolation_lint_test` pass.
- Smoke, `tools/probe/run_headless.sh` on scratch home/settings/library (`build/wt-5b/build/b14-smoke`, 45 s, then
  stopped by its timeout as asked): the log runs (43 lines through PpMessageLog), `[Profiler] GPU memory source:
  "Metal"` and `[GpuMem] at=tick ... source=Metal(unified)` print (the library's `.mm`).
- Cabin (Debug, MSVC, both passes), run `studio PC-wt-5b-20261124-203525` over 4d80463b (summary in
  `build/wt-5b/build/studio PC-runs/`): build exit 0 (jom -j16, 4 min 8 s; `pp_platform` and all 16 Core tests compiled and
  linked under MSVC). Pass 1: 327/338 pass, 11 fail; pass 2: 41/43, 2 fail. **Every failure is in main's known studio PC
  set** (same 11 + 2 in lane 2's 48387973 and 16:59 runs and lane 1's 46a7e4a9 run, which also had session_list_test
  and diag_stage_panel_test): tendencies, dtl_posture, shot_commands, migrate_step2, parity_json,
  session_diagnostics, athlete_diagnostics, reduction_rule_golden, folder_lock ("held through the symlink", a
  Windows symlink case, failing on main too), charts_delivery_presenter, athlete_repository; tst_charts_panel,
  tst_session_report. Test count unchanged (338). No new failure.
- **Not done here:** the Windows *app* link. The studio PC runner builds only the tests umbrella, so the MSVC link of
  PinPointStudio.exe against `pp_platform` (Debug or Release) is unverified. Release link **pending: the coordinator
  runs a Release link after visit 1, by a route that doesn't install** (`studio-build.cmd app` would overwrite the
  studio's installed app; coordinator's correction).
- **Build times: not re-taken.** `tools/method/build_times.sh` builds origin/main's tip with no patch (its header,
  line 39; `--dry-run` confirms the wrappers call `studio-build.cmd build nopull` with no patch). Run now, it would
  time unchanged main (≈ e10eddf7's 1082/228/762/11 s) and say nothing about B1.4. Pending: re-take after the
  coordinator's commit (compare with build/wt-37c/build/build-times/e10eddf7.txt), or give the tool a patch option
  (outside this item).

## 3. MSVC findings (the list Part 2 starts from)
- **No MSVC finding in the pilot's scope**: the library compiled and every Core test linked against it first time
  under MSVC (Debug, NMake JOM): the run's `tests_build.log` has no `LNK` line at all (no LNK4006/4221/4098/2005)
  and no `warning C` on a library source. The scope it did not reach: the app's own MSVC link (above).
- Flag parity for that link: the library inherits the root's directory-wide definitions (`PP_SHIPPING_BUILD`, the
  PPCP harness ones, set before `add_subdirectory(src/Core)`; no library source reads them) and `CMAKE_AUTOMOC`
  (an empty moc step runs for it: no `Q_OBJECT`). It does not get the app target's PRIVATE ones (debug level,
  profiler tiers, `HAVE_*`, `PINPOINT_INSTALLED`, `/bigobj`); none is read by a library source.
- Static-library dropping: nothing in pp_platform relies on static registration or `Q_OBJECT`, so the pilot cannot
  show that class of problem; Part 2's libraries with `Q_OBJECT`/QML types or registrars will need their own check.
- `/bigobj` is on the app target only; the library didn't need it in the studio PC's Debug build. A library cut from the
  diagnostics sources will (C1128).
- Unverified: the standalone `cmake -S src/Core/tests` configure (only the umbrella was configured), and the Linux
  branch (`${CMAKE_DL_LIBS}`).

## 4. Questions for the Solution Architect (binding on the other libraries; not precedents)
1. Name `pp_platform` vs Buffer's `pinpoint_buffer`.
2. Exported include path: INTERFACE `src/Core` exposes all of Core's headers, not only Platform's. A tight export
   needs the Platform headers in their own directory (a C9 file move, not in this item).
3. Where a ring library is added: here inside `src/Core/tests` under `if(NOT TARGET)`, vs beside Buffer in
   `tests/CMakeLists.txt`. The latter is needed once several suites link it in any order.
4. Stubs: none needed here, and `pp_log_stub.cpp` looks unused (delete, or keep as a stub target for a suite that
   wants a silent log). A suite with its own `PpLogStream` that also linked `pp_platform` would be MSVC's
   duplicate-symbol case.
5. One library build per configuration: fine here (no library source reads a test's DEFINES); a ring whose sources
   read configure macros would need a variant per value or the macro on the library.
6. The store-isolation lint walks executables only, so a source moved into a library leaves its manifest. Fine now
   (no settings use in pp_platform); a blocker for Part 2's libraries that touch `ppSettings()`.

## 5. Paths changed; EDGE
`CMakeLists.txt`, `src/Core/CMakeLists.txt` (new), `src/Core/tests/CMakeLists.txt`, `BUILDING.md`.
Real stores: none reachable by the change (build files only); the smoke used scratch home, settings and library.
EDGE: none.
