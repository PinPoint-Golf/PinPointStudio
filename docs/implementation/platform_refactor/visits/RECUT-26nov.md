# The studio PC visits, re-cut to the 26 Nov rule

> **Later on 26 Nov: visits 2 and 3 are combined** (the Product Owner; `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Visits 2 and 3 combined"). §1's visit 2 and 3 rows and its bay-time line (now about 85 min in one visit), the choice under §1, and §4's C-16b line are overtaken: see `docs/implementation/platform_refactor/visits/FOLD-2-into-3.md`. The rest of this page, as reviewed and signed off, still holds.

**The ruling** (the Product Owner, 26 Nov 2026, midday, in his words): "a studio PC visit with more than one phone is not going to happen as I only have 1 phone. so studio PC visit 5 is not happening. also please do make sure those visits need golf clubs and a ball being hit. I don't want to waste time there doing checks we can automate or do at the desk."[^ruling]

**What is ruled, and what is proposed.** Three things are ruled: visit 5 is cancelled; a visit holds only steps that need clubs and a ball hit; and each long-lived thread gets an automatic quit test, not a hand-run quit at the studio (E1, morning batch 4)[^mb4]. Every placement below is the docs lane's reading of those rulings: **(proposed, for the Product Owner)** unless a row says it is already true. Visit numbers are kept, so the plans and notes still match.

**The test used for each step:** would the step still exist if no ball were hit? If yes, it leaves the visit. A ball step's own set-up (starting the app, an install between passes, the sensors put on) and the reading of its results go with it (BT-43). A shot hit only so that End keeps the session (End on a session with no shots throws it away) does not make a step a visit step.

**Where a step goes:**
- **(a)** automated, unattended on the studio PC through the studio PC runner: tests and builds only, never the app;
- **(b)** automated on the Mac: the headless launcher, or a test in the Mac's suite;
- **(c)** a short desk check by the Product Owner on the Mac;
- **#272**, Stage F's parked follow-up, when a check truly needs two phones or Stage F's own parked work;
- **no home**: a check that could only be automated with work no item holds. §3 sizes each one.

## 1. The visits now

| Visit | Was | Now | Desk time on the Mac it moves to | Commits that wait for it |
|---|---|---|---|---|
| 1 (Stage A) | about 50 min | **done 25 Nov**; not re-cut | — | — |
| 2 (Stage C) | about 60 min | **about 20 min**: End and quit while a shot is analysing (#280, #281), four sessions with the cameras and the launch monitor only | about 25 min: the three trunk ceremonies (15), the HackMotion routine twice (10) | none (Stage C has no parked chain); Stage C's close (C-16b) reads its shots, and the sensors' part of it waits for visit 3 (row 2.7) |
| 3 (Stage D) | about 90 min, two installers, two phones | **about 70 min**: the latest-main pass, then main plus the parked chain, one phone (an estimate: SD-15a writes the procedure; row 3.1) | none | Stage D's parked timing items (SD-8b, SD-8d, SD-11c) |
| 4 (Stage E) | about 15 min at the studio PC, plus 15 at the desk | **goes**: no step in it needs a ball | about 15 min: the signed upgrade (it was already a desk step); optional, about 10 min of quits with real devices busy (§2, row 3.9) | SE-2b and SE-7b, re-gated (§4) |
| 5 (Stage F) | about 60 min, two phones and a cabled phone | **cancelled (ruled)** | none now; the cabled phone, about 5 min, if #272 is picked up | none: Stage F is parked (#272) |

**Time at the bay still ahead: about 90 minutes in two visits** (about 20 + 70), against about 225 before (60 + 90 + 15 + 60). About 25 minutes newly move to the desk (the ceremonies and the HackMotion routine), plus an optional 10; the signed upgrade's 15 was already a desk step.

**Visit 2 is now short enough to ride on any practice at the bay.** It needs only the latest main, which the Product Owner installs every visit anyway, so it can run the next time he is hitting balls there (proposed). The other choice is to fold it into visit 3, which already has "End with a shot in flight"; that saves one trip but makes Stage C's close wait for Stage D's visit.

## 2. Every visit step: where it goes

### Visit 2 (`docs/implementation/platform_refactor/visits/VISIT-2.md`, as it stood on 25 Nov after C-V1)

Steps D1 to D11 already ran before the visit and are unchanged, except that **D3's answer is now yes**: "End anyway" and "Close anyway" are on main since `a878282f` (25 Nov), so V5 and V6 run.

| # | Step | Goes to | Why |
|---|---|---|---|
| 2.1 | V1: start Premier and the app; put on the sensors | **stays** as the start of V5a (about 3 min), **without the sensors** | starting the app and the launch monitor is the set-up of a ball session; the sensors aren't needed for V5 and V6 |
| 2.2 | V1: #105, watch both trunk sensors connect | **(a)** and **(b)**: `wt901_config_check_test` and `wt901_config_retry_test`, in the suite on both machines (D2). The real connect on Windows happens anyway at visit 3, which wears the sensors, and its settings read-back lines are read from that session's Message Log, exported straight after set-up (the log keeps 500 lines). No step | needs no ball. A sensor check on the desk would be a device test, which AD-11 forbids asking of the owner |
| 2.3 | V2 to V4: three trunk ceremonies, each in its own session with 1 shot | **(c)**: three set-ups on the Mac with the cameras and the ball step skipped, each to a started session, about 15 min, once the sensors' harness has arrived. The ceremony file is written when the session starts and is copied out then, so End with no shot losing the session doesn't matter. **Not checked:** by reading `src/Gui/setup/SetupSteps.qml` the cameras step has no gate and the ball step can be skipped, so a session should start with the sensors alone; nobody has run it. If it won't start, the ceremonies come from visit 3's sessions with the trunk sensors, each of which writes one at its start. **AD-11:** the trunk step and the HackMotion calibration are the app's own set-up routines, used as a golfer uses them; they are not tests of a sensor's axes or configuration, which AD-11 forbids asking for (proposed, for the Product Owner to confirm) | the shot only kept the session. Nothing in the programme waits for the ceremonies (C-17 is out of scope; they are for #291) |
| 2.4 | 4w: how the sensors were worn, what the voice said | **(c)**, with 2.3 | goes with the ceremonies |
| 2.5 | V2.2: #105's real symptom ("Hip sensor isn't upright") | seen at the desk ceremonies (2.3) and at visit 3's trunk step, if it happens; no step | needs no ball |
| 2.6 | V5: End while a shot is analysing, waiting and End anyway (#280, #281) | **stays**, about 7 min, with the cameras and the launch monitor only | needs a real shot still being analysed |
| 2.7 | V11.6, and the old step 2: shots with the sensors worn, for the hip's, chest's and arm's `rig` blocks (C-8b, C-6b.2) | **visit 3**, whose live run wears the sensors anyway; read from its shots after the visit. Stage C's close (C-16b) reads that part then, or closes with it named as a follow-up (proposed). **The other choice:** wear the sensors in V5a (about 8 more minutes for the trunk and arm set-up steps at the bay), which makes the #280 run depend on two calibrations passing | the `rig` block needs a real shot, but the sensors' set-up needs no ball, and visit 3 does it anyway |
| 2.8 | V6: quit while a shot is analysing, waiting and Close anyway (#280) | **stays**, about 8 min | needs a real shot being analysed. It is also the only quit on Windows with the real cameras busy (row 3.9) |
| 2.9 | V11.1–V11.5: the HackMotion routine, a short raise on purpose, then twice in a row (#270) | **(c)**: on the Mac, a set-up with the cameras and ball skipped, to the arm step; the short raise, then two proper attempts with Recalibrate; about 10 min. The HackMotion connects to the Mac (its integration was verified on this hardware). **Not checked:** the arm step is in set-up (`SetupSteps.qml`, "calibrateArm"); the only other host of the routine is the session toolbar's sensor panel, inside a session. If the Mac can't reach the arm step, #270's real-device sighting waits for #291, where its fix is; the fake-device harness already shows the fault | needs the device and an arm, but no ball |
| 2.10 | V11.7: close the app and Premier at the end | **stays**, as the end of the visit | — |
| 2.11 | The Message Log exported after each session | **stays** where the session stays (30 s each) | the log is lost at quit |
| 2.12 | D6: the Product Owner installs A from his own account | unchanged: before the visit, over Remote Desktop if that works into his account (still untested), otherwise at the studio PC before the visit starts | needs his account; not a visit step |

### Visit 3 (`docs/implementation/platform_refactor/plans/PLAN-D.md`, "Cabin"; no procedure yet: SD-15a)

| # | Step | Goes to | Why |
|---|---|---|---|
| 3.1 | Install latest main, run the cases, then install main plus the parked chain and run them again | **stays, both passes** (the coordinator's call, 26 Nov, AG-3)[^b2]. The latest-main pass hits balls (3.2–3.6 again), and it is the only real baseline on main for a late reading (3.5) and a device-only shot (3.6). The install between the passes is the set-up of a ball step. Afterwards the studio PC is on an uncommitted build: he re-installs the latest main from his account after the visit (over Remote Desktop if that works), or the chain commits first and the latest main then holds it. **Undo:** drop the pass, about 25 min saved, with no main baseline for 3.5 and 3.6 | each pass is ball cases |
| 3.0 | The sensors worn in the live run (already in the plan) | **stays**, as the set-up of its ball sessions. It now also gives #105's connect on Windows (2.2), the sensors' `rig` blocks on real shots (2.7), and the ceremonies' fallback (2.3), with no step of their own | ball sessions |
| 3.2 | Back-to-back swings | **stays** | ball |
| 3.3 | A swing just before End | **stays**, with the one phone recording, so a phone clip arriving after End is seen (row 5.5) | ball |
| 3.4 | End with a shot in flight | **stays** | ball |
| 3.5 | A late launch-monitor reading | **stays** | ball |
| 3.6 | A device-only shot | **stays** | ball |
| 3.7 | Two phones on one ball | **#272** (SD-9c, the arbiter, is parked there; only one phone) | ruled: no visit with two phones |
| 3.8 | 10–15 min of hand-run quits, each with one device busy (`docs/implementation/platform_refactor/rulings/MORNING-RULINGS-E.md` §2 (a)) | **withdrawn** (ruled: E1). The home is E1's stand-in quit pins on the Mac **(b)**, **narrowed** to the camera and the sensors (E1-0, E1-cam, E1-imu, about 7.5–12.5 h; the coordinator's call, 26 Nov 08:50)[^e1n]. The microphone, speech and phone-listener quits are dropped: those threads hold no golfer data, and a quit that lingers a few seconds is not an issue. They stay named at Stage E's close (SE-11b), not as visit steps | needs no ball |
| 3.9 | About 10 min of hand-run quits for the vendor layer and Windows (`docs/implementation/platform_refactor/plans/SIZING-E1-E4.md` §1.4) | **withdrawn** (ruled: E1). The real-camera quit on Windows is already a by-product of visit 2's V6. A quit with each real device busy on the Mac (the HackMotion, a Witmotion, the microphone, speech mid-sentence) could be **(c)**, about 10 min, optional. Windows' own quit points: **no home** (§3, item 1) | needs no ball |
| 3.10 | #196: live analysis time per shot and Home's recompute time on the studio PC (SE-6f's "a visit's numbers") | read from visit 3's app log after the visit; no step. Home's side is logged ("[Charts] relations recompute N ms", "[Consistency] recompute N ms"). **EDGE:** the per-shot line ("[ShotProcessor] analysis done", `shot_processor.cpp:1590`) carries no duration, and visit 3 is the only live chance to take it, so one log line (about 0.5 h, a guess) needs an item before visit 3. No item holds it (§3, item 3) | the timings come from shots hit anyway |
| 3.11 | A change to `studio-build.cmd`, which the studio PC runner can't check, "checked at a visit" (`docs/implementation/platform_d_capture_seam.md`, T1's cost) | **(a)** a run of the committed script on the studio PC outside a visit, by the coordinator, after telling the Product Owner (BT-44), once the change has landed on main, since builds there go only through the committed script. No procedure exists yet: the item that changes the script (SD-2a) writes it | needs no ball |

### Visit 4 (`docs/implementation/platform_refactor/plans/PLAN-E.md`, "Cabin"; ORDER's Stage E row)

Checked against the plan: **none of its steps needs a ball**, and the plan already said Stage E needs "no visit of its own if they ride on others". Visit 4 goes.

| # | Step | Goes to | Why |
|---|---|---|---|
| 4.1 | (a) The "before" quit run on the studio PC | **done** at visit 1 (25 Nov) | — |
| 4.2 | (b) The quit script after SE-2b: a quit at each start-up point, on the studio PC | **(b)**: the Mac's quit pins (SE-1b: start-up, ready, warm-up, after the warm-up, an athlete on screen, a re-analysis, and five more), run unedited after SE-2b. Windows: **no home** (§3, item 1) | needs no ball |
| 4.3 | A quit during the pose warm-up on the studio PC (#20's hang) | **(b)**: the Mac's `warm-up` and `re-analysis-in-warm-up` pins. Windows: **no home** (§3, item 1) | needs no ball |
| 4.4 | (c) A signed upgrade on the Mac with the keys in the keychain, after SE-7b | **(c)**, about 15 min. It was already a desk step | needs no ball |
| 4.5 | SE-11a, "the visit pack" | re-scoped: the steps for the desk check (4.4) only; the studio PC's quit script goes. About 0.5–1 h (a guess; was 1.5) | no visit to pack for |

### Visit 5 (`build/platform/DEF/PLAN-F.md`, "Cabin"): cancelled (ruled)

Stage F is already parked on #272 (25 Nov), so there is no parked chain to install.

| # | Step | Goes to | Why |
|---|---|---|---|
| 5.1 | Latest main first: a phone paired on the build before must still pair | **(a)** and **(b)**: `ppcp_stored_pairing_pin` (landed 25 Nov, `dd38fe04`), registered with no platform guard, so in the suite on both machines. A check with a real phone goes with SF-3d (the pairing store) on #272 | needs no ball |
| 5.2 | Main plus the parked chain (seven patches) | **#272** | Stage F is parked; nothing to install |
| 5.3 | Two phones | **#272** | only one phone (ruled) |
| 5.4 | A cabled phone: the wired channel's drop about every 50 s, and the phone that connects twice (SF-6c) | **#272**; when it is picked up, **(c)** on the Mac with the one phone on its cable, about 5 min | needs no ball; SF-6c is parked |
| 5.5 | A clip that arrives after End (#208, closed) | **visit 3**, row 3.3, with the one phone | needs a real shot and one phone |
| 5.6 | A library with the legacy ledger | **(a)** and **(b)**: `TheLegacyLedgerFoldsInIdempotentlyAndSurvives` (`src/Ppcp/tests/ppcp_bundle_import_test.cpp`), registered with no platform guard, so in the suite on both machines | needs no ball |
| 5.7 | SF-7a, the visit pack | **#272** | no visit |

## 3. Checks with no home yet

1. **The quit on Windows** (rows 3.9, 4.2, 4.3). The quit pins run only on the Mac and Linux: they are registered under `if(UNIX …)` and use the bash launcher. On the studio PC, `headless_probe_test` is excluded because it can reach the real settings there. Running the pins there through the studio PC runner needs three things:
   - a Windows form of the launcher's guards (a scratch home, settings and library, and the probe switch checked on the process);
   - the pins' driver, which is already Python, taught Windows' process handling;
   - the exclusion lifted once the isolation is proved.
   
   **Roughly 4–8 h, a guess; no item holds it.** It also needs a ruling, because it starts a headless copy of the app on the studio PC, and the rule there is never to launch the app. Without it, Windows' quit points stay a named gap at Stage E's close (SE-11b), as `docs/implementation/platform_refactor/plans/SIZING-E1-E4.md` §1.5 already lists them. Visit 2's V6 gives one real Windows quit, with a shot analysing.
2. **E1 itself** (row 3.8): narrowed on 26 Nov to the camera and the sensors (E1-0, E1-cam, E1-imu, about 7.5–12.5 h)[^e1n]. Its homes now exist as items; the microphone, speech and phone-listener quits, and HackMotion's own barrier, are named gaps at Stage E's close, not visit steps. The only place the microphone and speech appear with a real device is row 3.9's optional desk check.
3. **#196's per-shot timing in the log** (row 3.10; EDGE): about 0.5 h, a guess; needed before visit 3. No item holds it.

## 3a. Edges (AG-22)

- **EDGE: #196's per-shot timing** (row 3.10, §3 item 3): no log line carries it, and visit 3 is its only live chance.
- **EDGE: visit 3's pack item (SD-15a) gains riders it doesn't yet list**: the sensors' `rig` blocks on real shots for Stage C's close (2.7); #105's Message Log export straight after set-up (2.2); #196's timings (3.10); the one phone's clip after End (5.5); and the ceremonies' fallback (2.3). The sensors at visit 3 also need the harness to have arrived, which neither PLAN-D nor ORDER said until now.
- **EDGE: visit 2 confirms an automated fix.** `app_state_test` S10 (`a878282f`) pins End and quit waiting, End anyway, Close anyway and Cancel. V5 and V6 still need a ball (the real shot pipeline sets the saving flag, and V6 is the only real Windows quit), and visit 3's row 3.4 overlaps V5: folding visit 2 into visit 3 saves a trip.
- **EDGE: D12's start on the Mac is by reading only**: the trunk steps appear only when the Mac's settings give a sensor the pelvis or thorax role, and the ceremony file is copied out before End. Nothing in the programme waits for D12 (C-17 is deferred), unless C-14 has landed in A; the Product Owner could defer it to #291.
- **EDGE: `ppcp_stored_pairing_pin` runs only where OpenSSL is found** (`src/Ppcp/tests/CMakeLists.txt`, the guard before it); not confirmed on the studio PC.

## 4. Commit gates that move (proposed, for the Product Owner)

- **SE-2b** (the teardown change), parked "until its check", the "after" quit run: it commits after review, with the Mac's quit pins passing unedited and the studio PC's full suite. No visit.
- **SE-7b** (the API keys into the keychain), parked until the signed upgrade: it commits after the desk check (row 4.4). No visit.
- **ORDER's "the settings split and teardown changes commit after it"** (visit 4) becomes those two gates.
- **Stage C's close (C-16b)**, "every item in scope, and the visit": the visit is visit 2's short run (2.6, 2.8), plus the desk checks 2.3 and 2.9; the sensors' `rig` blocks on real shots come from visit 3 (row 2.7).
- **Stage D's parked items** still commit after visit 3, with both passes. Unchanged.

## 5. Edited in place

- `build/platform/ORDER.md`: the Cabin cells of Stages C, D and E; "Your studio PC visits, in order"; the 🔴 rows of "next"; "Waiting on".
- `docs/implementation/platform_refactor/visits/VISIT-2.md` and `VISIT-2-FOR-CLAUDE.md`: the visit as above.
- `docs/implementation/platform_refactor/plans/PLAN-C.md`, `docs/implementation/platform_refactor/plans/PLAN-D.md` (its Cabin paragraph and SD-15a's riders), `docs/implementation/platform_refactor/plans/PLAN-E.md` (and E1's narrowing), `PLAN-F.md`: their Cabin paragraphs and visit items.
- `build/platform/C/BRIEF-C-16a.md` (re-cut to the 20-minute visit and the two desk checks) and `BRIEF-C-16b.md` step 9 (the ceremonies from the desk).
- `docs/implementation/platform_refactor/rulings/MORNING-RULINGS-E.md` §2: marked superseded.
- `docs/implementation/platform_refactor/plans/SIZING-E1-E4.md`: the hand-run quit recommendation withdrawn wherever it appears, and E1's narrowing noted.
- Tracked: `governance/controls/test.md` (BT-43 tightened; commit message in `BT-43-commit-msg.txt`); `docs/implementation/platform_refactor_assessment.md` §5 rule 2; `docs/implementation/platform_d_capture_seam.md` (row 3.11); the assessment's Stage F "Cabin visit" row (visit 5 cancelled); `docs/design/rig_design.md` §10, "The studio PC visit" (proposed, for the Solution Architect). The docs edits are a separate `Docs:` commit, `docs-commit-msg.txt` (GIT-8: one area per commit).

**Not edited, tracked:** the plan's stage sections still describe the old visits in their "Cabin visit" rows (`docs/implementation/platform_refactor_assessment.md`, Stages A, C, D and E), and so does the Stage F design (`docs/implementation/platform_f_ppcp.md`, its "Hands-on visit" row). They are proposals of their day; §5 rule 2 now states the rule they fall under.

**Not edited** (records of what was said at the time): `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, the dashboard's log, `HANDOVER-26nov.md`, `LEVERS/`, `NOTES-VISIT-2.md`, `docs/implementation/platform_refactor/visits/VISIT-1.md`, `docs/implementation/platform_refactor/plans/FORECAST.md`, `G/draft`. **Still stale, outside this lane:** the local agent file's line "Every step that needs no real shot, hands or devices runs beforehand", and the agent's memory note `feedback_visits_only_what_needs_the_bay.md`. Both restate the old BT-43. The share's staged copy of `VISIT-2-FOR-CLAUDE.md` (`scratch/visit-2`) is re-staged from this folder at D9.

[^ruling]: `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Midday rulings" (5), 26 Nov 2026.
[^mb4]: The Product Owner and the Solution Architect, 26 Nov 2026 06:55, `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Morning batch 4": "each long-lived thread gets an automatic quit test while busy, not a hand-run quit at the studio". Its sizing was not accepted at midday (4).
[^b2]: The coordinator's call, 26 Nov 2026 (AG-3; `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`): the review's B2 is resolved by keeping visit 3's latest-main pass, because it hits balls; not put to the Product Owner as a time cut.
[^e1n]: The coordinator's call, 26 Nov 2026 08:50 (AG-3, for the Product Owner's review; `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Coordinator's calls" (1)): E1 is narrowed to E1-0, E1-cam and E1-imu; E1-mic, E1-tts and E1-phone are dropped. Undo: add the three back (about 6.5–10.5 h).
