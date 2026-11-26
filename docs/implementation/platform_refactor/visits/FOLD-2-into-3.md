# Visit 2 folded into visit 3 (26 Nov)

**The ruling** (the Product Owner, 26 Nov 2026): visits 2 and 3 combined, to save time. It is logged as "Visits 2 and 3 combined" at the end of `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`[^ruling]. The tracked docs cite that entry instead of quoting it, because the brief's wording and the log's differ slightly.

**What is ruled, and what is proposed.** The ruling sets four things: there is one visit left, visit 3; it carries visit 2's V5 and V6; Stage C's close (C-16b) waits for it; and the desk checks D12 and D13 stay at the Mac. Everything else below is the docs lane's call, unattended (AG-3), for the Product Owner's review. The calls are in §4.

## 1. The combined visit

Visit 3 keeps both its passes, as RECUT row 3.1 and the coordinator's B2 call left them. Visit 2's two ball steps join the **latest-main pass**:

| Visit 2 step | In visit 3 | Why |
|---|---|---|
| V5a's start (the launch monitor and the app) | **merged**: visit 3's own start | the same start; not run twice |
| V5a: End while a shot is analysing, then wait at the dialog | **merged with row 3.4** (End with a shot in flight): in the latest-main pass, one session, End pressed while shot 2 is still being saved, checked against both pass lines: #188's (the shot lands in that session, not a stray folder) and V5a's (the dialog appears; waiting keeps both shots and the session) | the two overlap: End with a shot not yet saved. SD-15a writes the steps so that one session meets both conditions. 3.4 in the chain pass is unchanged, because it checks the chain's build, not main's |
| V5b: End anyway | **added**, straight after 3.4 | a new case: visit 3 had no End anyway |
| V6a, V6b: quit while a shot is analysing (wait; Close anyway), through `quit.ps1` | **added**, at the end of the latest-main pass, before the install of the chain | a new case. It is the one real quit on Windows with the cameras busy, and it should run on main, not on the uncommitted chain |
| The Message Log export after each session | **merged**: visit 3 exports after each session anyway | — |
| Closing the app and the launch monitor's software | **merged**: visit 3's end | — |

**The sensors.** Visit 3's live run wears them, so V5 and V6 run with them on when they are already worn. V5 and V6 don't need the sensors, so a failed trunk or arm set-up doesn't stop them: they then run with the cameras and the launch monitor only, as written in `docs/implementation/platform_refactor/visits/VISIT-2.md`. Their shots also give the sensors' `rig` blocks for C-16b, which visit 3 was to give anyway.

**The installer** is built once: visit 3's latest-main installer is the one used for V5 and V6. There is no separate installer A, and no visit-2 build. Visit 3 still has its second installer, main plus the parked chain, for its second pass. Any build of main for visit 3 is from after `a878282f`, so visit 2's D3 ("does the build carry the fix?") is always yes and goes.

