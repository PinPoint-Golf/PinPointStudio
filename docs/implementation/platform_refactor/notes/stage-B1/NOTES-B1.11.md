# B1.11 · Core's placements, run before B1.9 and without its last step (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. Before the reset, wt-37c was clean, at detached HEAD = origin/main `14f37426`
(B1.10), nothing off main, no stash. Fetched, `reset --hard origin/main` (`14f37426`), `clean -fd`. Excerpt regenerated
(`make_im_excerpts.sh build/wt-37c`). **Order (coordinator, JUDGEMENT-CALLS.md last entry, 26 Nov 02:30):** B1.11's
placements now; **R4's second part, the `Core/` directory rule becoming Platform, is NOT done**: it waits until B1.9 has
moved the studio profile out of `Core`. It is left undone on purpose, and said so in the code comment, the IM and the commit.

## 0. The rulings (the brief's "stop and propose")

All four are recorded: `docs/implementation/platform_b1_placements.md` R1 (line 32), R2 (44), R3 (60), R4 (76), each
"the Solution Architect, 24 Nov 2026"; and `JUDGEMENT-CALLS.md` PR-30 (`12992896`): "All fifteen ruled as recommended".
- **The one judgement call: R3's ruling is wider than the brief's R3 bullet.** The ruling is "(a), Platform, *with the
  guard*: lint A8's name check for `pp_settings` widened to the Vocabulary, Rules and Domain-model rings"; the brief says
  "A8 keeps its name check ... No code change". The brief also says "build only ... exactly as ruled", and NOTES-B1.9 §4
  reads R3 the same way, so I built the guard (a lint-scope change, C4 and C9; 0 offenders today, no allowlist line).
- **A8 would have gone dead for `pp_settings`:** `isAcquisitionOrAppState` returned false at `if (r != Adapters)` before its
  name test, so a Platform `pp_settings` would pass A8 silently. The three named stems are now matched before the ring
  test. **For B1.9:** the same hoist covers `Core/studio_profile` once it is Application in `App/` (the tool re-keys the name).

## 1. Check first (on 14f37426)

