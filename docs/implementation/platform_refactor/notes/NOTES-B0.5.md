# NOTES B0.5 · A build-only mode for the studio PC's test tree

Lane 1, `build/wt-37c`, at origin/main f7881dd8 (already up to date). Nothing committed; no builds; nothing run on the studio PC.

## 1. What changed

**`tools/studio/studio-build.cmd`** (ASCII, CRLF: `file` says "ASCII text, with CRLF line terminators", 442 lines and 442 CRs, no byte above 0x7F):
- Header: `build` added to the usage line (:6). Its own paragraph and a FIRST RUN note (:45-53). Exits 10 and 11 reworded to cover `build` (:82-83). Patch support noted (:86-87). `jobs` is for tests and build (:22, :96).
- Arguments: `jobs` accepted for `build` (:111). `build` added to the mode list and both USAGE echoes (:101, :123-124).
- Idle guard: `build` takes the same start line and `:busy` refusal (exit 10) before the pull (:137-150). It skips the APPDATA and TEMP refusals (no settings snapshot) and adds one `tests: build only ...` line.
- Dispatch: `build` goes to `:tests` (:195). It shares that section: cmake `.` (tests_cmake.log), the three `ctest -N` listings (tests_list*.log), then `jom -j%TJ%` (tests_build.log). The "pass 1 will run" line is skipped in build mode (:263). After a good build it goes to `:builtonly` (:269, :297-299), which writes one end line and then `goto done`. So a patch is always restored through `:done`, and a failure through `:fail` (exit 11) as today.
- Tests mode: every line it prints and every step it runs is unchanged, since the new lines are all conditional on `build`. `cabin_run.sh` parses that output.

**`tools/method/build_times.sh`**: the header says the build mode is used (:11-14), and the "also runs both test passes" sentences are gone (:24). The wrapper calls `studio-build.cmd build nopull jobs $JOBS` (:150). The `ctl` ctest-pass parse is dropped, and so is its part of the notes column (:337). Everything else is unchanged.

**`tools/method/cabin_run.sh`**: no change. It hard-codes `tests` and parses no mode list.

**Docs**: `docs/developer/testing_developer_guide.md:299` has a new short subsection, "On the studio PC", covering both modes. `tools/method/README.md:13` (build_times' line) now names the build mode.

## 2. Tests (Mac, through ctest and the lock)
- `zsh -n tools/method/build_times.sh`: OK.
- `cabin_run_selftest`: Passed (50 s). `one_at_a_time_selftest`: Passed. `architecture_lint_test` and `design_system_lint_test`: Passed.
- `build_times.sh build/wt-37c --dry-run --no-clean --header src/Core/pp_debug.h`: both wrappers call `studio-build.cmd" build nopull jobs 16`.
- `jom_secs` (build_times' parser, sourced from the script) over a hand-made build-mode console gives `75 0`. That proves the shared `tests: ...; jom -jN` and `tests: build exit` lines still parse.
- I read the `.cmd` diff line by line against `:tests`. No new `&&`, no delayed expansion, and no `( ) & | < >` in new `:say` text.
- Cabin full suite: not run. A change to `studio-build.cmd` is refused by `cabin_run.sh` (exit 21), as the brief says.

## 3. Proof for the coordinator, after commit (on main)
**Precondition: the studio PC's current copy refuses `build` with exit 8 before it pulls** (at the `jobs` gate, or at the mode check without jobs). So the brief's direct command fails until the studio PC's checkout holds the commit. Never pull by hand while a studio PC run is live: that rewrites `studio-build.cmd` under a running copy, and cmd reads a running batch file from disk. Run in this order:
1. Run `tools/method/build_times.sh build/wt-37c --no-clean --header src/Core/pp_debug.h` first. It takes the studio PC lock, pre-flights, and its first wrapper pulls before it calls the script. Expect about 4 min (before: 228 s plus about 17 min of tests). The table's notes should read `studio-build exit 0` with no pass results.
2. Then, with `the share absent and nothing building there (the direct call takes no lock), run `ssh to the studio PC studio PC "cmd /c C:\Users\developer\Projects\PinPointStudio\tools\studio\studio-build.cmd build pull jobs 16"`. Expect exit 0 and these console lines in order:
   `tests: start <date> <time>` · `tests: build only - build\tests-vs18 is built, and no ctest pass is run` · `tests: studio idle - N processes, ...` · `tests: cmake <t>` · `tests: <n> tests registered; jom -j16 <t>` · `tests: build exit 0 <t>` · `tests: built only, no ctest pass run and no real store checked - end <date> <time> - exit 0` · `== status after` (empty) · `== end`.
   It should print no `ctest pass`, `stores` or `pass 1 will run` lines. tests_summary.log should hold the same `tests:` lines.
3. "About 1 to 2 minutes when nothing changed" is the brief's expectation, not yet measured. It appears in the script header (:50) and the guide (:304). Correct both from step 2's time.

## 4. Paths, calls, EDGE
- Paths: `tools/studio/studio-build.cmd`, `tools/method/build_times.sh`, `tools/method/README.md`, `docs/developer/testing_developer_guide.md`.
- Real stores: the build mode reads and writes none of the settings, Recycle Bin or Credential Manager, and it runs no test. It writes only the studio-build log folder on the share and the build tree. The guard is the same idle check as tests (exit 10).
- CALL: build mode keeps the `tests:` prefix on its lines. The prefix names the test-tree lane and its tests_*.log files. Keeping it lets build_times' existing parser work, and no new prefix needs teaching.
- CALL: the brief said to update the guide "where it lists the studio modes". It listed none (only `migrate` :667 and `tool` :678 in passing), so I added a short subsection.
- CALL: the three `ctest -N` listings stay in build mode ("the same steps", a few seconds). Only the first (tests_list.log) feeds a printed line, the jom line's test count. The other two only write their list files, since their counts are printed only by the tests mode.
- EDGE: in build mode, an older tests run's `tests_ctest.log` and `tests_qml_ui.log` stay in the log folder. Neither cabin_run.sh nor build_times reads them for a build run.
- EDGE (existing, not touched): `:putback` has unreachable lines after `goto :eof )` (studio-build.cmd :435-442).
