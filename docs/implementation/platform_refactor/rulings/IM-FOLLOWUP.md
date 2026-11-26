# The information-model edits owed after B1.6 (25 Nov 2026; part B rewritten 26 Nov 2026)

**The ask, in plain words (rewritten for plain English, 26 Nov).** Solution Architect: approve the exact wording of two changes to the information model, both already ruled in substance (A on 25 Nov, B on 26 Nov).
- **A. The capture buffer's data types move again.** Last night's rulings reversed the decision to leave the buffer's seven stream-type headers in their own folder; they move into the shared vocabulary folder in Stage D, as first ruled. The model is edited back to say so, with the reversal recorded and the old history kept. *What changes:* only the model's wording; no file moves until Stage D.
- **B. R-QML-4 gets its check and its three exceptions** (rewritten 26 Nov 2026 to match Morning batch 2, (2) and (4); this supersedes the earlier "one accepted exception" wording, which the precheck's finding 1 showed was untrue). *What changes:* the model's QML-rules row names the check (`session_setup_lint_test` W12) and the three exceptions: the calibration screens (#291), the Consistency grade words (#219, the Design Lead's) and the files frozen on the check's exception list, each pointing to its issue.
- **Status.** **A landed** as IM 1.12.14 (9f086b4d). **B is built** as IM 1.12.17, uncommitted in lane 1's W12 item (`build/wt-5b`), with the check itself; its exact wording is below, for the Solution Architect to approve before that commit. B no longer waits on B1.6 or rides with A.

**Part A, as it was applied (kept as the record).** It was to apply only after lane 1's B1.6 had committed its IM change. B1.6 renumbers to the next version and edits the rows beside these (the header Version line, the RING-RULES row next to RING-VOCABULARY, the change log). So this page gives old-string/new-string pairs, not line numbers, and never a revert of e38ad103. Re-read each old string on the main of the day first: if B1.6 changed one, apply the same meaning by hand.

A was planned with B as one commit and one version (written `1.12.N` below); A landed alone as 1.12.14, and B is its own version, 1.12.17 ("Part B, as built"). B's old clauses (edit 5, and B's parts of edits 1, 6, 7 and the checks) are removed from this record: 1.12.14 never held them.

- **A. Undo 1.12.4's Buffer amendment** (C9). The rulings: D-V3 reversed (the Product Owner and the Solution Architect, 25 Nov 21:56, `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, the last entry, ruling 2). Buffer's stream types move to `src/Vocabulary/` in Stage D as J-109 ruled, keeping the Solution Architect's 23 Nov mandate ("the architecture lint ends with one Vocabulary rule. This is not optional").
- **B. R-QML-4's check and its three exceptions** (C11). The rulings: Morning batch 2 (the Product Owner and the Solution Architect, 26 Nov 2026 06:45, `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`): (1) option 1, every screen file holding rules frozen at its count on a shrink-only exception list, each line pointing to an issue; (2) the check stays in `session_setup_lint_test` W12 and is recorded here as R-QML-4's check; (3) the list's owner is the Solution Architect; (4) the exceptions are the calibration screens (#291), the Consistency grade words (#219, the Design Lead's) and the frozen files; AD-52 matches. **The row's exact wording is proposed, for the Solution Architect**; the substance is ruled. B is applied as its own version, 1.12.17, in the W12 commit (see "Part B, as built" below).

**Keep the history.** Change-log row 1.12.4 and the footnote `[^sa25-buffer]` stay word for word; a new row and a new footnote record the reversal. Change-log row 1.12.1's "still to come" stays as it is.

**Not part of this edit.** The FILE-SESS-04 and FILE-SESS-05 reader columns for U-12's interim Q4 (c) land in U-12's own commit (`docs/implementation/platform_u_presenters.md`, `[^u-q4c]`), with the lifecycle model's APP-T4 and ATH-T8.

## Part A's edits (applied as 1.12.14)

### 1. Header Version line
Old (as 1.12.4 left it; B1.6 will have replaced it with its own):
```
| **Version** | 1.12.4, 25 Nov 2026 ([§15 Change log](#15-change-log)): `Buffer`'s stream types stay in their own folder (Stage D no longer moves them). |
```
New (whatever B1.6 wrote is replaced):
```
| **Version** | 1.12.N, <date> ([§15 Change log](#15-change-log)): `Buffer`'s stream types move under `src/Vocabulary/` in Stage D again. |
```

