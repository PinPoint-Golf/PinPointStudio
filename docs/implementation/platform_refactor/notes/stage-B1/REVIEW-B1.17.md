# REVIEW · B1.17 · the store doors' A5 lines (independent reviewer, AG-21; 26 Nov 2026)

Worktree `build/wt-37c`, detached at origin/main `f38e2339`, 3 files modified, index empty. Read: the brief, COMMON,
R9 (`platform_b1_placements.md` :133-147), J-88 (06a :536), J-95 (:567-569), J-110 (assessment :88, :309), NOTES,
the patch, the IM hunks only (AG-32), the logs and `b117/` scripts. Nothing edited, nothing rebuilt.

## What I checked and saw
1. **Tree = patch.** `git -C build/wt-37c diff` is byte-identical to `B1.17-diff.patch`. The studio PC's `wip.patch`
   has the same sha256 (`55b8a969…`).
2. **Route = R9 (a), exactly.** `countedStoreDoor()` is deleted. `a5Exempt()` is `Stores/` or Platform again, so the
   doors are exempt for both file access (:554) and repository includes (:542). The IM's A5 row names each door with
   its store, using R9's own pairing (`session_list_reads` and `session_index_summary` with the session store,
   `rule_data_reads` with `RuleDataStore`). NOTES §0's reading holds on R9's own text: R9 was ruled after J-110, it
   cites J-110, and it names `rule_data_reads` in option (a). Adding the stem and deleting the list in the same
   change nets out to no stem. J-110's "cleared in Stage B" is met, and no line was ever added.
3. **Accept row met.** The allowlist loses exactly the two door lines (A5 1 + 3 = 4 findings). My count of
   non-comment entry lines tagged `# platform refactor (#224)`: base 34, tree **32**. All entry lines: 134 → 132.
   No line is added. The lint's debt line reads 415 in 132 (419 in 134 before).
4. **Lint.** Nothing else in the lint uses the deleted function. The fixture is re-pointed, not deleted: the three
   doors' read, include, `QFile` and `QDir` walk count 0, and the same read in `Gui/session/session_list_presenter.cpp`
   and a store include from a `Gui` file count 1. Under the old code the first sub-check would fail, so the fixture
   tests the change. The self-test prints `[PASS] A5 (R9) …`. The plants match the scripts: a read in a `Gui` file
   fails A5 (P11); a door-named file in `Diagnostics/norms` fails (rules message); one more `QFile` in each door gives
   no finding. The three door files were restored byte-identical to their `.orig` copies, and `git status` shows only
   the 3 files. The lint binary is newer than its source.
   **My re-run** (through `one_at_a_time.sh`): `architecture_lint_test`, `store_isolation_lint_test`,
   `im_excerpt_selftest`, `im_changelog_merge_selftest` and `artefact_paths_check` passed, **5/5**.
   **Logs:** both builds exit 0 with 0 errors; ctest 17/17; parity exit 0 with **0 differences** against `b1-base-2`.
5. **IM.** Version 1.12.14 follows 1.12.13, and the change-log row is last and in the 1.12.13 row's format.
   `[^sa24-r9]` is defined once (:1991), in the `sa24-r6` style, and cited at :144, :1252 and :2107. R9's ruling is
   quoted verbatim, and J-110's quote matches :88. The P11 row's figures are right. The only IM text that still says
   "counted store doors" is change-log row 1.12.13, which is history. §11's rows :1315 and :1317 already call the doors
   the store's file work.
6. **GIT-6.** Nothing outside the lint changes, so no other tests are owed. B1.13, B1.14 and B1.16 left the plan docs
   alone, so the plan's stale rows (below) follow that precedent.

## Findings
- **EDGE (pending gate, not seen):** the studio PC full suite (`studio PC-wt-37c-20261126-063840`) was still building at
  06:43 (log ends at `jom -j16 6:39:33`). Its result is the coordinator's gate before commit. A red run would be
  BLOCKING.
- **EDGE, minor (NOTES, fix in item):** there is no EDGE list (COMMON deliverable 4). The NOTES cite "EDGE 1" and
  "EDGE 3", but there is no EDGE 2 and no list. The brief's "#224: the closing sentence for J-88's doors, in NOTES" is
  missing. NOTES §4 also needs the studio PC summary.
- **EDGE, minor (wording, fix in item):**
  - The 1.12.14 change-log row says `rule_data_reads` was "counted by the 24 Nov ruling (J-110), is cleared". The
    lint never counted it. Better: "ruled to be counted (J-110) and, by R9, never counted: no line".
  - "No code outside the lint, file or behaviour changes" reads badly. Better: "No code outside the lint changes; no
    file or behaviour changes".
  - The footnote's second quote lowercases R9's "The counting".
- **EDGE (AG-14 note):** the A5 row's definition "a store door, a store's file work in a file of its own" is new
  wording; the IM has no prior definition of store door. It is consistent with R9's "Today" and with §11 :1315. Accept
  it under `[^sa24-r9]`, or put it on the Solution Architect's ratification list.
- **EDGE (ruled consequence):** nothing in the lint now flags a new door in `src/Stores`. NOTES says so plainly, and R9
  ruled it ("naming each in the IM is that decision"), but nothing checks that a new door gets named.
- **EDGE (stale docs):** these lines still say A5 counts `rule_data_reads` or still list J-88's lines as open:
  - `platform_refactor_assessment.md` :88, :239, :304, :309, :722;
  - `tools/refactor/move_files.py:84`, where `LINT_TABLE_FUNCS` still names `countedStoreDoor` (harmless: `func_body`
    returns None).
  - Already wrong on main, not this item's: the assessment's `[^j110]` says "Not yet ruled" while :309 says ruled.

## Verdict: **SIGNED OFF**, conditional on the studio PC run passing
I found no blocking finding. The NOTES and wording minors can be fixed in the item without a re-review.
