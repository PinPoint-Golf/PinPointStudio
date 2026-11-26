# B1.9 · the studio profile to `src/App/`: STOPPED at the brief's check 2 (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. **Nothing was committed or pushed, so there is no sha.** The item stopped
at its own stop condition. Before the reset, wt-37c was clean, at HEAD = origin/main `14f37426` (B1.10), with nothing
off main and no stash. I fetched, ran `reset --hard origin/main` (`14f37426`) and `clean -fd`, then regenerated the
excerpt (`make_im_excerpts.sh build/wt-37c`). After the trial (§3), wt-37c was reset to `14f37426` again:
`git status` shows 0 lines.

## 1. Check first (on 14f37426)

1. **References** (`grep -rn studio_profile src tools tests cmake CMakeLists.txt`). The brief's list, at today's
   lines: `Core/studio_profile.cpp:19`, `Gui/main.cpp:90` (bare), `Gui/report/session_report_model.cpp:64`,
   `Gui/tests/report_studio_test.cpp:37` (bare), `Core/tests/isolated_stores_test.cpp:27`, root `CMakeLists.txt:748`,
   and the lint's `fileRings()` at `:228` and A8 at `:433`. Not in the brief's list: `Core/tests/CMakeLists.txt:72,74`,
   `Gui/tests/report_studio_tests.cmake:4,14` and `tools/refactor/context_check.py:65`. The tool rewrites all of them.
2. **The ring change. The brief's stop condition fires here.**
   - `count_includers.py Core/studio_profile`: 5 files. Production 3: `main.cpp`, `session_report_model.cpp` and
     the file itself. Tests 2. None is in Rules, Vocabulary or the Domain model, so that half is fine.
   - `studio_profile.cpp` includes three project headers: `atomic_write.h` (Platform), `pp_debug.h` (Platform) and
     **`pp_settings.h` at `:23` (`Core/pp_settings`, Adapters in `fileRings()`)**. `ppSettings()` is called at
     `:170` and `:198`, and the file also uses `QFile` and `QDir` (A5, §1.3).
   - After the move, `App/studio_profile.cpp` is in the Application ring (4), and `Core/pp_settings.h` is still in
     Adapters (5), so **A1 fires**. The brief says to stop and log it.
3. **A5:** `Core/studio_profile` is not exempt, and it has allowlist lines. There are two: `:76` (A5, 9) and `:131`
   (A6 on the `.h`, 4). The tool re-keys both, and no line is added.

## 2. Dry run on today's main (`B1.9-dryrun.txt`)

The map is `src/Core/studio_profile -> src/App/`. It is parked at `b19/B1.9-studio-profile.map` and is not in the
tree. Dry-run totals: 2 files moved, 7 include lines rewritten in 5 files (3 of them outward includes inside the
moved file, including `"pp_settings.h" -> "../Core/pp_settings.h"`), 14 path mentions (build 5, lint 2, allowlist 2,
context 1, docs 4), 0 include directories, 0 can't place, 0 errors.

## 3. Proof that A1 fires (a trial, then reverted)

- I ran the real move in wt-37c with `--over-work` (`B1.9-run-trial.txt`: 2 moved, 13 rewritten). Then I ran
  `ctest -R '^architecture_lint_test$' -V` through `one_at_a_time.sh` with a 5-minute watchdog, using the existing
  tests tree and no rebuild (`B1.9-lint-trial.log`). The result is exit 8, with **one failure, the only one**:
  `App/studio_profile.cpp:23  Application → Adapters: includes Core/pp_settings.h` ("1 finding, 0 allowed").
  A2 to A8 all pass, A5 included, with its re-keyed allowlist line.
