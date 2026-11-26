# B1.17 · The store doors' A5 lines cleared, by R9 (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. Before the reset wt-37c was clean, detached at origin/main `f38e2339`
(B1.16), no stash. Fetched (origin/main still `f38e2339`), `reset --hard origin/main`, `clean -fd`; excerpts regenerated
(`make_im_excerpts.sh build/wt-37c`, the information model's sections this brief names (the excerpt files were local) read).

## 0. The rulings (checked before any edit)
- **R9** (`docs/implementation/platform_b1_placements.md` §R9), **Ruled:** "(a), each door named in the IM as part of its
  store; the lint exempts it like the rest of the store, and the counted-door list is deleted (the Solution Architect,
  24 Nov 2026)". This is the brief's route (a). The brief's HARD stop does not apply.
- **J-88** (`docs/implementation/swe-audit/06a-judgement-calls.md` :536), approved as J-95 (:569, "J-88: lint A5 is
  widened at 5.9"): the two session-list doors counted. **J-110** (`platform_refactor_assessment.md` :88, :309, ruled
  24 Nov): A5 counts `rule_data_reads.cpp`, "with one allowlist line cleared in Stage B".
- **Reading (logged, for the reviewer):** the brief's step 1 (add `rule_data_reads` to `countedStoreDoor()`) and R9
  ("the counted-door list is deleted") net out to deleting the function in this same change, so the stem is never
  added. J-110's "counted … and cleared in it, no line" is met by the decision being made: the door is named in the IM as
  part of `RuleDataStore`, and A5 exempts it with the store. No line was ever added for it.
- **IM version:** main was at 1.12.13 (B1.16) when I started. Main's `9f086b4d` then took 1.12.14 (Buffer's stream types), so at
  the rebase this change became **1.12.15** (§8). The line numbers below are from before the rebase.

## 1. Duties inventory (AG-10), before the edit
| Door (`src/Stores/`) | What it reads | Beside | Callers | Tests |
|---|---|---|---|---|
| `session_list_reads.{h,cpp}` | the session folder's mtime (`QFileInfo::lastModified`), the drawer's order and last fallback date | `SessionRepository` (the session store) | `Gui/session/session_list_presenter.{h,cpp}` (`SessionListFacts`), whose rows `session_review_controller.cpp` and `session_report_list_model.cpp` read | `session_list_test`, `report_list_test` |
| `session_index_summary.{h,cpp}` | the swing roll-up (`SwingDocReader::readSwingSummary`), size on disk (`SessionArchiver::folderBytes`), work-on 1 and the `gates` block of `work_ons.json` (through `SessionDiagnosticsFiles`) | `SessionStore`, set by `setSummaryReads` | `Gui/main.cpp:370,413` (session and athlete stores); `athlete_store.cpp:328` passes it on | `session_gates_test`, `session_list_test`, `report_list_test` (each sets it as `main.cpp` does), `athlete_test_library.h` |
| `rule_data_reads.cpp` | `RuleDataStore::readFile`, `shippedPath` and the norm stack's reads (FILE-APP-02, 04, 05), through the `PINPOINT_CORE_*` seam | `RuleDataStore` (its members; a TU of its own so the light tools link it without the settings file) | `Diagnostics/norms/merged_norm_provider.cpp`, `rule_data_store.cpp`, the offline tools and light tests' link lists | `rule_data_test`, `norm_*`, `shared_norm_provider_test` |
| `countedStoreDoor()` (the lint, deleted) | made a door's file reads and repository includes show as A5 findings, so a new door was seen and decided | — | `a5Exempt()` | the fixture at lint :813-817 |

