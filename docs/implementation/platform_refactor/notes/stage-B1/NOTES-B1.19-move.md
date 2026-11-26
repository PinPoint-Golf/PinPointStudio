# B1.19 step 6 · Diagnostics' code moves into four folders by layer (26 Nov 2026, wt-37c at f271b823)

**State: DONE on the Mac, uncommitted** (226 paths: 54 renames, 170 modified, 2 new). Ruling: R8's file list, option
(B), `model` → `content` → `derived` → `engine` (`DECISION-R8-files.md`, the Solution Architect, 26 Nov 20:15).
Builds: 2 of 2, plus the lint test rebuilt alone (call 3). No studio PC run, no commit, no allowlist line, no app launch. Lane 2 was released at 21:31 by
`B1.19-verify.txt`.

## What changed, with counts
- **Order check before the move** (step 2; `B1.19-step6-order-check-before.txt`): the parked check, with the ruled
  order and the option B map, on f271b823: **0 upward includes, 0 folder cycles**, exit 0. Every file was placed: 120
  files (model 20, content 26, derived 56, engine 18). The tree as it was had 1 cycle over all five folders (16 edges).
- **The move** (`tools/refactor/maps/B1.19-diagnostics.map`, run from a scratch copy as C-β did, then copied in): 54
  files moved; 315 include lines rewritten in 148 files (rel 286, src 23, base 6); 554 path mentions; can't place 1
  (`B1.19-step6-dryrun.txt`, `B1.19-step6-run-real.txt`). The folders `packs`, `norms` and `measures` are gone.
- **The map's header** now gives the ruled order, cites the ruling and names the seven re-placements; its file lines
  are the option B map's, byte for byte (the trap).
- **`--verify HEAD --configure`** (tools and lm_repair on, seed `build/wt-37c/build`): renames 54, 217 files, listed 2
  (the map; `gate_catalogue_rules.h:137` under `--allow-code-path`), **other lines 0, VERIFY PASSED, CONFIGURE PASSED**
  (both trees; "lm_repair enabled"). Kept as `B1.19-verify.txt`. The `gate_catalogue_rules.h:137` edit (the rule's
  producer path, `…/measures/live_measure_source.cpp` → `…/engine/…`, which `gate_catalogue_test` opens) was made
  before this verify and passed inside it.
- **Unresolved includes: 0.** The order check exits 2 on any include it can't resolve, and it passed; `--verify`
  passed; both trees built.
- **Includers** (`count_includers.py`, 40 stems + `Diagnostics/`, `B1.19-step6-includers-{before,after}.txt`): files and
  includes identical stem by stem at the new paths; `Diagnostics/` 284 files, 760 includes before and after. Only the
  spelling moves (relative 552 → 545, bare 163 → 170: includes now inside one folder).
- **Global include paths under `src/Diagnostics`: 4 → 0** (`CMakeLists.txt:936`, the one line deleted). No bare
  include relied on them: a scan of src, tools and tests for any quoted include that resolves only in a Diagnostics
  folder found 0, no Diagnostics header has a Q_OBJECT, Q_GADGET or QML_ macro, and no generated source in
  `build/app` includes one by bare name. The app build is the proof (below). None added.
- **Hand edits after the verify:**
  - `architecture_lint_test.cpp:531-541`, `isRulesIoDir()`: its keys `Diagnostics/norms/` and `measures/` matched
    nothing after the move, and the tool could not re-key them, since each splits across three folders. They are
    re-keyed to the old keys' image: `model/`, `derived/` and `engine/`, plus `content/`'s eight stems from `engine/`
    and `norms/`, by name. The pack loaders stay outside it, as they were. Call 3 has the detail.
  - Comments naming the old folders: `delivery_grid_numbers.h:30`, `rule_data_reads.cpp:20`, `rule_data_store.h:38`.
- **The check** `tools/refactor/dir_order_check.py` is restored from `parked-B1.19/`. Its docstring now gives the
  ruled order and the ruling, and it gains `--self-test`, six fixture cases: a clean tree passes; a planted upward
  include is red (and makes a cycle); with it removed the tree is green; an unlisted folder is exit 2; an unresolved
  include is exit 2; tests are skipped.
  - ctest entry `diagnostics_dir_order_check` (`src/Gui/tests/CMakeLists.txt:741-749`): `--self-test --order
    model,content,derived,engine --cycles`, no `--map`, so it also runs on Windows. Labelled
    `pp_requirement(diagnostics_dir_order_check CTL-BT-40)`. It and lint A9 still skip tests.
