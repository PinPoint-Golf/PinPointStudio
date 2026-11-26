# REVIEW B1.4 · pp_platform pilot (build/wt-5b, uncommitted) — independent review, 24 Nov

**Verdict: SIGN OFF.** No blocking finding. Four non-blocking notes (N1–N4); N1 is a commit instruction, not a code fix.

Ran (through the lock): `ctest --test-dir build/wt-5b/build/tests -R 'core|pp_|work_pool|architecture_lint|store_isolation' -j 3`:
25/25 pass, incl. all 16 Core suites, `architecture_lint_test`, `store_isolation_lint_test`. No allowlist file in the diff
(`git status`: BUILDING.md, CMakeLists.txt, src/Core/tests/CMakeLists.txt, new src/Core/CMakeLists.txt). Not run by me:
`design_system_lint_test` (outside my permitted regex; no QML touched, NOTES report it passing).

## 1. MSVC static-library traps — none apply
Searched the 9 sources + .mm + their headers for `Q_OBJECT`/`Q_GADGET`/`Q_NAMESPACE`, `Q_GLOBAL_STATIC`,
`Q_COREAPP_STARTUP_FUNCTION`, `Q_CONSTRUCTOR_FUNCTION`, `qRegisterMetaType`, `Q_DECLARE_METATYPE`, `qAddPreRoutine`,
`atexit`, `__attribute__((constructor))`, `init_seg`, `Q_LOGGING_CATEGORY`, `Q_INIT_RESOURCE`, `thread_local`: **zero hits.**
File-scope statics, all passive data read only by functions in the same TU (nothing runs on its own):
- `pp_profiler.cpp:31` `Profiler::s_deepEnabled` (atomic, read via `deepEnabled()`).
- `pp_os_metrics.cpp:159-169` `g_mutex`, `g_threads`, `g_proc*`, `g_threadsPrevWallNs`.
- `pp_gpu_metrics.cpp:37-54, 131, 174-179` `g_mutex`, `g_initAttempted`, `g_available`, `g_backend`, `g_deviceName`,
  `g_unified`, `g_peakProc`, DXGI `g_factory/g_adapter/g_deviceTotal`, NVML `g_lib/g_nv*/g_device/g_pid`.
Function-local statics (Meyers singletons, initialised on first call, so a reference is what pulls them in):
`PpMessageLog.cpp:28`, `PpStatsLog.cpp:26`, `AnalysisProfileLog.cpp:26`, `pp_profiler.cpp:35`, `folder_lock.cpp:36,41`,
`work_pool.cpp:40` (counter), `work_pool.cpp:272` (`QEvent::registerEventType()` — lazy, not a file-scope registrar),
`pp_os_metrics.cpp:98`. The `.mm`'s `macInit`/`macProcessBytes` are called from `pp_gpu_metrics.cpp` in the same archive.
**Nothing relies on an unreferenced object being linked.** The app tree's library also carries an empty
`pp_platform_autogen/mocs_compilation.cpp.o` (AUTOMOC is on there, off in the tests tree; `build/app/build.ninja:17522`):
no symbols, nothing depends on it (see N4).

Header macros: `pp_debug.h:32,61` and `pp_profiler.h:63-66,233,280` branch on `PINPOINT_DEBUG_LEVEL`/`PINPOINT_PROFILE*`
**only inside macro definitions** — no class layout or inline body changes with them, and no library source uses
`ppDebug()`/`PP_PROFILE_*` (grep: zero). So building the library without the app's PRIVATE defines is not an ODR risk.

## 2. Every target that used the sources
- **App**: removed from its lists (`CMakeLists.txt` 748, 954-ish block, 2980, 3099) and linked at 952. The Mac app link
  line has `src/Core/libpp_platform.a` and no loose object of the 9; `ar t` of every other archive on the app's link line
  (swingstore, buffer, ppcp, ppswing, wrist, gspro, whisper, ggml, samplerate, OpenCV) holds no Platform object, and
  `nm -u` of swingstore/buffer/ppcp/ppswing/wrist/gspro shows no reference to a Platform symbol (so no GNU-ld order
  issue on Linux either). No source outside `src/Core` (tests excluded) defines a Platform symbol.