1. `ls src/Core` against `fileRings()`: 16 Platform stems, the three unplaced headers, `kf3_smoother`, `pp_settings`,
   `app_info`, `device_enumerator`, `studio_profile` (B1.9's), `CMakeLists.txt`, `tests/`. Nothing else.
2. Includes: `headless_probe.h` `<QByteArray>`, `<QtGlobal>`; `same_folder.h` `<QDir>`, `<QString>`; `version.h` none;
   **`pp_settings.h` only `<QSettings>`**. R1 and R3 buildable as ruled.
3. `count_includers.py`: `kf3_smoother` 2, `app_info` 2 (1 inside the set), **`pp_settings` 25** (the brief's 24 was on
   bdd04541; one more since). None in a Vocabulary, Rules, Domain-model or Analysis directory (tests are not linted).
4. A8's name check: kept and widened (§0).
- `context_check.py --list c`: **3** rows, the three `Ppcp` includes of `Core/pp_settings.h` (the brief's 5 predates B1.7).

## 2. What changed

- **Map** `tools/refactor/maps/B1.11-core.map`: `src/Core/kf3_smoother -> src/Kinematics/` (R2), `src/Core/app_info ->
  src/Gui/app/` (R4 part 1). Dry run `B1.11-dryrun.txt` = real run `B1.11-run-real.txt` (`--over-work`, the map was
  untracked): 3 moved; 4 include lines in 4 files (`club_rate_filter.h:39`, `pose_smoother.cpp:21`, `main.cpp:62` bare ->
  `app/app_info.h`, `app_info.cpp:25` `version.h` -> `../../Core/version.h`); 16 path mentions (CMakeLists 962, 963, 2828,
  3916; `cmake/pp_deps.h.in:5`; lint 2; allowlist 1 (`app_info.h` A6, re-keyed, count same); context 2; docs 5); 0 include
  dirs; 0 can't place; 0 errors.
- **Lint table hand edits** (`architecture_lint_test.cpp` `fileRings()` 202-205, 226-231): Platform entries for
  `Core/headless_probe`, `Core/same_folder`, `Core/version` (R1); `Core/pp_settings` Adapters -> Platform (R3); the re-keyed
  `Kinematics/kf3_smoother` and `Gui/app/app_info` entries deleted (their directory rules give the same rings, Rules and
  Adapters). The 16 Platform entries, `device_enumerator` and `studio_profile` stay; `dirRings()` `Core/` stays Vocabulary;
  the block comment says the rule flips after B1.9.
- **Lint A8 code** (`:77-81` header, `:429-450` `isSettingsHandle` + the hoist, `:590-593` the widened check, fixtures
  `:755-756`, `:778-787`, `:888-889`): any Vocabulary, Rules or Domain-model file including `Core/pp_settings` is an A8
  finding; Analysis still is; adapters and Platform pass. New self-test cases for each, and ring-table checks of the four.
- **`context_check.py` FILES:** P for the three headers and `pp_settings`; `kf3_smoother` deleted (S -> Diagnostics by
  `Kinematics/`, R2); `app_info` deleted (HUB by `Gui/`). `'Core/': S` stays (the flip's).
- **IM 1.12.8:** §1.4 A8 row; RING-PLATFORM (the three headers, `pp_settings`, and that the directory rule flips after
  B1.9); RING-RULES (`kf3_smoother` out of "to place", in `Kinematics`, Diagnostics); RING-ADAPTERS (`pp_settings` and
  `app_info` out of `Core`'s list; `Gui/app/app_info` with the rest of `Gui`); spanning table's `Core` row; footnote
  `[^sa24-r1-4]`; version line; change-log row. **Left for the flip:** RING-VOCABULARY's "the rest of `Core`" (it names the
  directory rule, which still says Vocabulary); "`Core`, but for `device_enumerator`" is not written yet.
- **Docs:** `testing_developer_guide.md:675` A8's sentence; `club_rate_filter_design.md:6,34` (tool). The deck: slide 11
  lists `Core` among the directories that span rings, still true (Platform + two adapters): **no deck change**.
- Real stores: none reachable. `pp_settings.h` is not edited (no behaviour change); its guard is `IsolatedStores`
  (`src/Core/tests/isolated_stores.h`), enforced by `store_isolation_lint_test` (passed).

## 3. Verify and configure

- **The move and the table edits** (`B1.11-verify-tool.txt`, through the lock): `--verify HEAD --allow-prose
  governance/artefacts/information_model.md --configure <scratch> --seed build/wt-37c/build --cmake-arg
  -DPINPOINT_BUILD_TOOLS=ON --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`: renames 3, 13 files, listed 21 (each table line),
  **other lines 0, VERIFY PASSED, configure app PASSED, configure tests PASSED, CONFIGURE PASSED** ("lm_repair enabled").
  That state is `B1.11-tool-part.patch`.
- **The whole tree** (`B1.11-verify.txt`, no configure: only a .cpp and a doc changed after): VERIFY FAILED on 24 other
  lines, exactly the A8 code and `testing_developer_guide.md:675`, i.e. `B1.11-hand-edits.patch` (lint + guide).
- `count_includers` after: `Kinematics/kf3_smoother` 2, `Gui/app/app_info` 2, `Core/pp_settings` 25, old stems 0.

## 4. Accept (R4 part 2 not done)

- Files in `src/Core` outside Platform: **2** (`device_enumerator`, Stage D; `studio_profile`, B1.9). Brief's 1 after the flip.
- `fileRings()` entries under `Core/`: **21 -> 22** (+3 R1, -2 moved). After B1.9 21, after the flip 1.
- `context_check.py --list c`: **3 -> 0** (the three `Ppcp` rows gone); (b) 66 -> 65, Platform 209 -> 240.

## 5. The Mac half (on 14f37426 + the change)

Every heavy job through `tools/method/one_at_a_time.sh`, `-j 3`, under `b111/dog.sh` (`ps -o etime=`).
- Builds (2, the budget): tests tree exit 0, 0 errors (`b111-build-tests.log`); app tree with `PINPOINT_BUILD_TOOLS=ON`,
  `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled", `b111-configure-tools.log`), exit 0, 0 errors (`b111-build-app.log`).
  `swing_window_parity_test`: PASS, 0 check failures (`b111-swing-window-parity.log`).
- Both lints on the real tree: pass (`b111-lint.log`); the new A8 self-test cases pass; no `note:` (no count dropped).
- **Plants** (`b111-lint-plant.log`, `b111-context-plant.log`), one per changed table, then removed (`git status` clean of
  them): `same_folder.h` + a Vocabulary include -> A1 "Platform → Vocabulary" (R1 entry live); `pp_settings.h` + a Vocabulary
  and a `Kinematics/kf3_smoother.h` include -> A1 ×2 (R3 entry live; kf3 is Rules); `Kinematics/kf3_smoother.h` +
  `Gui/app/app_info.h` -> A1 "Rules → Adapters" (both deleted entries fall to their folders); `Diagnostics/derived/`,
  `DomainModel/` and `Analysis/` files including `Core/pp_settings.h` -> A8 each (the widened guard and the kept one).
  Context check: `pp_settings.h` -> kf3 "Platform -> Diagnostics", kf3 -> `app_info` "Diagnostics -> Domain+Application",
  a `Ppcp` plant -> kf3 "Acquisition -> Diagnostics": the three FILES edits read.
- Named ctest (`b111-ctest.log`): **64/64 passed**: every `src/Core/tests` suite, every `ppcp_*` (interop and conformance
  included), `club_rate_filter`, `pose_smoother`, `kinematic_*`, `headless_probe_test`, `run_headless_selftest`, the four
  lints (architecture, design_system, store_isolation, session_setup), `artefact_paths_check`, `design_system_tokens_check`,
  `move_files_selftest`, `context_tree_test`, `im_excerpt_selftest`, `im_changelog_merge_selftest`, `namespace_collision_test`.
- **Parity:** `parity_baseline.py b111-core` exit 0; `parity_diff.py b1-base-2 b111-core`: **0 differences**.

## 6. Cabin run

`b111/cabin_wrap.sh` (B1.10's wrapper, log `b111-studio PC.log`); the permission check did not refuse it. Run
`studio PC-wt-37c-20261126-024139` (summary `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-024139.txt`, run dir kept on
the share): exit 12. Cabin HEAD 14f37426 + patch 9927ff7a… (18 files); build exit 0 (25 s); stores unchanged; checkout
restored clean; wall 17 min.
- **Pass 1: 347/347**, 1 not run (`session_list_test` L7a, #250, passed this time).
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231), `tst_session_report` page count (#253).
- Accept: no new failure against B1.7's and B1.10's runs.

## 7. Independent review

A fresh reviewer agent (not a fork), given the brief, COMMON, MOVES, the excerpt, the rulings, the order change, the diff
and these notes: `REVIEW-B1.11.md`, **SIGNED OFF**, no blocking finding. It re-ran the lint, `context_tree_test` and
`move_files_selftest` through the lock (3/3), `--list c` (0) and `count_includers`, and showed that without the hoist the
two Analysis fixtures fail and without the `else if` the three inner-ring fixtures fail. Its edge findings are filed as
one issue (§8).

## 8. Commit

Main still `14f37426` at commit time (IM 1.12.7 there, so 1.12.8 is free); the rebase was a no-op, so no re-run was
needed. Committed in wt-37c (`git add -A .`, 15 files: 3 R, 1 A, 11 M), message `B1.11-commit-msg.txt` re-read against
GIT-8/9/10/13 (no trailer; area `Platform:` as the coordinator's title gave it, new in the log). **`9a2150b9`** pushed to
main (fast-forward from `14f37426`) under GIT-2 after the sign-off, the Mac half and the studio PC run. Edge findings filed as
**#301** (`edge case`). wt-37c clean; app tree set back to tools off (`b111-configure-tools-off.log`).
- **For B1.9:** run it as briefed; A1 now passes `App/studio_profile.cpp -> Core/pp_settings.h` (Application -> Platform).
  The A8 hoist already covers `studio_profile` by name. **Then the deferred step:** `dirRings()` `Core/` -> Platform,
  delete the 16 + 3 + `pp_settings` Platform entries (and `studio_profile`'s, gone with B1.9), keep `device_enumerator`
  with a Stage D comment; `context_check.py` `'Core/'` S -> P with the same deletions; IM RING-PLATFORM "`Core`, but for
  `device_enumerator`", RING-VOCABULARY's "the rest of `Core`" out; accept: `Core/` entries -> 1.

## EDGE (filed together, §8)

- Application-ring files (`src/App`, `Gui/shot`) may now include `pp_settings` with no lint stopping them: as ruled (B1.9
  needs it), but R3's "what it gives up" doesn't say so. Count today 0.
- The `Core/x.cpp` fixture is labelled "from Platform" but is Vocabulary until the flip (it checks A1 only).
- RING-VOCABULARY's "the rest of `Core`" names no file until the flip (deliberate).
- A8's summary label still reads "Analysis past the Recording contract" (the finding text says which case).
- `docs/research/context_map/check_includes.py` (the research copy) keeps the old keys; no change needed.
- The move tool does not re-key the stem inside `isSettingsHandle`; it matters only if `pp_settings` ever moves.

## 9. The held last step: `Core/` becomes Platform (26 Nov 2026, after B1.9)

Lane 1 (`build/wt-37c`), unattended. wt-37c was clean at detached HEAD = origin/main `df996920` (B1.9; IM 1.12.9), no
stash; fetched, `reset --hard origin/main`, `clean -fd`. Ruling: R4 part 2 (`platform_b1_placements.md:76`).
- **Check:** `ls src/Core` = 21 stems: the 20 Platform entries (16 + R1's 3 + `pp_settings`) and `device_enumerator`;
  `CMakeLists.txt`, `tests/` (not linted). Nothing else, so **no real file changes ring**: the flip changes only the
  default for a new file.
- **Lint** (`src/Gui/tests/architecture_lint_test.cpp`): `dirRings()` `Core/` Vocabulary -> Platform (`:169`);
  `fileRings()` loses the 20 Core Platform entries, keeps `Core/device_enumerator` Adapters with a Stage D comment
  (`:204-206`); header prose `:90-92`. New fixture (`:765-770`): `Core/x.cpp` including a Vocabulary header is A1
  "Platform → Vocabulary" (passed before the flip), and `device_enumerator.cpp` including `Buffer/source_ring.h` passes;
  ring-table check gains `Core/zz_new_file.h` == Platform, `Core/device_enumerator.h` == Adapters (`:865`).
- **`tools/refactor/context_check.py`:** `'Core/'` S -> P (`:40`); the 20 `Core/*: P` entries go; `'App/studio_profile':
  HUB` goes (redundant with `App/`, #302's first item); `Core/device_enumerator: ACQ` stays.
- **IM 1.12.10** (main 1.12.9): version line; RING-PLATFORM "`Core`, but for `device_enumerator`"; RING-VOCABULARY's
  "the rest of `Core`" out; spanning table's `Core` row; `[^sa24-r1-4]`'s tail; one change-log row. **Deck:** slide 11
  lists `Core` among directories that span rings: still true (Platform + `device_enumerator`): **no deck change**.
  Left alone: the frozen `03-target-architecture.md:246`, `platform_refactor_assessment.md`, `B1.11-core.map:5`'s comment
  (a record of that map), the research copy `docs/research/context_map/check_includes.py`.
- **Accept:** files in `src/Core` outside Platform **1** (`device_enumerator`); `fileRings()` entries under `Core/`
  **21 -> 1**; `context_check.py --list c` **0** (unchanged); whole output byte-identical to main's script on the same tree
  ((b) 65, Platform 240; `b111b/context_check*.out`).
- **Mac (on df996920 + the change), every heavy job through `tools/method/one_at_a_time.sh`, `-j 3`, under
  `b111b/dog.sh`:** tests tree exit 0, 0 errors (`b111b-build-tests.log`); app tree with tools + lm_repair ON, exit 0
  (`b111b-build-app.log`, "lm_repair enabled"). Named ctest 10/10 (`b111b-ctest.log`): the four lints, `artefact_paths_check`,
  `design_system_tokens_check`, `context_tree_test`, `move_files_selftest`, `im_excerpt_selftest`,
  `im_changelog_merge_selftest`. Lint debt line identical to B1.9's (419 in 134 lines; allowlist 176; no `note:`).
- **Plants** (`b111b-plants.log`, `b111b/plants.sh`), then removed (`git status`: the 3 files only): new
  `Core/zz_b111b_plant_vocab.h` -> A1 "Platform → Vocabulary" and "Platform → Rules"; new `Core/zz_b111b_plant_adapter.cpp`
  including `device_enumerator.h` -> A1 "Platform → Adapters" (the exception is live); `device_enumerator.cpp` +
  `../Video/display_tone.h` -> no finding (Adapters -> Adapters). Context check: the three plants are class c, "Platform
  includes a context".
- **Parity:** `parity_baseline.py b111b-core` exit 0; `parity_diff.py b1-base-2 b111b-core`: **0 differences**.
- Real stores: none reachable (a test table, a Python script and a doc; no product code). App tree set back to tools
  off (`b111b-configure-tools-off.log`). Diff: `b111b/B1.11-flip-diff.patch`.
- **#301 / #302 closed by this step:** #301's "`Core/x.cpp` fixture labelled from Platform but Vocabulary" and
  "RING-VOCABULARY's the rest of `Core` names no file"; #302's `context_check.py:65-66` entry and the IM's "waits for
  B1.9" wording.
- **EDGE (scope notes):** a new `Core` file is now Platform, so A5 exempts it (Platform helpers may touch the disk) and
  A7 does not measure it (rings 1-2 only); as ruled, and true of every Core file already.
- **Cabin run** (`b111b/cabin_wrap.sh`, 2 h watchdog, log `b111b-studio PC.log`): `studio PC-wt-37c-20261126-033542`, exit 12;
  studio PC HEAD df996920 + patch c00ab88c… (3 files); build exit 0 (16 s); stores unchanged; checkout restored; wall 17 min.
  Pass 1 **346/347**: the one failure is **new**, `diag_stage_panel_test` "TRASH before the owner settled: the trash goes
  through though the report holds a use" (`diag_stage_panel_test.cpp:~1500`). The test builds none of the changed files;
  on the Mac `--repeat until-fail:3` passed 3/3 (`b111b-diagstage.log`). Judgement call: unrelated, filed **#303** (bug),
  not blocking. Pass 2 **41/43**, the standing two (#231, #253).
- **Review:** fresh agent, `REVIEW-B1.11b.md`, **SIGNED OFF**, no blocking finding (it checked every Core stem's ring old
  vs new, that both new fixtures fail on the old table, context tags identical over 1055 files). Fixed from its edge list:
  lint header `:90-92` (my rewording misread 03; back to the original sentence plus the R4 one) and the change-log row
  now lists the footnote edit; lint rebuilt (target only) and re-run after, pass (`b111b-lint2.log`); the studio PC ran the
  patch before those two comment/doc fixes. Commit message given the studio PC result. The rest filed as **#304** (`edge case`).
- **Commit:** main still df996920 (1.12.10 free), no rebase needed. **`176e6d44`** pushed to main (fast-forward) under
  GIT-2, message `b111b/commit-msg.txt` re-read against GIT-8/9/10/13 (area `Platform:`, in the log; no trailer).
  wt-37c clean; app tree back to tools off.
