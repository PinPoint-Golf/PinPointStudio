# Stage B, Part 1: the standing block for every scripted move

Read after the standing block (#402). It binds B1.5, B1.6, B1.9, B1.12, B1.15b, B1.16, B1.19 and B1.20, and any other item that moves or renames files with `tools/refactor/move_files.py` (built in B1.2).

**One lane.** While a move runs, no other lane has code open under `src`, `tests`, `tools` or `cmake`. First `git merge --ff-only origin/main`, and check `git status` is clean in BOTH worktrees. The other lane restarts from the new main after your commit.

**The map is the item.** Write the move as a map file, `tools/refactor/maps/<item>.map` (one `old path → new path` per line, directories allowed, listed from the tree at run time, never from a count in a brief). The tool is run with that map; nothing is moved by hand. The map and any tool change are committed with the move, so another lane can re-run it over its own work.

**What the tool rewrites** (check each in its `--dry-run` report before the real run):
1. `git mv` of each file; a directory's `tests/` folder goes with it.
2. Every `#include "…"` of a moved header, in `src`, `tests` and `tools`: a path with a directory keeps its style; a bare name or a `../` path becomes the path from the including file to the new home.
3. Every outward include INSIDE a moved file (its depth changed).
4. Bare includes that resolved only through a global include directory (`src/Core`, `src/Metrics`, `src/Coaching`, `src/LaunchMonitor`, … in the root `CMakeLists.txt:852`) are rewritten to relative paths. **Never add an include directory** to make one resolve; remove a global directory once nothing needs it, and say which.
5. Path mentions in `CMakeLists.txt` files, `cmake/*.cmake` and `tests/cmake/`, `tests/CMakeLists.txt`'s suite list, a test's or a tool's own include directories (`INCLUDE ${SRC}/Metrics`, `target_include_directories`), `tools/`, `packaging/`, `.github/`, and comments.
6. `tools/refactor/context_check.py`'s `DIRS` and `FILES` tables (the contexts; B1.2), re-keyed like the lint's.
7. The lint's path-keyed tables in `src/Gui/tests/architecture_lint_test.cpp`: `dirRings()`, `fileRings()`, `countedStoreDoor()`, `isRepositoryHeader()`, A8's settings and studio-profile names (about line 439), A1's `Recording/` rule (about line 684), and the fixtures. **The tool re-keys an entry to its new path; deleting an entry that a directory rule now covers, or changing a ring, is a hand edit your brief lists, and `--verify` allows it only on a table line of that file.** **A stale key passes without checking anything**, so for each table you touch, plant one violation in a scratch file, see the lint fail on it, and remove it. Say so in NOTES.
8. `src/Gui/tests/architecture_allowlist.txt`: a moved file's lines are re-keyed to its new path. The number of lines and each count stay the same; a line is never added.
9. Docs: `docs/developer/` and `docs/design/` path mentions (not dated snapshots under `docs/implementation/`, PR-19), and the IM rows your brief names.

**Proof (the self-check tier rests on this).**
- `move_files.py --verify <map>` against the commit before: the diff holds only renames (100% similarity, or the listed files with include lines changed), include lines, and path strings in build files, lint tables, the allowlist and docs. Any other changed line is listed and fails. Put its summary in NOTES.
- `python3 docs/implementation/platform_refactor/pending-tools/count_includers.py --root <your worktree> <the moved set>` before and after: the includer count is the same, at the new paths; unresolved includes: 0.
- A clean configure, then the app and the test tree build on the Mac (budget in your brief; a tree-wide move rebuilds most of both).
- Both lints, `im_excerpt_selftest`, and the suites your brief names, on the Mac.
- The full suite on the studio PC: `tools/method/cabin_run.sh`. **Accept: no new failure against Stage A's close** (the coordinator's record of it; failing that, the last studio PC run on main, which your NOTES name), not "all pass": the studio PC has standing failures with issues filed.
- Parity (`tools/parity/`, the Mac half, against the baseline label `b1-base-2`, taken on main at 1918a017 after B1.5, which replaces B1.3's `b1-base` from B1.6 on): 0 differences. It shows the diagnostics and athlete levels only; `--verify` is what shows the analysis stages' code is untouched.
- A release build is not needed per item; the stage's close makes one.

**Not in a move:** no namespace, type, function or behaviour change; no fix to anything the move exposes (log it as a finding: a defect found by an item is a new item). If a file can't move without a code change (an include cycle, a missing header), stop, leave it out of the map, and log it.

**IM.** A move changes the ring or context of a directory or file only where your brief says a ruling covers it (C9). Edit only the IM rows the brief names, opened by grep at those rows; add one changelog row with `tools/method/im_changelog_merge.py`; say whether the walkthrough deck shows what changed.

## Added 24 Nov 2026 (B1.2a's review): every move configures before it is called done
Every scripted move runs `--verify --configure <scratch dir> --seed <the lane's build dir>` after the real run, so both trees configure with 0 CMake errors. A `--verify` without `--configure` is not a pass. Since B1.2b, the tool reads function and macro bodies where they're called, and lists what it can't decide (a set under an undecided if, unset(), return(), PARENT_SCOPE) as "can't place", leaving it untouched. `--verify` without `--configure` prints NOT CONFIGURED and exits 3: it is never a pass. `keep:` (in the map) or `--keep <file>:<line>` leaves named lines exactly as written, such as dated records in the IM. `--cmake-arg -D…` overrides the seed (use it to configure with `PINPOINT_BUILD_TOOLS=ON` and `PINPOINT_BUILD_LM_REPAIR=ON`).

`--no-configure-ok` is for the tool's own self-test only, never for a move.

## Added 26 Nov 2026 (after B1.7): a file split by hand goes in the map
If your brief splits a header out of another by hand (B1.7 cut `capture_time_types.h` out of `swing_analysis.h`), run the map first, then make the split, and declare it in the map before `--verify`:
`hand: <new file> <- <source file>:<lines>[,<lines>…]`, for example
`hand: src/Vocabulary/Recording/capture_time_types.h <- src/Vocabulary/Analysis/swing_analysis.h:116-158,514-550,625-649`.
Line numbers are the source's before the move (`git diff -U0 <base> -- <source>` gives them). Each range is one block, cut whole and unedited, with its comments.
`--verify` then lists and accepts exactly the split:
- each block in the new file, byte for byte, in order, in the same namespace;
- the new file's other lines, which may only be blank lines, comments, `#pragma once`, includes or namespace lines (no `#define`, no `#if`, no include guard, no line ending in a backslash), with each block read as code exactly as in its source;
- the source's one added include of the new file;
- an includer of the source re-pointed at the new file;
- a build list's line naming it.

Anything else fails, and a `hand:` line that matches nothing is refused. **Don't put the split's headers under `--allow-prose`** any more: that waives their every line, including the deleted ones the `hand:` check proves. The map, with its `hand:` lines, is committed with the move as always.
