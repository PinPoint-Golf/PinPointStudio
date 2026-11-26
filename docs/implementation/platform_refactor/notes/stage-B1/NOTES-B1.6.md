# B1.6 · today's src/Coaching moves to src/Diagnostics/derived/ (25 Nov 2026)

Lane `build/wt-37c`, reset to origin/main `8f33e190` (no stash). P-3b is on main (`session_gates.h` is here).
IM excerpt regenerated (`make_im_excerpts.sh build/wt-37c`, IM at `governance/artefacts/`).

## 1. Check first (on 8f33e190)

- `count_includers.py coaching`: 36 files; **113 includer files, 244 includes** (57 bare, 20 dir-qualified, 167
  relative). The brief's 111/238/56 were on bdd04541; the rise is main since then (`session_gates.h`'s includers,
  #283's `app_state` files and others).
- `src/Diagnostics` has no `derived/`; `comm -12` of its basenames with the moved ones: empty. One basename is shared
  tree-wide already: `variability_measures.h` (also `Gui/variability/`); no bare include of it, unchanged by the move.
- Tests of these files stay in `src/Analysis/tests`, `src/App/tests`, `src/Diagnostics/tests`, `src/DomainModel/tests`,
  `src/Gui/tests`; only their includes change. `src/Coaching` has no `tests/`.
- Since the brief: #283's `app_state` files name no moved path; AD-50 and `lifecycle_model.md` name none; the README's
  src/ table row is a hand edit (§2). Generated sources (`*qmltyperegistrations*.cpp`, moc) in the lane's trees include
  none of the 36 basenames (the three headers `PinPointQmlModule.cmake` lists have no QML_ELEMENT), so removing the
  global include directory breaks no generated include.
