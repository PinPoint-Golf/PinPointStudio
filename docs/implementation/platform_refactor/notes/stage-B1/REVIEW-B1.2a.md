# REVIEW · B1.2a · the move tool reads CMake variables by scope (`build/wt-37c`, uncommitted on 62f236e6)

**Verdict: SIGN OFF.** F1 is fixed, and the fix holds for B1.5 and B1.6. I found six CMake constructs that `CMakeVars` gets wrong (section 1). None of them touches a line either map writes.
- `--verify` shares `CMakeVars`, so it is blind to the same six. `--configure` is the independent proof, but it can be skipped without notice.
- **Before B1.12, B1.16 or B1.19:** close the function-body gap (Fix A), and make an absent configure visible (Fix B).

## What I ran
- I read `CMakeVars` (`move_files.py:413-770`), the `--verify` scope check (`:1713`), `configure()` (`:1548-1631`), the wiring (`:2209-2215`) and the new self-test fixture (`:2088-2164`).
- On a `git archive` copy of 62f236e6 plus the patch, in `$TMPDIR`:
  - the old tool (`git show HEAD:…`) and the new one, dry runs of both maps, comparing their build rewrites;
  - the new self-test spliced onto the old tool;
  - a real B1.5 run, `--verify`, and planted spellings.

  The copy is deleted. I ran no configure of my own.
- `one_at_a_time.sh ctest --test-dir build/wt-37c/build/tests -R 'move_files|architecture_lint'`: 2/2 passed. `move_files_selftest -V` prints 62 checks.

## Checks
**1. Scopes.** What `CMakeVars` gets right:
- a set in the file itself, line-aware: a use before its set is unknown;
- a set in a parent before the `add_subdirectory()` (a copy at that line);
- an `include()`, read in the includer's scope;
- `foreach` over literal items;
- several routes into one file must agree, so the standalone suites count as routes;
- a non-forced `CACHE` set is unknown. That is the safe direction.

I probed the class directly. **Six constructs leave a variable "known" when CMake would give it another value, or none:**

| Construct | What the tool believes | What CMake does |
|---|---|---|
| `include()` (or a first `set`) under an undecided `if(WIN32)` | the variable is known after the `endif` (NOTES C3, "optimism") | it is empty when the branch is not taken |
| a `function()` body | uses the **definition-site** scope (`scope(path, line)` inside the body) | uses the caller's scope, at call time |
| a `macro()` that sets a variable | the body is skipped, so the old value is kept | the caller's variable changes |
| `unset(X)` | X stays known | X is gone |
| a child's `set(X … PARENT_SCOPE)` | ignored | the parent's X changes after `add_subdirectory` |
| `return()` before a set | the set is still known | the set never runs |

None of these touches a line B1.5 or B1.6 writes:
- There is no `PARENT_SCOPE` in the repo's own build files.
- `unset()` is used only on `_wrist_*`/`_ppcp_*`/`_gspro_*` temporaries.
- The only `return()` is inside `pp_win_bundle_dll`.
- `PP_ROOT` is set in the `else()` of `src/Gui/CMakeLists.txt:44-422`, and all 20 of its uses are inside that same branch.
- The only rewritten lines inside a function body are `CMakeLists.txt:3534` and `:3535` (`pinpoint_configure_offline_target`). They carry no variable: plain `src/…`, read relative to the root, where the function is called.

**2. `--verify` and `--configure`.**
- The scope check fails a changed build word that brings in a `${VAR}` the line can't see.
  - On the B1.5 rehearsal, putting Pose `:36` back to `${SRC}/…` gives FAILED: "`${SRC}` is not set in this file's scope at this line (set only in src/Analysis/tests, src/Gui/tests, src/LaunchMonitor/tests, src/Update/tests)". The same happens for `${GUI}/../…`.
  - The rehearsal itself PASSED: 57 renames, 388 files, 0 other lines.
  - The check uses the same `CMakeVars`, so it cannot catch the six cases in check 1.
