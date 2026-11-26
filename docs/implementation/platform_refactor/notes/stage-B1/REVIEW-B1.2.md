# REVIEW · B1.2 · the move tool (`build/wt-5b`, uncommitted on 48387973)

**Verdict: BLOCKING.** The include rewriting is right and matches lint A1. The dry-run figures reproduce, and so do the self-test, idempotence and the refusals. Three things are wrong with the proof that every self-checked move will rest on. I reproduced each one. Each fix is small. Fix 1 is the one that breaks B1.5.

## What I ran
- Dry runs of both maps on `build/wt-5b`, plus `--self-test`. Then `one_at_a_time.sh ctest … -R 'move_files|architecture_lint|im_excerpt'`: 3/3 passed. A content hash of every tracked and untracked file, plus `git status`, was the same before and after: **the dry runs wrote nothing.**
- **Disclosed:** on two throwaway `git archive` copies under the worktree's ignored `build/` (HEAD + this patch, and `bdd04541` + the tool), I made scratch commits and real runs. Both copies are deleted. I made no real run and no commit on `build/wt-5b`.

## Checks
1. **Include resolution: matches A1.** It tries the file's own folder, then `src/`, then `src/Gui/`, then a unique basename (`move_files.py:243` against `architecture_lint_test.cpp:570`). An ambiguous name is an error, raised only when a moved name is involved (`:647`). The tool's basename table also holds `tests/`, `.c` and `.m` files, so it is stricter than the lint, never looser.
   - The only basename clashes that move are `box_stats.h` (also in `Gui/typeset`) and `variability_measures.h` (also in `Gui/variability`). No includer relies on either through an include directory, and the dry runs report 0 errors.
   - Things I checked and found absent:
     - `__has_include`, a macro used as an include target, `#include_next`;
     - a `<…>` include naming a moved basename;
     - `.qrc` or `.in` files under `src`;
     - a QML-registered header among the moved files (the generated `*_qmltyperegistrations.cpp` names none).
   - `#if` branches and `.mm`/`#import` lines are rewritten like any other.
   - What it misses or skips: Fix 1 below, and the non-blocking notes N1–N3.
2. **`--verify` proves less than it claims.** These parts hold:
   - every include in `src`, `tests` and `tools` resolves to the map's image, never through an include directory;
   - every moved file is at its new path;
   - an added or deleted line fails, and so do a CRLF change, an allowlist count change and a code-string change.

   It does not catch a stale line that was left unchanged, or a changed binary file (Fixes 2 and 3). It shares `Plan`, `Tree.resolve` and `CMakeVars` with the rewriter, so it is a second reading of the diff, not an independent resolver: a resolver bug would pass both. The configure, build and suites stay the independent proof.
3. **The real run is safe.**
   - It uses `git mv`; an untracked file is moved with `os.rename`, and the move is printed.
   - It refuses on any error, and on a dirty tree without `--over-work`, before writing anything.
   - It is idempotent: B1.5 on the scratch copy, then a second run, gave 0 files moved and 0 files rewritten; `--verify` then PASSED (57 renames, 388 files).
   - It adds no allowlist line. It re-paths include directories but never adds a word.
   - EDGE: it is not atomic. The rewrites are written before the `git mv`s, so a `git mv` failure halfway leaves a half-moved tree. That is recoverable on a clean tree; under `--over-work` it is untidy, but no content is lost.
4. **The "pre-added context tags" are only `tools/refactor/context_check.py:52-54`.** They are three `DIRS` keys: `Vocabulary/Club/` and `Vocabulary/Common/` as Shared, `Vocabulary/LaunchMonitor/` as Acquisition. Nothing in the lint, the allowlist or `dirRings`/`fileRings` changed. The values match IM §4.1's RING-VOCABULARY Context cell, and they match no file today: `--list c` and `--list all --matrix` are byte-identical to the original on main.
   - They are still a context mapping made ahead of B1.5, and they are why B1.5's dry run says "contexts that change: 0" rather than three NO TAG rows.
   - **Move them to B1.5** (Fix 4). The reason NOTES C2 gives for putting them here is that `--verify` fails any hand edit of `context_check.py`. That is the real defect, and every later item that creates a folder will hit it.
5. **Self-test and maps.**
   - The 44 checks cover:
     - the bare, directory-qualified and `../` spellings, `#import`, an outward include;
     - a global and a test-own include directory;
     - an ambiguous basename, idempotence, and the error and dirty-tree refusals;
     - 13 verify mutations.
   - Not covered (N4): the `src/Gui/` step (B1.5 has 4 such includes; I read them and they are right), the `<…>` refusal, a changed binary, and a stale unchanged line.
   - The two maps match the B1.5 table and the B1.6 brief exactly. They expand to 57 files (12 named + 45 in the three folders, 4 of them tests) and to 36.
