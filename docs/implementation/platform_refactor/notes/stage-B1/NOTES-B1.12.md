# B1.12 · LaunchMonitor's inferred reads move to `src/LaunchMonitor/inferred/` (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. Before the reset, wt-37c was clean (0 lines of `git status`), detached HEAD =
origin/main `176e6d44` (B1.11's flip; IM 1.12.10), and had no stash. I fetched, ran `reset --hard origin/main` (`176e6d44`)
and `clean -fd`, then regenerated the excerpt (`make_im_excerpts.sh build/wt-37c`).
**One lane (MOVES):** wt-stack is clean at `ebd8c952`. wt-5b (`027f9cbf`) has 4 modified files and 1 new one
(`governance/controls/{build,test}.md`, `src/Gui/tests/{CMakeLists.txt,session_setup_lint_test.cpp,qml_rule_allowlist.txt}`).
This move rewrites none of those files, so I left them alone, as B1.9 did.

## 0. The ruling (confirmed before any edit)

`docs/implementation/platform_b1_placements.md` R5, "Ruled:" (a), `src/LaunchMonitor/inferred/`, Rules, context
Diagnostics; the connectors Acquisition (the Solution Architect, 24 Nov 2026). The brief's "the ruled directory" and its
accept figures match it, so the stop condition does not fire.

## 1. Check first (on 176e6d44)

1. **The lint's list** (`architecture_lint_test.cpp:223-230`): the brief's eight stems, unchanged. `ls src/LaunchMonitor`:
   the other files are the GC Quad, GSPro and Premier monitors and parsers, the base, and the factory, plus two calls:
   `premier_club.h` (Premier's club code -> PinPoint's club) and `gspro_reading.*` (GSPro's wire format -> a reading).
   Both translate a device's format, so they are connectors and stay. No file there is a rule by its content.
2. **Their includes:** `launch_monitor_reading.h` (Vocabulary), each other (`lm_inferred_reads.h:43` ->
   `lm_session_reductions.h`), `Vocabulary/Analysis/gate_record.h` (`lm_gates.h:37`), and
   `Diagnostics/derived/handedness_gloss.h` (`lm_inferred_reads.h:44`; the brief's `Coaching/` path is stale since B1.6).
   **None includes a connector.**
3. **The other direction:** no connector in `src/LaunchMonitor` includes one of the eight. Only two tests in
   `src/LaunchMonitor/tests` do (`gcquad_pairing_test.cpp:14`, `standalone_gate_test.cpp:13`, bare). The connectors'
   callers in `Gui/launchmonitor` include them as well.
4. **`count_includers`:** 9, 5, 2, 3, 1, 7, 3 and 3 files (33 includes in 28 files). The brief's "71 includer files" is
   stale. The tests stay where they are (`src/LaunchMonitor/tests`, `src/Analysis/tests`, and others); only their includes change.
- Allowlist: no line names the eight, so none is re-keyed (176 lines before and after). No `hand:` line: nothing is split.

## 2. The map and the move

- Map `tools/refactor/maps/B1.12-lm-reads.map`: the eight stems -> `src/LaunchMonitor/inferred/`, plus
  `keep: tools/lm_flight_fit/README.md:51`. That line is `git show 16f12352:src/LaunchMonitor/lm_flight_path.h`, a path
  at a past commit, and re-pathing it would break the command.
- Dry run on today's main (`B1.12-dryrun.txt`) = real run (`B1.12-run-real.txt`, `--over-work` because the map was
  untracked). Their `diff` is the "done" line only. 9 files moved; 36 include lines in 30 files (5 outward); 28 path
  mentions (build 4, lint 8, context 8, docs 6, comments 3); 1 kept; 0 include directories; **5 can't place**; 0 errors.
- **Hand edits:**
  - Lint `src/Gui/tests/architecture_lint_test.cpp`: `dirRings()` gains `LaunchMonitor/inferred/` -> Rules (`:191`), and
    `LaunchMonitor/`'s comment now says connectors (`:190`). `fileRings()` loses the eight re-keyed entries and their
    comment (old `:221-230`). The header prose says LaunchMonitor's inferred reads have their own folder (`:93-94`). The
    ring-table check gains `LaunchMonitor/inferred/lm_gates.h == Rules` and `LaunchMonitor/gcquad_monitor.cpp == Adapters`
    (`:860`).
  - `tools/refactor/context_check.py`: `DIRS` gains `'LaunchMonitor/inferred/': DIA`, and `'LaunchMonitor/': ACQ` moves out
    of the "to place" block (both R5). `FILES` loses the eight re-keyed entries and their "to place" comment. The output
    on the new tree is **byte-identical** to main's script on main's tree, once the paths are mapped
    (`b112/ctx-new.out` vs `ctx-main.out`): 1055 files, (a) 928, (b) 65, (c) 0.
  - The five "can't place" string literals, the gate producers that `gate_catalogue_test` opens:
    `Vocabulary/Metrics/gate_catalogue_lm.h:34` and `gate_catalogue_rules.h:279,284,289,293` -> the new paths
    (`--allow-code-path`). One other reader of the `producer` field: the model browser shows it as "where it fires"
    (`Gui/diagnosticmodel/model_browser.cpp:4290`), so it now shows the new, correct path. My first `git grep` missed it,
    because it looked for `.producer` and the browser reads `e->producer`. The reviewer caught it. Parity does not dump
    the browser's text.
  - `src/LaunchMonitor` stays in the root's global include list, because the connectors' bare includes use it. No path is
    added for `inferred/`.
- Old paths left anywhere outside `docs/implementation` and `docs/research` (dated snapshots, PR-19): only the kept README line.
- **IM 1.12.11** (main is at 1.12.10): the version line; RING-RULES (the context cell names `LaunchMonitor/inferred` under
  Diagnostics and drops it from "to place"; the directories cell names the folder); RING-ADAPTERS (`LaunchMonitor` is the
  connectors, no longer "to place"); the spanning table's `LaunchMonitor` row goes; a new footnote `[^sa24-r5]` (the
  ruling); one change-log row. The tool re-pathed §11's direction-rule row (`:1294`).
  **Deck:** `information_model_walkthrough.pptx` has the slide text "Eight directories span rings (Export, Analysis,
  Core, LaunchMonitor, Buffer, IMU, Pose, Gui)". That is now stale for `LaunchMonitor`, so **the Product Owner needs to
  refresh the deck**. I did not edit the deck.

