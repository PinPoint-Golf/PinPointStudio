# NOTES: the documents brought into line with the 25 Nov scope rulings (commit e38ad103)

## What was done
- **IM v1.12.3 → 1.12.4 (D-V3, C9, approved by the Solution Architect).** These now say Buffer's stream types stay in `src/Buffer` with per-file entries: the RING-VOCABULARY directories cell, the "Directories that span rings" `Buffer` row (not split) and `[^sa24-q15]` (the withdrawal is appended and the 24 Nov text is unchanged). Also a change-log row, the header Version line and the new footnote `[^sa25-buffer]`. Change-log row 1.12.1's "still to come" was left as history. `im_excerpt.py --self-test` passes.
- **U pack** (`platform_u_presenters.md`): the status line; U-V0 on Q2's ruling; U-V1 on "Items that can start", Q10, Q12, Q13 and Q15; footnotes `[^u-v0]` and `[^u-v1]` (#289). **Q3 and Q4 were not touched (U-V2 held).**
- **D pack** (`platform_d_capture_seam.md`): the status line; a new section, "The lever pass of 25 Nov", covering D-V1, D-V2, D-V3, D-V4's order and the three added items; markers on S1, S2, S3, S9, S11, "What can start", "Already ruled", E-09, the waits table and `[^j109]`.
- **Assessment** (`platform_refactor_assessment.md`): the J-109 rulings row, and Stage D's Buffer and `CameraInstance` bullets, via `[^lever-25nov]`. **B2 pack**: P3's ruling gains a note that D-V2 re-opened it (#292).
- **Stage E pack, levers 7 and 8:** checked only. Both are recorded with lever 8's quit-test condition (`[^e-ruled-25nov]`). No change.
- **Plans (local):**
  - PLAN-U: U-P1, U-8a–c, U-9a–b and U-10a–c are out of scope (#289). U-18b is re-sized to 1–1.5 h and U-8q (2–4 h) is added. U-11 and U-12 are marked held. The Accept row is re-set.
  - PLAN-C: see the holds below (C-V1 is done; C-V2 is not taken).
  - PLAN-D: SD-3, SD-4, SD-11a, SD-11b and SD-12a are dropped, and SD-14 goes to #292. Added: SD-8t (the teardown, 2–4 h, waits on GU-3), SD-9t (the trigger policy's owner, 2.5–4 h) and SD-2o (#281's patch, 3–5 h). The D-V4 order is in place: SD-8d runs after SD-2b and SD-2c, and the split sits behind a gate whose wording is proposed, for the Product Owner. SD-11c and SD-12b are re-based.
- **Issues:**
  - Filed: #289 (U-V1), #290 (U-V2 and the five exceptions), #291 (C-V1) and #292 (D-V2).
  - Commented on #224 (links #289 and #290), #266 and #270 (both say Stage C no longer ports the routines; links #291). #291's body was edited after C-V1's release.
  - D-V1 and D-V3 were dropped, so they got no issue.

## The holds (coordinator, mid-run)
**C-V1: held, then released.** The Product Owner kept C-V1 as ruled: the calibration QML is all or nothing. Done after the release:
- #291's body now names the 3,978 lines as a known breach of "QML holds no rules and only binds", RP-7 and §4.8, pinned by the three golden traces.
- `rig_design.md` gains `[^c-v1]`, in 22 places. They are the status line, RP-7, §4.8 (a "Deferred" paragraph, step 2, the drivers, the ceremony file), §4.9, §5's three rows and §6 rule 3. Also §7's running-state row, §8's R-RIG-d and the no-C10 sentence, §9 Q14 (the global stays), and §10's C-11 to C-14 and C-17 rows and the visit paragraph.
- The assessment's Stage C Accept row is marked deferred to #291.
- PLAN-C: C-11a, C-12a, C-13a, C-11b.1, C-11b.2, C-12b, C-13b, C-15 and C-17 are out of scope. C-14 is re-based on C-6b.2 and re-scoped (the file write, with #265's fix). C-16a carries installer A only, the Accept row is re-set, and the effort is about 66 h.

**U-V2: still held.**
- #290 was filed, and #224 was commented on, before the hold. Not undone.
- #289's body points at #290 for the full list of five exceptions.
- #224's comment says #224-U "closes at 5 of its 28 lines". If U-V2 is not deferred, it is 3 (all on #289).
- Correct #224 and #289, or close #290, once the Product Owner's focused change is settled.

What I would have written for U-V2, had the hold not come:
- **U pack, Q3's Ruled line:** "The building is deferred by the lever pass, with a trigger: the next `resultsVersion` bump of the session summary, which rewrites every summary anyway, so the library is rewritten once[^u-v2]."
- **U pack, Q4's Ruled line:** "The building is deferred by the lever pass, with a trigger: the next change to the athlete level's freshness check. The athlete level does not yet hold five of the facts the scan reads (U-2i §8.2)[^u-v2]."
- **U pack, `[^u-v2]`:** "The Product Owner, 25 Nov 2026 (lever U-V2, as recommended); the two exceptions to P11 (HARD), and the five allowlist lines #224-U leaves, approved by the Solution Architect. U-11 and U-12, 7 h (8.5–10.5 h with U-12's growth), deferred with a trigger; home #290. The rulings stand as the approach."
- **PLAN-U:** U-11 and U-12 out of scope (#290). Accept 28 → 5. (They are marked "held" there now.)

## Calls made (logged for review)
- **J-1. The IM version collides with lane 1.** Lane 1's staged B1.6 (in `build/wt-37c`, and `B1.6-wip-on-8f33e190.patch`) also bumps the IM to **1.12.4**. Its edits touch the header Version line, the RING-RULES row (next to RING-VOCABULARY) and the change log (it inserts after 1.12.3). I used 1.12.4 as the brief ordered. **B1.6 has to renumber to 1.12.5 and resolve three hunks by hand when it rebases.** Lane 1's other files were not touched.
- **J-2. D-V2 has no ruled trigger.** #292 says so, and offers one marked "proposed, for the Product Owner".
- **J-3. PLAN-D's split gate.** The wording is mine, marked proposed. It keeps the split in scope as a written gate, not a hold (AG-8).
- **J-4. PLAN-D still lacks some of the 20:20 pack rulings**: SD-12c and SD-12d out (V1, V2), T2's new item (SD-2t), SD-2a at 5 h, and SD-9c parked with Stage F. Not reconciled, since that is outside this brief. The lever page's baseline (86.5 h) already counts them.
- **J-5. History left alone.** The swe-audit 06 and 06a files are untouched, and so is the assessment's carry-in (i), which is reported below.

## Conflicts with governance (reported, not fixed)
1. **D-V3 against the Solution Architect's "mandatory" one-root ruling.** The Solution Architect ruled on 23 Nov that "the architecture lint ends with one Vocabulary rule. This is not optional." That is the assessment's carry-in (i), `[^sa23-vocab]` and #224's first comment. D-V3 keeps seven per-file Vocabulary entries, so the lint can no longer end with one rule. The Solution Architect withdrew J-109's Buffer part, but no record amends the 23 Nov mandate itself. **It needs an explicit amendment from him.** Carry-in (i) still says "one Vocabulary rule".
2. **SD-8t rests on an unruled GU-3.** POL-4 and AG-3 say a HARD item is not built before its ruling. GU-3 ("an accepted shot is never lost without a trace") is proposed, for the Solution Architect, and the added 2–4 h depends on it. I marked the item as waiting on GU-3, but the plan now holds an item whose rule is not approved.
3. **#285 places GAP-25, GAP-29 and GAP-30 on Stage C, and no Stage C item closes them** (the lever page's §2.3). That breaks AG-9's "every finding placed". It is unaffected by the hold, and still open.
4. **#265's fix is in the wrong brief** (BRIEF-C-16a, not BRIEF-C-14; the lever page's §2.3). A lost ceremony is loss of evidence, which the loss rule treats as blocking. This is independent of C-V1.
5. **U-V1 and AG-14 in #289.** U-V1 defers QML-read defects found by reading (`kindText` always ""; Charts reading keys nobody writes). Under "Show with a warning, never hide" and "a 0 is a bug" these are latent, not seen, so parking them as edge cases fits the rules. I note it only because a golfer-visible "" would be blocking once it is observed.
6. **If U-V2 is taken later, two P11 (HARD) exceptions are approved for an indefinite time.** The triggers (the next `resultsVersion` bump; the athlete level's freshness check) have no date. The allowlist's "only shrinks" rule holds, but a HARD principle stays broken with no end date.
7. **C-V1 and the hands-free calibration rule.** The Product Owner's rule is that a calibration routine runs without the golfer touching the app (#266 cites it). The arm routines are not hands-free today: a timed start, and Recalibrate after a failure. C-V1 ties #266's fix to #291's track after the programme, so the breach of the hands-free rule lasts until then. The breach of RP-7 and §4.8 is now accepted explicitly by both roles and named on #291. The hands-free one is not named there as accepted, so it needs the Product Owner's word, or #266 built in QML first, with its trace re-frozen.

## Resolved by the three rulings of 25 Nov 21:56 (commit 78f09b93)
- **The U-V2 hold:** released. U-V2 withdrawn; U-11 (Q3 (a), with the dry run) and U-12 (Q4's interim (c)) are back in PLAN-U and the U pack. #290 edited and closed as not planned; #289 and #224's comment corrected to 3 exceptions. Nothing further to correct on #224, #289 or #290.
- **Conflict 1 (D-V3 against the one-root mandate):** resolved. D-V3 reversed; SD-3 and SD-4 back in PLAN-D; the D pack and the assessment undone with footnotes. **The IM still says 1.12.4 until the information model's sections this brief names (the excerpt files were local) is applied after B1.6** (B1.6 had not landed at 78f09b93).
- **Conflict 4 (#265 in the wrong brief):** fixed. The paragraph moved from BRIEF-C-16a to BRIEF-C-14.
- **Conflict 7 (the arm routines' hands-free breach):** accepted until #291 (with #266); named on #291 (comment), in `rig_design.md` §4.8 and in BRIEF-C-16a.
- **J-4 (PLAN-D lacked the 20:20 pack rulings):** reconciled: SD-12c and SD-12d out (V2, V1), SD-2t added (T2, 3 h), SD-2a at 5 h, SD-9c parked with Stage F (#272); lanes, studio PC, dependencies and effort (about 82–88 h for 32 items) re-set.

## Still open after 78f09b93
- **BRIEF-C-14 and BRIEF-C-16a predate C-V1's re-scope** (C-14 still moves the taps and removes `trunkCeremony`; C-16a still builds two installers). Only #265's paragraph moved, plus a "not yet re-scoped" note on C-14. Re-write both before they run.
- **Q4's end state (a) has no issue** now that #290 is closed: its trigger (the next change to the athlete level's freshness check) lives only in the U pack's `[^u-q4c]`.
- **Call (logged): U-12 no longer waits on Q11 in PLAN-U.** Option (c) reads `SwingStore::info` from `src/Export` exactly as today, so it doesn't need the Shot's stamp read. The committed U pack still lists U-12 under Q11 (its table, Q4's and Q11's Waits); Q11 is ruled and built anyway, so nothing stalls. Correct the pack's four mentions with the next U-pack edit. Undo: put Q11 back in U-12's After column.
- **R-QML-4's row wording and the two new IM clauses in the information model's sections this brief names (the excerpt files were local) are proposed, for the Solution Architect;** the exception and the Buffer reversal themselves are ruled.
- Conflicts 2, 3, 5 and 6 are unchanged (6 is moot: U-V2 was not taken).

## The docs lane after 78f09b93 (bf66039c)
- **Done:** BRIEF-C-14 and BRIEF-C-16a re-written for C-V1 (local). #293 filed for Q4's end state (a); #224 commented. The U pack's four U-12-behind-Q11 mentions corrected, plus the two counts they made false (line 33's "four items and three", line 44's "free nothing alone"), with `[^u12-q11]`; Q4's footnote names #293.
- **Mismatches found cross-checking the briefs against PLAN-C (reported, not fixed):**
  1. STANDING-C §9 still lists C-14 in the parked chain; PLAN-C's re-scoped row no longer parks it. BRIEF-C-14 follows PLAN-C (commits normally).
  2. PLAN-C's C-16a row still says "After: the chain reviewed", but the chain is out of scope. Its Lanes table, "Stage accept" paragraph and "Cabin" section (installer B, "If B passes") also predate C-V1; only the lever section at the bottom overrides them. BRIEF-C-16a starts any time.
  3. A6 ×2 on the recorder: PLAN-C says they stay unless Stage D's narrowed A6 removes them. But `rig_design.md` §4.8 and §10's C-14 row, `docs/implementation/platform_refactor/duties/DUTIES-trunk_ceremony_recorder.md` and BRIEF-C-16b still say A6 ×2 → 0. And I found no item in PLAN-C or PLAN-D that builds the narrowed lint (PLAN-D's SD-12c only records V2 as ruled), so its timing is unplaced.
  4. The Rig's `evidence` pointer (old C-14 What 4: the session id and file name on the `sensorAlignment` record) is in no PLAN-C row after the re-scope ("the file's write only"). I left it out of C-14, so it is unplaced.
  5. #265's body still says its fix goes in "C-16".
  6. #265's two extra loss paths (no session folder; quitting while held) and its golfer-facing warning have no ruled behaviour, and GU-18 is proposed, not ruled. BRIEF-C-14 carries a written gate: the app log always; the golfer only through an existing surface (UI-20), else a proposal to the Design Lead.
  7. PLAN-C does not name #270's two HackMotion attempts at visit 2; #291 and `rig_design.md` §10 do. BRIEF-C-16a follows them (V11 on installer A).
  8. `docs/implementation/platform_refactor/visits/VISIT-2.md` and `VISIT-2-FOR-CLAUDE.md` still hold Part 2 on installer B. That is now C-16a's work.

## The docs lane, Stage C's local plan files (overnight 25–26 Nov)
- **Fixed (local):** PLAN-C (intro, C-16a's and C-16b's After, Lanes, Stage accept, Effort, Cabin, the coordinator's hands-free paragraph); STANDING-C §1, §6 (AD-52's exception), §7 (the accepted arm-routine breach) and §9 (no parked chain: C-14 commits normally); BRIEF-C-16b (no parked chain; What 5, 6, 9, 10, 11); `docs/implementation/platform_refactor/duties/DUTIES-trunk_ceremony_recorder.md`'s A6 and global lines. Mismatches 1–4 and 7 above are closed in the plan files.
- **Committed:** `docs/design/rig_design.md` §4.8 (A5 ×7 → 0; A6 ×2 stay unless the narrowed A6 counts them out), §10's C-14 row (a test per #265 loss path; the A6 wording), §10's studio PC-visit paragraph (one installer; #270 on today's routine under the accepted breach), and `[^c-v1]`.
- **Not decided, put to the roles:** `docs/implementation/platform_refactor/rulings/MORNING-RULINGS-C.md` (three rulings: the narrowed lint has no item; #265's two extra loss paths and its golfer warning; GAP-25's link-loss halves and GAP-30 on Stage C with no item). Mismatch 4 (the `evidence` pointer) is settled by the addendum's decision 2, which names C-14: PLAN-C's C-14 row says so; BRIEF-C-14 still needs the line (not this lane's file). Decision 4 already sends GAP-29 to its own issue (not yet filed) and a role move to after Stage C.
- **Still open (not this lane's files):** mismatch 5 (#265's and #285's bodies say "C-16"), mismatch 8 (`VISIT-2*.md`, C-16a's work).

## The docs lane, settled fixes (overnight 25–26 Nov, after 15a8a67b)
- **Fixed (local):** BRIEF-C-14 carries the `evidence` pointer (What 7, Check 8, Accept, the test list, `[^add2]`; its `[^not-parked]` no longer says STANDING-C §9 lags). VISIT-2.md and VISIT-2-FOR-CLAUDE.md run on installer A only, about 60 min (45 without V5–V6): V7–V10 and rows 7a–10 and 12 dropped, the V numbers kept, V11 on A's routine with the kit change (trunk sensors off, HackMotion on), the ceremonies kept for #291 (C-V1, LEVERS §2.2). PLAN-E: Q5 ruled (H4 `[^e-ruled-25nov]`, D pack V2); SE-9a out; `studio_profile.h` ×4 stays with SE-4.
- **Issues:** #295 filed (GAP-29, "Zero orientation", addendum decision 4; part of #285; labels Calibration, follow-up, priority: low; no `bug`, found by reading). #265 and #285: "C-16" → "C-14" (three places), a one-line comment on each.
- **Calls (logged):**
  1. BRIEF-C-14 What 7: the pointer is approved, but no design names who tells the Rig (decision 4: only device objects call the sink). The brief builds the rest and leaves the pointer unset until ruled; the question is MORNING-RULINGS §4, recommending (a), a narrow Rig command called by the hand-over after a good write. Undo: drop the conditional from What 7 once §4 is ruled.
  2. VISIT-2-FOR-CLAUDE: with `BEFORE.md` missing, V1–V4 and V11 run and V5–V6 are skipped (D3 unknown); it used to be "Part 1 runs, Part 2 doesn't". Undo: let V5–V6 run on his word.
  3. #295's fix direction (clear the alignment, or correct the message) is not ruled: put as a choice on the issue and as MORNING-RULINGS §5 (reproduce first).
- **Still open:** PLAN-E's other rows lag the 20:20 Stage E ruling (MORNING-RULINGS, "Still open"). Stage E's 15 A6 findings and Stage D's 10 still wait on the lint edit no item holds (MORNING-RULINGS §1).
