# B1.8 · independent review

Reviewed `build/wt-37c` at `46a7e4a9` plus the uncommitted change (36 files ` M`, new `tools/refactor/`), against BRIEF-B1.8 and COMMON. No build, edit or commit made; nothing run on the studio PC.

## Verdict: SIGN OFF

There are no blocking findings. The four non-blocking notes at the end are for the coordinator.

## 1. Nothing persisted or QML-facing changed: confirmed
- **The proof.** I wrote my own check, separate from the tool: for each of the 35 changed files (all but the IM), I mapped every new name back to its old name, whole-word, and compared the result with `git show HEAD:<file>`. All 35 match byte for byte, and all 36 new names occur. So the diff holds the 36 renames and nothing else. It also shows that no new name occurred in these files before the change, so no name was captured or merged with another. The tool's own `--verify` agrees: 33 C/C++ files, 0 string or character literals changed.
- **String literals, JSON keys, settings keys, pack keys and context-property names** are all string literals here, and none changed. No `.qml`, `.js` or `.json` file is in the diff.
- **Qt meta lines.** No `Q_PROPERTY`, `Q_INVOKABLE`, `Q_ENUM`, `Q_GADGET`, `Q_OBJECT`, signal or slot line, `setContextProperty`, `qmlRegister*`, `Q_DECLARE_METATYPE` or `qRegisterMetaType` line was added or removed.
- **Enum names never reach a file.** None of `PriorityClass`, `PriorityStatus` or `PriorityScope` has `Q_ENUM` or a metatype. No `QMetaEnum`, `valueToKey`, `typeid`, `#x` stringify or `Q_FUNC_INFO`/`__func__` appears in any changed file. Enumerators are written through `priorityClassToString` and `priorityStatusToString`, whose literals are unchanged.
- **Version constants keep their values:** `kPrioritySchemaVersion = 3` and `kPriorityRuleVersion = 6` (`src/Coaching/work_ons.h:83,93`, the same lines as at HEAD). The other `kPriority*` constants keep theirs too: 5, 3, 0.50, 0.75, 2, 11.0, and `kStillPriorities` 3.
- **No old name is left.** None of the 36 old names remains in any C/C++ file in the worktree outside `build/`. None is declared virtual, so a rename cannot leave an override behind silently.

## 2. Declared work-on and Gui's presenters keep the word: confirmed
- **The declared work-on keeps its word.** It is `App/session_focus.h`, `DomainModel/session.h` (`setSessionIntent`) and `IntentPage.qml`, and none of them is in the diff.
- **The presenters keep their names.** `WorkOnsController`, `DescribedWorkOns`, `describeWorkOns`, `AthleteDiagnostics::workOns()` and the report models are unchanged; the reverse-map check covers this. In `src/Gui` and `src/DomainModel`, only the rules' type and function names used there changed.
- **The two extra renames are rules-side.** `priorityClassFor` is a free function in `src/Coaching/condition_role_pack.h:80`, and `isMovementPriority` is an inline non-virtual member of the `ConditionRole` struct. Neither is a QML name or a presenter name.
- **`CauseItem::workOnRank` → `priorityRank`** (`session_report.h`) is a plain C++ field on a non-gadget struct, and the map keys built from it are unchanged literals.

## 3. The six kept data members
Keeping all six is right for this item. They are not in the brief's list (types, enums, structs and free functions). Five of them would also catch the presenters' and QML's spellings under a whole-word rename:

| Member | C++ | QML/JS | Literal |
|---|---|---|---|
| `workOn` | 77 | 12 | 14 |
| `workOns` | 174 | 89 | 11 (it is also `WorkOnsController`'s and `AthleteDiagnostics`' name) |
| `workOnsActive` | 10 | 7 | 4 |
| `workOnsNew` | 9 | 5 | 4 |

All six are rules-side by owner, so a scoped rename by hand is possible later. One is safe to rename now: `StrengthPick::causeOfWorkOn` (4 uses, all in `session_report.h`; 0 in QML; 0 in literals). It is optional, not blocking. Two of the members are priorities by meaning, and a later pass should rename them by hand, scoped, with the map keys kept:
- `ConditionRole::workOn` is typed `PriorityClass`;
- `AreaItem::workOn` is the athlete-level priority status.