- **Where each duty goes.** The doors' code, callers and tests: unchanged (no source file outside the lint moves or
  changes). `countedStoreDoor()`'s duty, "a new door is seen and decided", goes to the IM: §1.4's A5 row names each door
  with its store (R9's "naming each in the IM is that decision"). **Said plainly:** from this change a new file in
  `src/Stores/` is exempt from A5 by default, as every store file was before J-88; nothing in the lint now flags a new door.
- The fixture is **re-pointed, not deleted**: the three doors' file read and repository include now count 0, and the same
  read in a `Gui` file still counts 1.

## 2. What changed (3 files; `B1.17-diff.patch`)
- **Lint** `src/Gui/tests/architecture_lint_test.cpp`: `countedStoreDoor()` deleted; `a5Exempt()` (:342) is again
  `Stores/` or Platform. Header comment (:63-67) and the A5 comment above `a5Exempt()` say what a store door is and that
  the counted list is deleted (R9). Fixture (:807-813), re-pointed: a file read in `session_list_reads.cpp`, a
  repository include and a `QFile` in `session_index_summary.cpp`, a `QFile` and a `QDir` walk in `rule_data_reads.cpp`
  all count **0**; the same read in `Gui/session/session_list_presenter.cpp` and a store include in a `Gui/session` file
  count **1** each.
- **Allowlist** `src/Gui/tests/architecture_allowlist.txt`: the two `#224` lines deleted (`session_list_reads.cpp` A5 1,
  `session_index_summary.cpp` A5 3: **4 findings → 0, 2 lines → 0**); the header's "second exception" (:21-25) records it.
  Entry lines 134 → 132; `# platform refactor (#224)` entry lines **34 → 32**, P2's decided figure (Stage B's 5 lines
  cleared: B1.13, B1.14 and these two; R8's A7 line is not tagged #224). **No line added**; `rule_data_reads` never got one.
- **IM 1.12.15 (written as 1.12.14 before the rebase)** `governance/artefacts/information_model.md`: the version line (:6); §1.4's A5 row (:144) names the three
  doors with their stores ("`session_list_reads` and `session_index_summary` are the session store's, `rule_data_reads`
  is `RuleDataStore`'s. No file in `Stores` is counted by name"); §11's P11 row (:1252) records the two lines deleted;
  change-log row 1.12.15 (:2108 after the rebase); new footnote `[^sa24-r9]` (:1991) quoting R9 and citing J-88/J-95 and J-110. §11's
  session-list row (:1315) and 7.1's row (:1317) already call the doors the store's file work: unchanged.
- Not touched: the three doors, their callers and tests; `isRepositoryHeader()` (adding the doors' headers would make
  `session_list_presenter.h`'s include a new A5 finding, which R9 doesn't rule: EDGE 1); the placements doc (B1.16 did not
  mark R6 built there either); `tools/refactor/move_files.py`'s `LINT_TABLE_FUNCS` still names `countedStoreDoor` (EDGE 3).
- **Real stores in reach:** none. No product code changed; the lint reads the source tree. The doors' tests
  (`session_list_test`, `report_list_test`, `session_gates_test`, `rule_data_test`) each use a temporary directory, and
  `store_isolation_lint_test` (the isolation helper's guard) passed. Parity ran on share copies of the parity set.

## 3. Mac (builds 2 of 2, each under `b117/dog.sh`, 2 h, through `tools/method/one_at_a_time.sh`, -j 3)
- `b117/build2.sh`: tests tree exit 0 (24 steps, the lint relinked; `b117-build-tests.log`), app tree exit 0
  (`b117-build-app.log`), 0 errors.
- **ctest** (`b117/ctest.sh`, `b117-ctest.log`, -LE qml_ui): the brief's regex plus the four lints, the IM's checks and
  the doors' other tests: **17/17 passed** (architecture, design_system, store_isolation and session_setup lints;
  `im_excerpt_selftest`, `im_changelog_merge_selftest`, `artefact_paths_check`; `session_list`, `report_list`,
  `session_gates`, `session_summary`, `rule_data`, `norm`, `norm_pack`, `shared_norm_provider`, `norm_measure_source`,
  `wrist_norm_render`).
- **Plants** (`b117/plants.sh`, `b117-plants.log`, `b117/plants-lint.out`), one lint run, then removed: exit 8 (failed) with
  exactly two findings: P1 `Gui/session/zz_b117_p1.cpp:2` "file access outside the repositories (P11 …)"; P3
  `Diagnostics/norms/zz_b117_rule_data_reads.cpp:2` (a door's name outside `Stores`) "file I/O in a rules directory". P2,
  one more `QFile` in each of the three doors: **no finding** (the ruled route). The lint's debt line with the plants: "415 findings allowed in 132 allow-list lines (… platform refactor (#224): 32 …)" (419 in 134 before). After removal `git status` shows only
  the item's 3 files, and the lint exit 0.
- **Parity:** `parity_baseline.py b117-store-doors` exit 0 (8 sessions; `b117-parity.log`); `parity_diff.py b1-base-2
  b117-store-doors`: **0 differences** (`b117-parity-diff.log`). Run kept at `scratch/parity/b117-store-doors/out`.

## 4. Cabin run
`b117/cabin_wrap.sh` (B1.16's wrapper, 2 h ceiling; log `b117-studio PC.log`), launched 06:38 with the edits final, before
the plants (the patch is taken at launch: the 3 files). Run `studio PC-wt-37c-20261126-063840` (summary
`build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-063840.txt`): exit 12. Cabin HEAD f38e2339 + patch 55b8a969… (3
files); build exit 0 (jom -j16, 14 min 23 s); stores unchanged; checkout restored clean; wall time 32 min.
- **Pass 1: 346/347**, 1 not run. `session_list_test` L7a (the Windows trash, "could not"): **#250**, open, intermittent, the same
  line as B1.16's and earlier runs on main. It passed on the Mac (§3). (`paced_connect_queue_test`, #263, passed this time.)
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231) and `tst_session_report` page count (#253).
- **Accept: no new failure** against B1.16's studio PC run (the same three).
- The review's wording edits to the IM came after the patch was taken (prose only): `architecture_lint_test`,
  `im_excerpt_selftest`, `im_changelog_merge_selftest` and `artefact_paths_check` re-run on the Mac, 4/4 (`b117-ctest-after-review.log`).

## 5. Review (AG-21)
Fresh reviewer agent (not a fork), given the diff and these notes: `REVIEW-B1.17.md`, **SIGNED OFF** (on condition the
studio PC run passes), no blocking finding. It confirmed tree = patch = the studio PC's patch (sha256), the route is R9 (a)
exactly, §0's reading, the Accept row (its own count: `#224` entry lines 34 → 32, all 134 → 132, none added), the
fixture (it would fail under the old code), and re-ran 5 suites (5/5). **Fixed in the item (wording only):** the 1.12.14
row's "counted … is cleared" (the lint never counted `rule_data_reads`: now "ruled to be counted (J-110), is never
counted"), its "No code outside the lint changes; no file or behaviour changes", the footnote's quote capitalised as R9
has it; these notes' EDGE list and #224 sentence (§6, §7). The IM edits came after the studio PC's patch was taken: prose
only, re-checked on the Mac (§4). The patch was re-saved.

## 6. EDGE (filed as one issue)
1. `isRepositoryHeader()` does not list the doors' headers, and `Gui/session/session_list_presenter.h:52` includes
   `Stores/session_list_reads.h` directly (a view including a store's door). Listing it would make a new A5 finding;
   R9 doesn't rule it. With `session_list_reads.h`'s own open question (the mtime on the entry), for the Solution Architect.
2. Ruled consequence: nothing in the lint now flags a new store door in `src/Stores`, and nothing checks that a new door
   is named in the IM (R9: "naming each in the IM is that decision").
3. AG-14: the A5 row's definition ("a store door, a store's file work in a file of its own") is new wording, under
   `[^sa24-r9]`; for the Solution Architect's ratification list.
4. Stale plan text: `platform_refactor_assessment.md` :88, :239, :304, :309, :722 still say A5 counts `rule_data_reads`
   or list J-88's lines as open (B1.13, B1.14, B1.16 also left the plan alone); its `[^j110]` says "Not yet ruled"
   while :309 says ruled (already wrong on main).
5. `tools/refactor/move_files.py:84`: `LINT_TABLE_FUNCS` still names `countedStoreDoor` (harmless: a missing function is skipped).

## 7. #224: the closing sentence for J-88's doors
"J-88's store doors are closed by B1.17 (d4622819): by R9 each door is part of its store, named so in the information model
(1.12.15) and exempt from A5 with the rest of `src/Stores`; the lines for `session_list_reads.cpp` (A5 ×1) and
`session_index_summary.cpp` (A5 ×3) are deleted, and `rule_data_reads.cpp` (J-110) never needed one."

## 8. Committed and pushed
**`d4622819`** on main (GIT-2). Main had moved to `126e9cf7` (`9f086b4d` Governance, `126e9cf7` SwingLab: docs, governance
and tools only, no `src`). Rebased: the IM's version line and change-log row clashed with main's 1.12.14. The row went
through `im_changelog_merge.py`, the version line was resolved by hand, and both are now **1.12.15**. The rebased diff
equals the reviewed patch apart from the version number. On the rebased tree, the item's 17 suites passed again, 17/17
(`b117-ctest-rebased.log`; no rebuild, since nothing in `src` came in). Message: `b117/commit-msg.txt`, re-read against
GIT-8/9/10/13 (area `Platform:`, no trailer). Edges are filed as **#310** (`edge case`, `documentation`, `priority: low`).
wt-37c is clean at d4622819. The parity tree `scratch/parity/b117-store-doors` is kept, as the earlier items' were.