- String literals (B1.5's lesson): `git grep '"Coaching'` in src/tools/tests/cmake: the folder lists of
  `swing_doc_test.cpp:2394` and `ppcp_annotation_test.cpp:322` (both now fail on a missing folder), the gate
  producers `gate_catalogue_rules.h:38,40,41,42` (read by `gate_catalogue_test`, which opens each producer file; no other
  reader, not serialised). The rest are UI words ("Coaching" tab, wrist "Coaching" row) and the lint's tables.

## 2. The map, the run and the hand edits

- Map `tools/refactor/maps/B1.6-coaching.map` (committed in 4d80463b) + two keep stanzas:
  - dated: IM `1429` (GL-71: the rename is B1.8's, which edits it; until then it says "today's `src/Coaching` files"),
    `2066` (change log 1.9.46), `2074` (1.10.0);
  - **still true after the move** (the old spelling is the new meaning, the Coaching context's folder): IM `418`, `423`
    (§4 "The Coaching folder", first and last lines) and `docs/design/coaching_layer.md:340` (the ruling on the name,
    as recorded; its "becomes its folder at Stage B, Part 1" left in the future tense: call).
- Dry runs on 8f33e190 (`B1.6-dryrun-8f33e190.txt`, then `-final.txt` with the keeps). TOTALS: move 36 files; 228
  include lines in 107 files; 133 path mentions (140 before the keeps); 2 include dirs re-pathed; 7 can't place; 6 kept;
  0 errors. Real run `--over-work` (`B1.6-run-real.txt`): the same report; 36 moved, 147 files rewritten.
- Hand edits:
  - Root `CMakeLists.txt`: the global include directory `src/Coaching` (`:863`, re-pathed by the tool) deleted, and the
    word at `:3529` (offline targets) removed. `src/Diagnostics/derived` is not added. No include of a moved file needs it.
  - Lint `dirRings()` `:177`: back to `Coaching/` → Rules, comment "the Coaching context's folder, empty: its first file
    has a ring (IM §4)". `Diagnostics/` covers `Diagnostics/derived/`. `isRulesIoDir` `:363` and the fixtures
    (`:732,741,745,748,783,798,839,849`) left as re-keyed to `Diagnostics/derived/`.
  - Allowlist: **2 entry lines re-keyed** (`Diagnostics/derived/shot_outcomes.h` A7, `…/tendencies.h` A7) and one
    comment mention (`:52`); none added or removed.
  - `context_check.py`: `'Diagnostics/derived/': DIA` and `RESULT_DIRS` as re-keyed; the entry's stale comment reworded.
    `Coaching/` gets no tag (no ruling for the script; see EDGE).
  - Code paths (`--allow-code-path`): `gate_catalogue_rules.h:38,40,41,42` (the gate producers `gate_catalogue_test`
    opens); `swing_doc_test.cpp:2394` and `ppcp_annotation_test.cpp:322` (`"Coaching"` → `"Diagnostics/derived"`,
    the map's image; both scans are recursive and both fail on a missing folder).
  - `cmake/PinPointQmlModule.cmake:642,807,809` read by eye: re-pathed, nothing else.
  - README src/ table: the `Diagnostics/`, `Coaching/` row splits in two (Diagnostics with `derived/`; Coaching, the
    context, no code yet).
  - IM v**1.12.4**: header; §2.4 M-SHOT-LAYERS layer 3's producer (`:335`, the tool's rewrite read "`src/Diagnostics/derived`
    today, `src/Diagnostics/derived/` from Stage B": reworded); §4 "The Coaching folder" `:419-421` past tense; §4.1
    RING-RULES Context cell (`Diagnostics/derived/` holds the derived facts; Coaching: the `Coaching` folder, empty) and
    Directories cell (`Diagnostics` (with `Diagnostics/derived`), `Coaching` (empty)); §11 rows and GL-26/GL-34 rewritten
    by the tool; change-log row 1.12.4. `:354` ("Coaching/Diagnostics rules", P4) left: words, not a path.
  - Docs by the tool: `diagnostics_developer_guide.md:535,539`; 13 design docs; `PpSankey.md:10`. `coaching_layer.md:340`
    kept (above). Left: `gate_words_pack.h:36` (names `src/Coaching/report_gate_words.h`, a file that no longer exists:
    history).
- `src/Coaching/`: 0 files (git keeps no empty folder; no placeholder).

## 3. Verify and configure

`move_files.py … --verify HEAD --allow-prose README.md --allow-prose governance/artefacts/information_model.md
--allow-code-path src/Vocabulary/Metrics/gate_catalogue_rules.h:38,40,41,42 --allow-code-path
src/Export/tests/swing_doc_test.cpp:2394 --allow-code-path src/Ppcp/tests/ppcp_annotation_test.cpp:322 --configure
<scratch> --seed build/wt-37c/build --cmake-arg -DPINPOINT_BUILD_TOOLS=ON --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`
through the lock (`B1.6-verify.txt`, on 8f33e190): renames 36, files with changed lines 165, listed 20, **other lines
0, VERIFY PASSED, configure app PASSED, configure tests PASSED, CONFIGURE PASSED**, "lm_repair enabled"; `ctest -N` 405
(= the lane's tree). Scratch deleted.
- Python checks: `count_includers.py Diagnostics/derived/` **113 files, 244 includes** (= `coaching` before);
  `coaching` after: 0; `artefact_paths_check.py` 0 stale (3,565 files); `make_design_system.py --check` current;
  `context_check.py` full report equal to HEAD's (on a `git archive` copy), `--list c` exit 0; `move_files.py --self-test`
  PASSED.

## 4. Rebased onto 1e3d9691 (main moved during §3: bebf8bb6 lifecycle model, three planning docs, #281's stopgap)

Main's five commits name no moved path (`git diff | grep Coaching`: none); only `CMakeLists.txt` is touched by both.
WIP saved (`B1.6-wip-on-8f33e190.patch`), wt-37c reset to origin/main 1e3d9691 (no stash), patch applied
`--3way --index`: every file clean. IM unchanged on main, so the keep line numbers hold.
- Re-verified on 1e3d9691 (`B1.6-verify-1e3d9691.txt`): renames 36, 165 files, listed 20 (the same 20, CMake line
  numbers +1), **other lines 0, VERIFY PASSED, CONFIGURE PASSED**; `ctest -N` 406 (main's `missed_swing_notice_test`).

## 5. Cabin run

`build/platform/B1/b16_cabin_wrap.sh` (watchdog: retries 20/24 every 10 min up to 7 h in all; kills the Mac side only
if it outlives its own 2 h limit by 10 min after "launched"). Log `b16-studio PC.log`. Queued at 20:27 behind lane
`i183-ab`'s lock (#183's runs).

## 4b. Rebased onto e38ad103 (the coordinator's heads-up: D-V3 took IM 1.12.4; #183's display curve landed)

- The 20:27 studio PC attempt was stopped while it waited for the studio PC lock (nothing launched). WIP saved
  (`B1.6-wip-on-1e3d9691.patch`); reset to origin/main e38ad103; `git apply --3way --index`: every file clean but the IM.
- IM: three conflict blocks resolved by hand, both changes kept: the Version line is B1.6's, **renumbered 1.12.5**;
  RING-VOCABULARY is main's (D-V3, `[^sa25-buffer]`) and RING-RULES is B1.6's; the change log keeps main's 1.12.4 and
  then B1.6's row as **1.12.5**. The keep lines (418, 423, 1429, 2066, 2074) are at the same numbers on e38ad103.
- Verify + configure on e38ad103 (`B1.6-verify-e38ad103.txt`): renames 36, 165 files, listed 20, **other lines 0,
  VERIFY PASSED, CONFIGURE PASSED**; `ctest -N` 407 (main's #183 test). artefact_paths 0 stale (3,571 files); tokens
  current; `context_check --list c` 0; `count_includers Diagnostics/derived/` 113/244.
- Main then gained dbe26ea6 (governance only: AGENTS.md, controls/build.md; no overlap). The studio PC applies the patch on
  that tip. Cabin restarted 21:55, queued behind lane `i183-ab`.

### Cabin result (run `studio PC-wt-37c-20261125-233131`, summary `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261125-233131.txt`)

Exit 12. The studio PC tested the patch (201 files, sha256 c1acdc94…) on main's tip 5f10d75c; build exit 0 (13 min 8 s);
stores unchanged; checkout restored clean. Run dir kept on the share.
- **Pass 1: 344/346**, 1 not run. `session_list_test` L7a ("corrected and trashed: the session … could not" be moved
  to the trash): **#250**, open, the same line in three earlier studio PC runs (lane 2 and wt-37c, 24–25 Nov, without this
  move); a Windows trash move, nothing to do with include paths. `paced_connect_queue_test` T4 (77 ms): **#263**.
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231), `tst_session_report` page count (#253).
- Suites over moved files all passed, among them both lints, `gate_catalogue_test`, `ppcp_annotation_test`,
  `swing_doc_test`, `session_gates_test`, `work_ons_test`, `tendencies_test`, `reduction_rule_golden_test`.
- Accept: no new failure against the last studio PC runs on main.

## 6. Rebased onto dbe0a50a before the Mac half

Main gained twelve commits (governance, docs, #294's quit tests, the Gemini key fixes, the AssemblyAI removal, the
cloud-fallback switch); only `CMakeLists.txt` is touched by both (separate hunks) and none names a moved path. WIP
saved (`B1.6-wip-on-e38ad103.patch`), reset to dbe0a50a, `git apply --3way --index`: every file clean.
- Re-verified on dbe0a50a (`B1.6-verify-dbe0a50a.txt`): 36 renames, 165 files, listed 20, other lines 0, VERIFY and
  CONFIGURE PASSED; `ctest -N` 421; artefact_paths 0 stale (3,576); tokens current; context `--list c` 0; includers 113/244.

## 7. The Mac half (on dbe0a50a + the move)

Through `tools/method/one_at_a_time.sh`, `-j 3` (`b16_mac_builds.sh`). **Watchdog defect:** the script's `ps -o etimes`
does not exist on macOS, so its 90 min kill never armed; both builds ended on their own, exit 0. Fix the helper before reuse.
- Builds (2, the budget): tests tree exit 0, 0 compile errors (`b16-build-tests.log`); app tree reconfigured with
  `PINPOINT_BUILD_TOOLS=ON`, `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled", `b16-configure-tools.log`) and built,
  exit 0, 0 errors (`b16-build-app.log`): `swinglab_run`, `lm_repair`, `swing_window_parity_test` built (proves the
  offline targets without `src/Coaching`). `swing_window_parity_test` (not a ctest; run as B1.5 did, through the lock):
  PASS, 0 failures. App tree set back to tools off afterwards (`b16-configure-tools-off.log`).
- Named ctest (`b16-ctest.log`): the brief's regex plus `lint|artefact_paths|design_system_tokens|gate_catalogue|
  ppcp_annotation|swing_doc|move_files|im_excerpt|context_tree|namespace_collision|im_changelog_merge`: **37/37 passed**,
  among them the four lints (architecture, design_system, store_isolation, session_setup), tokens, artefact_paths,
  gate_catalogue_test, ppcp_annotation_test, swing_doc_test, session_gates_test, qml_ui.tst_session_report and
  qml_ui.tst_swing_areas.
- **Plants** (`b16-lint-plant.log`): `src/Diagnostics/derived/zz_plant.h` (a Gui include and a `QFile` read) and
  `src/Coaching/zz_plant.h` (a Gui include). `architecture_lint_test` FAILED as it should: both "Rules → Application:
  includes Gui/shot/shot_processor.h" (A1; the Coaching line proves the kept `Coaching/` key) and "QFile: file I/O in
  a rules directory" for the derived file (A5; proves `isRulesIoDir`'s re-key). Removed (`src/Coaching` gone again);
  the lint passes.
- **Parity:** `parity_baseline.py b16-derived` exit 0, 8 sessions counted; `parity_diff.py b1-base-2 b16-derived`:
  **0 differences** (`b16-parity-diff.log`).
- TU-header compare (B1.5's extra) not taken: no before-snapshot on this base; `--verify` and the include check stand.

## 8. Final rebase and commit

Main gained c88a6491 and 721da30f (quit tests, lifecycle rows; no shared file): `git merge --ff-only` over the work
tree. Re-run: the four lints, tokens and artefact_paths 6/6 passed (`b16-ctest-rebased.log`); `--verify --configure`
on 721da30f: 36 renames, 165 files, other lines 0, PASSED (`B1.6-verify-721da30f.txt`); artefact_paths 0 stale.

## Accept (the brief)

- Files in `src/Coaching`: **0**. Includers rewritten: **113 files, 244 includes** (today's count), 0 unresolved
  (`--verify` resolves every include to the map's image, none through an include directory).
- `src/Coaching` gone from the global include list (and from the offline targets').
- Allowlist: 2 entry lines re-keyed, same number of lines and counts.
- **B-08's non-wrist half closed:** the adapter and what it adapts are now one context and one ring.
  `src/Diagnostics/packs/swing_themes_pack.h:61-62` includes `"../derived/swing_themes.h"` and
  `"../derived/condition_role_pack.h"`; `src/Diagnostics/derived/{shot_outcomes,swing_areas,work_ons}_pack.h` include
  `"../engine/characteristic.h"` (and `condition_reading.h`). Nothing extra moved.
- The deck: slide 8 of `information_model_walkthrough.pptx` says "src/Coaching's present files are derived facts. They
  move to src/Diagnostics/derived/ in Stage B, Part 1": the Product Owner's refresh ("Needs you").

## Calls and EDGE

- Calls: GL-71 keeps "today's `src/Coaching` files" until B1.8 renames the types; `coaching_layer.md:340` (first kept)
  rewritten in the past tense before the commit, naming "the `Coaching` folder" (its keep dropped from the map;
  `--allow-prose docs/design/coaching_layer.md`; a doc-only edit after the studio PC run); IM `:335`
  reworded; `context_check.py` left the re-keyed `Diagnostics/derived/` entry and gave `Coaching/` no tag (no ruling
  for the script; with no file it changes no total).
- EDGE (for the coordinator): `isRulesIoDir` no longer lists `Coaching/`, so the Coaching context's first file will not
  get A5's file-I/O check unless that item adds it; `context_check.py` has no `Coaching/` tag for the same reason.
  `gate_words_pack.h:36` names `src/Coaching/report_gate_words.h` (a file deleted long ago; history, left).
- Real stores: none reachable (paths only); the studio PC run reported stores unchanged; parity runs on share copies.

## 9. Committed and pushed

Before the commit (advisor's review): `session_list_test` passes on the Mac; on the studio PC its failing lines (L7a's
trash move, then L7a–L8) are identical to 18 earlier studio PC runs on main without the move (#250). GIT-2 / rule 5 of
`04-remediation-plan.md`: review = the brief's self-check tier, on `--verify` (passed); touched tests and the lints
pass; parity 0 differences; only the move's files staged (130 M + 36 R, `git add -A -- . ':!build'`); "Area: …" title,
no attribution. Final `--verify --configure` on 721da30f with the coaching_layer edit (`B1.6-verify-final.txt`): 36
renames, 166 files, other lines 0, PASSED; artefact_paths 0 stale.
**`008b8eb4`** on main (fast-forward from 721da30f), message `B1.6-commit-msg.txt` (plain words, GIT-9/GIT-13, one
reference line at the end). wt-37c clean; app tree back to tools off.
- For the coordinator: deck slide 8 on "Needs you"; the EDGE above (no A5 check and no context tag on `Coaching/`);
  the build-script watchdog defect (`ps -o etimes` is not on macOS).