- **Why the trial is valid:** the lint reads the source tree at run time (`PP_SRC_ROOT` = wt-37c's `src`). The
  binary was not rebuilt, so it still held the old `Core/studio_profile` key. That key matches nothing now, so
  `App/studio_profile.cpp` took its ring from the `App/` directory rule. That is exactly the state the brief's hand
  edit (deleting the entry) produces. The tool alone, rebuilt, would have re-keyed the entry to `App/studio_profile`
  (Adapters), and A1 would have passed and hidden the problem.
- I then reset wt-37c to `14f37426`, with nothing left behind.

## 4. For the coordinator and the Solution Architect: the order is inverted

- **R3 is already ruled.** On 24 Nov the Solution Architect ruled option (a): `pp_settings` becomes Platform, and
  A8's name check is widened to the Vocabulary, Rules and Domain-model rings. Sources:
  `docs/implementation/platform_b1_placements.md`, "R3", and `JUDGEMENT-CALLS.md` PR-30 at `12992896`. R3 is built
  in **B1.11**. But `PLAN-B1.md:21` makes B1.11 depend on B1.9, while B1.9 can pass A1 only once `pp_settings` is
  Platform. Each item waits on the other.
- **Recommendation:** run B1.11 (or its R3 part alone) before B1.9. B1.9 then runs exactly as briefed, with no new
  ruling needed. B1.11's accept ("Core's files outside Platform → 1") would read 2 until B1.9 lands, because the
  studio profile would still be in Core.
- **I did not fold R3 into B1.9.** R3 is B1.11's work. It is a hand change to a ring, plus the widened A8 guard, and
  B1.11 is reviewed independently. The alternatives are also ruled out: an A1 allowlist line is forbidden (COMMON),
  and injecting the settings is a code change, which a move does not allow (MOVES "Not in a move").

## 5. What was not done, and why

The real move, `--verify --configure`, the Mac builds (0 of 1 used), the studio PC run (none launched), the named suites,
parity, the independent review, the IM edit (the IM stays at 1.12.7), the commit and the edge issue (there are no
review findings). The only heavy job was the one lint ctest in §3.
Real stores touched: none (the trial ran the lint only, which reads source text).

Paths: `build/platform/B1/NOTES-B1.9.md`, `B1.9-dryrun.txt`, `B1.9-run-trial.txt`, `B1.9-lint-trial.log`,
`b19/B1.9-studio-profile.map`.

---

# B1.9, second attempt: the move, after B1.11 (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. Status before reset: clean, detached HEAD = origin/main `9a2150b9` (B1.11),
nothing off main, no stash. `fetch`, `reset --hard origin/main` (`9a2150b9`), `clean -fd`; excerpt regenerated
(`make_im_excerpts.sh build/wt-37c`). **One lane (MOVES):** wt-stack clean; wt-5b (at `027f9cbf`) has 4 modified files and
1 new one under `governance/controls/` and `src/Gui/tests/` (`session_setup_lint_test.cpp`, `CMakeLists.txt`,
`qml_rule_allowlist.txt`). None of them is a file this move rewrites, so I went on and left it alone; it rebases over this.

## 6. Check first (on 9a2150b9)

1. References: as §1, at the same lines but the lint's (`fileRings()` `:235`, A8 `:440`) and `context_check.py:66`.
2. Ring change: `count_includers` 5 files (production 3, tests 2), none in Rules, Vocabulary or the Domain model.
   `studio_profile.cpp`'s project includes: `atomic_write.h`, `pp_debug.h`, `pp_settings.h`, **all Platform now** (B1.11,
   R3). So A1 passes, and the stop condition no longer fires.
3. A5: not exempt; two allowlist lines (`:76` A5 9, `:131` A6 4), re-keyed by the tool, none added.
- **The accept count in the task is stale.** "Core's Adapters entries 4 -> 3 (`device_enumerator`, `app_info`, `pp_settings`
  left)" was written on `bdd04541`. B1.11 has since moved `app_info` out and made `pp_settings` Platform. On `9a2150b9`
  Core has **2** Adapters entries (`device_enumerator`, `studio_profile`). After this move it has **1** (`device_enumerator`).

## 7. The move