6. **The counts reproduce.**
   - Today: Vocabulary 279/402 (78 bare, 19 directory-qualified, 305 relative); Coaching 113/243. Both equal `count_includers.py`.
   - On `bdd04541`: 279/402 and 111/238.
   - 0 errors and 0 unresolved.
   - `context_check.py --list c` on `bdd04541`: byte-identical to the original, with 9 `c` rows.

## Fixes (blocking)
1. **`tools/theme/make_design_system.py:40` is neither rewritten nor listed.**
   - The line is `os.path.join(ROOT, "src", "Metrics", "variability", "variability_constants.h")`.
   - The tool rewrites the same path in that file's message at `:42` and its usage text at `:346`, but not the `open()`.
   - After B1.5's run on the scratch copy, `make_design_system.py --check` raises FileNotFoundError, so `design_system_tokens_check` fails. `--verify` PASSED.
   - Fix part (a), in this item: make line 40 one anchored string, `os.path.join(ROOT, "src/Metrics/variability/variability_constants.h")`. Behaviour is the same, and B1.5's run then rewrites it. A hand edit in B1.5 can't do this, because adding a join component changes the word count and `--verify` fails it.
   - Fix part (b): report split paths as "can't place". Do this in `.py`, `.sh` and build files, for adjacent quoted words that join with `/` into an old path.
   - Fix part (c): put a line in B1.5's brief to regenerate `governance/artefacts/design_system/tokens.json`. Its line 1727 goes stale once `:346` is rewritten, and `docs/design/` is scanned for `.md`/`.txt` only. That is a path in a usage string, not a token change.
2. **A changed binary file passes `--verify`.** I appended one byte to `src/Gui/resources/lm/ball/ball_side_dark.png`: VERIFY PASSED.
   - Cause: at `move_files.py:1081-1085`, both texts are `None`, so `ot == nt` holds and the check moves on.
   - Fix: compare the raw bytes when either side is binary. Compare the file mode too: a mode-only change also passes today.
3. **A stale spelling on an unchanged line passes `--verify`.** On the scratch B1.5 result I put three lines back to their base spelling:
   - `src/Analysis/tests/CMakeLists.txt:636` `${SRC}/Identity/tests/…`, which passed;
   - the `Vocabulary/Identity/` `dirRings` key, which passed;
   - an allowlist key, which was caught only because the allowlist had other changes.

   A dry run of the same map on that tree finds all three. Fix: end `--verify` with the rewriter's pass over the working tree, using the same map.
   - Any file still to move, include to rewrite or mention to rewrite fails.
   - A pending re-key on a lint table line is listed instead, for MOVES 7's hand edits (B1.6 restores `Coaching/`, below).

   Add a self-test mutation for it.
4. **Context tags: move `context_check.py:52-54` out of this item.** Let `--verify` treat `context_check.py`'s `DIRS`/`FILES`/`L2_GAP` lines as it treats the lint's table lines: a hand edit there is listed, not failed. Today `:1087` sends `CONTEXT` to `check_pair`. B1.5 then adds the three tags in its own commit, beside the IM row they mirror.

## Notes (not blocking; for the coordinator and the next briefs)
- **N1 · B1.6 must hand-edit `src/Metrics/gate_catalogue_rules.h:38-42`. These are not provenance strings,** as NOTES suggests.
  - I read this; I did not run it. `gate_catalogue_test.cpp:46-48,87-88` opens `PP_SRC_ROOT/<producer>`, and after the move it prints "cannot read".
  - That edit fails `--verify` (self-test: "a path respelled in C++ code"). The only way past it is `--allow-prose <file>`, which waives the whole file.
  - Add a narrower allowance before B1.6, either here or with B1.6's tool change: a string path changed to exactly the map's image is listed, only for files the brief names.
- **N2 · Files outside the scan are silent.** These are not reported as "can't place" (suggest a report-only scan of `src/**/*.json` and `src/**/*.md`):
  - `src/Resources/diagnostics/gate_words.json:10` and `:22`, which name `Metrics/gate_catalogue.h` and `src/Coaching/report_gate_words.h`;
  - `src/Analysis/tests/data/swing_themes/README.md:3`, which is stale after B1.6.

  `docs/reference/` and `docs/validation/` are outside MOVES 9 by design.
- **N3 · B1.6's brief:**
  - The tool re-keys `dirRings`' `Coaching/` (lint `:178`) and `context_check.py:42`/`:117` to `Diagnostics/derived/`. The brief rules that `Coaching/` stays as Rules, so revert `:178` by hand. The context for an empty `Coaching/` is the Solution Architect's call.
  - The tool re-paths root `CMakeLists.txt:859` and `:3535` to `src/Diagnostics/derived`, which the brief says is not added. Delete both by hand.
  - Lint fixture `:903` (`"../../Coaching/swing_themes.h"`) is resolved relative to the lint file, not to the fixture's `Gui/x/y.cpp`. It comes out right only because the depths match.
- **N4 · Gaps in the tool and the self-test:**
  - The ring report reads only `dirRings`/`fileRings`. A file that leaves an `a5Exempt` prefix (`frame_decode` leaves `Export/`) is not reported. B1.5's grep, its check 4, covers this one.
  - `<…>` detection matches exact paths only, not basenames.
  - `--verify` merely lists a change to `move_files.py` itself, so a lane could weaken the verifier in its own move commit. I suggest it fails unless the tool is unchanged since the base.
