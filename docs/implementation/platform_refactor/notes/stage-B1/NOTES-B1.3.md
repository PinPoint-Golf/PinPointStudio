# NOTES · B1.3 · build-time and parity baselines (lane 1, build/wt-37c, at origin/main e10eddf7)

## Rulings and calls
- **Hold, then ruling.** The coordinator held the Mac clean builds mid-item. The Product Owner then ruled (24 Nov) that all B1.3 build timings are taken **on the studio PC only**, so none were taken on the Mac and no Mac scratch tree was made. No earlier timings existed (the brief's grep found none).
- **No clean option in `studio-build.cmd tests`** (`:tests`, lines 216-248: `cmake .` then `jom -j%TJ%` in `build\tests-vs18`), and there is no third tree on the studio PC. What stood in is `jom clean` in `tests-vs18`, run by the step's wrapper just before `studio-build.cmd tests nopull jobs 16`. CMake's cache and autogen's outputs are kept. `studio-build.cmd` is not edited.
- **Builds go only through `studio-build.cmd`** (the HARD studio-build rule), so both ctest passes ran after every build (about 15 min a step). They are not judged.
- **Incremental steps touch the file, they don't patch it.** `copy /b <f> +,,` leaves the checkout clean and causes no extra rebuild later. The link file is `src/Analysis/tests/session_summary_test.cpp`, the only source of one target. The repeat is `swing_analysis.h`.
- **Cabin lock**: taken in `cabin_run.sh`'s holder format (lane `build-times-wt-37c`, own token, re-read), with a 5 h ceiling and **no `run=` line**, so no waiter could reclaim it between steps. It was **held 17:20 to 19:30 without advance notice to the coordinator**, then released.
- **Mac**: 1 incremental build (`--target regrade_ledger athlete_dump`, ccache on) for parity, through `build/refactor/one_at_a_time.sh`. Disk had 25 GiB free.

## Cabin timings (baseline): `build/wt-37c/build/build-times/e10eddf7.txt`
Commit **e10eddf7** (the studio PC's HEAD, and origin/main on the Mac). `tests-vs18`, Debug, NMake JOM, **jom -j16**. **No compiler cache**: there is no launcher in its CMakeCache. Peak memory isn't measured there.

| step | seconds | compiles | links |
|---|---:|---:|---:|
| clean (`jom clean` 3 s, then the build) | 1082 | 5614 | 356 |
| one-header `src/Core/pp_debug.h` | 228 | 785 | 129 |
| one-header `src/Vocabulary/Analysis/swing_analysis.h` | 762 | 3183 | 173 |
| link `session_summary_test.cpp` (whole jom run) | 11 | 1 | 1 |
| repeat `swing_analysis.h` | 769 | 3183 | 173 |

- **Self-check**: +0.9% drift with the same compile count, within 10%.
- **The pp_debug.h row is plausible.** The Mac test tree's `ninja -t deps` gives the same ratio: 792 objects depend on `pp_debug.h` and 3483 on `swing_analysis.h`. So `pp_debug.h` is not the widest header on the test tree; `swing_analysis.h` is.
- **Suites**: every step's passes exited 8. Step 5's logs, the only ones read, show 326/337 and 41/43. The failures are the same set as the 16:35 studio PC run: 11 in pass 1, plus `qml_ui.tst_charts_panel` and `tst_session_report`. Main's failures, not this item's.

## Parity baseline
`python3 -I build/wt-37c/tools/parity/parity_baseline.py b1-base` through the Mac lock: exit 0. Label **`b1-base`**, commit **e10eddf7**, folder `the share (out/ only, 18 MB). It took 8 sessions of 4 athletes, and the dump counted all of them. `parity_diff.py b1-base b1-base`: **0 differences**. run_mac.json's "2 changed paths" are this item's uncommitted script and README; neither is built into the two tools.

## What changed, tests, docs
- New: `tools/method/build_times.sh`. Its header says what it times and how to compare two runs. `--no-clean` and `--dry-run` are the options.
- Changed: `tools/method/README.md`, "Five small tools", with the `build_times.sh` paragraph and its missing self-test noted.
- Checks: `zsh -n`; `--dry-run` with and without `--no-clean` (the pull is in step 1 only; wrappers are ASCII); a bad `--header` exits 21. The real run is the test. No suite run on the Mac, since there is no product change.
- Real stores: none reached. The tests mode's own store guard ran as usual, and parity copies the library read-only.

## EDGE
- The "clean" row is not a fresh configure. Compare it only with this script's own clean row.
- About 75 min of the 2 h 10 min studio PC hold was unmeasured ctest. *Proposed, for the coordinator:* a build-only switch in `studio-build.cmd`, as its own item, since the studio PC runner can't test that file (exit 21).
- If the Mac side dies mid-step, the studio PC lock is kept until its 5 h ceiling (by design: no `run=`).
- `build_times.sh` has no `--self-test` and no ctest.
- I sent one mistyped no-op command through `one_at_a_time.sh`. It found no command and did nothing.