- **src/Core/tests**: every former hand-list now `LINK pp_platform`; 0 compiled Platform sources remain.
- **Every other target that hand-lists them** — offline tools in the root (`lm_repair`, `pps_convert_library`,
  `backfill_analysis_context`, `pps_migrate_step2`, `_pinpoint_offline_sources` + segmenter branch for
  `swinglab_run`/`swing_window_parity_test`), `src/Gui/CMakeLists.txt` 92-108, 283-293, and the Analysis, Ppcp, Pose,
  LaunchMonitor, Update, IMU test lists: **unchanged and none links `pp_platform`** (grep: `pp_platform` appears only in
  the root's app link, `src/Core/CMakeLists.txt`, `src/Core/tests/CMakeLists.txt`). No target both lists a source and
  links the library; no target lost one. The root's `.mm` ARC property (3103) is still needed for the offline segmenter
  branch (3554) and correctly kept.

## 3. Include directory
`PRIVATE` + `INTERFACE` on the same `src/Core` path = `PUBLIC` in effect; acceptable. It reaches only the app (already has
`src/Core`, root 860) and Core tests (all already pass `INCLUDE ${CORE}`). **No new include directory reaches any target.**
That the export shows all of Core's headers is NOTES Q2 for the Solution Architect, correctly not decided here.

## 4. macOS
`.mm` compiles in the library with `-fobjc-arc` in both trees (app `build.ninja:17505ff`, tests `40751ff`: FLAGS end
`-fobjc-arc`; same `-std=gnu++20 -arch arm64 -isysroot` as the app's own objects, e.g. `pp_debug.cpp.o`). The property is
set in `src/Core`'s directory scope, the target's own directory, so it applies. Metal + Foundation are `PUBLIC` on the
library; the app also links Metal itself (3169) — a harmless duplicate. NOTES' smoke shows `source=Metal(unified)`.

## 5. The Windows gap — what could break the app's link but not the tests'
Only three things differ between the studio PC's proven link (tests tree) and the app's:
(a) the app's own objects now resolve Platform symbols from an archive instead of loose objects — MSVC rescans libraries,
order-insensitive, and no other app library defines or needs them (checked on the Mac's identical dep set);
(b) the app tree's directory-wide defines (`PP_SHIPPING_BUILD` in Release, PPCP harness ones) reach the library — no
library source or header reads them;
(c) AUTOMOC's empty `mocs_compilation.obj` in the app tree's library — at most `lib.exe` warning LNK4221.
Runtime library (`/MD`) and C++20 are global (root 216; no `MSVC_RUNTIME_LIBRARY`, `/GL`, LTCG or IPO anywhere in the
root or `studio-build.cmd`), so no LNK2038/LTCG mismatch is possible from this diff.
**The morning Release link need check only:** link exit 0; `findstr LNK` in the app's build log shows no LNK2005,
LNK2019, LNK2038, LNK4006 or LNK4098 (LNK4221 on mocs_compilation is expected and fine); `pp_platform.lib` built in the
Release tree. A Debug app link adds nothing the Debug tests link did not already prove. No runtime check is required for
link correctness; if the exe is started anyway (not deployed), the `[GpuMem] ... source=DXGI` line confirms the GPU
backend.

## 6. Lints and BUILDING.md
Lints pass with no allowlist line (above). BUILDING.md's new paragraph is accurate: membership, `pp_debug.cpp` reason,
links, `LINK pp_platform` + inherited include, `if(NOT TARGET)` self-add, only Core tests so far.

## Notes (non-blocking)
- **N1 — commit:** `src/Core/CMakeLists.txt` is **untracked** (`git diff --stat` does not show it). The clerk must
  `git add src/Core/CMakeLists.txt` explicitly, or the commit breaks every configure (root 713 and Core tests 36).
- **N2 —** `src/Core/tests/CMakeLists.txt` still passes `INCLUDE ${CORE}` alongside `LINK pp_platform` (redundant, harmless;
  leave for Part 2's convention).
- **N3 — BUILDING.md 534:** the edited sentence keeps the stale "ten standalone unit-test suites (194 tests under the
  umbrella)"; the studio PC run counts 338. Optional one-word-level fix: drop the count or say "eleven suites (338 tests)"
  after confirming the suite count.
- **N4 —** AUTOMOC on `pp_platform` in the app tree only (inherited `CMAKE_AUTOMOC`); harmless now. If Part 2 wants the
  two trees identical, `set_target_properties(pp_platform PROPERTIES AUTOMOC OFF)` — a precedent, so a question for the
  Solution Architect, not a fix here.
- Standalone `cmake -S src/Core/tests` and the Linux branch remain unverified (NOTES says so); not blocking.
