# Stage C: five rulings for the morning (overnight 25–26 Nov 2026)

Three rulings from last night are now in Stage C's plan, its standing rules, the close's brief, the recorder's local duties page and the committed Rig design (15a8a67b):
- the calibration routines keep their logic in the screens' code until a separate calibration project after this programme (lever C-V1, #291);
- the arm routines' timed start is accepted until then;
- the fix for lost trunk-calibration data goes into the item that moves its file writing (#265, item C-14).

No ruling settles the five questions below, so the documents leave them open. Nothing they touch is scheduled until it is ruled.

## 1. Who edits the automatic architecture check, now that one of its rules has been narrowed? (Solution Architect)

**What it is.** The automatic architecture check, run with the tests, has a rule against public interfaces that pass loose, untyped bundles of data (rule A6). The object that stores a trunk calibration's raw sensor data (the trunk ceremony recorder) breaks that rule twice, in the two calls the trunk calibration screen makes to it. The Rig design said Stage C would remove both. That could only happen once the calibration logic moved out of the screens, which last night's ruling put off until the separate calibration project. So both stay on the check's list of allowed exceptions.

The Solution Architect has already ruled that the rule should cover only what its original wording names: the objects that feed the screens (presenters) and the golf objects themselves (domain types). The device-side objects would then leave it (Stage D's ruling V2). **But no item in any plan edits the check itself.** On the strength of that ruling, Stage D dropped its item to tidy the five device headers (10 exceptions). Stage E's plan prices its matching item at 0 h on the same assumption (15 exceptions). Nobody has scheduled the edit to the check that would make those exceptions go.

**The choice.**
- (a) **Add one small item to Stage D.** It narrows the check to the ruled wording and recounts the exceptions. My estimate is 1–2 h. It runs alone, never beside another item that edits the check or its exception list. If the recorder falls outside the narrowed rule, its two exceptions go then too. It is neither a presenter nor a golf object, so I expect it does.
- (b) **Leave the check as it is.** The recorder's two exceptions stay until the calibration project. Stage D's 10 and Stage E's 15 also stay, and no plan holds the work to remove them, so those stages' targets would miss.

**Recommendation: (a).** Without it, the ruling changes nothing in the code, and three stages' targets count on it.

## 2. Two more ways a trunk calibration's raw data can be lost, and whether the golfer is told (Product Owner; Design Lead only if a new screen element is needed)

**What it is.** A trunk calibration records the raw sensor data of the whole routine (the "ceremony"). The app holds it in memory and then writes it into the session's folder. Bug #265 found ways it can be lost without anyone knowing. Item C-14 fixes the two that #265 names: a failed write, and a failed backup copy of the earlier file. **Two more cases have no agreed behaviour:**
- a session starts with no folder to write into;
- the app is quit while the data is still held and not yet written.

Whether the golfer is told when this data cannot be saved depends on a guarantee that has been proposed to the Product Owner but not ruled: "a calibration's raw data is written with its session, or the golfer is told it was not" (GU-18).

For now, C-14's brief carries a written rule of its own. The app log always records a failed save. The golfer is told only through a screen element that already reports a failed save. If no such element exists, the item writes a proposal for the Design Lead and is accepted without that part.

**The choice.**
- No session folder:
  - (a) keep the data in memory and write it as soon as a session with a folder exists; *(added by the precheck, 26 Nov; proposed: if that can be a later session than the one the calibration was made in, ask the **Solution Architect** too, because the information model's row for this file says the session "is handed the ceremony when it starts, and the file shares the session's life" (FILE-SESS-11))*;
  - (b) record the loss in the app log and drop it.
- Quit while held:
  - (a) write it into the running session as the app closes, and if no session is running, record the loss in the app log;
  - (b) record the loss in the app log in every case.
- The golfer:
  - (a) adopt the guarantee as written, so the golfer is told whenever data that a session's shots depend on could not be saved;
  - (b) the app log only, for now.

**Recommendation: (a) for all three.** Losing the data means the golfer has to calibrate again, and "show with a warning, never hide" is a hard product rule. When no session is running, no shot used that calibration, so a line in the app log is enough there.

## 3. Three calibration faults listed under Stage C that no Stage C item fixes (Product Owner; Solution Architect for the guarantees)

**What it is.** The audit of how objects live and end found, by reading the code, places where the calibration the app shows can differ from the one the sensor applies. None has been seen happening. All are listed under Stage C on the tracking issue (#285). Stage C keeps today's calibration behaviour exactly (ruling PR-48), so no item fixes them, and the plan breaks the rule that every finding is placed (AG-9).

Rulings on the Rig design's addendum (decision 4) already place two parts:
- "Zero orientation" says the calibration is cleared but leaves it applied (GAP-29). The addendum rules that this becomes an issue of its own: filed as #295 (§5 below has its one open choice).
- A sensor moved to another body part keeps the old part's calibration (part of GAP-25). This is for the Product Owner and the Coaching Lead after Stage C.

Decision 4 also has the Rig's own record follow a HackMotion that drops out of "calibrated" for any reason, from item C-6b.2.

**Still unplaced:**
- A Witmotion sensor that drops out in the middle of a stream keeps its calibration and does not reconnect (GAP-25).
- When a HackMotion loses its calibration, the golfer sees only a changed status, with no message (GAP-25).
- A HackMotion can drop out of "calibrated" without sending the signal the rest of the app listens for (GAP-30).

The guarantees these break are both proposed to the Solution Architect and not ruled: "a sensor's calibration is cleared on a lost link or a move, and the golfer is told" (GU-17), and "the calibration shown is the one the sensor applies" (GU-27).

**The choice.**
- (a) **Give each its own issue**, marked "reproduce first", and take it off Stage C on #285.
- (b) **Move them to the calibration project after this programme (#291).**
- (c) **Add a Stage C item.** That breaks the ruling that Stage C changes no behaviour.

**Recommendation: (a).** These are faults in how the sensor objects handle a lost link and a status change. They are not part of the routines' logic that the calibration project moves, and decision 4 already sent the "Zero orientation" fault to an issue of its own.

## 4. Who tells the Rig where a trunk calibration's raw data was saved? (Solution Architect)

**What it is.** When the trunk calibration finishes, the app keeps a record of it in the Rig (the app's list of what cameras and sensors are set up, and how each is calibrated). The record can carry a pointer to the raw data the calibration came from: the session and the file name of the trunk ceremony. The Solution Architect has ruled that the pointer is not part of the Rig's fingerprint, so setting it changes nothing else, and that item C-14 sets it (the Rig design addendum's decision 2). **But no design says which part of the app tells the Rig.** The obvious channel, the one-way "calibration changed" link into the Rig (the sink), may only be called by the sensor objects themselves (decision 4), and the sensor objects never see the ceremony file. Choosing a caller adds a way into the Rig, which is an information-model matter.

**The choice.**
- (a) **The hand-over sets it, after a good write.** The small hand-over that the app's start-up code gives the trunk recorder (C-14's What 2) already passes the ceremony to the session. Once the session reports the file written, the hand-over tells the Rig the session and file name, through a new, narrow Rig command ("attach where this calibration's raw data is kept"), not through the sink. A failed write sets no pointer, so it never points at a file that was not saved.
- (b) **The session tells the Rig itself** when its write succeeds. The session, a golf object, would then reach into the Rig directly.
- (c) **Leave the pointer unset until the calibration project (#291)**, where the trunk routine's new driver owns the whole run and can set it. Nothing reads the pointer before then.

**The timing is part of the choice.** The ruling says the pointer is set when the session takes the ceremony. Under #265's fix a ceremony can be taken and then fail to write, so "taken" and "written" are no longer the same moment: (a) sets it only once the write succeeds.

**Recommendation: (a).** It keeps "only the sensor objects call the sink" true, it adds the least (one command, one caller, both in the code that already wires the hand-over), and it sets the pointer only when the file exists. Until this is ruled, BRIEF-C-14 builds everything else and leaves the pointer unset (logged), so C-14 is not held up.

## 5. What "Zero orientation" should do, once #295 is reproduced (Product Owner; Coaching Lead if a golfer instruction changes)

**What it is.** In Settings → Motion Sensors, and on the Capture page's sensor panel, each Witmotion sensor has a "Zero" button that resets which way the sensor counts as "straight". After a trunk calibration, the sensor's log then says the calibration is cleared, but the calibration stays applied and the next shots are saved as calibrated (#295, found by reading, not yet seen). The addendum's ruling only sends this to its own issue; it does not say which way to fix it.

**The choice.**
- (a) **Zero also ends the calibration**, and the golfer is told the sensor needs calibrating again. *Needs (added by the precheck, 26 Nov; proposed): the **Solution Architect** too. This is part of guarantee GU-27 ("a zero clears it"), which is proposed to the Solution Architect as a change to the Rig design and a new Rig rule (an information-model change), and the clear must go through the Rig (R-RIG-3).*
- (b) **Zero keeps the calibration, and the message is corrected.**

**Recommendation: reproduce first, then choose by what the readings show:** (a) if the calibration is measured against the "straight" the zero resets, (b) if it is not. Either fix waits until C-6b.2 has landed, so it is made once, on the Rig's paths.

---

## Settled tonight by earlier rulings (logged under AG-4; not questions)
- **The link from a calibration to its raw data was not unplaced.** The addendum's decision 2, ruled as recommended, names C-14 as the item that sets the pointer (`evidence`): the session and file name of the ceremony. Last night's re-scope changed C-14's sensor taps and the trunk ceremony's screen-facing object (its global), not this pointer. PLAN-C's C-14 row now says so, and so does BRIEF-C-14 (What 7, Check 8, its Accept, its test list and `[^add2]`). Who tells the Rig is not settled: §4 above.
- **C-14 is not held back for the visit** (STANDING-C §9 re-written). Holding back applied to items that replace a routine the golfer runs, and the re-scoped C-14 replaces none. To undo: put C-14 back in §9.
- **PLAN-C's figures are re-derived from its own table:** 30 items, about 66 h (lane 1 about 34.5 h, lane 2 about 22 h, docs about 9.5 h). Lane 2's gaps are filled with another stage's ready item, never a bug (PR-72).
- **STANDING-C §7** states the hands-free rule, and that the arm routines break it, accepted until #291. No Stage C item changes either routine. §6 carries the one accepted exception for rules in QML (AD-52).
- **The recorder's duties page** is local (`docs/implementation/platform_refactor/duties/DUTIES-trunk_ceremony_recorder.md`, gitignored), not under `docs/`. It was corrected there.

## Left for the coordinator (settled, outside this lane's files)
- ~~Add the `evidence` pointer to BRIEF-C-14.~~ Done (the caller waits on §4).
- ~~File the "Zero orientation" issue.~~ Done: #295 (part of #285).
- ~~#265's body and #285's GAP-26 line say "C-16".~~ Done: both say C-14, with a one-line comment on each.
- ~~`docs/implementation/platform_refactor/visits/VISIT-2.md` and `VISIT-2-FOR-CLAUDE.md` hold Part 2 on installer B.~~ Done: one installer, about 60 minutes; V7 to V10 dropped, the step numbers kept; V11 on A's routine.
- ~~PLAN-E lists Q5 as unruled.~~ Done: SE-9a out, `studio_profile.h`'s 4 findings with SE-4, citing H4 and V2.
- **Still open:** PLAN-E's other rows (Q6, Q9, Q17, the composition root's ring, the settings groups, the secrets store, AssemblyAI, #234, levers 7 and 8) still read as unruled, though the whole Stage E pack was ruled as recommended at 20:20. Re-set them, with the effort, when PLAN-E is next edited.