- **Lint tables and the allowlist:** the drill pack's A9 entry (`:378`) and its fixture (`:1139`) are re-keyed to
  `Diagnostics/content/drill_pack`; so is `context_check.py:84`'s `TRUE_FILES` entry. The allowlist has 164 lines
  before and after, 6 of them keyed `Diagnostics/`, with one re-keyed (`model/measure_sample.cpp`, A7).

## Runs and logs (all through the lock, -j 6, each under `timeout`)
- Build 1, the tests tree, `-k 0`: exit 0, 0 errors, about 29 min (`B1.19-step6-build-tests.log`).
- Build 2, the app tree with `PINPOINT_BUILD_TOOLS=ON` and `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled";
  `B1.19-step6-configure-tools-on.log`), `-k 0`: exit 0, 0 errors, 1274 steps (`B1.19-step6-build-app.log`).
  `PinPointStudio`, `swinglab_run` and `lm_repair` all linked with the four include paths gone. The tree was then
  set back to tools and lm_repair off, by a configure only (`B1.19-step6-configure-tools-off.log`; CMakeCache OFF).
- **ctest** (`-LE qml_ui`): every suite in `src/Diagnostics/tests` (40), the brief's regex, both lints,
  `diagnostics_dir_order_check`, `move_files_selftest`, `rtm_check`/`rtm_selftest`, `im_excerpt_selftest`,
  `im_changelog_merge_selftest`, `artefact_paths_check`, `gate_catalogue_test` and `store_isolation_lint_test`:
  **58/58 passed** (`B1.19-step6-ctest.log`). `ctest -N` 436 → 437 (the new check).
- **Red (BT-40), `B1.19-step6-plants.log`:**
  - Planted: `model/zz_b119_p1.h` including `../engine/fire_findings.h`, and a `QFile` line in a new file in each of
    `model/` and `content/`. `diagnostics_dir_order_check` went **red**: 1 upward include (model → engine) and 1 cycle.
    `architecture_lint_test` went **red**: A5 "file I/O in a rules directory" on both plants, which shows the re-keyed
    `isRulesIoDir()` reads the new folders.
  - Removed: both **green**, and no plant is left.
- **context_check.py:** HEAD's report (from a `git archive` copy) and this tree's are the same in every summary line.
  The drill pack's 13 crossings still read "Coaching → Diagnostics" on the true tags, so its re-keyed `TRUE_FILES`
  entry is live. `--list c` exits 0.
- **Lint A9's drill-pack entry:** a plant can't tell a stale key from a live one here. The entry gives the same context
  as its directory (Diagnostics), as its comment says ("named, though its directory says the same"). Checked by eye
  instead.
- **Parity, Mac half:** `parity_baseline.py b119-diagnostics` exit 0 (8 sessions; `b119-parity.log`);
  `parity_diff.py b1-base-2 b119-diagnostics`: **0 differences** (`b119-parity-diff.log`). Run kept at
  `the share
- **Re-proof of the narrowed rule** (`B1.19-step6-plants2.log`; `architecture_lint_test` rebuilt alone,
  `B1.19-step6-build-lint.log`):
  - A `QFile` line in `content/norm_pack.cpp` (from `norms/`) and in a new `model/` file: "file I/O in a rules
    directory".
  - One in a new `content/` file: "file access outside the repositories".
  - Then removed (`norm_pack.cpp` restored byte for byte), and both lints and the check are green.
- **Final line check over the whole diff** (`B1.19-step6-verify-final-lines.txt`, no configure): its 16 "other lines"
  (with `--allow-prose` on the IM, the guide, the method README, the RTM and the placements page): 18 "other lines",
  exactly the hand edits after the verify, by design:
  - the three comments;
  - the nine-line ctest entry;
  - six lines of `isRulesIoDir()`.
  The include-path deletion is listed as allowed. The move-only pass is `B1.19-verify.txt`.

