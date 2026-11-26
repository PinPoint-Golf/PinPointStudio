# NOTES B1.4 landing prep (lane 1, build/wt-37c), 25 Nov

Unattended. No commit. Lane 2 (build/wt-5b, #283) and the studio PC untouched.

## 0. Lane 1's End fix saved first
- `build/platform/A/parked-280-end.patch`: 676 lines, 14 files (13 modified + new
  `governance/controls/product/capture-and-session.md`), taken with `git add -A . && git diff --cached --binary`,
  index reset after. `git apply --check --cached` against a scratch index read from origin/main 15d41578: applies.
- Then `reset --hard origin/main && clean -fdq` in wt-37c only; status empty, HEAD 15d41578.

## 1. Rebase onto origin/main 15d41578
- `git apply --3way build/platform/B1/B1.4-parked.patch`: BUILDING.md, CMakeLists.txt, src/Core/tests/CMakeLists.txt
  applied cleanly (offsets only; BUILDING.md's blob had moved, 0fd40a6a → bec348c9, merged without conflict);
  src/Core/CMakeLists.txt added. **No conflict to resolve.** The +/- lines of the rebased patch are identical to the
  parked one's (diff of the change lines: empty); only context and index lines differ.
- Semantic checks on today's tree: RING-PLATFORM (im_excerpt on the merged IM) names the same stems as on 24 Nov;
  all ten library sources exist in src/Core; src/Core/tests/CMakeLists.txt hand-lists no Platform source (the grep
  hits left are comments and each test's own file).
- Saved: `build/platform/B1/B1.4-rebased.patch` (`git diff --cached --binary --no-renames HEAD`, the form
  studio-build.cmd expects); `git apply --check --cached` on origin/main: applies. The changes are left staged in
  wt-37c (so the new src/Core/CMakeLists.txt is in the index: review N1).

## 2. Mac: builds, tests, the review's proofs (2 of 3 builds used)
- What moved under B1.4 since its base 4d80463b (62 commits): no library source or header changed
  (`git log 4d80463b..origin/main` over the ten stems: none). src/Core/tests gained test-only headers
  (refused_file.h, dir_link.h) and folder_lock_test cases (#243/#244); 4a10080c widened the store-isolation lint to
  follow linked libraries (proven then on a scratch copy of B1.4). So the review's §1 (no static-registration,
  Q_OBJECT or self-registering statics in the library) holds as written.
- Builds, through `build/refactor/one_at_a_time.sh` with `-j 4`, each under a 60 min alarm started after the lock:
  app tree exit 0 (`b14land-build-app.log`); tests tree exit 0, 0 errors (`b14land-build-tests.log`).
- ctest (`b14land-ctest.log`), named exactly: the 16 suites of src/Core/tests, store_isolation_lint_test,
  architecture_lint_test, design_system_lint_test, namespace_collision_test, design_system_tokens_check,
  artefact_paths_check (exact names, in place of the review's loose regex, whose 25 held incidental matches):
  **22/22 pass**, 22 configured of 22 named, none skipped (artefact_paths_check ran: Passed,
  not its 77 skip code). The store-isolation lint passes in its new form, which folds pp_platform's sources in.
- Review proofs repeated, all still hold: the app's link line has `src/Core/libpp_platform.a` and no loose object of
  the ten (only `pp_debug.cpp.o`, which stays in the app by design); `ar t` lists the 9 `.cpp` objects + the `.mm` +
  the empty AUTOMOC `mocs_compilation.cpp.o` (N4); `pp_platform` appears only in the root, src/Core and
  src/Core/tests CMake files; folder_lock_test's link has `pp_platform-lib/libpp_platform.a` and no loose Platform
  object; every remaining mention of a library source in the root is an offline tool's list (lm_repair,
  pps_convert_library, backfill_analysis_context, pps_migrate_step2, `_pinpoint_offline_sources`, the segmenter
  branch), none of which links pp_platform, as the review found. No allowlist line added (the patch touches neither
  allowlist; architecture_allowlist.txt is main's, 176 lines). No include changed, so no include-graph count moved.
- The whole tests umbrella built (every suite still links against the rebased tree); the full suite has not run on
  it (BT-4: only the affected suites; the studio PC's full run is the commit gate).
- Not repeated: the headless app smoke (not asked for here). Review N3 (BUILDING.md's stale "ten suites (194 tests)")
  left as it was signed off.

## 3. The studio PC's Release check: `build/platform/B1/b14_release_check.sh` (prepared, NOT run)
Run from the repo root after the current installer build ends: `build/platform/B1/b14_release_check.sh`
(`--dry-run` prints the plan and the studio PC wrapper and touches nothing). Its header lists every step and exit code.
Modelled on installer_main.sh, with these differences, each a call (logged here):
- **Drop folder: `-DropDir the share never the default.** local_release.ps1
  names the staged file `…-local-<sha>.exe` with `Copy-Item -Force` and rewrites `LATEST.txt`. The studio PC's HEAD will
  be the same sha as the main installer being built now (15d41578), so the default folder would overwrite the clean
  main installer with the patched one, which the Product Owner installs at the studio PC. The staged patched installer is
  deleted after the verdict (its name and size stay in the summary). No `-InstallHere`, so nothing is installed.
- **Detached on the studio PC, restore in the wrapper.** run.cmd is launched through Win32_Process Create (as
  cabin_run.sh does), so a dropped ssh cannot leave the tree patched; its `:restore` (`git checkout -- .` then
  `git clean -fd`) runs after every path once the patch is applied. The Mac trap restores over ssh only if run.cmd
  has died without an rc.txt; if it may still run, the lock is kept with `run=b14-release/<stamp>` in the holder,
  so cabin_run.sh's waiters judge it over by its rc.txt (run.cmd writes console.log first, because `run_over`
  judges a run dir without one as never started after 10 min). The Mac's fallback restore runs only when run.cmd
  logged applying the patch; otherwise the checkout is only shown. A scratch-share .cmd also makes other lanes'
  cabin_run.sh pre-flight wait while it runs. A detached process has the developer's APPDATA/LOCALAPPDATA/TEMP
  (cabin_run's console.log of run studio PC-wt-37c-20261125-073247 shows them); run.cmd logs them too.
- **Never cleans what isn't ours:** a checkout that is not clean before is refused (rc 31) and left untouched, so
  `git clean -fd` can only remove what the patch or the build made (build\ is ignored: `.gitignore:43`, so the
  Release-Installer tree stays). studio-build.cmd's own patch route deletes only the listed created files instead
  of a blanket clean; the brief asked for `git clean -fd`, which is equivalent here given the clean-before refusal.
- **No false green:** every step is gated (pull, `git apply --check`, `git apply`); PASS needs local_release exit 0,
  the patch's four paths in `git status` after apply, a pp_platform.lib in the Release tree, no
  LNK2005/2019/2038/4006/4098/LNK1xxx line (the review's §5 list; LNK4221 counted as expected), and a clean checkout
  after. The full log, not the last 12 lines: `the share with
  `summary.txt` and `mac.log` beside it.
- **Busy check fails closed** (ssh or query failure = no build); matches pinpoint|premier|foresight|fsx|gspro, and
  refuses while jom/ctest/cmake/cl/link/cpack/ISCC or a build-script cmd.exe runs. The lock is the shared
  studio PC.lock with cabin_run.sh's holder format and token re-read; it waits (60 s polls, 2 h 40 min cap), never
  reclaims.
- Checked here: `zsh -n`; `--dry-run`; the wrapper's bytes (ASCII only, CRLF, backslashes intact).
  Not checkable without the studio PC: the run itself.
- Left as is (outside this item): build_installer.ps1's `cmake --build` passes no job count (jom's default, the
  studio PC's 24 cores), against BT-1's explicit limit (16 on the studio PC). Every installer build runs that way today.