### 2. RING-VOCABULARY (§4.1), three replacements in the one row
2a. Old:
```
`src/Vocabulary/`, the ring's root folder, an area per subfolder[^sa-vocab-root], for every Vocabulary file but `Buffer`'s stream types[^sa25-buffer]: its first area,
```
New (the pre-1.12.4 text):
```
`src/Vocabulary/`, the ring's one root folder, an area per subfolder[^sa-vocab-root]: its first area,
```
2b. Old:
```
the rest of `Core`; in their own folder, which they keep, the stream types of `Buffer`
```
New (the pre-1.12.4 text):
```
the rest of `Core`; still in their own folder, the stream types of `Buffer`
```
2c. Old:
```
`imu_sample`, `pixel_format_names`), each placed by its own entry in the lint's ring table[^sa25-buffer] |
```
New (the pre-1.12.4 text, plus a clause saying when they move and the new footnote; the clause's wording proposed, for the Solution Architect):
```
`imu_sample`, `pixel_format_names`), until Stage D moves them to `src/Vocabulary/Buffer/`[^sa25-buffer-back] |
```

### 3. "Directories that span rings" (§4.1), the `Buffer` row
Old:
```
| `Buffer` | Stream types (vocabulary) and the ring buffer (adapter); not split: the stream types stay, placed file by file[^sa25-buffer] |
```
New (the pre-1.12.4 text, "Stream types (vocabulary) and the ring buffer (adapter)", plus a new clause and the footnote; the clause's wording proposed, for the Solution Architect):
```
| `Buffer` | Stream types (vocabulary) and the ring buffer (adapter); split in Stage D, the stream types to `src/Vocabulary/Buffer/`[^sa25-buffer-back] |
```

### 4. `[^sa24-q15]`, its last sentence
Old:
```
Buffer's part was withdrawn on 25 Nov 2026: its stream types stay in `src/Buffer` (`[^sa25-buffer]`).
```
New:
```
Buffer's part was withdrawn on 25 Nov 2026 (`[^sa25-buffer]`) and reinstated the same night (`[^sa25-buffer-back]`): its stream types move in Stage D.
```

### 6. Change log (§15): a new row after B1.6's
```
| 1.12.N | <date> | **`Buffer`'s stream types move under `src/Vocabulary/` again** (C9; platform refactor Stage D): 1.12.4's amendment is undone. §4.1's RING-VOCABULARY row names `src/Vocabulary/` the ring's one root folder again, with `Buffer`'s seven stream-type headers in their own folder until Stage D's SD-4 moves them to `src/Vocabulary/Buffer/`; the "Directories that span rings" row for `Buffer` says it is split in Stage D; `[^sa24-q15]` records the reinstatement. Rows 1.12.1 and 1.12.4 are kept as history. No file moves and no behaviour changes | the Product Owner and the Solution Architect, 25 Nov 2026[^sa25-buffer-back] |
```

### 7. A new footnote, after `[^sa25-buffer]`
```
[^sa25-buffer-back]: The Product Owner and the Solution Architect, 25 Nov 2026 (later the same night as `[^sa25-buffer]`): lever D-V3 reversed. Buffer's stream types move to `src/Vocabulary/` in Stage D as J-109 ruled (`[^sa24-q15]`), keeping the Solution Architect's mandate of 23 Nov 2026 that the architecture lint ends with one Vocabulary rule (`[^sa-vocab-root]`). `docs/implementation/platform_d_capture_seam.md`, "The lever pass of 25 Nov".
```

## Checks after the edit
- `python3 tools/method/im_excerpt.py --self-test` passes, and `python3 tools/method/im_excerpt.py RING-VOCABULARY` prints the new rows with the new footnote.
- `grep -c 'sa25-buffer-back' governance/artefacts/information_model.md` is 5 (2c, 3, 4, the change-log row and the footnote).
- **Two committed footnotes go stale when this lands; update them in the same commit:** `docs/implementation/platform_d_capture_seam.md` `[^d-v3]` and `docs/implementation/platform_refactor_assessment.md` `[^buffer-back]` both say the IM's v1.12.4 "still says" the headers stay and that it "is undone in its next version". Replace that sentence in each with "The information model says so again from v1.12.N."
- The commit says it is an information-model change (C9), approved by the rulings above. Tell the Product Owner to refresh the IM walkthrough deck only if it shows `Buffer`'s folder.

## Part B, as built (IM 1.12.17, uncommitted in `build/wt-5b`; the wording proposed, for the Solution Architect)

The before and after text of each edit is in `build/platform/E/NOTES-W12.md`, "The information model, before and after". In short:
1. **Header Version line:** 1.12.16 → 1.12.17, "R-QML-4 has a check (`session_setup_lint_test` W12) and names its three exceptions."
2. **R-QML-4's row (§9.2):** the Rule column names the three exceptions, each of which may shrink and must not grow: the calibration screens until #291; the Consistency grade words by the Design Lead's ruling (#219, `[^dl-219]`); the other screen files that held rules when the check landed. The check's exception list (`src/Gui/tests/qml_rule_allowlist.txt`, the Solution Architect's) freezes each file the check counts rules in at its count, each line pointing to its issue (AD-52, `[^sa26-qml-check]`). The Forbids column adds "a threshold or a pass/fail comparison in a QML or JS file (`session_setup_lint_test` W12)". The row holds no counts, so it doesn't go stale as the list shrinks.
3. **§11.1's R-QML-4 row:** status "partly; checked", the check's counts on 26 Nov 2026 (120 in 51 files: 18 calibration, 1 grade-word vocabulary, 101 in 46 others, 30 of them not rules), and the issues (#219, #291, #313 to #319).
4. **Change log:** row 1.12.17 (C11), approved "the Product Owner and the Solution Architect, 26 Nov 2026 (the substance); the wording proposed, for the Solution Architect".
5. **Footnote `[^sa26-qml-check]`**, after `[^sa25-buffer-back]`: the batch 2 rulings (1) to (4). It does not say "no other QML may hold a rule" (the precheck's finding 1).

**Checks:** `python3 tools/method/im_excerpt.py --self-test` passes; `python3 tools/method/im_excerpt.py R-QML-4` prints the new row with `[^sa26-qml-check]` and `[^dl-219]`; `grep -c 'sa26-qml-check'` is 4 (the row, §11, the change log, the footnote). AD-52 (`governance/controls/build.md`) is reworded the same way in the same change. The walkthrough deck: tell the Product Owner to refresh it only if it shows R-QML-4 (#286).
