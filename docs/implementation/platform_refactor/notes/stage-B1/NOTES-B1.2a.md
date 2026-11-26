# NOTES B1.2a · the move tool reads CMake variables by scope; `--verify` and `--configure` catch F1

Lane 1, `build/wt-37c`, on `62f236e6` (origin/main; ff-only: already up to date). Not committed.

## What changed
- `tools/refactor/move_files.py:486` **`CMakeVars` rewritten.** The old one treated any variable set to one value anywhere as global (F1). The new one walks the build the way a configure does:
  - It starts from every `CMakeLists.txt` that calls `project()`: the root, `tests/`, the nine standalone suites and `src/Buffer`.
  - It reads statements in order:
    - `add_subdirectory()` gives the child a copy of the scope at that line;
    - `include()` reads the file in the includer's scope, and a module name is looked up through `CMAKE_MODULE_PATH`;
    - `foreach()` with literal items is expanded, which is how `tests/` reaches each suite;
    - `function()`/`macro()` bodies are skipped;
    - only `CACHE INTERNAL`/`FORCE` sets are known (`:680`);
    - `if(NOT COMMAND x)` and `if(PROJECT_IS_TOP_LEVEL)` are decided.
  - `scope(path, line)` (`:758`) is **line-aware**. A file reached by several routes knows a variable only where every route gives it the same value (`:732`).
  - The command parser is `cmake_commands` (`:446`).
- Rewriter:
  - It reads the scope per line (`:1140`).
  - Respell (`:1057`): it uses only a variable the line knows. It picks the deepest variable that holds the path, then the one the file already spells most, then sorts by name. Failing that it uses a repo-root variable the line knows; failing that the path goes under "can't place". It never invents a variable.
  - The report lists the build files the walk does not reach (`:1316`).
- `--verify`: a changed build word that brings in a `${VAR}` its line can't see fails, with its own message (`:1713`).
- `--verify … --configure <dir> [--seed <build>] [--cmake-arg a]` (`:1568–1650`, `:2211`):
  - It runs `cmake -S <root> -B <dir>/app` and `cmake -S <root>/tests -B <dir>/tests`, and exits 1 on any configure error.
  - It refuses a folder that is not empty.
  - The seed gives the generator, the toolchain keys and every `FETCHCONTENT_SOURCE_DIR_*`, and its `_deps/` downloads are cloned in (APFS clone).
- Self-test (`:2088`): a second fixture adds 10 checks (52 → 62), the repo-root fallback among them.
- Docs: the tool's docstring, and `tools/refactor/README.md` (verify, `--configure`, self-test, and a new "CMake variables: what a line knows" paragraph).

