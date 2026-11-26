# NOTES B1.2b · the move tool reads function bodies where they are called, and a skipped configure is never a pass

Lane 1, `build/wt-37c`. I started on `c8d3eb9d`. The worktree is now at `b3e656cf` (origin/main): two commits since then, neither under `tools/refactor/`. Not committed.

## What changed (`tools/refactor/move_files.py` unless named)
- **`CMakeVars` (`:549-1103`)** replaces B1.2a's class. Each value is a string, unknown (`None`), or a `Doubt` (`:508`): the values it may take, and why the walk can't decide.
  - **Item 1, function and macro bodies.** `_call` (`:880`) reads a body where it is called, never where it is defined.
    - A function runs in a copy of the caller's scope, with its arguments. A macro runs in the caller's own scope, so its sets change the caller's variables.
    - Each call is a frame limited to the body's lines (`_Frame.span`).
    - A body line knows only what every call agrees on. A body that no call reaches knows nothing (`info`, `:1085`).
    - Inside a body, `CMAKE_CURRENT_LIST_DIR` and `CMAKE_CURRENT_SOURCE_DIR` are the caller's, as in CMake.
  - **Undecided `if`.** Each branch is read from a snapshot of the state before the `if` (`_block`, `:759`; `_if_close`, `:834`) and the branches are merged at `endif`. A variable they leave different, or that the branch not taken leaves different, becomes a Doubt. A first set is still known inside its own branch.
  - **`unset()` and `set(X)`** unset the variable (`_unset`, `:666`).
  - **`set(… PARENT_SCOPE)`** writes to the parent, through a parent pointer (`:920`). If it runs on some paths only, the parent's value is a Doubt.
  - **`return()`** ends the file or the function when it runs on every path. When it runs on some paths only, what follows it is a Doubt for the includer or the caller (`_run`, `:715`).
  - Foreach passes are tagged, so an `if` merge on one line is not mistaken for a loop body.
- **Item 3: can't place, never a guess.** `cmake_cant_place` (`:1459`) and `rewrite_span` list these lines with a `CMake scope:` reason and leave them as they are:
  - a `${VAR}/…` word whose undecided VAR, on one of its values, names a moved path;
  - a word whose VAR has no value the tool can read, when its tail names a moved path. This closes a gap: `${LM}/lm_gates.h` was skipped in silence;
  - a relative path in a body whose calls come from another folder.

  `--verify` fails a rewrite through an undecided variable, through a body no call reaches, or of a relative path in a body (`:2173`). Its final pass lists each can't-place line (`:1985`).
- **Item 2: a configure that did not run is not a pass** (`main`, `:2869`; `configure`, `:2063`).
  - `--verify` without `--configure` prints `NOT CONFIGURED: this is not a pass` and exits 3. It exits 1 if the line checks failed as well.
  - `--no-configure-ok` turns this off, for the self-test only.
  - A tree with no `CMakeLists.txt` is now FAILED, not skipped.
- **Added at the coordinator's request (ADVICE-B1.5, risks 4 and 1): keeping dated lines.**
  - **Setting a kept line.** Use a map stanza, `keep: <file>:<lines>`, or `--keep <file>:<lines>`, which can be repeated (`parse_keeps`, `:166`).
  - **The rewriter** leaves each kept line exactly as it is. It drops that line from the report's counts and lists it with what the move would have made it (`rewrite_file`, `:1506`).
  - **Errors.** A kept line the map would not rewrite is an error, since its line number is stale. So is a `keep:` that names a file not in the tree.
  - **`--verify` (`:1844`)** finds each kept line through its file's diff. This happens before the `--allow-prose` skip, so it covers the information model too. A row added above a kept line moves the line, not the check.
    - A kept line must sit unchanged in an equal block, or verify fails.
    - The final pass accepts the old spelling on exactly those lines and names each. An old spelling anywhere else still fails.
  - **`--cmake-arg`** already passed any token, `-D…` included. It now goes after the seed's arguments, so it wins.
  - **`SEED_KEYS`** (`:2002`) adds `PINPOINT_BUILD_TOOLS` and `PINPOINT_BUILD_LM_REPAIR`.