## 4. `diag_stage_panel_test` ARCHIVEh2 on the studio PC: unrelated to this change (timing)
- **What failed.** Cabin run `studio PC-wt-37c-20261124-201422` printed "writes after it landed 2; shot 5's derived intent \"draw\"". The check expects exactly 1 write (`src/Diagnostics/tests/diag_stage_panel_test.cpp:1168`). ARCHIVEh3 and h4 passed: the file holds the command, and the other 11 shots' rows are unchanged. The end state is right; the file was written one extra time.
- **The mechanism is a race.** `SessionDiagnostics::settleIfDone` (`src/DomainModel/session_diagnostics.cpp:922-935`) asks for the reduction on its lane and starts the 300 ms write debounce (the test's `Rig(300)`). When the reduction is applied (`:1494-1496`), the timer starts again. If the reduction comes back within 300 ms, the timer restarts and there is 1 write; if it takes longer, there are 2. The run took 308 s against 261 s at 16:35 (18% slower), which fits.
- **A rename cannot change behaviour.** The change is token-identical up to a consistent renaming to fresh names (§1). None of the names is reflected or stringified, and none is virtual, so the code compiled from it does the same thing in the same time.
- **The other lane passed on the same source.** `studio PC-wt-5b-20261124-193127` (48387973 plus wt-5b's 7-file patch) passed this suite in 322 s. `git diff 48387973 46a7e4a9 -- src` is empty.
- **The Mac passed.** It passed in the implementer's run and in this review's run (below).
- **Not proven by a run.** An optional studio PC re-run of `diag_stage_panel_test` alone would confirm it. It does not block sign-off. ARCHIVEh2's 300 ms debounce race against the reduction lane is a test flake and should be filed as an issue.

## 5. The tool: confirmed
`tools/refactor/rename_identifiers.py`:
- **Whole-word.** Each row uses `(?<![A-Za-z0-9_])old(?![A-Za-z0-9_])`.
- **Literals are never edited.** Literal segments ("…", raw strings with a prefix, char literals, `#include "…"`) are skipped.
- **Refusals.** A name found in a literal is refused unless `reviewed` is filled, and so is a new name already in use. Duplicate old names or duplicate new names are table errors (exit 2).
- **Deterministic.** It walks files in sorted order and applies rows in table order.
- **No chaining.** A→B, B→C would have been refused as a clash, because B is in use.
- **The table matches the diff.** 36 rows, all 36 new names present after the change, 0 old names left.

## 6. The heading: confirmed
- **Deleted.** `16aa865c` (28 Oct, "Work-ons and Swing diagnostics are gone") deleted `src/Gui/home/HmWorkOns.qml` (457 lines). It is not in HEAD, and nothing references `HmWorkOns` in `src`.
- **No panel heading says "Work-ons" today.** The only visible "Work-ons" titles are a table column (`HmSwingAreas.qml:178`) and its gallery and test mirrors. Taking the stop branch, with the candidates listed for the Design Lead, was right.

## Tests run in this review
`build/refactor/one_at_a_time.sh ctest --test-dir build/wt-37c/build/tests -R 'work_ons|session_results|reduction|condition_role|diag_stage_panel|architecture_lint|design_system' -j 3` passed 10 of 10:
- reduction_rule_golden;
- condition_role;
- work_ons;
- lm_session_reductions;
- dashboard_reductions;
- work_ons_controller;
- diag_stage_panel, in 42.9 s, with ARCHIVEh2 passing;
- architecture_lint;
- design_system_lint;
- design_system_tokens_check.

`reduction_rule_golden_test`'s athleteWorkOns failure on the studio PC was already there: it fails the same way in the 16:35 run on main.

## Non-blocking notes for the coordinator
- **N1 · design docs.** `docs/design/work_ons_design.md` (3 old names) and `docs/design/session_report_design.md` (6) are the design docs of these types and still name the old types. The brief named only two docs, so this is not blocking. Update them in B1.6, or say they are history.
- **N2 · audit data.** `docs/implementation/swe-audit/data/*.json` names 4 old names. Leave them: they are the audit pinned at 37289e33.
- **N3 · the IM walkthrough deck.** Slide 8 says "The priority types are renamed in Stage B (WorkOnClass → PriorityClass)". This is not an IM change, and the slide stays true. The Product Owner may want "are renamed" to read as done at the next regeneration.
- **N4 · issue for the flake.** File an issue for ARCHIVEh2's debounce race (§4).