## Docs (GIT-6)
- **IM 1.12.32:**
  - the version line;
  - §2 "The packs": a sentence giving the four folders, their order and the check, plus the screen and drill paths
    (the tool's);
  - CLS-PACKS-NORMS: which folders hold the packs' and norms' code;
  - stale bare folder names: §5.3 OBJ-PHASE-GRID (`Diagnostics/measures` → `model`) and §11.1's "Rules without file
    I/O" (`Diagnostics/norms` → `content`);
  - the tool's rewrites: FILE-SHOT-07 and §11.1's two rows;
  - one change-log row, written by hand (`im_changelog_merge.py` only resolves rebase clashes, so the row may
    renumber on rebase), and the footnote `[^sa26-r8]` (to the ruling).

  RING-RULES names only `Diagnostics` and `derived`, so it is unchanged. The changelog row 1.10.8 keeps its old paths
  (a dated record). The walkthrough deck names no moved folder (only `Diagnostics/derived`), so it needs no refresh.
- **R8's row** in `docs/implementation/platform_b1_placements.md`: the first ruling's order is marked superseded; a new
  "Ruled, the file list" bullet gives the order, the seven re-placements, the build and the check, with the footnote
  `[^r8-list]`.
- **The developer guide** `diagnostics_developer_guide.md`: the folder table (§14) is rewritten for the four folders and
  the rule; the Location line and two `measures/live_measure_source.cpp` mentions now read `engine/…`.
- **The tools' READMEs** (`tools/refactor/README.md`, `tools/method/README.md`) now cover `dir_order_check.py`. The
  tool rewrote the rest: 6 design and developer docs, and `requirements.md:149,198`.
- **Traceability:** the Mac gate list was retaken (437 tests) and `rtm.md` regenerated. `rtm_check` ok; the baseline
  stays at 961 lines, 0 new gaps. No test was renamed, so no `--rename`.

## Calls (each logged for review; undo in brackets)
1. **The map ran from a scratch copy** and was then copied into `tools/refactor/maps/`, because the tool refuses a
   dirty tree (C-β's call 1). `dir_order_check.py` went in after the verify too. (Undo: none needed.)
2. **`gate_catalogue_rules.h:137` re-pathed** under `--allow-code-path`. The brief doesn't name this edit; phase 1's
   dry run and B1.6 did. Left stale, it would name a missing file. (Undo: revert the line.)
3. **`isRulesIoDir()` re-keyed to the old keys' image**, not widened, since a change to a lint's scope is an
   information-model change the brief doesn't approve. The function picks only A5's message, not whether A5 fires.
   - `swing_themes_pack.h` now reads as a rules file through `derived/`. That follows from its ruled placement.
   - This took a third, one-target build (the lint test alone), over the two-build budget.
   - Proposal for the Solution Architect: key `Diagnostics/content/` whole, so the pack loaders get the rules
     message too. (Undo: the old two keys.)
4. **The self-test lives inside the check's own ctest entry** (one label; the baseline doesn't grow), as
   `architecture_lint_test` runs its fixtures first. (Undo: split it out with its own label.)
5. **Build 2 ran with the tools on**, as B1.16 did, so `swinglab_run` and `lm_repair` compile over the moved paths.
   The app tree is set back to tools off afterwards with a configure only.
6. **Three comments that named the old folders, fixed by hand**, though `--verify` lists them: they were stale paths
   in the comments, not words.
7. **IM §5.3's OBJ-PHASE-GRID row** (`Diagnostics/measures` → `model`), outside the rows the brief names: a stale path
   the ruling's "the IM's paths follow it" covers (AG-4). (Undo: revert the cell.)

**Real stores:** none in reach. Paths and includes only. Parity ran on share copies and deleted them. No app launch,
no settings, keychain or library touched.

## EDGE (for an issue)
1. The tool doesn't scan `docs/research/` or `docs/implementation/` (dated): `docs/research/context_map/*.txt`,
   `research/data/gates/census_20261016.md:177` and `research/data/plane_stack/extract.py:8` still give the old
   paths.
2. `src/Gui/tests/CMakeLists.txt:94` (lm_session_model_test's `${SRC}/Diagnostics`) and `src/Gui/CMakeLists.txt:155,373`
   (qml_ui_test's) still name `src/Diagnostics` itself, not a sub-folder. Whether anything still needs them was not
   checked; they are out of the brief's four.
3. `governance/artefacts/information_model.md:397` and `architecture_lint_test.cpp:31` say "packs/norms" in words, in
   a ring diagram.
4. For the coordinator: the studio PC pass is `diagnostics_dir_order_check`'s first Windows run. The WIP patch takes the
   untracked `tools/` files (`make-wip-patch.sh` adds them intent-to-add). There is only a macOS gate list.