- **Self-test: 62 to 90 checks** (96 after the review, below).
  - Every existing `--verify` call passes `--no-configure-ok`.
  - The second fixture gains a function body read at its call. Its `--configure` passes, and it fails on the definition-site spelling the old reading left.
  - New configure checks: NOT CONFIGURED; a tree that is missing; and a `--cmake-arg -D…=ON` that beats a seed's OFF.
  - `constructs_selftest` (`:2309`) covers the six constructs and the nine keep cases. It runs no cmake, so it also runs on Windows.
- **Docs.** The tool's docstring (options, exit 3, keep) and `tools/refactor/README.md`: the keep stanza, `--configure`/`--cmake-arg`, the self-test, and the "CMake variables" paragraph, rewritten.

## Calls
- **C1 · Model, don't refuse, wherever CMake's answer is decidable.**
  - Modelled: a body called once or the same way every time, a macro's set, `unset`, a `return()` on every path, and a `PARENT_SCOPE` set on every path.
  - Only what stays undecided is "can't place".
  - The review's Fix A ("no variable known in a body") is stricter than it needs to be where the answer is decidable. It refuses no more than this does where the calls disagree.
- **C2 · Fix A's exception (`CMAKE_CURRENT_LIST_DIR` known in a body) is wrong for CMake.**
  - Inside a function, CMake gives the caller's listfile folder. `CMAKE_CURRENT_FUNCTION_LIST_DIR` exists for the defining file's folder.
  - The tool follows CMake, and it now knows `CMAKE_CURRENT_FUNCTION_LIST_DIR`.
  - A grep finds no body in the repo that uses `CMAKE_CURRENT_LIST_DIR`.
- **C3 · Exit 3 for NOT CONFIGURED**, not 1, so that the self-test's `rc == 1` checks can't pass for the wrong reason.
- **C4 · When an unknown variable is "can't place".** It fires only when the word's tail names a moved path, and `*_BINARY_DIR` is excluded. The result is no CMake-scope can't-place on any map, so no noise.
- **C5 · Where the draft maps live.** They are in `build/platform/B1/drafts/`, not `tools/refactor/maps/`, because each move item writes its own.
  - **B1.19's draft is a guess by file name, not R8's file list.**
  - The ruled destinations: R5 (a) `src/LaunchMonitor/inferred/`; R6 (a) `src/Stores`; R8's folders.
- **C6 · Spending a second configure.** The coordinator offered one, and I needed it: `pinpoint_configure_offline_target` (`CMakeLists.txt:3532`) sits under `if(PINPOINT_BUILD_TOOLS)` (`:3413`), which the seed has OFF. So the first configure never read the only body lines that item 4 writes.
- **For the coordinator, not done by me:**
  - `MOVES.md`: the "Proof" bullet's `move_files.py --verify <map>` now exits 3 unless `--configure` is added; the "Until B1.2b lands…" sentence needs updating.
  - The briefs for B1.6, B1.9, B1.12, B1.16 and B1.19 point at MOVES; B1.5's brief already names `--configure` with the tools on.
  - **No script calls the move tool's `--verify`.** I grepped `build/platform/B1/*.sh`, `build/refactor/*.sh`, `build/run-me`, `tools/`, `docs/developer/` and `BUILDING.md`. `b18_roundtrip.sh` doesn't call it; the other hits are `codesign --verify` and `rename_identifiers.py`.

## Tests
- **Mac suites.** `one_at_a_time.sh ctest --test-dir build/wt-37c/build/tests -R '^(move_files_selftest|architecture_lint_test|design_system_lint_test)$' -j 3`: **3/3 passed** (`build/wt-37c/build/b12b-ctest.log`). With `-V`: 90 `ok`, 0 FAIL (`b12b-selftest-V.log`).
- **Before.** I spliced the 81-check self-test (before keep) onto `c8d3eb9d`'s tool, accepting `--no-configure-ok` as a no-op. 17 checks fail:
  - **12 for a semantic reason:** the function-body `${LAYOUT}` line; the two-calls, uncalled and relative body cases; the `if` set and its `include`; the macro; `unset`; `return`; `PARENT_SCOPE`; and two verify mutations.
  - **5 on the output format, or because the configure fails:**
    - NOT CONFIGURED;
    - both of d2's verifies: the stale `${LAYOUT}` line fails its configure;
    - the missing tree;
    - the constructs fixture's verify listing.
  - "Known inside its own branch" passes on both tools. It is the guard against an over-strict fix.
  - The same splice of the final 90 checks fails 28, because the old parser rejects the fixture's `keep:` stanza and the constructs fixture then fails as a whole. That makes the 81-check figure the semantic one.
