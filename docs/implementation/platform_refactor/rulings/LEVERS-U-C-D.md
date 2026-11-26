# Lever pass: #224-U, Stage C and Stage D (25 Nov 2026)

## Summary

*Proposed, for the Product Owner (and the Solution Architect where a row says so). Nothing here is ruled. "Take" totals count only the levers recommended; hours are effort hours, as ORDER counts them.*

| Plan | Left now | Lever, in plain words (id) | Hours saved | What happens to them | Recommendation |
|---|---|---|---|---|---|
| **#224-U**, the presenter residue | 77.5 h, 29 items | The trust views need no code, as already ruled (U-V0) | 1–1.5 | dropped | **take** |
| | | Type the three big presenters' maps only when a screen that reads them is next changed (U-V1) | 23–25 | deferred, with a trigger | **take** |
| | | Leave two file reads for later: the condition history and the work-ons' stale-session check (U-V2) | 7 (8.5–10.5 with U-12's likely growth) | deferred, with a trigger | **take**; the Solution Architect approves the exceptions |
| | | Leave the shot-file "stamp" read and its three users for later (U-V3) | 7.5–10 | deferred, with a trigger | **your call** |
| | | **Take, in all** | **about 32 h** (31–33.5) | 77.5 h → about 45.5 h; 23 of 28 lines close, 5 go to an issue | |
| **Stage C**, the Rig | 68 h, 28 items | Keep the calibration routines' logic in QML for now; move it to C++ later, together with #266 and #270 (C-V1) | 22–23 | deferred, with a trigger | **take, if the Solution Architect agrees** (it defers part of his approved design) |
| | | Skip the scripted QML rename of the camera-view type (C-V2) | 1.5–2 | dropped | **don't take** |
| | | **Take, in all** | **about 22.5 h** | 68 h → about 46 h; visit 2 about 90 → 60 min | |
| **Stage D**, the capture seam | 86.5 h, 33 items | Leave out the camera and manager tidying (C++ splits that close nothing) (D-V1) | 8.5–9 | dropped | **take** |
| | | Send the vision-rules library work to an issue (D-V2) | 2.5–3 | moved to an issue | **take** |
| | | Leave the capture buffer's seven headers where they are (D-V3) | 4–4.5 | dropped | **your call** (re-opens a Solution Architect ruling) |
| | | Fix when a shot is created first, on today's code; decide the shot processor's split after the new tests (D-V4) | 10–12.5 | a decision deferred | **ask the Solution Architect first, then your call** |
| | | **Take, in all** | **about 12 h** (11–12) | 86.5 h → about 74.5 h | |
| | | **Missing from the plan, whatever is taken:** making a stopped shot finish or be recorded (GAP-1's other half), one owner for shot triggering (S11), landing the #281 overlap patch | **+7.5–13 h** (a guess) | to add | §3.4 |

**Recommended: about 66 h across the three, none of it work that an observed fault needs.** Your calls are about 21.5–27 h more (U-V3, D-V3, D-V4). Stage D is also missing about 7.5–13 h of ruled or seen work; add it whatever is taken.

**Nothing recommended cuts an item that an observed fault, a seen lifecycle loss or a ruled must-have needs.** Stage D's S2 (GAP-1) and T2 stay, and GAP-1's missing half is added. Where a lever leaves an approved design unbuilt or an exception to a HARD principle, its section says so plainly.

**Were these levers ruled before?** The forecast's levers 4, 9 and 11 (D-V2, D-V1, C-V1) were put to the Product Owner on 24 Nov[^fore]; no ruling on them is recorded in `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md` or on the dashboard. D-V2 re-opens B2's P3, which sent the work to Stage D.

This page does for three plans what Stage E's pack did with levers 7 and 8[^e-levers]. For #224-U it answers the Product Owner's condition that the items which only clear a lint line come back to keep, defer or accept[^u-cond] (§1.2).

---

## How to read this page

**What counts as value.** Four kinds, as the brief asks:
- **G**: a fix a golfer would notice, with its issue.
- **L**: a lifecycle-model gap closed (`governance/artefacts/lifecycle_model.md`; the audit, `docs/implementation/lifecycle_audit_25nov.md`; tracked on #285). "Seen" or "by reading", as the audit labels it.
- **R**: a ruling or a HARD rule satisfied, named by its id.
- **P**: a precondition for a later item that has value of its own.
- **lint** marks an item whose only effect is that an allowlist line goes. The principle the line stands for is named, because some are HARD (P11, "the persistence of an object is its owner's concern"; "models belong to the session").

**How items are ranked.** First what a golfer would notice or a ruled must-have needs; then what a later item needs; then rulings with no golfer change; then lint only. Within a band, cheaper comes first, and a chain keeps its order. The rank is a reading, not a formula.

**What a lever's hours mean.** *Dropped*: the work leaves the plan for good. *Deferred, with a trigger*: it leaves this programme's total and comes back when the named trigger fires, as lever 8's helper does after a failed quit pin[^e-levers]. *Moved to an issue*: it leaves the programme and waits on its issue. *A decision deferred*: the hours are still in the plan until the decision is made.

**Baselines, against ORDER.** Each plan's item table, less the items done and the items already ruled out. ORDER's percentages are by effort and were set at different moments, so they don't match these counts exactly[^order].
- **#224-U:** 84 h for 33 items; U-0 and the three inventories are done (6.5 h). **77.5 h for 29 items.** ORDER still says 2%.
- **Stage C:** 88 h for 39 items; eleven are done (C-0, the six inventories, C-1a, C-1b, the three golden traces: 20 h). **68 h for 28 items.** ORDER says 32%.
- **Stage D:** 98 h for 38 items; SD-0, SD-1c and SD-5 are done (7 h); SD-12c and SD-12d ruled out (V1, V2: 6 h); SD-9c parked with Stage F (3 h); T2's new seam item added (3 h) and SD-2a grown by 1.5 h (T1)[^d-ruled]. **86.5 h for 33 items.** ORDER's row still shows the plan's 70–125 h range.

---

## 1. #224-U, the presenter residue

### 1.1 Every remaining item, ranked

"Verdict" is this page's recommendation for each item (proposed, for the Product Owner).

| Rank | Item | h | Buys | Lint only? | Verdict |
|---|---|---|---|---|---|
| 1 | U-17 · the data viewer's export and Score row | 2 | **G**: the Score row reads 0 where nothing was scored; it goes (Q8, the Design Lead; the score retired from view, PR-32[^pr32]; "a missing value shows '-', a 0 is a bug"). The CSV export moves to Q7's helper (A5 ×4) | no | keep |
| 2 | U-4 · eleven unread members leave the QML API | 1.5 | 11 findings for 1.5 h: the adapter 29 → 20 (U-1i found a ninth unread member, F1), work-ons 14 → 13, the PDF writer 1 → 0; **P** for any later typing | no (dead API) | keep |
| 3 | U-P2 · pins for the report model and the review load | 3 | the first tests that build `SessionReportModel` or `SessionReviewController` (no test does today); **P** U-13, U-16a | no | keep |
| 4 | U-13 · the review load and bulk export through the Session | 3.5 | **R** P11 (HARD) and Q9: today a view's controller *writes* a shot's summary sidecar itself; a write outside its owner, not just a read. A5 ×4 | no | keep |
| 5 | U-18a · the Charts and Consistency presenters from a door | 3 | **R** "models belong to the session" (HARD) and Q1: two views create seven session-scoped presenters. A2 ×7 | no | keep |
| 6 | U-18b · the trust views and the data viewer | 1–1.5 (was 2.5) | **R** Q2, ruled view-local: the lint's list loses two names, four lines go, no code moves (U-V0) | no | keep, at 1–1.5 h |
| 7 | U-8q · **new, only if U-V1 is taken**: Q13's readout built in C++ as a typed value, and Q12's `expectations` retired | 2–4 | **R** Q13 (the Solution Architect: a view assembles a readout of its own) and Q12 (the Design Lead: retire the empty block). A slice of U-8a and its pin | no | add, if U-V1 is taken |
| 8 | U-19 · the close | 2 | the counts re-taken by an agent that built none of it | no | keep |
| 9 | U-6a · the mailer's results typed | 1.5 | A6 ×4 (P9); 12 uses in 3 files | yes | keep |
| 10 | U-7 · the report list typed | 2 | A6 ×3 (P9); 15 uses | yes | keep (not beside SE-2d) |
| 11 | U-14b · the kinematic-sequence value typed | 2 | A6 ×1 (P9) | yes | keep |
| 12 | U-15a · Markup's overlay through the review views' door | 2 | A5 ×2 (P11); the door exists | yes | keep |
| 13 | U-6b · the mailer's PDF through the Session | 2 | A5 ×4 (P11); Q5, Q7 | yes | keep |
| 14 | U-5 · three one-finding headers typed | 2.5 | A6 ×3 (P9); **P** U-13 | yes | keep |
| 15 | U-16b · the stills' video read and their cache | 2.5 | A5 ×3 (P11); Q7 | yes | keep (re-based to follow U-P2 if U-V3 is taken) |
| 16 | U-14a · the replay's load through the `RecordingSource` | 3 | A5 ×2 (P11); the door exists | yes | keep (not beside SD-8f) |
| 17 | U-15b · the delivery strip through the Shot | 2 | A5 ×2 (P11); needs Q11's new read | yes | your call (U-V3) |
| 18 | U-15c · the 3-D view's session read through the Session | 3 | A5 ×3 (P11); Q6's stop: if the source can't load only the parts it needs, 30 shots parse 30 whole documents | yes | your call (U-V3) |
| 19 | U-16a · the report's fingerprint and reads through the Session's shots | 3.5 | A5 ×3 and ×5 (P11); every fingerprint must stay byte for byte, or every report reads as out of date | yes | your call (U-V3) |
| 20 | U-11 · the history read through session results | 3.5 | A5 ×4 (P11); Q3. Costs a library-wide rewrite of every session summary (`resultsVersion` 5) | yes | defer (U-V2) |
| 21 | U-12 · the work-ons scan through the athlete level | 3.5, likely 5–7 | A5 ×7 (P11); Q4. The athlete level lacks five of the facts the scan reads (U-2i §8.2), which this item would have to add | yes | defer (U-V2) |
| 22 | U-P1 · the adapter's and work-ons' golden shapes | 2.5 | a pin for the typing only (the adapter already has 8 suites) | yes | defer (U-V1) |
| 23–25 | U-8a, U-8b, U-8c · the adapter typed | 10.5 | A6 ×20 (P9); Q12 and Q13 sit inside (taken out as U-8q) | yes | defer (U-V1) |
| 26–27 | U-9a, U-9b · the work-ons typed | 6 | A6 ×13 (P9) | yes | defer (U-V1) |
| 28 | U-10a · the report's `preflight`, `state` and options typed | 2.5 | A6 ×4 (P9); Q15. Carries a hazard of its own: the session list calls `state` by name, so typing it breaks the list silently unless that call moves too (U-3i F2) | yes | defer (U-V1) |
| 29–30 | U-10b, U-10c · the report's payload typed | 5.5 | A6 ×2 (P9); U-10c waits for Stage D's SD-10c | yes | defer (U-V1) |

**Lint only, in all: 22 items, about 60 h of the 77.5.** No item on the list closes a lifecycle gap (#285 places none on #224-U), and only U-17 changes what a golfer sees.

### 1.2 The lint-only items: keep, defer or accept

The Product Owner asked for each to be kept, dropped, or left as an accepted exception with a reason. Under the method's rule that every finding is placed[^sched], a dropped item's line can't just stay tagged "#224": it becomes a **deferral to an issue with a trigger** (the tag `# #NNN`, as `# #222` already is), or a **permanent exception** (`# kept (reason)`). This page proposes no permanent exception: each line below has a trigger at which the work becomes cheaper or necessary. It follows the draft test strategy's idea of an accepted gap: each line carries its reason, its approving role and the date, and the list only shrinks[^ts]. `#NNN` is an issue still to be opened, one for the five lines. Re-tagging a line is the Solution Architect's to approve, as U-0 classed each "(c) keep the line" option[^u0]; the hours are the Product Owner's.

- **Keep (8 items, 17.5 h):** U-5, U-6a, U-6b, U-7, U-14a, U-14b, U-15a, U-16b. Each is cheap, uses a door that exists or one already ruled, and has few call sites.
- **Your call (3 items, 8.5 h):** U-15b, U-15c, U-16a (U-V3).
- **Defer (11 items, 34 h as planned):** U-P1, U-8a–c, U-9a–b, U-10a–c (U-V1, less the U-8q slice), U-11, U-12 (U-V2).

The reasons, ready to paste into `architecture_allowlist.txt` once the tags are approved (proposed, for the Solution Architect). Each count is the one left after U-4.

| Line | Rule × n | Reason (the WHY text) | Trigger | Approves |
|---|---|---|---|---|
| `Gui/diagnostics/session_diagnostics_model.h` | A6 ×20 | P9: the diagnostics adapter's 20 remaining members reach QML as maps over the owners' results; typing them all (about 14 h with a pin) was deferred by the #224-U lever pass, since no fault is seen and a mistyped key fails silently in QML; each member is typed when its QML reader is next changed, from INVENTORY-U-1i.md | the next change to a reader of a member | the Solution Architect |
| `Gui/diagnostics/work_ons_controller.h` | A6 ×13 | P9: the work-ons' 13 members reach QML as maps; deferred by the #224-U lever pass, typed when a reader is next changed, from INVENTORY-U-2i.md; the Flow shape is shared with the report and is typed with it | the same | the Solution Architect |
| `Gui/report/session_report_model.h` | A6 ×6 | P9: `preflight`, `state`, the make options and the payload are maps; deferred by the #224-U lever pass. When typed, the session list's by-name call to `state` moves with it (INVENTORY-U-3i F2) and the options keep their bytes in FILE-SESS-13 (Q15) | the next change to the report's QML or to SD-10c's board map | the Solution Architect |
| `Gui/diagnostics/session_diagnostics_model.cpp` | A5 ×4 | P11: the condition history lists the golfer's other session folders and reads each ledger itself; ruled to go through the session summary (Q3), deferred by the #224-U lever pass to ride on the next `resultsVersion` bump, so the library's summaries are rewritten once, not twice | the next `resultsVersion` bump | the Solution Architect |
| `Gui/diagnostics/work_ons_controller.cpp` | A5 ×7 | P11: the stale-session scan reads stamps and the club baseline itself; ruled to ask the athlete level (Q4), which does not yet hold five of the facts the scan reads (INVENTORY-U-2i §8.2); deferred by the #224-U lever pass until the athlete level is next extended | the next change to the athlete level's freshness check | the Solution Architect |
| *If U-V3 is taken:* `Gui/charts/charts_delivery_presenter.cpp`, `Gui/swing3d/swing_rig_driver.cpp`, `Gui/report/report_meta.h`, `Gui/report/session_report_model.cpp` | A5 ×2, ×3, ×3, ×5 | P11: the presenter tells a shot's state by its document's size and time (`SwingStore::info`); ruled to read that through the Shot (Q11), deferred by the #224-U lever pass to the plan that replaces the stamp with the shot's events and stage versions (Q11's option b, P5) | the P5 staleness plan | the Solution Architect |

### 1.3 The levers

#### U-V0 · Q2's ruling, applied to U-18b's estimate
- **Hours:** 1–1.5, dropped (U-18b then takes 1–1.5 h, against the plan's 2.5). Q2 was ruled view-local, so U-18b edits the lint's list and deletes four lines, with no code moved. The plan priced it at 2.5 h for the door option and said itself that the view-local ruling saves 1.5 h[^planu].
- **What is lost:** nothing.
- **Depends:** nothing.
- **Risk:** none.
- **Recommendation: take.** (proposed, for the Product Owner)

#### U-V1 · The three big headers typed only when a reader is next changed
- **Hours:** 27 h of items deferred (U-P1 2.5, U-8a–c 10.5, U-9a–b 6, U-10a 2.5, U-10b 3.5, U-10c 2), less a new 2–4 h slice (U-8q) that keeps the two rulings inside the chain: **23–25 h, deferred, with a trigger.** This is the plan's own lever, "leaving the three big headers typed only where a reader is touched saves 35 h"[^planu]: of its 35 h, the 5 h of inventories are spent and U-P2 (3 h) stays, because U-13 and U-16a need it and it gives the report its first test.
- **What is lost:**
  - Three allowlist lines stay, with about 39 findings (adapter about 20, less what U-8q types; work-ons 13; report model 6). The reasons are in §1.2.
  - The latent defects the inventories found by reading stay latent until a reader is touched. For example, `kindText` is read and never written, so it always shows "" (U-1i F3), and the Charts components read keys their producer never writes (U-3i F5). None is seen by a golfer; each is noted in its inventory.
  - The inventories (5 h, done) were made to type these headers safely. They stay as the map for later typing, but they drift as the code changes.
  - **The Accept row is not met:** #224-U's "lines 28 → 0, findings 115 → 0" becomes "28 → 5, 115 → about 50" with U-V2, and the tagged lines "32 → 4" become about 9. It should be re-set to that, with the five lines named on the issue, as lever 8 re-set its own row[^e-levers].
- **Depends:**
  - Q13's ruling (a view builds its own readout) says U-8 moves the readout. Moving it into C++ and handing it back as a map would likely add an untyped member, which the allowlist forbids (the list only shrinks). So **U-8q** types the readout as a value with its own pin. If an existing map member can carry it unchanged, U-8q's first check will show that, and the slice is smaller (about 2 h). It is the "this shot" slice of U-8a, and it retires Q12's empty `expectations` (the Design Lead's ruling). If U-8q is not wanted either, Q13's fix is deferred with the chain, and a view keeps assembling a readout, against the HARD rule that models belong to their owner.
  - Q15 (the options keep their bytes) and Q10 (close at 27 of 28 until SD-10c) become moot: #224-U then closes with five lines on an issue, and U-10c no longer waits for Stage D.
  - The "after" columns change: U-11 and U-12 no longer wait for U-8c or U-9b (both are deferred anyway under U-V2), U-16a follows U-P2 alone, and U-10b goes.
- **Risk:**
  - Of not doing it: nothing observed needs it. One case would change that. Each read of a map property deep-copies the whole map into JavaScript (U-1i F10: `shotLog` is copied on every re-evaluation). If #284's freezes after each shot trace to those copies, typing the members on that path becomes a golfer-visible fix and comes back. Nothing shows that yet. #284's `[UiStall]` lines would show it, and should be read before anyone types for speed.
  - Of doing it: the inventories found QML that copies these maps with `Object.assign` in five places (U-1i F5, U-2i F4, U-3i F11). Whether a typed value survives that copy is not established, and if it doesn't, the screen loses keys silently.
- **Recommendation: take**, with U-8q. (proposed, for the Product Owner; the re-tags for the Solution Architect)

#### U-V2 · The history read (U-11) and the work-ons scan (U-12) deferred
- **Hours:** 7 h as planned, **deferred, with a trigger.** Counting U-12's likely growth, 8.5–10.5 h: the athlete level would first have to hold the five facts the scan reads that it doesn't (U-2i §8.2: `work_ons.json`'s own stamp, the frozen baseline, the folders' times, the fingerprint test and folders outside the index).
- **What is lost:** two lines and 11 findings stay. **These are exceptions to P11, a HARD principle**: two presenters keep reading files that are another object's. The Solution Architect's rulings on Q3 and Q4 stay unbuilt until the trigger.
- **Depends:** nothing waits on either item. U-11's trigger is the next `resultsVersion` bump. That bump rewrites every summary in the library anyway, so U-11 then costs no extra rewrite.
- **Risk:**
  - Of not doing it: none observed. The history read is off the GUI thread and runs only while reviewing (U-1i F13).
  - Of doing it: U-11 rewrites every session summary in the library once, a library-wide write-back. U-12 changes how the catch-up decides which sessions are stale, which feeds the coaching a golfer reads.
- **Recommendation: take.** (proposed, for the Product Owner; the exceptions for the Solution Architect)

#### U-V3 · The Shot's stamp read (Q11) and its three users deferred
- **Hours:** 8.5 h as planned (U-15b 2, U-15c 3, U-16a 3.5), or 7.5–10 h: the low end if U-16b's re-base is free, the high end if U-15c's stop had tripped. **Deferred, with a trigger**: the plan that replaces the stamp with the shot's events and stage versions, which Q11 itself names as the better end.
- **What is lost:** four lines and 13 findings stay, all **exceptions to P11, a HARD principle**. Q11's approved read on the Shot is not built, because U-12 (deferred under U-V2) is its only other user.
- **Depends:** U-16b re-bases to follow U-P2. Nothing else waits.
- **Risk:**
  - Of not doing it: none observed.
  - Of doing it: U-16a must keep every report fingerprint byte for byte, or every report reads as out of date. U-15c has a stop: if the recording source can't load only the parts the 3-D view needs, a 30-shot session parses 30 whole documents.
- **Recommendation: your call.** These three are the core of what #224-U is for (presenters reading through their owners), and they cost about as much per line as the items kept. Take this if hours matter more than closing most P11 lines in one go. (proposed, for the Product Owner)

---

## 2. Stage C, the Rig

### 2.1 Every remaining item, ranked

| Rank | Item | h | Buys | Verdict |
|---|---|---|---|---|
| 1 | C-2a · the one perspective header | 1.5 | **P** C-3; **R** the design's Q10 | keep |
| 2 | C-1c · the sensor and shot sites use the keys | 2 | **P** C-6a.1; **R** C5's identities | keep |
| 3 | C-7b · the two assumptions as `assumed` records | 1.5 | **R** RP-8, so an assumption is never recorded as a measurement (#178); **P** C-8b (RP-10) | keep |
| 4 | C-3 · the Rig's types, ids and digest | 3 | **P** every Rig item; **R** ID-RIG | keep |
| 5 | C-4 · `Rig`, its store, `app.rig` | 3 | **P** every Rig item; **R** LC-RIG's creation at launch | keep |
| 6 | C-8a · the `rig` block's reader | 2.5 | **R** the ruled requirement that each Recording names the rig state it was captured with[^im23]; **P** C-8b | keep |
| 7 | C-8b · the `rig` block's writer, switched on | 3 | the same **R**; **P** Stage D's SD-1a and SD-1b | keep |
| 8 | C-5a.1 · the camera commands, Impact exclusive, `RigChanged` | 2.5 | **R** one writer for the camera set-up (RIG-T4); **P** C-5b | keep |
| 9 | C-5a.2 · the copy from settings, with its fingerprint | 2.5 | **R** the design's Q6: no setting silently ignored after a downgrade (loss is blocking); **P** C-5b | keep |
| 10 | C-5b · the cameras cut over | 3 | **R** SET-09's one writer; **P** SD-1d, SE-5f | keep |
| 11 | C-6a.1 · the role rules move to `src/IMU` | 1.5 | **R** C9; **P** C-6a.2 | keep |
| 12 | C-6a.2 · the sensor half of the Rig | 2.5 | **R** one claim per role, held by the Rig; **P** C-6b.1 | keep |
| 13 | C-6b.1 · the sensors cut over | 3 | **R**; **P** C-6b.2 | keep |
| 14 | C-6b.2 · every set path commits, every clear path clears | 3 | **R** R-RIG-3 and the addendum's decision 4 (the record follows the live sensor); **P** C-8b, SD-1e. The Rig then tells the truth about GAP-25 and GAP-29's states. It does not fix them (§2.3) | keep |
| 15 | C-7 · the club-length prior becomes the Rig's | 2 | **R** DEC-153; **P** SE-5f | keep |
| 16 | C-14 · the ceremony file behind the Session's door | 2.5 (+0.5–1 for #265) | **L** GAP-26, by reading (#265), once the fix is in this brief (§2.3); **R** the design's Q5; A5 ×7 | keep (re-scoped under C-V1) |
| 17 | C-16a · visit 2's pack | 1.5 | **P** the visit | keep |
| 18 | C-16b · the close | 2 | **P** SE-5f (Stage E removes the copied keys after Stage C's close) | keep |
| 19 | C-2b · the scripted QML rename of the perspective | 2.5 | **R** the audit's A-13 (two enum types become one), approved as the design's Q10. About 120 uses in 19 QML files, by script | keep (C-V2) |
| 20 | C-11a · `TrunkProcedure` | 3 | **R** RP-7 (approved): thresholds and transitions in C++, with time as an input. No golfer change | defer (C-V1) |
| 21 | C-12a · `WitmotionArmProcedure` | 2.5 | the same | defer (C-V1) |
| 22 | C-13a · `HackMotionArmProcedure` | 2.5 | the same; where #270 would be fixed if it is real | defer (C-V1) |
| 23 | C-11b.1 · the trunk driver and `app.calibration` | 2.5 | the same; on `main.cpp` | defer (C-V1) |
| 24 | C-11b.2 · the trunk routine as bindings (parked) | 3 | the same; replaces a routine a golfer runs | defer (C-V1) |
| 25 | C-12b · the Witmotion routine as bindings (parked) | 3 | the same | defer (C-V1) |
| 26 | C-13b · the HackMotion routine as bindings (parked) | 3 | the same | defer (C-V1) |
| 27 | C-15 · what is left in the flow and the status view (parked) | 2 | the same | defer (C-V1) |
| 28 | C-17 · the three real ceremonies as fixtures | 1.5 | proves `TrunkProcedure` on real inputs | defer (C-V1); **the ceremonies are still recorded at visit 2** |

### 2.2 The levers

#### C-V1 · The calibration procedures stay in QML for now
- **Hours:** C-11a, C-12a, C-13a, C-11b.1, C-11b.2, C-12b, C-13b, C-15 and C-17 come to 23 h. C-14 stays, re-scoped (below), at the same 2.5 h or up to 1 h more, since its file write still moves but its taps don't. **22–23 h, deferred, with a trigger:** one calibration track after this programme, which builds the procedures together with #266 (the arm routines wait for the arm to be on and still) and #270 (a HackMotion attempt completed by the previous attempt's state). This is what remains of the forecast's lever 11 once the inventories and the golden traces are done[^fore].
- **What is lost:**
  - **This defers part of a design the Solution Architect approved (24 Nov): RP-7 and §4.8's procedures in C++.** It also defers the Product Owner's own wish of 14 Nov that QML holds no rules and only binds. It needs the Solution Architect's agreement.
  - 3,978 lines of calibration QML keep their thresholds and transitions.
  - The `trunkCeremony` global stays, which is consistent with Q17's rule that a global leaves only when its owner changes.
  - The addendum's decisions 1 (its second half), 3, 5, 6 and 7 wait with the track.
  - **The Accept row is not met:** "calibration QML (3,978 lines): thresholds, maths, transitions → 0" becomes "unchanged, pinned by the three golden traces", and the `trunkCeremony` global stays.
- **What is kept:**
  - The Rig and its records are complete without the procedures. C-6b.2 commits a record from today's paths, and under the addendum's decision 4 the device object is the only caller of the sink, whoever runs the routine.
  - The `rig` block and every Stage D and Stage E dependency are kept.
  - The three golden traces (8 h, done) stay as the QML routines' regression tests.
  - The three real ceremonies are still recorded at visit 2 on installer A. They are the only real inputs, and they cost a few minutes.
- **C-14, re-scoped:** the ceremony's file write goes behind the Session's door (FILE-SESS-11's one writer, Q5) while the recorder keeps its taps. **#265's fix (GAP-26) goes into this brief whichever way this lever goes.** Its 0.5–1 h is owed in any case, so it is not netted here (§2.3). A5 ×7 → 0 should still follow, since the line's own reason names only the recorder's file write; C-14's check-first lists the seven by line. Its A6 ×2 (`attachResult`, `checkTurn`) are the QML routine's calls on the recorder. They stay unless V2's narrowed A6 puts a capture object out of the rule's scope, which the lint's re-count will say.
- **Depends:**
  - Nothing in Stage D or Stage E. Stage D's inventories wait on C-5b, C-6b.2 and C-8b, all in the Rig's lane, not the procedures'.
  - It removes C-11b.1 and the driver half of C-14 from `main.cpp`, which Stage E and #224-U also queue for.
  - **Visit 2** (`docs/implementation/platform_refactor/visits/VISIT-2.md`) loses installer B's part: V7 to V10, the install and the trunk and Witmotion procedures on B, about 30 of its 90 minutes. **#105's check stays** (V1 and V2, on installer A). **The three ceremonies stay** (V2 to V4, on A). **#270's two HackMotion attempts in a row stay**, run on A's routine (V11), which is the routine #270 was found in. If the re-scoped C-14 lands before the visit, installer A carries it, and V2 to V4 check its write on real ceremonies.
  - **The cost of deferring:** the traces freeze today's QML. If #266 or #270 is fixed in QML first, each touched trace is re-frozen by decision (about 1 h a routine), and the C++ port then ports the fixed routine.
- **Risk:**
  - Of not doing it: no observed fault needs it. #270 has been seen only through the harness's fake device. #266 is an enhancement that closes a HARD rule's gap: the arm routines are not hands-free today. That can be built on either side of the port, and the trigger is chosen so it isn't built twice.
  - Of doing it now: it replaces three routines a golfer runs. Headless tests are no evidence that a routine is usable, and only the visit proves it. The work sits as a parked patch that must be re-keyed after each of Stage B's moves.
- **Recommendation: take, as a deferral, if the Solution Architect agrees.** (proposed, for the Product Owner and the Solution Architect)

#### C-V2 · The perspective's QML rename skipped
- **Hours:** 1.5–2 h, dropped (C-2b's 2.5 h, less a compile-time check that keeps `CameraInstance`'s enum equal to the one Vocabulary type).
- **What is lost:** the audit's A-13 stays half open (two enum types, one checked against the other). The design's approved answer to Q10 is not met.
- **Depends:** C-5b follows C-2b in the plan. It could re-base onto C-2a.
- **Risk:** a scripted QML rename fails only at run time, but the `tst_setup_*` files and the before-and-after sheets cover it. The saving is small.
- **Recommendation: don't take.** (proposed, for the Product Owner)

### 2.3 Two things the Stage C plan gets wrong today

1. **#265's fix is in the wrong brief.** #285 places GAP-26 on Stage C, and #265 says its fix goes with the move of the recorder's write. BRIEF-C-16a, the visit pack, carries it ("Added by the coordinator… fix the recorder's loss paths as part of this move"). BRIEF-C-14, which makes the move, says "each 'dropped' case behaves as today". Losing a ceremony is losing calibration evidence, which is blocking under the loss rule. The fix belongs in C-14's brief, with a test per path (+0.5–1 h).
2. **#285 places GAP-25, GAP-29 and GAP-30 on Stage C, and no Stage C item closes them.** Stage C keeps today's calibration behaviour exactly[^pr48], and the addendum's decision 4 files `zeroOrientation`'s mismatch as its own item. After C-6b.2 the Rig's record matches the sensor, so the gaps become visible in the data, but they are not fixed. Each is "verify first" on #285. Either #285 moves them to their own issues, or the calibration track takes them. This page adds no hours for them.

---

## 3. Stage D, the capture seam

### 3.1 Every remaining item, ranked

Three rows are not in the plan (marked **new**). They are ruled or seen work with no item; their hours are guesses (§3.4).

| Rank | Item | h | Buys | Verdict |
|---|---|---|---|---|
| 1 | SD-1a · inventory: post-roll, gather, freeze | 1.5 | **P** SD-2a (a duties inventory before any cut-over[^ag10]) | keep |
| 2 | SD-2a · the capture test rig | 5 | **P** every pin; **R** T1; the first test that builds a shot processor (E-09: "untested") | keep |
| 3 | SD-2t (name proposed) · T2's seam: a synthetic camera back-end and a deferred-source interface | 3 | **R** T2, a ruled must-have; **P** the 4–7 pins the rig can't reach alone, #53's two cases among them | keep |
| 4 | SD-2c · write pins and the scratch cases | 3 | **G** reproduces #188, #192, #235 (1) and #53's back-to-back and near-End cases on the rig, before anything moves | keep |
| 5 | SD-2b · timing pins | 3 | **P** SD-8a onward; the shot processor's timing invariants (27 ⚠ notes) | keep |
| 6 | SD-1b · inventory: export, bookkeeping, analysis start, replay | 1.5 | **P** SD-8d (the allocation is in the export job's builder today) | keep |
| 7 | SD-8d · bind the session at the trigger, allocate at the freeze | 3 | **L** GAP-1, seen (#280, #188); **R** S2, a ruled must-have; **G** #188 (a shot saved into a stray session) and #235 (1) | keep |
| 8 | **new** · the teardown that finishes or records a shot | 2–4 | **L** GAP-7, GAP-18, GAP-19 (seen: folders with media and no document are in the studio's library). GAP-1's own text says the allocation alone saves no shot: it turns each drop into a folder with no document[^gap1]. GU-3, proposed for the Solution Architect, names this work beside SD-8d | **add** |
| 9 | SD-8e · late launch-monitor readings through the Shot | 2 | **G** #192, if it reproduces (the plan drops it otherwise, N5) | keep, conditional |
| 10 | SD-10a · the device-only create's writer, with handedness | 2.5 | **R** R-REC-8 (approved): a device-only recording records handedness; A5 ×1 | keep |
| 11 | **new** · S11 and S13: one owner of the trigger policy | 2.5–4 | **R** S11, S13 (ruled 25 Nov). Today the launch monitor's rescue overwrites a shot's trigger source (SD-1c F4): a source fact lost, which the loss rule treats as blocking | **add** |
| 12 | **new** · land the #281 overlapping-capture patch | 3–5 | **L** GAP-2, GAP-3, GAP-4, seen (#281, #53). The Product Owner ruled it not urgent (raking, not practice) and that Stage D starts from it | **add** |
| 13 | SD-15a · visit 3's pack | 1.5 | **P** the visit, the only live proof that no shot is lost | keep |
| 14 | SD-15b · after the visit: live equals re-analysis | 2 | the same | keep |
| 15 | SD-15c · the close | 2 | the counts re-taken | keep |
| 16 | SD-1e · inventory: the managers, the 24 fallbacks | 1.5 | **P** SD-12b | keep |
| 17 | SD-12b · the managers' 20 fallbacks: settings injected | 2 | **R** the 24 fallbacks go in Stage D (ruled); a missing settings object today falls back to the real store, which no test may touch | keep |
| 18 | SD-10b · the board owned by the session | 3 | **R** S5 and the HARD rule that models belong to the session; A2 | keep |
| 19 | SD-10c · the board's `graphics` map typed | 3 | a renamed board key reads 0 or NaN today, silently (U-3i F19: "a 0 is a bug"); A6 ×3; **P** U-10c, if U-V1 is not taken | keep |
| 20 | SD-9b · the shot source as a persisted type | 2.5 | **R** S4: no future source can silently change what an old file's number means | keep |
| 21 | SD-6 · the enumerator out of `Core`, the GUI-thread assert | 3 | **R** S6; the thread assert the Rig design asks for | keep |
| 22 | SD-7 · the motion-capture probe out of `src/Pose` | 2.5 | **R** S7; #86's two items | keep |
| 23 | SD-8a · the capture interfaces, through thin adapters | 3 | **R** S1's ruled shape; **P** SD-8b, SD-9a | your call (D-V4) |
| 24 | SD-8b · the gather half out (parked) | 3.5 | **R** S1; E-09's "mixes everything"; A1 13 → about 5 | your call (D-V4) |
| 25 | SD-8c · the export job and frozen window out (stacked) | 3 | the same | your call (D-V4) |
| 26 | SD-8f · analysis start and replay out; the class deleted | 3 | the same; A1 → 0; the class's 4 fallbacks | your call (D-V4) |
| 27 | SD-9a · `ShotController` on narrow interfaces | 2.5 | A1 ×3 (lint); S1's interfaces | keep (re-based if D-V4 is taken) |
| 28 | SD-13 · the capture files out of `Gui` | 3 | **R** S9: one ring to a directory; **P** SD-14. Runs alone | keep |
| 29 | SD-1d · inventory: `CameraInstance` | 1.5 | **P** SD-11c | keep |
| 30 | SD-11c · live pose and ball across the boundary (parked) | 3 | **R** S8 (R-CTX-1) | keep (re-based to follow SD-1d under D-V1) |
| 31 | SD-3 · the window's handle as a plain value | 2.5 | lint A1 ×2; **R** S3 (the coordinator's call) | your call (D-V3) |
| 32 | SD-4 · Buffer's seven headers to `src/Vocabulary/Buffer/` | 2 | lint: 7 per-file entries; **R** J-109. A scripted move that runs alone | your call (D-V3) |
| 33 | SD-11a · `CameraInstance`'s pipeline to a collaborator (parked) | 3 | none: closes no lint line and no issue | drop (D-V1) |
| 34 | SD-11b · the ball-presence monitor to a collaborator (parked) | 2.5 | none | drop (D-V1) |
| 35 | SD-12a · the managers share selection mechanics | 3.5 | none named. GAP-7 lives in this code, but the new teardown row covers it, not this item | drop (D-V1) |
| 36 | SD-14 · `pp_rules_vision` | 3 | Rules sources leave 55 of 5,698 test listings (1%) | to an issue (D-V2) |

### 3.2 The levers

#### D-V1 · Camera and manager tidying left out (the forecast's lever 9)
- **Hours:** SD-11a 3, SD-11b 2.5, SD-12a 3.5: 9 h. SD-11c re-bases to follow SD-1d (0–0.5 h more). **8.5–9 h, dropped.**
- **What is lost:** `CameraInstance` stays 2,186 lines, and the two managers keep a copy each of their selection mechanics. No lint line or issue closes on these items[^fore]. The three items' Accept counts ("no line of the pipeline in the instance", "one copy of each") go with them.
- **Depends:** two of visit 3's parked patches go. Stage E's SE-6d no longer waits on SD-11b (lever 8 dropped it). SD-12b re-bases to follow SD-1e.
- **Risk:** none observed needs it. These are among the stage's riskiest edits (the cameras' threads), and the visit is the only proof of them.
- **Recommendation: take.** (proposed, for the Product Owner)

#### D-V2 · `pp_rules_vision` to an issue (the forecast's lever 4)
- **Hours:** 2.5–3 h, **moved to an issue** (3 h, less the half hour or so of writing the issue).
- **What is lost:** the Rules sources stay linked into 55 of 5,698 test listings (1%).
- **Depends:** this re-opens B2's P3 (the Product Owner's ruling of 24 Nov, which sent it here). Nothing waits on SD-14.
- **Risk:** none.
- **Recommendation: take.** (proposed, for the Product Owner)

#### D-V3 · Buffer's headers stay where they are (SD-3, SD-4)
- **Hours:** 4–4.5 h, dropped (SD-3's 2.5 and SD-4's 2, less re-setting SD-13's Accept count for Buffer).
- **What is lost:** the window types keep their A1 ×2, and the seven Buffer headers keep per-file Vocabulary entries. **It re-opens J-109, the Solution Architect's ruling that Buffer's stream types move in Stage D,** and SD-13's "Buffer no longer spans rings" is re-set.
- **Depends:** nothing reads the new location. SD-4 runs alone on the tree, so dropping it gives back a slot in which both lanes stop.
- **Risk:** none: the per-file entries keep the lint exact.
- **Recommendation: your call, after the Solution Architect.** I lean towards taking it, since it is lint only. (proposed, for the Product Owner and the Solution Architect)

#### D-V4 · The split decided after the pins, with S2 first on today's class
- **The order.** Do SD-8d and the new teardown item straight after the pins, on today's shot processor, and decide the split (SD-8a, SD-8b, SD-8c, SD-8f) with the pins' results in hand.
- **Evidence for and against.**
  - For: today's class already allocates at the gather's end on one path. The skip-analysis capture calls `startSwingSave()` there (`src/Gui/shot/shot_processor.cpp:604-607`), which reaches `Session::addShot` (`:1234`). The #281 patch also reworks the unsplit class.
  - Against: PLAN-D's studio PC paragraph says the split can't be re-ordered because "the gather half has to leave first". S1's shape was ruled tonight, and the allocation sits inside the export job's builder (`:1228-1240`), which SD-8c moves.
  - **So this is a question for the Solution Architect before it is a lever:** can S2 be built on the unsplit class, with the split carrying it afterwards?
- **Hours:** if the split is later dropped, 12.5 h less about 1 h to delete the class's four fallbacks on their own, and SD-9a re-based: **10–12.5 h.** Until the decision, nothing leaves the plan, so none of it is in this page's "take" figure.
- **What is lost if the split is dropped:** S1's ruled shape stays unbuilt. A1 ×17 and E-09's "mixes everything" stay. SD-13 moves the class whole.
- **What is gained by the order alone:** GAP-1's fix no longer sits behind two parked patches (SD-8b, SD-8c), and visit 3's stack is smaller.
- **Risk:** of the order, an edit to today's class that the split must then carry, under the pins. Of dropping the split, none observed, though future overlapping capture would be easier on the split class.
- **Recommendation: ask the Solution Architect first; then your call.** If he agrees, take the order. I would not drop the split yet. (proposed, for the Solution Architect and the Product Owner)

### 3.3 Not offered as levers
- **SD-8e** is already conditional on #192 reproducing (the plan's N5).
- **SD-9b** could become a compile-time check on the numbers (S4's option c) now that SD-9c is parked, saving about 1.5 h. S4 was ruled (a), and S11's fix to the overwritten trigger source needs a type to hold two sources, so it is kept.
- **SD-6 and SD-7** are ring moves, but each carries something else: a thread assert, and #86's two items.

### 3.4 What the Stage D plan does not hold
Each of these is ruled or seen, and has no item. The hours are guesses.
1. **GAP-1's teardown half (2–4 h).** The shot processor's `finishNowBlocking` stops the workers and clears the flags their next steps need, so a shot in its post-roll, gather or save is dropped (GAP-7, GAP-18, GAP-19)[^gap1]. S2 alone moves where the loss lands, not whether it happens. GU-3 ("an accepted shot is never lost without a trace") is proposed for the Solution Architect and names this work beside SD-8d. It needs his ruling first.
2. **S11 and S13 (2.5–4 h).** One owner for trigger policy, out of `main.cpp`'s lambdas. The device-only shot goes through it, the rescue records its own source beside the trigger's, and the local arbiter folds in. Ruled tonight, with no item.
3. **The #281 landing (3–5 h).** A test of the shot queue on SD-2a's rig, and the memory per queued shot (est. 1.2 GB) and the copy time measured on the studio PC. The patch exists, and its notes list what is untested.

Together about **7.5–13 h**: Stage D is nearer 94–100 h before any lever, and 82–87.5 h after D-V1 and D-V2.

---

## 4. Across the three plans

- **U-10c and SD-10c.** Under U-V1, #224-U no longer waits for Stage D: the report's payload line is deferred, and SD-10c stays in Stage D for its own sake (F19).
- **`main.cpp`.** It allows one lane at a time. C-4, C-11b.1 and C-14 (Stage C), SE-2a–d (Stage E), and U-6b, U-11, U-12, U-13, U-15c, U-16a and U-18a (#224-U) all queue for it. C-V1, U-V2 and U-V3 remove five of those slots.
- **Stage E's SE-6d** no longer waits on Stage D's SD-11b (lever 8), so D-V1 touches nothing in Stage E.
- **Stage D's inventories** wait on Stage C's C-5b, C-6b.2 and C-8b, all in the Rig's lane. C-V1 moves none of them, so it does not delay Stage D. It brings Stage C's close, and so SE-5f, forward.
- **The visits.** Visit 2 loses installer B's part under C-V1 (V7 to V10, about 30 of its 90 minutes); #105, the ceremonies and #270 stay. Visit 3 loses two parked patches under D-V1, and two more if the split is decided against (D-V4).
- **#273** (the session type steers nothing): each item that rewrites a file removes that file's uses. A deferred item leaves its file's uses for #273's own work.

---

[^e-levers]: `docs/implementation/platform_e_host_settings.md`, V7 and V8; ruled 25 Nov 2026 in the coordination session, with the Product Owner's condition on lever 8 (quit tests aimed at GAP-20 and GAP-21; a fault they find brings the helper back).
[^u-cond]: `docs/implementation/platform_u_presenters.md`, the ruling's footnote: the Product Owner's condition, 25 Nov 2026.
[^order]: `build/platform/ORDER.md`, the estimate table, as updated 25 Nov 2026 20:20.
[^d-ruled]: `docs/implementation/platform_d_capture_seam.md`, "Effect on the item plan"; ruled 25 Nov 2026 (the Solution Architect and the Product Owner), with S13's two-phone part parked with Stage F (#272).
[^pr32]: The Design Lead, 24 Nov 2026 (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, PR-32); Q8 ruled (a) by the Design Lead, 25 Nov 2026.
[^sched]: The method's rule that every audit finding is placed: a step, an agreed deferral or a rejection.
[^u0]: `docs/implementation/platform_u_presenters.md`: each question's "(c) the line is re-tagged `# kept`" option is classed under the Solution Architect; the allowlist's header makes a new entry an architecture decision.
[^planu]: `docs/implementation/platform_refactor/plans/PLAN-U.md`, "Effort", its levers.
[^im23]: `governance/artefacts/information_model.md` §2.3, as cited in `docs/design/rig_design.md` §1.
[^fore]: `docs/implementation/platform_refactor/plans/FORECAST.md`, "The scope levers", rows 4, 9 and 11.
[^pr48]: `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, PR-48: the refactor keeps today's calibration behaviour exactly.
[^ag10]: `governance/controls/kinds-of-work.md`, AG-10: a duties inventory before a cut-over.
[^ts]: `build/platform/TEST/test_strategy.md` §8, item 8 (draft 0.2, proposed, for the Solution Architect).
[^gap1]: `docs/implementation/lifecycle_audit_25nov.md`, GAP-1: "Saving every accepted shot needs both changes: the early allocation, and a teardown that finishes or records the shot." The lifecycle model's GU-3 (proposed, for the Solution Architect) says the same.