- `--configure` really runs `cmake -S <root> -B <dir>/app` and `cmake -S <root>/tests -B <dir>/tests`. It fails on a non-zero exit or on any `CMake Error` line, and it refuses a scratch folder that is not empty. The self-test proves the failure on the test tree.
- `--configure` cannot pass a configure it skipped, except in one case: a tree whose `CMakeLists.txt` is missing is printed as "skipped" and does not fail. Neither tree can be missing in this repo.
- **But `--verify` without `--configure` passes silently**, and nothing says the configure was not run.
- **Two things a configure does not prove:**
  - The standalone suite routes (`cmake -S src/X/tests`). The walk covers them; `--configure` does not.
  - An include-directory word with an empty variable. CMake accepts a missing include directory on a normal target. After a move no include relies on one, so the result is a dead `-I`, not a broken build.
- The MOVES "Proof" step, the Mac configure and build of both trees, stays the catch-all. It is what caught F1.

**3. The ten new self-test cases against the original tool.** I spliced the new `self_test` onto `HEAD`'s tool.
- **8 fail, not 7 as NOTES C6 says:**
  - F1 (`${GUI}` in Analysis);
  - parent → child, with `GDIAG` set later (the old tool writes `${GDIAG}`);
  - the root's `${CMAKE_SOURCE_DIR}` fallback (the old tool writes `${GUI}`);
  - `--verify` plus `--configure` (the old tool rejects the option, exit 2);
  - the two verify mutations;
  - configure fails;
  - not empty.
- **2 pass:** "the move runs", and "a variable set in an include()d file is used in the includer". These are positive cases the old global model also gets right. They guard against an over-strict fix, not against F1, so their passing is expected.
- Three of the eight fail only because the old tool has no `--configure`, not for a semantic reason.
- Every check in the first fixture passes on both tools.

**4. B1.5 and B1.6 dry runs, old against new: confirmed.**
- B1.5: 323 build mentions, with exactly one difference. The line is `src/Pose/tests/CMakeLists.txt:36`: `${POSE}/../Export/frame_decode.cpp` became `${SRC}/…` (old) and becomes `${PP_SRC}/…` (new).
- By reading, `SRC` is set in neither `src/Pose/tests/CMakeLists.txt` nor `tests/cmake/PinPointTests.cmake`. The suites are sibling `add_subdirectory` scopes from the `foreach` at `tests/CMakeLists.txt:36`, and the standalone route doesn't set it either. So the Pose configure would have failed.
- `PP_SRC` comes from `PinPointTests.cmake:49`, a `CACHE INTERNAL` set on both routes: `include(PinPointTests)` under `if(NOT COMMAND pp_add_test)`, `Pose/tests:10-14`.
- B1.6: all 15 mentions are identical between the two tools.

**5. Windows skip: not a gap that matters.**
- Moves, `--verify` and `--configure` run on the Mac (MOVES: one lane, the Mac builds).
- Only the two `project(… NONE)` cmake runs are skipped. The scope logic is pure Python and still runs on the studio PC.

## Fixes (not blocking B1.2a; due before B1.12, B1.16 or B1.19 runs)
- **A. Function and macro bodies.** In a line inside a `function()`/`macro()` body, treat no variable as known except `CMAKE_CURRENT_LIST_DIR`. Use the line's own spelling or "can't place", and make `--verify`'s scope check do the same. Today a body is read in its definition-site scope, but CMake reads it in the caller's.
- **B. A skipped configure must be visible.**
  - `--verify` without `--configure` should print `CONFIGURE NOT RUN`.
  - A "skipped" tree should be FAILED.
  - Each move brief (B1.5, B1.6, B1.12, B1.16, B1.19) should name `--verify … --configure <scratch> --seed <build>`, and ask for its `CONFIGURE PASSED` line in NOTES.
- **C. (EDGE)** Under an undecided `if`, treat a first set as unknown outside its own branch. Model `unset()`, `return()` and a child's `PARENT_SCOPE`, or list each one under "can't place" when a rewrite would use the variable they touch.
- **NOTES-B1.2a corrections:**
  - C6: 8 of the 10 new checks fail against the original, not 7.
  - EDGE: add the function-body and `unset`/`return`/`PARENT_SCOPE` cases.