- **Old tool against new on the real tree (`c8d3eb9d`).**
  - The dry runs of B1.5, B1.6, B1.13 and B1.18 are byte-identical apart from the walk's "not reached" list. `cmake/swingstore/CMakeLists.txt` is now reached, through `pp_define_swingstore()`.
  - **Lost** in the per-line scope diff, for the reviewer:
    - `_pp_ocv_repo`: `PinPointOpenCV.cmake:37` has `return()` under `if(PP_OPENCV_NO_PIN)`.
    - `CPACK_*`: `PinPointPackaging.cmake:21`.
    - `PP_ROOT`: only after `src/Gui/CMakeLists.txt:422`'s `endif`; inside its branch it is still known.
    - `CMAKE_CURRENT_LIST_DIR` in bodies.
    - `PinPointSwingStore.cmake`'s root variables: it is now also reached from 653 suite calls.
  - **Gained:** each body line knows what its callers know.
  - No line the tool writes uses any of these.
- **Item 4.** I took a `git archive` of `c8d3eb9d` plus this tool in `$TMPDIR` and really ran B1.5, then B1.6, on it. **B1.15b was not applied.** Then each of the three drafts was dry-run with both tools and really run, one after another.
  - **Every CMake line each would write** is in `NOTES-B1.2b-cmake-lines.txt`:
    - B1.12: 4 mentions in 3 lines;
    - B1.16: 395 mentions in 334 lines;
    - B1.19: 507 mentions in 386 lines.
  - **The only difference from B1.2a** is B1.16's `cmake/swingstore/CMakeLists.txt:17-21`. Those 5 mentions were "can't place", and are now rewritten as `${PP_SWINGSTORE_ROOT}/src/Stores/…`. The value comes from `pp_define_swingstore(${PP_REPO_ROOT})` and its root call, both `''`.
  - **Lines in a function body:** 2, `CMakeLists.txt:3534` and `:3536`.
  - **CMake-scope can't-place:** 0 on every map.
  - **Verify.** `--verify --no-configure-ok` on each map: VERIFY PASSED, 0 other lines (9 renames, 69 and 55).
  - **Configure 1,** through the lock, of the stacked result (B1.5, B1.6, B1.12, B1.16 and B1.19): VERIFY PASSED; app and tests PASSED, with 0 CMake errors each; no download; 42 s (`build/wt-37c/build/b12b-configure.log`). The tools were OFF, so this run did not read `:3534` or `:3536`.
  - **Configure 2,** the same with `--cmake-arg -DPINPOINT_BUILD_TOOLS=ON --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`: VERIFY PASSED; app and tests **CONFIGURE PASSED**, 0 CMake errors; the log says "lm_repair enabled"; the cache has both options ON; no download; 43 s (`b12b-configure-tools.log`).
    - CMake fails a missing source at generate time, so this proves the source line `:3536`.
    - `:3534` is an include directory, which CMake accepts even when it does not exist, so this does not prove it. Only a build of `swinglab_run` does.
  - **Keep on B1.5's map,** counts only (I did not open the IM): `--keep governance/artefacts/information_model.md:1578,1998,2030` keeps 3 lines, each of which the move would have rewritten. Path mentions go from 517 to 514, and can't-place from 11 to 10.
  - Every scratch copy and folder in `$TMPDIR` is deleted.
- **Disclosed.** While developing I ran `--self-test` directly four times, outside ctest and the lock, including its 0.1 s cmake runs. I also ran the spliced "before" versions twice.
- **Cabin.** `tools/method/cabin_run.sh build/wt-37c --wait`, run `studio PC-wt-37c-20261125-001659`, on HEAD `b3e656cf` plus this patch: exit 12.
  - Pass 1: 327/339 passed. Pass 2: 41/43 passed.
  - **No new failure.** The 14 that fail are the same as lane 2's run `studio PC-wt-5b-20261124-234430` on `c8d3eb9d`, less `namespace_collision_test`, which `b3e656cf` fixed.
    - Pass 1: `tendencies`, `session_list`, `dtl_posture`, `shot_commands`, `migrate_step2`, `parity_json`, `session_diagnostics`, `athlete_diagnostics`, `reduction_rule_golden`, `folder_lock`, `charts_delivery_presenter`, `athlete_repository`.
    - Pass 2: `qml_ui.tst_charts_panel`, `qml_ui.tst_session_report`.
  - **`move_files_selftest` passed on Windows** (18 s). This is the only Windows run of the constructs and keep fixtures.
  - The summary is in `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261125-001659.txt`.