- **N5 · NOTES-B1.2 corrections:**
  - The Vocabulary mention breakdown ("build 303 … comment 60") sums to 483, not 516. Today's run gives build 323, lint 11, allowlist 3, context 9, tool 9, doc 89, comment 72.
  - The `gate_catalogue_rules.h` line (N1) and the "rehearsal PASSED" claim need correcting: the rehearsal passed with `make_design_system.py` broken.

After Fixes 1–4: re-run `--self-test` (with new mutations for a binary change, a stale unchanged line and a context-table hand edit), both dry runs (expect "NO TAG" ×3 for B1.5 once the tags leave), and the three ctests. A scratch rehearsal of B1.5 should then pass `make_design_system.py --check` after the regeneration.

## Round 2 · SIGN OFF

**Verdict: SIGN OFF.** All four fixes are in and work. One sentence is missing from B1.5's brief (below); it is the coordinator's edit, not the tool's.

**How I checked.**
- I read the new code:
  - `split_paths` (`move_files.py:884-911`);
  - the byte and mode checks and the final rewriter pass in `verify` (`:1119-1131`, `:1204-1224`);
  - `context_table_lines` (`:1396`);
  - `--allow-code-path` (`:1278-1326`, `:1705`).
- Real runs only on a `git archive` copy of HEAD + this patch in `$TMPDIR`; it is deleted. `build/wt-5b` is untouched.
- `one_at_a_time.sh ctest … -R 'move_files|architecture_lint|design_system'`: 4/4 passed (`architecture_lint_test`, `design_system_lint_test`, `design_system_tokens_check`, `move_files_selftest`). The self-test prints 52 checks, not the 54 in NOTES, and none fails.

**The fixes, each checked on the scratch copy.**
1. **The generator path is fixed.** `make_design_system.py:40` is now one string, and `--check` passes before the move. B1.5's dry run lists `:40`, `:42` and `:346` as tool mentions, with 0 split paths. After the run the three lines are re-pathed together.
   - A split `os.path.join` is now reported as "can't place" (self-test `tools/y.py:3`).
2. **`--verify` now catches all three cases I planted:**
   - a byte added to `ball_side_dark.png`: "a binary file changed";
   - a mode-only change: "the file mode changed";
   - `src/Analysis/tests/CMakeLists.txt:636` put back to `${SRC}/Identity/…`: "the map still rewrites".

   It fails on each, and PASSED again once they were undone.
3. **The context tags have moved to B1.5.**
   - `context_check.py` holds none of the three; B1.5's dry run shows NO TAG ×3.
   - I made B1.5's hand edit (`BRIEF-B1.5.md:36`). `--verify` lists it ("a context table line added") and passes, and `context_check.py --list c` exits 0.
4. **`--allow-code-path` is narrow.** On B1.6 run over B1.5, I re-pathed `gate_catalogue_rules.h:38,40-42` to `src/Diagnostics/derived/…`:
   - With no option, `--verify` FAILED.
   - With `:38-42`, those four lines are listed and it PASSED.
   - Naming the wrong lines (`:43-44`) FAILED.
   - A retarget inside the allowance (`work_ons.h` → `tendencies.h`) FAILED: "not the map's image".
   - A change on unnamed line 43 FAILED.

**tokens.json: only a recorded source path changes; no token value does.** After B1.5's run, `--check` says stale. I regenerated it. The generator writes only `tokens.json` (`:360`). The changed `governance/artefacts/design_system/*.md` lines come from the move's own doc pass, not from the regeneration. The regeneration changes exactly one line, `tokens.json:1727`, the `variabilityHysteresis` token's `usage` text:
```
-    "usage": "… Equals kHysteresis in src/Metrics/variability/variability_constants.h (the generator checks); …"
+    "usage": "… Equals kHysteresis in src/Vocabulary/Metrics/variability/variability_constants.h (the generator checks); …"
```
A walk of every key in the old and new JSON finds one difference, `/behaviour/tokens[0]/usage`. The token's `value` stays `0.05`, and no other field changes. So this is not a design-system change.

**One brief edit for the coordinator.** `BRIEF-B1.5.md:37` says to regenerate, and why, but not that this is only a path. Add: *"The regeneration changes one line, `tokens.json:1727`, the `variabilityHysteresis` token's `usage` text (the source path). No token's value changes, so this is not a design-system change and needs no Design Lead approval. If the regenerated diff shows any other line, stop: that is a design-system change."*

**Still open from round 1, not blocking:**
- N2: `src/**/*.json` and `src/**/*.md` are not scanned.
- N3: B1.6's `dirRings` `Coaching/` revert and the `src/Diagnostics/derived` include-directory removal.
- N4: a change to `move_files.py` itself is only listed.
- NOTES says 54 self-test checks; it prints 52.
