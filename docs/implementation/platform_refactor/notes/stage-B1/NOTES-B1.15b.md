# B1.15b · The chart rule files leave `src/Gui` for `src/Diagnostics/derived/series/` (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. Before the reset wt-37c was clean, detached at origin/main `a0379ef8`
(B1.12; IM 1.12.11), no stash. I fetched (origin/main still `a0379ef8`), ran `reset --hard origin/main` and `clean -fd`,
and regenerated the excerpts (`make_im_excerpts.sh build/wt-37c`). **One lane:** wt-stack is clean at `ebd8c952`. wt-5b
(`027f9cbf`) has 4 modified files and 1 new one (`governance/controls/{build,test}.md`,
`src/Gui/tests/{CMakeLists.txt,session_setup_lint_test.cpp,qml_rule_allowlist.txt}`). This move rewrites
`src/Gui/tests/CMakeLists.txt` at :111-113 and :357-360; wt-5b's hunk is at :503-513, so they don't overlap. I left wt-5b alone.

## 0. The ruling, and the accept figure it sets
`docs/implementation/platform_b1_placements.md` R7, **Ruled:** `src/Diagnostics/derived/series/`, Rules, Diagnostics,
for the list as B1.15a grew it: eight of the ten, plus `session_series_types.h` and `session_curves` (19 files).
**`session_posture` and `session_delivery` stay in `src/Gui/charts` until #224-H** (the Solution Architect, 24 Nov 2026;
JUDGEMENT-CALLS: R1–R13 "all fifteen ruled as recommended"). The destination is the draft map's.
**The brief I was given says "10 → 0". The ruling makes it 10 → 2,** and the brief allows that ("or the ones the
inventory keeps, each with its reason"). I followed the ruling and did not move the two files.

## 1. Check first (on a0379ef8)
- **Includes:** the movers' include list matches NOTES-B1.15a §2. Only the paths changed: B1.5 moved `Core/pp_tuned_constants` to
  `Vocabulary/Common`, and B1.6 moved `Coaching/consistency_home` to `Diagnostics/derived`. There is no new include.
  `ad172f2c..HEAD` touched the movers only for those paths, plus `ConsistencyGrades::cellAt`'s deleted temporary twin
  (`consistency_grades.h:85`, the temporaries guard). The two files total 8,531 lines, not 8,529.
- **Q1/Q2** are answered by the ruling. **Q3 is unanswered**: the namespace stays `pinpoint::charts`, and no move renames it.
- `count_includers` before: `b115b/count-before.txt`. `context_check` before: `b115b/ctx-before.out`.

## 2. Part 1: the outward includes cut (one change; `B1.15b-part1.patch`; snapshot `20dd4f41`, an unreferenced commit object)
- **E1:** `session_curves.h:58` includes `Vocabulary/Metrics/box_stats.h`, not the Gui forwarder. Its comments at :101-102
  and `session_relations.h:72` name the same file.
- **E2:** `consistencyGradeScales` (its declaration and the provider's forward declaration, from `consistency_grades.h:60,69-70`,
  and its definition, from `.cpp:52-57`, with the `variability_env.h` include at :22) moves to
  `variability_env.h` (end) and `variability_env.cpp` (end). It keeps its name and namespace. `variability_env.h` includes
  `consistency_grades.h`. No caller changes. `:46`'s comment is still true, so it is unchanged.
- **E3:** `consistencyGuardWords()` moves from `session_consistency.h:148-149` to `charts_words.h` (`words::`). Its callers are
  `charts_consistency_presenter.cpp:490-491` and `charts_relations_presenter.cpp:2906` (`words::` added).
- **E4:** new files `Gui/charts/session_sequence_words.{h,cpp}`, made of whole blocks cut unedited. Header: `session_sequence.h:40,111-136`
  (`UnplacedWhy`, `unplacedWhy`, `peakTimesSentence`, `appendProbableWhen`, `appendPeaksWhen`, `segmentGates`). Source:
  `.cpp:22-24,168-288`. These use no file-local helper. At `session_sequence.cpp:314`, `words::isAfterImpact` becomes
  `pinpoint::figures::isAfterImpact`, the same function (charts_words.h:74 is a `using` of it), with `figure_format.h` included.
  The words header is now included by the traces and relations presenters, `trust/trust_readings.cpp` and
  `tests/charts_traces_presenter_test.cpp`. The words `.cpp` sits beside every `session_sequence.cpp` in 12 build lists. That is
  the inventory's list plus `report_model_tests.cmake:50`, which the inventory missed.
- **After part 1,** the 19 files include only Vocabulary, Rules (`Kinematics/gap_bridge`, `Wrist/wrist_profile`,
  `Diagnostics/derived/consistency_home`) and each other (grep, §2's list).
- **Build 1** (tests tree, `b115b-p1-build-tests.log`): exit 0. **ctest** (`b115b-p1-ctest.log`, -LE qml_ui): 29/29 passed.
  **Parity 1** `b115b-part1` against `b1-base-2`: **0 differences** (`b115b-p1-parity-diff.log`).

## 3. The move
- Map `tools/refactor/maps/B1.15b-charts.map` (a copy of the draft, updated): the ten stems, the ruling in its header, the two held
  files named in a comment, and the two `hand:` lines for E4.
- Dry run (`B1.15b-dryrun.txt`) and real run (`B1.15b-run-real.txt`, `--over-work`): their only difference is the
  `session_relations.h:72` comment, which I fixed by hand in between. 19 files moved; 83 include lines in 48 files (12 outward); 100 path
  mentions in the real run (101 in the dry run; build 85, docs 6, comments 5 + the one I fixed); 0 include directories; 7 can't place (the 6 gate-catalogue producer literals, plus
  `context_check.py:173`'s resolver, which is not a path); 0 errors. **Ring Adapters → Rules, context HUB → Diagnostics,
  from the existing `Diagnostics/` entries: no lint or context table edit.**
- **Hand edits after the move:**
  - `gate_catalogue_rules.h:43-45,210,214,225`: the producer paths (`--allow-code-path`).
  - **The QML module list** (brief, part 2): the 19 leave `PP_QML_SOURCES` for a new `PP_CHARTS_RULES_SOURCES` in
    `cmake/PinPointQmlModule.cmake`. All three consumers add it straight after their module: `CMakeLists.txt` (app),
    `src/Gui/CMakeLists.txt` (qml_ui_test, and session_setup_ui_test, which I found as a third consumer). The
    session_setup_ui_test line and its drop-count check are read, not exercised: that suite's build option is off in every tree. The header comment names the
    list. The global include list is unchanged: it keeps `src/Gui/charts`, and adds no path.
  - The IM (§5).
- `count_includers` after (`b115b/count-after.txt`): the same counts at the new paths, except consistency_grades +1
  (variability_env.h, E2) and session_sequence +1 (session_sequence_words.h, E4). The old paths have 0. Old paths left outside
  `docs/implementation`: 3, in a dated research census under `docs/research` (PR-19: dated snapshots are not re-pathed).
- **Test files re-pointed (17; only include and comment lines change, plus one added include):** charts_consistency_presenter_test,
  charts_delivery_presenter_test, charts_maths_fixture.h, charts_posture_presenter_test, charts_relations_presenter_test,
  charts_session_maths_test, charts_traces_presenter_test (+ the words header), charts_wrist_presenter_test,
  delivery_picture_test, folder_series.h, session_consistency_test, session_delivery_test, session_posture_test,
  trust_readings_test, variability_calibrate, variability_clubs_test, variability_env_test (all in `src/Gui/tests`).

## 4. Verify and configure
- **Whole tree vs HEAD** (`B1.15b-verify.txt`, through the lock, tools on): renames 19, 76 files. Both hand splits were
  **accepted byte for byte** (27 + 124 lines; the scaffold is listed). 12 build lines name the split. **Configure app PASSED, configure
  tests PASSED, CONFIGURE PASSED.** VERIFY FAILED on **136 other lines**, each a declared hand edit: 68 from part 1
  (= `B1.15b-part1.patch`) and 68 from the QML list's relocation.
- **The move alone vs part 1's snapshot `20dd4f41`** (`B1.15b-verify-move-only.txt`, the map without `hand:`):
  **68 other lines, all the QML list's relocation** (64 in `PinPointQmlModule.cmake`, 4 consumer lines). Every other changed
  line is a move.

## 5. IM 1.12.12 (C9, R7)
The version line, and RING-RULES: the context cell and the directories cell name `Diagnostics/derived/series/`. The note under
the table now says what is true ("domain code leaves `Gui` in step 7" is replaced: the Charts' maths left in Stage B, Part 1;
posture and delivery wait for #224-H). New footnote `[^sa24-r7]` and one change-log row. The tool re-pathed §11's
`SessionSnapshot` row (:1333). **Deck:** `information_model_walkthrough.pptx` names neither `Gui/charts` nor the note, so it needs no refresh.

## 6. Accept
- Of the ten, files left in `src/Gui`: **10 → 2** (`session_posture`, `session_delivery`, held by R7 until #224-H).
- A1 findings from the new folder: **0.** Lint debt line identical to main's (419 in 134 lines). Allowlist 176 lines before
  and after; lint tables, `context_check.py` and both allowlists untouched.
- **Lines of rules out of `src/Gui`: 4,801** (19 files). The words left in Gui: `session_sequence_words.*` (215 lines).
- **#224 F-11:** closed for the eight files and the two helpers. It stays open for `session_posture` and `session_delivery`, which wait
  for #224-H's word separation.

## 7. The Mac half (final tree)
- **Build 2:** tests tree exit 0 (`b115b-build-tests.log`); app tree configured with `PINPOINT_BUILD_TOOLS=ON`,
  `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled"), exit 0 (`b115b-build-app.log`); set back to tools off after.
- **ctest** (`b115b-ctest.log`, -LE qml_ui): **34/34 passed**. That is the brief's regex, plus gate_catalogue, trust_readings,
  report_model, replay_score, review_views_shot, session_series, swing_rig_driver, the four lints, artefact_paths_check,
  design_system_tokens_check, context_tree, move_files_selftest, im_excerpt_selftest and im_changelog_merge_selftest.
- **QML** (`b115b-qml.log`, -L qml_ui -R 'tst_charts|variability'): 3/4 passed. In `tst_charts_panel`, 40 passed and 1 failed:
  **test_15 (#231), the only failure.**
- **Plants** (`b115b-plants.log`, `b115b/plants.sh`; removed afterwards). `series/session_warp.h` including Gui's `charts_words.h` gives A1 Rules → Adapters.
  A new `series/zz_b115b_plant.h` including `charts_work.h` gives A1. `context_check` lists the first as Diagnostics → Domain+Application.
- **Parity 2** `b115b-charts` vs `b1-base-2`: **0 differences** (`b115b-parity-diff.log`). No mover has its own presenter dump
  (day_readings_dump covers posture and delivery only).
- **Real stores:** none in reach (includes, paths, build lists, one moved function body); store_isolation_lint passed.

## 8. Cabin run
`b115b/cabin_wrap.sh` (B1.12's wrapper, 2 h ceiling; log `b115b-studio PC.log`), launched once the tests tree had built and
before the plants (the patch is taken at launch). Run `studio PC-wt-37c-20261126-050309` (summary
`build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-050309.txt`): exit 12. Cabin HEAD a0379ef8 + patch 9ba958bb… (98 files).
Build exit 0 (5 min 18 s); stores unchanged; checkout restored clean; wall time 22 min.
- **Pass 1: 347/347**, 1 not run. **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231) and
  `tst_session_report` page count (#253). **Accept: no new failure against B1.12's run** (the same two).

## 9. EDGE (for one issue)
- `PP_CHARTS_RULES_SOURCES` must be added by each module consumer by hand. A fourth consumer that forgets it fails only at link time.
- `segmentGates` (session_sequence_words) has no caller in C++ or QML. It moved as it was.
- The review's edges below.

## 10. Review
Fresh reviewer agent (not a fork): `REVIEW-B1.15b.md`, **SIGNED OFF**, no blocking finding. It re-cut E4's blocks itself,
re-ran 5 suites (5/5), and confirmed the live diff equals `B1.15b-diff.patch`. Its edges:
- E1 and E7: the commit message claimed every suite passed, and it used code words. I reworded it (`b115b/commit-msg.txt`).
- E2-E5: wording in these notes. Fixed in §3 (the session_setup line is read, not exercised; 100 path mentions; 3 old paths
  in `docs/research`; the 17 test files listed).
- E6: cutting the blocks out left double blank lines in `series/session_sequence.{h,cpp}`. This is cosmetic, so it was not
  fixed (AG-22) and is filed instead.

## 11. Commit
When I committed, main was still `a0379ef8` and IM 1.12.12 was free, so the rebase was a no-op and needed no re-run. The staged diff
was byte-identical to the reviewed `B1.15b-diff.patch`: 79 files (19 R, 3 A, 57 M). The message is `b115b/commit-msg.txt`, re-read
against GIT-8/9/13. The area is `Diagnostics:`, which the log already uses; there is no trailer. **`4f3a843e`** is pushed to main
(fast-forward, GIT-2). The edge findings are filed as **#306** (`edge case`). wt-37c is clean at 4f3a843e, and the app tree is back to tools off.
The parity run trees `scratch/parity/b115b-part1` and `b115b-charts` are kept, as the earlier items' were.
- **For the coordinator:**
  1. The brief's accept figure "10 → 0" is 10 → 2 by R7.
  2. The main tree's untracked draft `tools/refactor/maps/B1.15b-charts.map` now collides with the committed map at the same
     path, so it will block the main tree's fast-forward. It differs from the committed map only by the two `hand:` lines and the
     header comments. It can be deleted, and has to be before that tree pulls. I did not delete it.