## Real stores
None reachable: this is a Python tool. Every real run and both configures were on a `$TMPDIR` archive copy. The seed, `build/wt-37c/build`, was only read.

## Paths changed
- `tools/refactor/move_files.py`
- `tools/refactor/README.md`
- new: `build/platform/B1/NOTES-B1.2b.md`, `NOTES-B1.2b-cmake-lines.txt`, `drafts/B1.12-lm-reads.map`, `drafts/B1.16-export.map`, `drafts/B1.19-diagnostics.map`

## EDGE
- **Still unmodelled:** `string()`, `file()`, `find_*`, `cmake_path()`, `option()`, `block()`, `while()`, `break()` and `continue()`, `return(PROPAGATE)`, and a user function that overrides a built-in. A variable one of these sets keeps its `set()` value, as before.
- **Relative paths in shared `.cmake` files.** A relative path in an `include()`d `.cmake` file that two folders reach (`PinPointQmlModule.cmake`) is still read from `cmake_base()`. Only function bodies get the per-caller check. B1.2a's EDGE stands.
- **Undecided paths.** An `include()` or `add_subdirectory()` whose path is undecided is not read.

## After the review (REVIEW-B1.2b.md, the coordinator's four fixes)
- **1. A line that can't be placed is left whole** (`whole_lines`, `move_files.py:1537`).
  - If any word on a build line is a CMake-scope "can't place", no word on that line is rewritten.
  - Each word the move would have changed is listed under "can't place" as "left with its line", with what it would have become.
  - Self-test: fixture line 36, `${UV}/engine/e.cpp src/Diagnostics/engine/e.cpp`, stays unchanged, and its second word is listed.
  - Fixture lines 26 and 30 mixed a can't-place word with a modelled one. Their modelled uses now sit on lines 37 and 38, which are rewritten.
- **2. `keep:` is for docs only** (`keep_errors`, `:180`). A keep on any file whose `file_kind` is not `doc` is an error, both in the dry run and the real run, and in `--verify`.
  - Self-test: a keep on `CMakeLists.txt` and one on a `.cpp` file are each refused in both places.
- **3. Keeps come from the map's `keep:` stanza only.** `--keep` is removed. Given anywhere it exits 2, saying to put the keep in the map (`main`, `:2925`).
  - Call: I removed `--keep` from the dry run and the real run too, not just from `--verify`. Otherwise a real run with `--keep` would fail its own `--verify`.
  - Self-test: `--keep` is refused with `--dry-run` and with `--verify`.
  - **For B1.5:** its three IM lines go in `B1.5-vocabulary.map` as `keep: governance/artefacts/information_model.md:1578,1998,2030`.
- **4. `--verify` lists the base reading's can't-place lines** (`:2020`).
  - It runs the rewriter over the base's build files and lists each such line marked `(base)`, then adds the result's own lines.
  - Each line appears once, keyed by its path after the move and its word.
  - Self-test: the two-calls line (`CMakeLists.txt:5`) is now listed. Once moved, neither of its values names a moved path, so before this fix it dropped out of the list.
  - The constructs check now expects line 15 as `(base)`.
- **Also stale keeps.** The stale-keep check now uses a doc line, `h.md:3`, through a map, since a keep on a `.cmake` file is refused.
- **Not done: the review's minor item 5** (`VERIFY PASSED (NOT CONFIGURED)` on one line). It was not in the coordinator's four. MOVES should say that `--no-configure-ok` is not for moves (coordinator).
- **Real tree:** the dry-run totals of B1.5, B1.6 and the three drafts are unchanged (0 CMake-scope can't-place on each, so fix 1 changes no line).
- **Tests:** `one_at_a_time.sh ctest … -R '^(move_files_selftest|architecture_lint_test|design_system_lint_test)$' -j 3`: **3/3 passed**. With `-V`: **96 `ok`, 0 FAIL**.
  - One direct `--self-test` run while developing, and one fixture re-run, both in `$TMPDIR`, since deleted.
  - No configure spent; no studio PC run, as told. The studio PC run above was on the 90-check version.
- **Docs:** `tools/refactor/README.md` (the keep paragraph, the "can't place" sentence and the self-test list) and the tool's docstring.