## 3. Verify and configure

- **The whole tree** (`B1.12-verify.txt`, through the lock): `--verify HEAD --allow-prose <IM> --allow-code-path
  gate_catalogue_lm.h:34 --allow-code-path gate_catalogue_rules.h:279,284,289,293 --configure <scratch> --seed
  build/wt-37c/build --cmake-arg -DPINPOINT_BUILD_TOOLS=ON --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`. Renames 9, 47 files,
  28 listed. **Configure app PASSED, configure tests PASSED, CONFIGURE PASSED.** VERIFY FAILED on **exactly 3 other
  lines**: the lint's two header-comment lines `:93-94` and the ring-table check line `:860`. These are hand lines outside a
  table, so I list them for review, as B1.11 did.
- **The same tree without those 3 lines** (`B1.12-verify-tool.txt`): other lines 0, **VERIFY PASSED** (exit 3, NOT
  CONFIGURED). The configure ran on the full tree above, and the 3 lines are a comment and a test expression.
- `count_includers` after (`b112/count-after.txt`): at the new paths 9, 5, 2, 3, 1, 7, 3 and 3, the same as before; at the
  old paths 0.

## 4. Accept

- `fileRings()` entries under `LaunchMonitor/`: **8 -> 0**. `src/LaunchMonitor` holds one ring (Adapters, by its
  directory), and `inferred/` is Rules by its own directory entry.
- Allowlist: 176 lines before and after, and no line re-keyed (none names the eight).

## 5. The Mac half (on 176e6d44 + the change)

Every heavy job went through `tools/method/one_at_a_time.sh`, `-j 3`, under `b112/dog.sh` (`ps -o etime=`).
- **Builds (2):** the tests tree, exit 0 with 0 errors (`b112-build-tests.log`). The app tree, configured with
  `PINPOINT_BUILD_TOOLS=ON` and `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled", `b112-configure-tools.log`), exit 0
  with 0 errors (`b112-build-app.log`). Afterwards I set the app tree back to tools off (`b112-configure-tools-off.log`).
- **The four lints on the real tree** (`b112-lint.log`): architecture, design_system, store_isolation and session_setup,
  4/4 passed. The debt line is identical to B1.11's (419 findings in 134 lines), the allowlist has 176 lines, and there is
  no `note:`.
- **Plants** (`b112-plants.log`, `b112/plants.sh`), then removed (`git status`: only the change):
  - `inferred/lm_gates.h` + `#include "../gcquad_monitor.h"` gives **A1** "Rules → Adapters". The tool alone would have
    kept the re-keyed entry, but the entry is gone now, so the ring comes from the new directory entry.
  - A new `inferred/zz_b112_plant.h` including `../premier_monitor.h` gives **A1** "Rules → Adapters". A new file takes
    the folder's ring.
  - `gcquad_monitor.cpp` + `#include "inferred/lm_session_reductions.h"` gives no lint finding (Adapters → Rules is
    allowed), but `context_check --list c` lists it: "Acquisition → Diagnostics: no relationship". So the context map
    would refuse a connector that includes an inferred read. None does today (§1.3).