**Length: about 85 minutes** (an estimate until SD-15a writes the procedure). Visit 3 alone was about 70. Added: V5b about 4 minutes, V6a and V6b about 8, and about 1 for the extra log exports, so about 13 in all. Not added, because visit 3 already has them: V5a's 6 minutes (its start, and its session, which is row 3.4), and the closing of the app and the launch monitor's software (about 2 of visit 2's 3-minute line; the other minute is the exports, counted above). Visit 2 on its own was 6 + 4 + 8 + 3 = 21, about 20; the fold adds 4 + 8 + 1 = 13 of it to visit 3. So 70 + 13 = about 83, rounded to **about 85**.

**Bay time left: about 85 minutes, in one visit.** After the morning's re-cut it was about 90 in two (about 20 + 70), and about 225 before that. The fold saves about 5 minutes at the bay, and one trip. **Most of the saving is the trip.**

## 2. Where visit 2's other parts went

| Part | Goes to |
|---|---|
| D1: build installer A | none: visit 3's latest-main installer (SD-15a) |
| D2: the full suite for that build | SD-15a, for the latest-main installer's commit |
| D3: does the build carry the fix? | goes: always yes |
| D4, D5, D7: the cut-over check (the settings copied before and after the install, compared, with the comparison rehearsed headless on the Mac) | SD-15a, around the latest-main install. By then every Stage C cut-over item has landed, so the check covers all of them, not only the ones in an early installer A |
| D6: the Product Owner's install from his account | SD-15a, for the latest-main installer; the chain's installer goes in at the visit, between the passes, as before |
| D8: the Recycle Bin look for visit 1's lost session | SD-15a (any time before the visit) |
| D9 to D11: the staging, the studio PC kept free, the harness | SD-15a; the harness was already one of its riders |
| **D12, D13: the trunk ceremonies and the HackMotion routine twice** | **stay at the Mac**, from `docs/implementation/platform_refactor/visits/VISIT-2.md` §3, any time before visit 3 (ruled). Their fallback is visit 3's sessions, as before |
| `VISIT-2-FOR-CLAUDE.md`: the hard rules, visit 1's nine lessons, the V5 and V6 checks, and write-up sections 5, 7 and 8 | SD-15a's file for the Claude session at visit 3 |

**C-16a's reading check, done here.** C-16a's What 2 asked for a check, by reading, that the desk run can reach the trunk and arm steps. From the code, on 26 Nov:
- set-up offers "Start anyway" when not everything is confirmed (`src/Gui/setup/ScreenSessionSetup.qml:434`);
- the ball step has Skip (`SetupSteps.qml`, "Skip is the way past");
- the trunk step appears only when `groups.trunk.inSession` is true, that is, when a present, enabled sensor has the trunk role (`SetupSteps.qml:252`);
- the arm step works the same way, with `groups.arm.inSession` (`:226`).

Not checked: whether the Mac's settings give a sensor that role. I didn't read the Product Owner's settings. That is seen at the desk: if no trunk step appears, D12 falls back as written. This is recorded in `docs/implementation/platform_refactor/visits/VISIT-2.md` §3.

## 3. What changed

**Local (gitignored):**
- `ORDER.md`:
  - the Stage C and Stage D Cabin cells (Stage C has no visit of its own; visit 3 is the only visit left, about 85 min, and Stage C's close waits for it), and their Updated stamps;
  - Stage C's effort note;
  - "Your studio PC visits", items 2 and 3;
  - "next" rows 7 and 9;
  - both "Waiting on" visit bullets.
- `docs/implementation/platform_refactor/visits/VISIT-2.md`:
  - a "folded" banner;
  - line 17's choice struck;
  - a §3 note: D1, D2 and D4 to D11 go to SD-15a, D3 goes, D12 and D13 stay here, and the reading check;
  - §4's "what happens next";
  - D12: don't press End until the ceremony file's copy is made (call 8);
  - `[^fold]`.
  
  The V5, V6, D12 and D13 text is kept as the only written source.
- `visits/VISIT-2-FOR-CLAUDE.md`: a "folded, don't stage" banner, and `[^fold]`.
- `docs/implementation/platform_refactor/visits/RECUT-26nov.md`: one dated pointer at the top. The reviewed text is not rewritten.
- `docs/implementation/platform_refactor/plans/PLAN-C.md`:
  - C-16a's row struck (retired, 0 h);
  - C-16b's After set to visit 3's latest-main pass;
  - the lanes row;
  - Effort: about 64.5 h for 29 items, and the docs' share about 8 h;
  - Cabin: no visit of its own, item 1 at visit 3, item 2's desk checks any time before visit 3, and the cut-over check around visit 3's install;
  - the lever section's C-16a line;
  - `[^fold]`.
- `docs/implementation/platform_refactor/plans/PLAN-D.md`:
  - SD-15a: the fold's riders, and its effort from 1.5 to 2 h;
  - SD-15b: hands V5's and V6's results and the sensors' `rig` blocks to C-16b;
  - Cabin: about 85 min, the only visit left, V5 and V6 in the latest-main pass, one latest-main installer, and the booking unchanged;
  - Effort: 82.5–88.5 h;
  - `[^fold-d]`.
- `C/BRIEF-C-16a.md`: a "retired, don't run" banner, with a table of where each duty went. The old text is kept for SD-15a to read.
- `C/BRIEF-C-16b.md`:
  - After: visit 3, its latest-main pass only;
  - check-first 1: C-16a retired;
  - step 9: C-14's check on "the build the ceremonies ran on" (REVIEW-RECUT-2's E-7);
  - a new step 9a: V5's and V6's results, the cameras' and sensors' `rig` blocks, and #105's lines, from SD-15b's hand-over. A failure is stated, not held. This settles E-8;
  - step 12: the closing installer is the studio PC's latest, not a visit's;
  - a new trap: main holds much of Stage D by then, so each figure names its commit and Stage D's changes are kept apart;
  - `[^fold]`.
- `DASHBOARD.md` "Needs you": the visit line is now about 85 min in one visit, Stage C's close waits for it, and C-16a is retired.

**Tracked (`docs/`, on top of the morning's re-cut):**
- `docs/design/rig_design.md` §10, "The studio PC visit": Stage C has no visit of its own. Its End and quit while a shot is analysing runs at Stage D's visit, and Stage C's close waits for it; the rest is unchanged. The "(proposed, for the Solution Architect)" marker now sits on the paragraph's first sentence (REVIEW-RECUT-2's E-5). It names no machine or visit number. `[^visits-combined]` is new.
- `docs/implementation/platform_refactor_assessment.md` §5 rule 2: "one visit per stage where possible" becomes "as few visits as possible: one visit may serve two stages, and a stage's close then waits for it" (proposed, for the Product Owner), with `[^visits-combined]`. The Stage F row is unchanged.
- `build/platform/visits/docs-commit-msg.txt`: rewritten to cover the whole `docs/` diff, the re-cut plus the fold. `governance/controls/test.md` stays in its own commit (`BT-43-commit-msg.txt`).

## 4. Calls (AG-3; for the Product Owner's review)

1. **C-16a is retired, not kept.** Its visit half (V5 and V6, the installer, and D1 to D11) is visit 3's pack by nature. Its desk half (D12, D13) is already written and needs no item, now that the reading check is done (§2). A C-16a kept only for the desk half would be an item with nothing left to do. Undo: restore the PLAN-C row and the brief, re-scoped to D12 and D13 (about 0.5 h).
2. **The visit's place is unchanged, despite the brief's wording.** The brief and the ruling's log entry say "at the end of Stage D", but PLAN-D books visit 3 for when its parked timing items are reviewed, about two-thirds through the stage (Cabin; N3). An end-of-stage visit holds twelve patches behind it instead of seven. The Product Owner's words don't move it, so the docs say "Stage D's visit", with ORDER's "about two-thirds through" kept. Undo: if the end is meant, change PLAN-D's booking, which is a coordinator's call with that cost.
3. **V5 and V6 run in the latest-main pass, V5a as row 3.4.** The fix they check is on main, C-16b closes on main, and after the chain's install the studio PC is on an uncommitted build. Row 3.4 still runs in the chain pass, as the chain's own check. Undo: run them in the chain pass (no time change; C-16b would then read an uncommitted build).
4. **The installer is built once:** one latest-main installer for V5, V6 and visit 3's first pass. The chain's installer stays, because the second pass needs it. Undo: none needed; a separate visit-2 installer would only matter if V5 and V6 ran on a different build.
5. **SD-15a goes from 1.5 to 2 h, and Stage C loses 1.5 h.** ORDER's Stage C and D ranges (46–86 h, 66–118 h) and its Overall row are not changed: the net −1 h is inside their spread. Undo: the plan rows.
6. **§5 rule 2 is generalised and marked proposed.** The ruling combined two visits. "One visit may serve two stages, and a stage's close then waits for it" is my reading of it as a rule, so it is marked proposed for the Product Owner. Undo: restore "batched into one visit per stage where possible" and drop `[^visits-combined]` from the assessment.
7. **The ORDER rows' Updated stamps** use this machine's clock (26 Nov 09:07), although the morning's re-cut stamped them 13:30. The dashboard's and ORDER's header stamps are the coordinator's and are not changed. Undo: set the two cells to the coordinator's time.
8. **D12 gains one sentence:** don't press End until the coordinator has said the ceremony file's copy is made. C-16a's What 2 asked for it, and D12 didn't say it. Undo: remove the sentence.

## 5. Edges (AG-22)

- **EDGE: C-16b now runs with about two-thirds of Stage D on main.** This is the ruling's real cost to the close. Every "run on main" figure takes in Stage D's work: the counts, the calibration files' log since C-10c, the parity against the entry baseline, "no new failure against Stage B's close", and the closing installer. BRIEF-C-16b gains a trap: name the commit each figure is taken at, and separate Stage D's changes from Stage C's.
- **EDGE: SD-15a and SD-15b have no briefs yet** (none in `DEF/` or `D/`). Their riders live only in PLAN-D's rows, so whoever writes the briefs must carry them from there and from §1 and §2 above.
- **EDGE: two items now wait behind the visit.** PLAN-E's SE-5f (the Rig's copied settings keys leave) and PLAN-F's SF-5 both depend on C-16b, so both now wait for visit 3. In ORDER's order, Stage E part 2 already comes after visit 3 (rows 9 and 10), and SF-5 is parked on #272, so neither is delayed today. If Stage E part 2 runs earlier, SE-5f could be re-based on C-16b's figures instead of its close (the coordinator's call).
- **EDGE: the real Windows quit (V6) and #280's live check now come later**, at visit 3 instead of at the next practice. Nothing waits on them: the logic is pinned by `app_state_test` S10.
- **EDGE: stale lines outside this lane's list.**
  - `docs/implementation/platform_refactor/plans/PLAN-E.md` line 82 and `docs/implementation/platform_refactor/plans/SIZING-E1-E4.md` line 117 say the real Windows quit is seen at "visit 2": it is now visit 3's V6.
  - `C/BRIEF-C-17.md` step 1, and #291's body, still place the ceremonies at visit 2 (REVIEW-RECUT-2's E-3).
  - `docs/design/rig_design.md` §4.8 (lines 511, 524 and 674) and §10's C-16 row still give the ceremonies a step at the visit. §10's new footnote names the C-16 row as the 24 Nov record; §4.8 was outside the brief.
  - The assessment's Stage C and D "Cabin visit" rows are proposals of their day, as the re-cut left them; the new footnote says so.
- **EDGE: if the harness hasn't arrived by visit 3,** visit 3 still runs V5 and V6 (they need no sensor). The sensors' `rig` blocks, #105's connect and the ceremonies' fallback are then missing, and C-16b names them as carried forward (its step 9a).
- **EDGE: GIT-8's `docs:` against the log's `Docs:`** (246 uses) is carried from both reviews. The draft follows the log.

[^ruling]: The Product Owner, 26 Nov 2026, as given in the docs lane's brief: "combine visit 2 and 3 — lets be efficient with time". The log's entry reads "combine visit 2 and 3, let's be efficient with time".