## Calls
- C1 · The standalone suites count as routes, because their headers document `cmake -S src/X/tests`. So a variable must exist in both the umbrella and the standalone build. As a result `${CMAKE_SOURCE_DIR}` is the repo root only in the app's own lists.
- C2 · Line-aware rather than per file (advisor). A variable set later in the file is unknown before its line; the self-test checks this with `GDIAG`.
- C3 · An undecided `if` reads both branches. A set under one that *changes* a known value makes it unknown. A first set under one stays known (the old optimism; `src/Gui/CMakeLists.txt`'s `PP_ROOT` lives inside such a branch).
- C4 · The 7 files the walk does not reach are the `cmake -P` scripts, plus `cmake/swingstore/CMakeLists.txt`, which is reached only from inside a function. They know only their own sets and `CMAKE_CURRENT_LIST_DIR`. The old `CMAKE_SOURCE_DIR = ''` guess is gone, and so is the `PP_REPO_ROOT = ''` hard-wiring: the walk supplies it.
- C5 · The self-test runs cmake on two `project(… NONE)` lists (about 0.1 s each), on the Mac and Linux only. It skips on Windows (`os.name != 'nt'`, as the file-mode check does; untried there, and 0 studio PC runs) and when there is no cmake. It is the only proof that `--configure` sees a failure.
- C6 · "Before": I spliced the new `self_test` onto the original tool (`$TMPDIR`, deleted). 7 checks fail. The rewriter writes `add_custom_target(analysis_t SOURCES ${GUI}/diag/layout.cpp …)`, which is B1.18's exact F1, and the verify mutations fail because the old verify passes `${GUI}`. After the fix: 62/62.

## Item 4 · B1.5 and B1.6 dry runs on main (full list: `NOTES-B1.2a-cmake-lines.txt`)
- **B1.5 would have hit F1, once:** `src/Pose/tests/CMakeLists.txt:36`. The old tool wrote `${SRC}/Vocabulary/Recording/frame_decode.cpp`, but `SRC` is set in neither `src/Pose/tests` nor `PinPointTests.cmake`. It was taken from the Analysis, Gui, LaunchMonitor and Update suites, so the Pose suite's configure would have failed. The fixed tool writes `${PP_SRC}/…`, which comes from `PinPointTests.cmake` (CACHE INTERNAL) and is known on both routes.
  - Every other one of the 323 mentions (317 lines, 22 files) is byte-identical to the old dry run.
  - Variables used: `SRC` 239 (Analysis, Gui and its `.cmake`s, LaunchMonitor, Update), `PP_ROOT` 20 (`src/Gui/CMakeLists.txt`), `PP_SRC` 2 (Pose, Ppcp), and none in 62 (root, `PinPointQmlModule.cmake`, relative paths).
  - Seven tokens change variable: the six LaunchMonitor `${LM}/launch_monitor_reading.cpp` → `${SRC}/Vocabulary/LaunchMonitor/…`, where `SRC` is set in that file, and Pose `:36`.
  - A check over every mention: 0 use a variable that is not in scope at its line.
- **B1.6 would not:** its 15 mentions in 4 files are identical to the old run. `${SRC}` appears 2×, the other 13 use no variable, and none changes variable.
- **B1.18 replayed** (dry run on a `git archive` of `fcd5ac53`): the fixed tool writes exactly B1.18's hand fix, `${SRC}/Gui/diagnosticmodel/layout/…`, on all 7 lines (8 tokens). The old tool writes `${GUI}/…`.
- B1.13 and B1.18 on main: nothing to move, and the reports are unchanged except for the new "not reached" list.

## Tests
- `one_at_a_time.sh ctest --test-dir build/wt-37c/build/tests -R '^(move_files_selftest|architecture_lint_test|design_system_lint_test)$'`: 3/3 passed (`build/wt-37c/build/b12a-ctest.log`).
- Rehearsal of B1.5 (1 configure of each tree, the budget): a `git archive` of 62f236e6 plus this tool in `$TMPDIR`, with `git init` and a base commit, then the real run (57 moved, 349 files rewritten). Then, through the lock, `move_files.py B1.5 --verify <base> --configure <copy>/build/b12a-cfg --seed build/wt-37c/build`: **VERIFY PASSED** (57 renames, 388 files, 0 other lines) and **CONFIGURE PASSED**: the app and the test tree each exit 0 with 0 CMake errors (the test tree in 17 s), and neither log has a download line. The Pose suite configures with `${PP_SRC}/Vocabulary/Recording/frame_decode.cpp` (`:36`). Log: `build/wt-37c/build/b12a-rehearsal.log`. The copy is deleted. Not run, to stay within budget: a configure of the old `${SRC}` spelling. That it fails is an inference from the self-test's same-pattern failure (`Cannot find source file`).
- No QML, no C++, so no build. 0 studio PC runs (budget 0).
- Disclosed: while developing I also ran `--self-test` directly (outside ctest and the lock) several times, including its 0.1 s cmake runs, and the "before" hybrid once. The suite results above are ctest's.

## Real stores
None reachable. This is a Python tool. The rehearsal configures only a `$TMPDIR` archive copy; the seed (`build/wt-37c/build`) and the shared fetched sources are only read.

## Paths changed
`tools/refactor/move_files.py`, `tools/refactor/README.md`; new `build/platform/B1/NOTES-B1.2a.md`, `NOTES-B1.2a-cmake-lines.txt`.

## EDGE
- A relative path in an included `.cmake` is still read from `cmake_base()`: its parent folder for `cmake/`, not each includer's `CMAKE_CURRENT_SOURCE_DIR`. `PinPointQmlModule.cmake` is included from both the root and `src/Gui`.
- These are not modelled: variables set by `string()`, `file()`, `find_*`, `cmake_path()` and `option()`; `return()`; `block()`; an `add_subdirectory()` inside a function (swingstore, reported); and a normal variable shadowing a cache entry set in a child folder. A variable set by `set()` and then overwritten by one of those keeps its `set()` value.
- For B1.12, B1.16 and B1.19: a `${VAR}/…` mention whose variable the walk does not know is listed under "can't place", not rewritten. `--verify` does not fail it; only `--configure` does. So read the can't-place list, and configure.
- `--configure` assumes the seed's `_deps` downloads are current. A seed that lacks one makes the configure download it into the scratch copy.