- **Named ctest** (`b112-ctest.log`, `-LE qml_ui`): **31/31 passed**. These are every suite in `src/LaunchMonitor/tests`
  (9), the brief's `lm_|flight` suites (`lm_*` 8 with `lm_corridor`, `lm_session_model`, `lm_device`), `gate_catalogue_test`,
  `swing_doc_test`, `recording_repository_test`, `session_delivery_test`, `report_lm_test`, the four lints,
  `artefact_paths_check`, `design_system_tokens_check`, `context_tree_test`, `move_files_selftest`, `im_excerpt_selftest`
  and `im_changelog_merge_selftest`.
- **Parity:** `parity_baseline.py b112-lm-reads` exit 0 (`b112-parity.log`); `parity_diff.py b1-base-2 b112-lm-reads`:
  **0 differences** (`b112-parity-diff.log`).
- **Real stores in reach:** none. The change is includes, paths, tables and docs, with no code that reaches a store.
  `store_isolation_lint_test` passed.
- **After the review's fixes** (a lint comment and IM wording), I rebuilt the lint target only and re-ran
  `architecture_lint_test`, `im_excerpt_selftest` and `artefact_paths_check`: 3/3 passed (`b112-lint2.log`).

## 6. Cabin run

`b112/cabin_wrap.sh` (B1.11's wrapper with a 2 h ceiling; log `b112-studio PC.log`). I launched it only once the tree was
final (plants out, the build done). Run `studio PC-wt-37c-20261126-041349` (summary
`build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-041349.txt`; the run dir is kept on the share): exit 12. Cabin HEAD
176e6d44 + patch 7781f43b… (57 files). Build exit 0 (4 min 29 s). Stores unchanged; checkout restored clean; wall time 21 min.
- **Pass 1: 347/347**, 1 not run. **#303 (`diag_stage_panel_test`'s trash check) did not fail again**, so I did not
  need a run on main without the change. `session_list_test` L7a (#250) also passed this time.
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231) and `tst_session_report` page count (#253).
- Accept: no new failure against B1.11's runs.
- The studio PC ran the patch before the review's two wording fixes (§5's last bullet).

## 7. Review and EDGE

Fresh reviewer agent (not a fork): `REVIEW-B1.12.md`, **SIGNED OFF**, with no blocking finding. It re-ran 4 named tests
(4/4) and confirmed the live diff equals `B1.12-diff.patch`. Its sign-off needed parity and the studio PC run in NOTES;
both are now in §5 and §6. I fixed the wording and comment minors it raised (AG-22):
- the lint's `:84` "straddle rings today" now reads "straddled";
- "eight files" now reads "eight stems (nine files)" in the IM's footnote, the change-log row and the commit message;
- the footnote now attributes the reasons to R5's recommendation;
- §2's claim about the `producer` field is corrected (the model browser reads it), and the commit message says so.

Filed as one `edge case` issue: the deck slide that still lists `LaunchMonitor` among the directories that span rings (for
the Product Owner to refresh), and the context map's verdict on a connector including an inferred read (§5, plant 3), which
the lint does not enforce.

## 8. Commit

When I committed, main was still `176e6d44` and IM 1.12.11 was free, so the rebase was a no-op and needed no re-run. I
committed in wt-37c (`git add -A .`, `build/` ignored; 48 files: 9 R, 1 A, 38 M) with the message `b112/commit-msg.txt`,
re-read against GIT-8/9/10/13. The area is `LaunchMonitor:`, which the log already uses 4 times; there is no trailer.
**`a0379ef8`** is pushed to main (fast-forward) under GIT-2, after the sign-off, the Mac half and the studio PC run.
- Edge findings are filed as **#305** (`edge case`). The #303 result is recorded on #303.
- wt-37c is clean. The app tree is back to tools off. The configure scratch is deleted. The parity run tree
  `scratch/parity/b112-lm-reads` is kept, as the earlier items' were.
- **For the coordinator:** (1) the brief's "71 includers" was stale: it is 28 files and 33 includes. (2) The walkthrough
  deck needs the Product Owner's refresh (#305 item 1).
