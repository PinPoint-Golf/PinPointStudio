# B1.5 · the Vocabulary move, real run (25 Nov 2026)

Lane `build/wt-37c`, reset to origin/main `115cc58b` (B1.2c's tool), then fast-forwarded to `10473584` (one governance
line, `governance/controls/plan.md`, no moved path or includer) so the studio PC runner's base check passes. Nothing
committed; the moved tree is uncommitted in wt-37c. No compile on the Mac (the Product Owner is rendering).

## 1. The move

- Prepared map copied to `tools/refactor/maps/B1.5-vocabulary.map` (+4 lines: the keep stanza).
- Dry run with the committed tool on `115cc58b` (`B1.5-dryrun-real-115cc58b.txt`) against `B1.5-dryrun-prepared.txt`:
  identical but for the header's map path. TOTALS: move 57 files; 370 include lines in 261 files; 517 path mentions;
  58 include dirs re-pathed; 12 can't place; 5 kept; 0 errors.
- Real run: the tool refused the dirty tree (the map copy), so `--over-work` (`B1.5-run-real.txt`): same TOTALS,
  "57 files moved, 352 files rewritten". Its report equals the dry run line for line.
- Against the rehearsal's diff (`B1.5-dryrun.diff`, tool only): `git diff -M HEAD` minus the map file is **identical**
  (index lines aside): 391 files, 57 renames, 869+/869-.