- Map `tools/refactor/maps/B1.9-studio-profile.map` (the parked one): `src/Core/studio_profile -> src/App/`.
- Dry run on today's main (`B1.9-dryrun-9a2150b9.txt`) = real run (`B1.9-run-real.txt`, `--over-work` because the map was
  untracked); `diff` of the two is the "done" line only. 2 moved, 7 include lines in 5 files (3 outward), 14 path
  mentions (build 5, lint 2, allowlist 2, context 1, docs 4), 0 include dirs, 0 can't place, 0 errors.
- Hand edits (`architecture_lint_test.cpp`): `fileRings()` loses its `Core/studio_profile` (re-keyed `App/...`) Adapters entry,
  so `App/`'s directory rule gives Application; the table comment `:205-207` says so. A8's name stays, re-keyed to
  `App/studio_profile` by the tool (`:439`).
- Left as the tool wrote it: `context_check.py:66` `'App/studio_profile': HUB`. It matches `App/`'s own tag, so it is now
  redundant; the brief lists no deletion there. B1.11 §8's held step plans to drop it with the other Core entries.
- No code change inside the two files: their diff is the three outward include paths only.
- IM 1.12.9 (main is 1.12.8): RING-ADAPTERS loses both mentions; RING-APPLICATION's `src/App` files cell gains it;
  the spanning table's `Core` row (it named the studio profile as still in Core); version line; one change-log row;
  footnote `[^sa24-q11]` (the plan's "Rulings of 24 Nov", Q11). The tool re-keyed FILE-APP-06's writer and §11's
  `StudioProfile` row. `docs/design/session_report_design.md:1023` (tool). **Deck:** no slide names Core's files or the
  studio profile's ring (slide text searched): **no deck change**.
- `--verify HEAD --allow-prose <IM> --configure <scratch> --seed build/wt-37c/build --cmake-arg -DPINPOINT_BUILD_TOOLS=ON
  --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`, through the lock (`B1.9-verify.txt`): renames 2, 14 files, listed 6 (the map,
  the IM prose, the three comment lines, the deleted table line), **other lines 0, VERIFY PASSED, configure app PASSED,
  configure tests PASSED, CONFIGURE PASSED** ("lm_repair enabled"). One state: every edit was in before the verify.
- `count_includers` after: `App/studio_profile` 5 files (production 3, tests 2), `Core/studio_profile` 0.
- The whole diff: `B1.9-diff.patch` (15 files: 2 R, 1 A, 12 M).

## 8. Accept

- `Core`'s Adapters entries in `fileRings()`: **2 -> 1** (`device_enumerator` left); see §6 for why not 4 -> 3.
- A8 still names the studio profile (`App/studio_profile`), proved by the plant below.
- Allowlist: 176 lines before and after; the two studio-profile lines re-keyed, counts 9 and 4 unchanged. The lint's
  debt line is identical to B1.11's (419 findings in 134 lines, every tag the same); no `note:`.

## 9. The Mac half (on 9a2150b9 + the change)

Every heavy job through `tools/method/one_at_a_time.sh`, `-j 3`, under `b19/dog.sh` (`ps -o etime=`).
- Builds (2): tests tree exit 0, 0 errors (`b19-build-tests.log`); app tree configured with `PINPOINT_BUILD_TOOLS=ON`,
  `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled", `b19-configure-tools.log`), exit 0, 0 errors (`b19-build-app.log`).
- Lints on the real tree (`b19-lint.log`): architecture, design_system, store_isolation, session_setup and
  `context_tree_test`, 5/5.
- **Plants** (`b19-lint-plant.log`), then removed (`git diff HEAD -M` = `B1.9-diff.patch`, byte for byte):
  `src/Analysis/zz_b19_plant_a8.cpp` including `../App/studio_profile.h` -> **A8** "Analysis reaches past the Recording
  contract: includes App/studio_profile.h" (Analysis and App are both Application, so only the name catches it);
  `App/studio_profile.cpp` + `#include "../Export/athlete_files.h"` -> **A1** "Application → Adapters" (the ring now comes
  from `App/`; the old entry would have made it Adapters and passed). The same plant showed A5's re-keyed line live
  (9 allowed, the plant's repository include the 10th).
- `context_check.py --src build/wt-37c/src`: (b) 65, Platform 240, unchanged from B1.11.
- Named ctest (`b19-ctest.log`, `-LE qml_ui`): **14/14 passed**: `report_studio_test`, `session_report_test`,
  `app_state_test`, `isolated_stores_test` (it compiles the moved `.cpp`), the four lints, `artefact_paths_check`,
  `design_system_tokens_check`, `im_excerpt_selftest`, `im_changelog_merge_selftest`, `move_files_selftest`,
  `context_tree_test`.
- `artefact_paths_check.py`: 3,579 files, 0 stale.
- **Parity:** `parity_baseline.py b19-studio` exit 0 (`b19-parity.log`); `parity_diff.py b1-base-2 b19-studio`:
  **0 differences** (`b19-parity-diff.log`).
- Real stores in reach: the settings store (SET-20, `Studio/*`) and `<AppData>/studio/` (FILE-APP-06), through
  `report_studio_test`. Its guard: `pinpoint::test::IsolatedStores` (`report_studio_test.cpp:57`, the first member) and
  `PINPOINT_STUDIO_DIR` set to a `QTemporaryDir` (`:81`); `store_isolation_lint_test` passed. The move changes no code
  that reaches them.

## 10. Cabin run

`b19/cabin_wrap.sh` (B1.11's wrapper, a 2 h ceiling; log `b19-studio PC.log`); the permission check did not refuse it.
Launched only once the tree was final (plants out). Run `studio PC-wt-37c-20261126-031002` (summary
`build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-031002.txt`, run dir kept on the share): exit 12. Cabin HEAD 9a2150b9 +
patch af2d1fc6… (17 files); build exit 0 (37 s); stores unchanged; checkout restored clean; wall 17 min.
- **Pass 1: 346/347**, 1 not run; the one failure is `session_list_test` L7a (#250), as in B1.10's run.
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231), `tst_session_report` page count (#253).
- Accept: no new failure against B1.10's and B1.11's runs.

## 11. Review and EDGE

Fresh reviewer agent (not a fork): `REVIEW-B1.9.md`, **SIGNED OFF**, no blocking finding; it re-ran the lint,
`report_studio_test` and `isolated_stores_test` (3/3). Its 1-3 (commit message: a pre-written studio PC sentence, "owns them",
the new `App:` area, so the title uses `Platform:` as B1.11 did, against the coordinator's suggested `App:`; GIT-8) and 4
(this section) are fixed. EDGE, filed as one issue:
- `context_check.py:65-66`: the entry sits under "to place" though Q11 ruled it; drop it in the Core-flip step.
- `docs/implementation/platform_e_host_settings.md:201` (proposed Stage E page) greps `src/Core/studio_profile.h`.
- `docs/research/context_map/check_includes.py:56` keeps the old key (research copy).
- IM `:435` RING-PLATFORM and `[^sa24-r1-4]` still say the Core rule waits for B1.9: true until the flip.

## 12. Commit

Main still `9a2150b9` at commit time (IM 1.12.8 there, so 1.12.9 is free); the rebase was a no-op, so no re-run.
Committed in wt-37c (`git add -A`, `build/` ignored; 15 files: 2 R, 1 A, 12 M), message `B1.9-commit-msg.txt`, re-read
against GIT-8/9/10/13 (no trailer). **`df996920`** pushed to main (fast-forward from `9a2150b9`) under GIT-2, after the
sign-off, the Mac half and the studio PC run. Edge findings filed as **#302** (`edge case`). wt-37c clean; app tree set back
to tools off (`b19-configure-tools-off.log`). Configure scratch deleted. Parity run tree `scratch/parity/b19-studio` kept,
as the earlier items' were.
- **For the coordinator:** (1) the accept count in the task was stale: it is 2 -> 1, not 4 -> 3 (§6). (2) B1.11's held step
  (the `Core/` rule becomes Platform) is unblocked now. (3) The title's area is `Platform:`, not the suggested `App:`
  (GIT-8: the area is reused).
