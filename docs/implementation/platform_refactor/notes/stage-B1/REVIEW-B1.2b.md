# REVIEW · B1.2b · the move tool's six unmodelled CMake cases, NOT CONFIGURED, `keep:` (`build/wt-37c`, uncommitted on b3e656cf)

**Verdict: SIGN OFF.** All six cases from REVIEW-B1.2a are modelled, or listed and left unrewritten. A skipped configure is no longer a silent pass. On unnamed lines, `keep` hides nothing.

Two claims are wider than the code:
- A "can't place" **token** is left alone, but the rest of its **line** is still rewritten (Fix 1).
- A `keep` on a non-doc line hides that line's stale path from `--verify` (Fix 2).

Neither touches any map today: B1.5 and B1.6 have 0 CMake-scope can't-place lines, and NOTES reports 0 for B1.12, B1.16 and B1.19. Fix 1 is due before any dry run shows one.

## What I ran
- I read the new `rewrite_span`/`cmake_cant_place` (`move_files.py:1415-1482`), `rewrite_file`'s keep pass (`:1506-1527`), `verify`'s keep and final pass (`:1844-1987`), the configure wiring (`main`), and the self-test lists.
- On `$TMPDIR` copies, all deleted afterwards:
  - my own fixture, with one line for each of the six constructs plus a keep, run as dry run, real run and `--verify`;
  - `CMakeVars.info` probes;
  - old-against-new dry runs of B1.5 and B1.6 on a `git archive` of b3e656cf;
  - the 90-check self-test spliced onto c8d3eb9d's tool.
- `one_at_a_time.sh ctest --test-dir build/wt-37c/build/tests -R 'move_files|architecture_lint'`: 2/2 passed. `-V` prints 90 `ok`.

## The six cases (my fixture, not the self-test's)
| Case | Result |
|---|---|
| Function body, two calls giving `${B}` two values (root `src/Lab`, `src/sub` `src`) | can't place: "the 2 calls that reach this line give it different values". Not rewritten. ✓ |
| Set under an undecided `if(WIN32)`, used after `endif` | can't place: "set under the undecided if at :4". ✓ |
| `macro()` that changes `${A}` before use | modelled: `A` is the macro's `src/Other`, so the line is correctly not rewritten. ✓ |
| `unset(U)`, then `${U}/x.cpp` | can't place. ✓ CMake would decide this one (U is empty); refusing is the safe side. |
| `return()` on some paths, in an `include()`d file | `X` is a Doubt `{src/A, src/B}`, "ends r.cmake on some paths only". A set after a `return()` on every path is unknown. ✓ |
| A child's `set(P … PARENT_SCOPE)` on every path | modelled: the parent knows `P` after `add_subdirectory`, and the child's set line is re-pathed. ✓ |

## Checks
**NOT CONFIGURED.**
- `--verify` without `--configure` exits **3** and prints `NOT CONFIGURED: this is not a pass`. A failed line check still exits 1.
- `--no-configure-ok` gives exit 0, with its own line.
- A tree with no `CMakeLists.txt` is FAILED.
- `--cmake-arg` goes after the seed's arguments (`:2076`), so it wins.
- Minor: `VERIFY PASSED` is still printed above the NOT CONFIGURED line. Nothing stops a lane from passing `--no-configure-ok`.

**`keep` can't hide a stale path on a line it doesn't name.** I re-ran the fixture with the stale `src/Lab/y.cpp` put back on unnamed line 2 of a kept doc:
- FAILED, "the map still rewrites";
- FAILED under `--allow-prose` too;
- with a row carrying the old spelling inserted above the kept line, under `--allow-prose`: the kept line is found at its new place, and the inserted row FAILED.

A kept line must be byte-identical to its base line. A `keep` on an `#include` line still FAILED in the resolution pass ("reaches a moved file only through an include directory").

**"Can't place" is per token, not per line.** On the fixture's `add_custom_target(t1 SOURCES ${W}/x.cpp src/Lab/y.cpp)`, the real run wrote `${W}/x.cpp src/Vocabulary/Lab/y.cpp`:
- the undecided token is left alone;
- its neighbour on the same line is rewritten;
- `--verify` passes the result and lists the line.

Each token is right on its own, but the line is half-rewritten. NOTES ("list these lines … and leave them as they are") and the brief ("refuses to rewrite it") both say the line is left alone.

**The self-test fails against c8d3eb9d's tool.** I spliced the 90 checks onto c8d3eb9d with two shims, so that the fixtures parse: the old `parse_map` skips `keep:` lines, and the old `main` takes `--no-configure-ok` and `--keep` as no-ops.
- **25 of 90 fail.** Every new behaviour has at least one failing check:

  | Behaviour | Failing checks |
  |---|---|
  | function bodies | 5, with its verify mutation and d2's two verifies |
  | undecided `if` | 3, with its verify mutation |
  | macro | 1 |
  | `unset` | 1 |
  | `return` | 1 |
  | `PARENT_SCOPE` | 1 |
  | the constructs fixture's verify listing | 1 |
  | NOT CONFIGURED | 1 |
  | missing tree | 1 |
  | `--cmake-arg` | 1 |
  | keep | 7 |

- Three new checks pass on the old tool: "constructs: the move runs", "a first set is known inside its own branch" and "keep: verify fails the old spelling on a line that is not kept". These are guards against an over-strict fix, and the old final pass already did the third, so passing is expected.
- With the shims, these failures are semantic. Without them, NOTES' figure of 28 includes parse failures.

**Real tree.** On b3e656cf, old against new, the B1.5 dry run (517 mentions, 11 can't place) and the B1.6 one (140 mentions, 6 can't place) are identical apart from the header lines:
- `cmake/swingstore/CMakeLists.txt` is now reached (through `pp_define_swingstore()`);
- there is a new "lines kept: 0" section.

Both have 0 CMake-scope can't-place lines.

## Fixes (not blocking; Fix 1 before any dry run that reports a `CMake scope:` can't-place line)
1. **Leave a can't-place line whole.** If any token on a build line is a CMake-scope can't-place, return the line unchanged (`_rewrite_file`), and list its other tokens as "left with it". Otherwise, correct NOTES, README and the docstring to say per token.
2. **Limit `keep` to docs.** Refuse a `keep` on any file whose `file_kind` is not `doc`, in both the dry run and `--verify`. Today a kept build, C++ or tool line keeps its stale path and passes `--verify`:
   - a source path is caught later by `--configure`;
   - an include directory or a code string is not caught at all.
3. **Keeps for `--verify` come from the committed map.** A `--keep` given only to `--verify` passes any line that is still equal to its base. At least list command-line keeps separately from the map's `keep:` lines.
4. **`--verify`'s can't-place list should include the base reading's.** Today it is recomputed on the moved tree, where a line can drop out. My two-call function-body line was listed by the dry run but not by `--verify`, because after the move neither value names a moved path.
5. **Minor:** print `VERIFY PASSED (NOT CONFIGURED)` on one line when exit 3. Say in MOVES that `--no-configure-ok` is not for moves.