- TU-header snapshot taken before the run: `B1.5-tu-before.json` (5,703 units, app 422 + tests 5,281; from the lane's
  trees as built 16:29/16:36, assumed to be B1.4's content before 40eec9eb (not checked), the tool-only commit 115cc58b changes no unit).
  The compare half waits for the Mac build after the render.

## 2. Hand edits (the brief's "What", the additions and the prep notes)

- README src/ table: `B1.5-readme-table.diff` applied as is.
- Lint (`architecture_lint_test.cpp`): `dirRings()` loses `Vocabulary/Metrics/`, `Vocabulary/Recording/`,
  `Vocabulary/Identity/` (tool-re-keyed; `Vocabulary/` covers them) → Vocabulary directory rules 5 → 2
  (`Vocabulary/`, `Core/`). `fileRings()` loses frame_decode, launch_monitor_reading, lm_device and their comment
  (LaunchMonitor's comment reworded for the rules left) → per-file Vocabulary entries 10 → 7 (Buffer's seven).
  A1's rule re-keyed to `Vocabulary/Recording/` by the tool. Fixture `:745` (was `:752`) → `"../Vocabulary/Metrics/metric_catalogue.h"`.
  **Not done: the planted A1 violation** (needs a lint compile): after the render.
- Allowlist: 3 lines re-keyed by the tool, none added or removed: `Vocabulary/Metrics/gate_catalogue_body.h` A7,
  `Vocabulary/Metrics/gate_catalogue_club.h` A7, `Vocabulary/Metrics/metric_catalogue_manifest.cpp` A7.
- Root `CMakeLists.txt`: the word `src/Vocabulary/Metrics` removed at `:863` (now `src/Coaching` alone, B1.6's) and
  `:3528` (offline targets). Every bare include of a moved file left (42) resolves in its own folder, so nothing needs
  the removed directory. The other 56 dead include directories are B2.13's (advice 5.1).
- `context_check.py` DIRS: `'Vocabulary/Club/': S, 'Vocabulary/Common/': S, 'Vocabulary/LaunchMonitor/': ACQ`.
  `--list c` exit 0; the full report's totals equal HEAD's (run on a `git archive` copy).
- `tokens.json` regenerated: one line, `variabilityHysteresis`'s usage path. Path-only, no Design Lead approval
  (JUDGEMENT-CALLS 25 Nov 16:54, call 2). `make_design_system.py --check`: current.
- IM (`governance/artefacts/information_model.md`), v1.11.2: header version; RING-VOCABULARY's Context and Directories
  cells name the areas as moved (Buffer's stream types still to come; Rig left out, C-3's); "Directories that span
  rings" loses its `Export` row, `LaunchMonitor` loses its vocabulary part; changelog row 1.11.2; footnote
  `[^sa24-q15]`, pointing at `docs/implementation/platform_refactor_assessment.md` "Rulings of 24 Nov" (Q15 and
  J-109, both Solution Architect rows; a first draft named it `sa25-b15` and cited the brief, fixed). **Call, §1.4's A1 row (`:138`) and §11's A1 row (`:1242`):** the bare `Recording` there
  names the folder the lint rule is keyed on, which is now `Vocabulary/Recording/` (and covers `frame_decode`, an
  Adapters file before, so the rule's reach is unchanged): §1.4's two mentions and §11's "a `Recording` file" now say
  `Vocabulary/Recording`; §11's row title and "none in `Export` or `Recording`" left (the decision's words; still true).
  Kept rows DEC-29, DEC-37, DEC-148, 1.8, 1.9.24 unchanged (verify lists all five as kept).
- `docs/validation/`: 7 lines in 3 files (`pipeline_validation_and_tuning.md:201,974`,
  `trail_wrist_corridor_seating.md:163`, `tunable_parameters_reference.md:16,291,308,1045`): all say where the file
  is today; rewritten to `src/Vocabulary/Common/`.
- Can't-place list, read one by one: rewritten: two C++ comment pointers `(Metrics/` + next line
  `gate_catalogue*.h` → `(Vocabulary/Metrics/` (`characteristic_engine.h:356`, `gate_words_pack.h:55`); README (above);
  the lint fixture (above). Left: `rig_design.md:406` (a planned file, already says it goes to `Vocabulary/Recording`
  once that exists), `rig_design.md:582,801` (a dated include inventory), `trust_and_gates_design.md:196` (names a
  manifest file that doesn't exist; design text), `trust_stage6_plan.md:77` (dated plan, 16 Oct),
  `swing_analysis.h:499` ("Metrics/scoring" is words, not a path).
- The deck: `information_model_walkthrough.pptx`'s slide naming `src/Identity` needs the Product Owner's refresh (say
  so in the commit and on "Needs you").

## 3. Verify and configure (Mac, configure only)

`move_files.py <map> --verify HEAD --allow-prose README.md --allow-prose governance/artefacts/information_model.md
[--allow-code-path src/Ppcp/tests/ppcp_annotation_test.cpp:322 --allow-code-path
src/Export/tests/swing_doc_test.cpp:2394, after §5's fix] --configure build/b15-scratch/cfg --seed build --cmake-arg -DPINPOINT_BUILD_TOOLS=ON --cmake-arg
-DPINPOINT_BUILD_LM_REPAIR=ON`, through `build/refactor/one_at_a_time.sh` (`B1.5-verify.txt`): renames 57, files with
changed lines 399, listed 27, **other lines 0, VERIFY PASSED, configure app PASSED, configure tests PASSED, CONFIGURE
PASSED**; the app log shows "Launch monitor tools: lm_repair enabled". First try failed on the new changelog row
naming the old folders by their old paths; reworded. Re-run after the fast-forward to `10473584`: the same. Final run, after §5's fix and the footnote: renames 57, 400
files, other lines 0, VERIFY PASSED, CONFIGURE PASSED, lm_repair enabled (one try failed on `Export/frame_decode` in
the footnote's words; reworded). Scratch `build/b15-scratch/` deleted.
- `ctest -N` in the scratch tests tree: 405, the same names as the lane's tree (diff empty).
- Python checks: `artefact_paths_check.py` 0 stale (3,561 files); `make_design_system.py --check` current;
  `count_includers.py vocab-after` 53 files, 280 includer files, 404 includes (= `vocab` before); `move_files.py
  --self-test` PASSED. The architecture and design-system lints are C++: studio PC.
- Not proven yet (the compile waits for the render): `:3528` and the `swinglab_run`, `lm_repair`,
  `swing_window_parity_test` builds; the TU-header compare; the A1 plant; the Mac's named ctest set; parity vs b1-base.

## 4. The studio PC patch

`make-wip-patch.sh` (`build/b15-scratch/patch/wip.patch`, sha256 bdb7d92c…, on 115cc58b): 57 A, 57 D, 342 M, 0 rename
hunks. `--no-renames` is by design (studio-build.cmd lists the files a patch creates to restore the checkout), so the
renames travel as delete + new file. Applied to a scratch index of HEAD with `git apply --cached`: its tree equals the
worktree's tree (dec94e17), so the patch carries the whole move. Nothing left out by the script's NOTE. (This was the tree before §5's two
test edits; the studio PC tested that patch, bdb7d92c.)

## 5. Cabin run

`cabin_run.sh build/wt-37c` (2 h watchdog; waited for lane 2's lock to 17:26), run `studio PC-wt-37c-20261125-172606`
(run dir kept on the share; summary `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261125-172606.txt`): exit 12. Cabin
HEAD 10473584 + patch bdb7d92c (456 files); build exit 0 (17 min 57 s); 389 tests registered (lane 2's run 16:57: 389);
stores unchanged; checkout restored clean.
- **Pass 1: 341/343.** Failures:
  - `ppcp_annotation_test` **NEW, caused by the move.** `NothingInAnalysisReadsAnAnnotation` lists the analysis folders
    as C++ strings (`"…", "LaunchMonitor", "Metrics"`) and asserts each exists: `src/Ppcp/tests/../../Metrics` is gone.
    The tool never rewrites a string literal (by design: a path in code is behaviour) and the advice's string search
    looked for paths, not bare folder names. **The same list is in `swing_doc_test.cpp:2394`**, which passed only
    because its loop doesn't check the folder exists: it silently stopped scanning `Metrics`. **Fixed** both lines to
    `"Vocabulary/Metrics"` (the map's image), `--verify … --allow-code-path src/Ppcp/tests/ppcp_annotation_test.cpp:322
    --allow-code-path src/Export/tests/swing_doc_test.cpp:2394`: VERIFY PASSED, CONFIGURE PASSED (400 files).
    Neither scan now covers the three LaunchMonitor reading-type files (now `Vocabulary/LaunchMonitor`); adding that
    folder is a word `--verify` refuses, so it is left for the coordinator (they are value types, not analysis code).
    Searched (`git grep`, every tracked file type, excluding docs/implementation and docs/research): `"Metrics"`,
    `"Recording"`, `"Identity"` (with or without `/`), each moved file's name in quotes, `(SRC_DIR|SOURCE_DIR)…<old
    path>`, and `"Core"`, `"Export"`, `"LaunchMonitor"` in src, tests, tools, cmake. Other hits are UI words, QML
    labels, `translate("LaunchMonitor", …)` contexts, the lint's own tables (A8's Adapters-only `LaunchMonitor/` list:
    the reading types were Vocabulary, so unaffected) and `tools/history/loc_history.py:74` (history data). The two
    lint comments `Export/ … frame decode below` and `LaunchMonitor/ … readings … below` (`:184-185`) are now stale
    words; left (a prose edit `--verify` refuses on a table line). All 13 folders in each scan list exist in wt-37c. **Not re-run on the studio PC** (budget: 1 run): the two
    edited tests are first compiled by the Mac build after the render; add `ppcp_annotation|swing_doc` to its filter.
  - `paced_connect_queue_test` "T4 b is not early (= gap after a, got 77 ms)": pre-existing timing flake, the same
    line on lane 2's studio PC run of 06:55 today (`studio PC-wt-5b-20261125-065523`); its three files are unchanged by the move and include
    no moved file (checked against `B1.5-run-real.txt` and the diff). Filed as #263 (open).
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231) and `tst_session_report` page count (#253).
- Suites over moved files all passed on the studio PC: both lints, `design_system_tokens_check`, `artefact_paths_check`,
  `move_files_selftest`, `store_isolation_lint_test`, `im_excerpt_selftest`, `identity_test`, `variability_grader_test`,
  `setup_capability_test`, `lm_device_test`, `frame_decode_test`, `club_standards_test`, `height_text_test`,
  `gate_catalogue_test`, `tuned_constants_parity_test`, `series_reduce_test`.

## Still to do (after the render)

Mac build of both trees with the tools on (`swinglab_run`, `lm_repair`, `swing_window_parity_test`; proves `:3528`);
`b15_tu_headers.py compare build/wt-37c build/platform/B1/B1.5-tu-before.json build/platform/B1/B1.5-dryrun-real-115cc58b.txt`;
the A1 plant; the Mac named ctest (+ `ppcp_annotation|swing_doc`); parity vs b1-base; the commit (the deck's refresh
named in it).

## Lane 2

`build/wt-5b` has `src/App/app_state.h` open; the move rewrites its line 50 (`../Identity/identity.h`). Lane 2 replays
the map with `--over-work` or rebases after B1.5 commits.

## 6. Rebased onto 29eec727 (the coordinator, after the handback)

- Saved the WIP (`B1.5-wip-on-10473584.patch`). Took the hand edits alone (`B1.5-hand-edits.patch`, 13 files) by
  diffing the WIP tree against a tool-only run in a scratch detached worktree (now removed). Then reset wt-37c to
  origin/main `29eec727` (b09b4125, the lifecycle model and DEC-204; 29eec727, its diagrams), with no stash.
- The IM moved down 4 lines, so the five keeps refused ("nothing the map rewrites"). They are re-derived by row id to
  `1464,1472,1583,2008,2040` in both map copies. Dry run (`B1.5-dryrun-real-29eec727.txt`) equals the 115cc58b one
  apart from IM line numbers: 57 / 370 in 261 / 517 / 58 / 12 / 5 kept / 0 errors. `lifecycle_model.md` names no
  moved path, so nothing there to rewrite. Real run with `--over-work`: 57 moved, 352 rewritten.
- `git apply --3way` of the hand edits: 12 files clean. The IM conflicted on the header and the change log, and both
  are resolved to **v1.12.1**, with its row after 1.12.0's and 1.12.0's State line kept. Against the saved WIP, the
  diff differs only there, in the map's keep numbers and in the test edits below.
- **The coordinator's call 2(a), folded into B1.5 because the move made the gap:** both folder-list scans now fail if
  a listed folder is missing, and both list `Vocabulary/LaunchMonitor`.
  - `ppcp_annotation_test.cpp:329` is `ASSERT_TRUE(fs::is_directory(…))` with a message.
  - `swing_doc_test.cpp` collects the missing folders and adds `check(missing.isEmpty(), "every listed analysis
    folder exists")`.
  - All 14 folders exist.
  - Verify: `--allow-code-path` covers only a string changed to the map's image, and these edits add words and lines.
    So the two files take `--allow-prose`, which waives their changed lines while the final pass still fails any
    old spelling left. This replaces §5's two `--allow-code-path` flags.
- Final `--verify HEAD --allow-prose README.md --allow-prose governance/artefacts/information_model.md --allow-prose
  src/Ppcp/tests/ppcp_annotation_test.cpp --allow-prose src/Export/tests/swing_doc_test.cpp --configure <scratch>
  --seed build --cmake-arg -DPINPOINT_BUILD_TOOLS=ON --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`:
  - verify: 57 renames, 400 files, the 5 keeps held, other lines 0, VERIFY PASSED;
  - configure: CONFIGURE PASSED, lm_repair enabled, `ctest -N` 405;
  - other checks: artefact_paths 0 stale (3,563 files), tokens current, `context_check --list c` 0, `vocab-after`
    280/404.
  - The scratch is deleted.
- The studio PC's run (§5) tested the 10473584 tree. Its two test edits and this rebase are first compiled by the Mac
  build after the render.
- **Lane 2's parked patches (2(b)):** B1.5 commits first and they rebase after.
  - The move changes only `src/App/app_state.h:50` (`"../Identity/identity.h"` → `"../Vocabulary/Identity/identity.h"`).
  - `parked-280-end.patch` touches app_state.h at `@@ -139`, `-171`, `-234`, `-257` and `-268`; `parked-283.patch`
    at `@@ -231`. None overlaps line 50.
  - `parked-283.patch` applies whole to the moved tree.
  - `parked-280-end.patch`'s app_state.h hunks apply to the moved tree. The whole patch already fails on plain
    origin/main at `governance/README.md:22` (main moved on), not because of the move.

## 7. The Mac half (after the render; on 29eec727 + the move)

Everything ran through `build/refactor/one_at_a_time.sh`, `-j 3`.
- **Builds.**
  - Tests tree: exit 0, 14 min 45 s (`b15-build-tests.log`).
  - App tree: exit 0, 4 min 51 s (`b15-build-app.log`).
  - Neither build has a compile error; every warning is of a kind already in the tree (deprecations, duplicate
    libraries, nodiscard, -Wswitch).
  - Then the app tree was configured with `PINPOINT_BUILD_TOOLS=ON` and `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair
    enabled"). `swinglab_run`, `lm_repair` and `swing_window_parity_test` built with exit 0 (`b15-build-tools.log`),
    which proves `CMakeLists.txt:3528`. `swing_window_parity_test` ran: PASS, 0 check failures. The app tree is set
    back to tools off afterwards.
- **TU headers** (`b15-tu-compare.log`): 5,703 units before and after.
  - First compare: 4 differences.
  - 2 were `ppcp_app_tu_syntax_probe`, an `EXCLUDE_FROM_ALL` target whose deps log was stale. Built it (exit 0):
    those 2 are gone.
  - The other 2 are one unit, `charts_session_maths_test`'s moc unit: the script keys a generated unit by the first
    repo file it reads, and that first file changed (`figure_format.h` sorted first before the move). Compared by
    hand under the map, its 41 headers are identical.
  - **Effective differences: 0.**
- **Named ctest:** `metric|identity|recording|frame_decode|club_standards|height_text|launch_monitor|variability|
  setup_capability|lm_device|gate_catalogue|tuned_constants|series_reduce|design_system|move_files|artefact_paths|
  store_isolation|ppcp_annotation|swing_doc|lint|im_excerpt_selftest`: **41/41 passed** (`b15-ctest.log`).
  - That covers all four lints (architecture, design_system, store_isolation, session_setup), the tokens check,
    artefact_paths_check, and ppcp_annotation_test and swing_doc_test with their new folder checks.
  - Also run: namespace_collision_test, context_tree_test, im_changelog_merge_selftest, one_at_a_time_selftest and
    cabin_run_selftest: 5/5.
- **A1 plant:** `#include "../../Gui/shot/shot_processor.h"` added to `src/Vocabulary/Recording/frame_decode.cpp`.
  - `architecture_lint_test` FAILED as it should: "Vocabulary/Recording/frame_decode.cpp:19 Vocabulary → Application".
    Reverted (the file is byte-identical to before), and the lint passes again.
  - Finding (edge, for later): the finding comes from the ring check. The `Vocabulary/Recording/` branch of A1's
    DEC-146 rule cannot fire for any Vocabulary file, because a Gui header is always an outer ring and the ring check
    fires first. It was just as unreachable when it was keyed on `Recording/`.
- **Parity:** `parity_baseline.py b15-vocab` (exit 0, 8 sessions counted), then
  `parity_diff.py b1-base b15-vocab`: **18 differences, all from main, none from the move.**
  - `workOns.ruleVersion` 6 → 7, in 8 sessions of diagnostics.json and the same 8 of athlete.json. This is 25d21cfd
    (#259), whose own message reports exactly this against the baseline.
  - `PINPOINT_CORE_REFERENCES`, the references.json hash, `502ac14c` → `abb8ffc6`, in both files. These are the sha1
    of references.json before and after 46a37699 (#195's citation path).
  - Against the newer `i259-wilson` (e1aeee20) only the references hash differs (2).
  - The move leaves `src/Resources` untouched.
  - **Call:** b1-base predates those two commits; the move adds 0 differences, so this counts as a pass. b1-base
    wants retaking on main for B1.6.
- **Committed and pushed:** `1918a017` on main (fast-forward from 29eec727; main had not moved). Under GIT-2. The
  message is `B1.5-commit-msg.txt`. wt-37c is clean. The app tree is back to tools off.
- For the coordinator: the walkthrough deck's `src/Identity` slide belongs on the dashboard's "Needs you" (the
  Product Owner's refresh); b1-base wants retaking on main before B1.6.

## 8. Parity baseline retaken (25 Nov, evening, wt-37c at origin/main 1918a017)

- Tools tree: `regrade_ledger athlete_dump` incremental build through the lock: no work to do (built 18:50, after the move).
- `parity_baseline.py b1-base-2`: exit 0, 8 sessions of 4 athletes, the dump counted every session. Label
  **`b1-base-2`**, commit **1918a017** (0 changed paths), folder `the share
  (`b1base2-parity.log`).
- `parity_diff.py b1-base b1-base-2`: **18 differences**, the same 18 as §7 (the report below its header is
  byte-identical to `b15-parity-diff.log`): `workOns.ruleVersion` 6 → 7 in 8 sessions of each file, and the
  references hash `502ac14c` → `abb8ffc6` in each file (`b1base2-diff-vs-b1base.log`).
- `parity_diff.py b15-vocab b1-base-2`: 0 differences (`b1base2-diff-vs-b15.log`); `b1-base-2` against itself: 0.
- From B1.6 on the parity base is `b1-base-2`: `BRIEF-B1.6.md` and `MOVES.md` "Proof" say so. For the coordinator:
  the later briefs B1.7, B1.12, B1.15b, B1.16, B1.17, B1.19, B1.20 and B1.22 still name `b1-base` literally; against
  it they would show these same 18.
