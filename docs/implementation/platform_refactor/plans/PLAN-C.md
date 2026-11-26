# Stage C, the Rig and calibration: the plan (24 Nov 2026)

Builds the approved design, `docs/design/rig_design.md` (§10's order)[^pr35], except the calibration procedures in C++, deferred to #291 (C-V1, "The lever pass" below)[^c-v1]. Premises checked on main `cb9f376c`: `NOTES-C-plan.md`. Every brief reads the standing block (#402), then `docs/implementation/platform_refactor/standing-C.md`, then its excerpt `IM-C-<item>.md` (regenerated first: `bash build/platform/C/make_im_excerpts.sh <worktree>`). Lane 1 is `build/wt-37c`, lane 2 `build/wt-5b`.

**What changed from the design's 22 items** (NOTES §3): 39 items, because ten of the design's are more than one run; the two property inventories §10 asks for are items (C-5i, C-6i); C-15 is new (the calibration logic the design assigns to no item); C-16 splits into the visit pack and the close; and **eleven items start before Stage B ends**, which the design did not plan (PR-35 allows it). C-V1 then took nine of the 39 out of scope: **30 items remain**[^c-v1].

**Beside Stage B Part 1.** **docs** = no code, runs any time, even during a scripted move (C-0 alone edits the IM, so it waits its turn behind any Stage B item that has the IM open). **now** = code in files that B1.5, B1.6 and B1.7 neither move nor rewrite; it runs between scripted moves, never across one (STANDING 3). Otherwise the move it waits for is named; "(C-4)" means no move of its own, it waits only through that item.

| # | Item (one agent run each) | After | Beside Stage B Part 1 | Accept | h |
|---|---|---|---|---|---|
| C-0 | The approved IM rows written in, each marked with the item that builds it; the camera design's note; the deck note | — | **docs** | every cited id resolves; the ring rows untouched | 1.5 |
| C-10a | Duties inventory: `TrunkRoutine.qml`, `CalibrationRoutineBase.qml`, `trunk_ceremony_recorder.*` | — | **docs** | scripted count: uncovered lines 0 | 1.5 |
| C-10b | Duties inventory: the two arm routines and `ArmCalibrationStatus.qml` | — | **docs** | the same count: 0 | 1.5 |
| C-10c | Duties inventory: `ImuCalibrationFlow.qml` and the four short files | — | **docs** | the same count: 0; all ten files covered | 1 |
| C-5i | Duties inventory: the 6 camera rig properties | — | **docs** | 6 properties, every reader and writer placed | 1 |
| C-6i | Duties inventory: the 6 sensor properties and every calibration set and clear path | — | **docs** | 6 properties; set and clear paths each with a line | 1.5 |
| C-1a | `DeviceKey`, `PlacementKey` and `device_key_test` (new files) | — | **now** | every spelling of §4.6 byte for byte | 2 |
| C-1b | The camera and monitor sites use the keys | C-1a | **now** | builds 8 → 2; shape-aware 6 → 4 | 2 |
| C-9a | The trace projection, and the trunk routine's golden trace | C-10a | **now** (test files only) | every C-10a transition reached; two runs equal | 3 |
| C-9b | The Witmotion arm routine's golden trace | C-9a, C-10b | **now** | the same, for its inventory | 2.5 |
| C-9c | The HackMotion arm routine's golden trace | C-9a, C-10b | **now** | the same; every abort reason | 2.5 |
| C-1c | The sensor, shot and SET-05 sites use the keys | C-1b | waits **B1.5** (`shot_processor`, `capture_context`), **B1.7** (`imu_role_map.h`) | builds 2 → 0; shape-aware 4 → 0 | 2 |
| C-2a | The perspective header, `Q_ENUM_NS`, `perspective_test` | — | waits **B1.5** (moves `recording.h`), **B1.7** (edits it) | one header; codes pinned | 1.5 |
| C-2b | The scripted QML rename; `CameraInstance::Perspective`, two constants and the test mirror go | C-2a, C-1b | (C-2a) | types 2 → 1; mirrors 1 → 0; constants 2 → 0 | 2.5 |
| C-3 | `Vocabulary/Rig` types, the ids, canonical bytes, the digest | C-1a, C-2a | waits **B1.5** (`identity.h`), **B1.7** (`Vocabulary/Common`) | digest pinned, Mac and studio PC; a third camera | 3 |
| C-4 | `Rig`, `RigRepository`, the store, `app.rig` read-only | C-3 | waits **B1.5** (`main.cpp`); **B1.16** renames its folder | real folder untouched; `rig.h` includers 3 | 3 |
| C-5a.1 | The camera commands, Impact exclusive, `RigChanged` | C-4, C-5i | (C-4) | `rig_commands_test`; a third camera | 2.5 |
| C-5a.2 | The copy from settings with its fingerprint | C-5a.1 | (C-4) | `rig_import_test`: 4 downgrade cases; keys untouched | 2.5 |
| C-5b | The cameras cut over | C-5a.2, C-2b | (C-4) | `AppSettings` properties −6; QML grep 0 | 3 |
| C-6a.1 | The role rules move to `src/IMU` | C-1c | waits **B1.7** | Domain model may include it; labels stay in `Gui` | 1.5 |
| C-6a.2 | The sensor half of the Rig | C-6a.1, C-5a.2, C-6i | (C-4) | a second claim refused as today; a HackMotion holds two roles | 2.5 |
| C-6b.1 | The sensors cut over | C-6a.2, C-5b | (C-4) | `AppSettings` properties −6; QML grep 0 | 3 |
| C-6b.2 | The sink; every set path commits and every clear path clears | C-6b.1 | (C-4) | after each path, the Rig's record = the live sensor | 3 |
| C-7 | SET-05 becomes `Rig::learnClubLength` | C-5a.2 | waits **B1.5**; never beside C-5b, C-6b.1 | `AppSettings` −1 (13 in all); same value read | 2 |
| C-7b | The two assumptions as `assumed` records | C-5b | waits **B1.5** (`pp_tuned_constants.h`) | a fixture's analysis byte-identical | 1.5 |
| C-8a | The `rig` block's reader, `Recording::rig`, pass-through tests | C-3 | waits **B1.5**, **B1.7** (`recording.h`), **B1.16** | an old swing reads `Unknown`; 7 writers keep the block | 2.5 |
| C-8b | The `rig` block's writer, switched on | C-8a, C-6b.2, C-7b | (C-4) | entries = rig-device streams; input stamp identical | 3 |
| C-11a | ~~`TrunkProcedure`~~ **out of scope: C-V1, #291** | C-9a, C-3 | (C-3) | equal to C-9a's trace | 3 |
| C-12a | ~~`WitmotionArmProcedure`~~ **out of scope: C-V1, #291** | C-9b, C-3 | (C-3) | equal to C-9b's trace | 2.5 |
| C-13a | ~~`HackMotionArmProcedure`~~ **out of scope: C-V1, #291** | C-9c, C-3 | (C-3) | equal to C-9c's trace | 2.5 |
| C-11b.1 | ~~The trunk driver and `app.calibration` (not yet used by QML)~~ **out of scope: C-V1, #291** | C-11a, C-6b.2 | (C-4) | driver test on fakes; QML unchanged | 2.5 |
| C-11b.2 | ~~`TrunkRoutine.qml` as bindings · **parked**~~ **out of scope: C-V1, #291** | C-11b.1 | (C-4) | its logic lines → 0; `tst_setup_trunk` re-pointed | 3 |
| C-12b | ~~The Witmotion driver; its routine as bindings · **parked**~~ **out of scope: C-V1, #291** | C-12a, C-11b.2 | (C-4) | logic → 0; test re-pointed | 3 |
| C-13b | ~~The HackMotion driver; its routine as bindings · **parked**~~ **out of scope: C-V1, #291** | C-13a, C-12b | (C-4) | logic → 0; the driver commits `deviceHeld` | 3 |
| C-15 | ~~What is left: the flow's routine choice, the status view's hold progress, the base · **parked**~~ **out of scope: C-V1, #291** | C-13b | (C-4) | thresholds, maths, transitions in `Gui/calibration`: 0 | 2 |
| C-14 | The ceremony file's write behind the Session's door, with #265's fix (a test per loss path); the record's `evidence` pointer set when the Session takes the ceremony (addendum decision 2)[^add2]; the recorder keeps its taps and the `trunkCeremony` global stays (re-scoped, C-V1) | C-6b.2 (C-V1) | waits **B1.5**, **B1.16** | A5 ×7 → 0 (A6 ×2 as V2's re-count says); same bytes; each loss path has a test (#265) | 3–3.5 |
| ~~C-16a~~ | ~~The visit pack: `docs/implementation/platform_refactor/visits/VISIT-2.md` and installer A (installer B goes with C-V1)~~ **Retired 26 Nov: visit 2 is folded into visit 3, so its pack is part of visit 3's (SD-15a)**[^fold] | — | — | — | ~~1.5~~ 0 |
| C-17 | ~~The three real ceremonies as fixtures~~ **out of scope: C-V1, #291** | the visit | — | each replays to its file's steps, verdicts, A and M | 1.5 |
| C-16b | The close: counts re-taken by an agent that built none of it; IM §11 | every item in scope, and visit 3 (Stage D's visit; its latest-main pass)[^fold] | — | the stage's Accept row, each figure with its command | 2 |

**Review tier: independent for every item** (a new owner, a format change, source data). The six inventories are also checked line by line by the coordinator before the code that depends on them starts.

**Lanes.** Each row: what runs, then the next two ready.

| When | Lane 1 (the Rig) | Lane 2 (the procedures) | Docs (also fills a lane stopped by a move) |
|---|---|---|---|
| **Now**, between B1's items | C-1a (C-1b, C-9c) | C-10a, then C-9a (C-10b, C-9b: the first two ready are docs) | C-0 (C-10b, C-10c) |
| During **B1.5**, **B1.6** (alone) | stopped: C-5i, C-6i | stopped: C-10c, C-10b if left | — |
| After **B1.5** and **B1.7** | C-2a → C-3 → C-4 (C-5a.1, C-5a.2) | C-1c → C-2b → C-6a.1 (C-8a, C-7) | — |
| The owner | C-5a.1 → C-5a.2 → C-5b → C-7b (C-6a.2, C-6b.1) | C-8a → C-7, never while C-5b or C-6b.1 runs (C-14 after C-6b.2) | — |
| The sensors, the block | C-6a.2 → C-6b.1 → C-6b.2 → C-8b (C-16b) | C-14, after C-6b.2 (C-16b) | ~~C-16a~~ (retired: SD-15a)[^fold] |

Lane 2's list is one item short before C-9a (C-10a must be checked first), and has no Stage C item from C-7's end until C-6b.2 lands, since C-V1 took the procedures out of it[^c-v1]. Fill it with a ready item of another stage, never with an open bug (PR-72)[^pr72].

**Stage accept** (C-16b re-takes each). Places that hold rig facts: 16 → 14 owned by the Rig, 2 `ppcp` keys named for Stage F. Open-coded keys 8 → 0; shape-aware sites 6 → 0. Perspective types 2 → 1; constants 2 → 0; mirrors 1 → 0. `AppSettings` properties −13. Calibration QML (3,978 lines): unchanged, pinned by the three golden traces (C-9a to C-9c); a known breach of RP-7 and §4.8 named on #291, AD-52's one accepted exception, which may shrink and must not grow[^c-v1]. The `trunkCeremony` global stays. `trunk_ceremony_recorder`: A5 ×7 → 0 (C-14); its A6 ×2 (`attachResult`, `checkTurn`, which the QML routine still calls) stay, unless lint A6 narrowed to P9's wording (Stage D's V2) counts them out; the lint's count at the close says which[^c-v1]. Each of #265's loss paths has a test (C-14). No allowlist line added. Scratch cases: a swing with no rig reference (C-8a); a third camera (C-3, C-5a.1); two Recordings either side of a calibration change (C-8b); a machine with no rig stored (C-4, C-5a.2).

**Effort.** About **66 h** for the 30 items in scope (the sum of the table's rows; C-V1 took out nine items, 23 h, and #265's fix adds 0.5–1 h to C-14)[^c-v1]. **Since the fold (26 Nov): about 64.5 h for 29 items**, C-16a's 1.5 h out (the docs' share about 8 h, not 9.5); SD-15a gains about 0.5 h for carrying V5 and V6[^fold]. ORDER's 12–20 h was set before the design existed and counted the stage as about eight items. Measured so far: 2.4 h and 344k tokens an item (22 items, `LEDGER.csv`), which gives about 72 h. By the table, lane 1 carries about 34.5 h, lane 2 about 22 h and the docs about 9.5 h: lane 1 sets the pace, **about 2.5 days of running** at 14–16 h a day, plus the waits for B1.5, B1.7 and B1.16, and the visit.

**Cabin.** Every code item runs the full suite on the studio PC through `tools/method/cabin_run.sh` (🟡, no hands); C-3 pins the digest there. **No visit of its own since 26 Nov:** visit 2 is folded into visit 3, Stage D's visit, and Stage C's close waits for it[^fold]. Its two ball steps, V5 and V6 (about 13 min more on visit 3, V5a merged with visit 3's row 3.4), run in visit 3's latest-main pass, on that pass's installer, which has the QML routines (they stay until #291)[^c-v1]; there is no separate installer A. ~~**One visit by the Product Owner** (visit 2, 🔴, hitting balls, **about 20 min**, from `docs/implementation/platform_refactor/visits/VISIT-2.md`, which C-16a re-writes), on **one installer, A: the latest main**.~~ **Re-cut 26 Nov**, then folded as above, to the rule that a visit holds only steps that need clubs and a ball hit[^recut] (`docs/implementation/platform_refactor/visits/RECUT-26nov.md`; the placements are proposed, for the Product Owner):
1. **At visit 3, its latest-main pass:** End and quit while a shot is analysing (#280, #281), four sessions (one of them visit 3's row 3.4), with the cameras and the launch monitor, and the sensors when they are already worn for the live run (V5 and V6 don't need them, so a failed sensor set-up doesn't stop them). Their shots' camera `rig` blocks, and the sensors' (C-8b, C-6b.2), are read back afterwards, with #105's settings read-back on Windows; C-16b reads them all from that visit[^fold]. ~~C-16b reads that part then, or closes with it named as a follow-up. The other choice, wearing the sensors at visit 2, adds about 8 min of set-up at the bay.~~
2. **Before it, at the desk on the Mac** (any time before visit 3; from `docs/implementation/platform_refactor/visits/VISIT-2.md` §3, D12 and D13, kept for them)[^fold]: **the trunk ceremony three times on the real sensors** (about 15 min, once the harness has arrived), each `trunk_ceremony.json` copied out when its session starts and kept on the share for #291, where they become its fixtures; if C-14 is in A, these also check its write on real ceremonies. **#270: the HackMotion arm routine twice in a row** (about 10 min), on A's routine, the one #270 was found in; its pass criteria follow today's behaviour: a timed start when the page opens, and Recalibrate after a failure, which is accepted until #291 (not a fault)[^arm-hf]. Neither has been run on the Mac yet: by reading, set-up lets the cameras and the ball step be skipped.
3. **No step:** #105's check is its two tests in the suite and the real connect's log lines at visit 3 (never a desk test of a sensor: AD-11; the trunk step and the HackMotion calibration in 2 are the app's own routines, not device tests, proposed for the Product Owner to confirm). The cut-over check (the cameras' and sensors' set-up as the cut-overs left it) is the desk comparison D5 and D7, now made around visit 3's latest-main install (SD-15a), by which time every cut-over item has landed[^fold].

There is no installer B and no parked chain (C-V1). Nothing of Stage C commits after the visit; Stage C's close (C-16b) reads it.

[^recut]: The Product Owner, 26 Nov 2026: "please do make sure those visits need golf clubs and a ball being hit. I don't want to waste time there doing checks we can automate or do at the desk." Governance BT-43, tightened the same day.
[^fold]: The Product Owner, 26 Nov 2026: visits 2 and 3 combined, to save time (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Visits 2 and 3 combined"). C-16a retired into SD-15a, and the placements: the docs lane's calls, `docs/implementation/platform_refactor/visits/FOLD-2-into-3.md`.

[^pr35]: The Solution Architect, 24 Nov 2026, "approve as recommended" (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, PR-35).

**Corrected by the coordinator (visit 2's procedure, 25 Nov):** the arm routines are not hands-free today: a timed start when the page opens, and Recalibrate after a failure. Under PR-48 Stage C keeps that exactly. The breach of the hands-free rule is accepted until #291's track, which builds #266's stillness gate with the procedures[^arm-hf]. Only the trunk routine is hands-free. Visit 2's pass criteria follow today's behaviour (`docs/implementation/platform_refactor/visits/VISIT-2.md`).

**The lever pass of 25 Nov 2026** (`docs/implementation/platform_refactor/rulings/LEVERS-U-C-D.md` §2; the rows, lanes, Stage accept, Effort and Cabin above now follow it): C-V2 not taken, so C-2b stays as planned. C-V1 taken (the Product Owner confirmed it after a brief hold: the calibration QML is all or nothing). **This plan's scope is now:**
- **Out of scope:** C-11a, C-12a, C-13a, C-11b.1, C-11b.2, C-12b, C-13b, C-15, C-17 (the procedures in C++; 23 h): #291, with #266 and #270.
- **Re-scoped:** C-14 is the ceremony file's write only, after C-6b.2, with #265's fix in this item (+0.5–1 h). C-16a carries installer A only (visit 2 about 90 → 60 min; about 20 after the 26 Nov re-cut[^recut]; then folded into visit 3 and C-16a retired, the same day[^fold]). The three real ceremonies are still recorded, now at the desk on the Mac, and kept for #291.
- **Accept, re-set:** "calibration QML (3,978 lines): thresholds, maths, transitions → 0" becomes "unchanged, pinned by the three golden traces; a known breach of RP-7 and §4.8 named on #291". The `trunkCeremony` global stays. `trunk_ceremony_recorder`'s A5 ×7 → 0 still follows; its A6 ×2 stay unless V2's narrowed A6 takes them out of scope.
- **Effort:** about 88 h → about 66 h.

[^c-v1]: The Product Owner and the Solution Architect, 25 Nov 2026, lever C-V1, as recommended, confirmed by the Product Owner the same night (all of the calibration QML moves at once or none of it): `docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, 21:31 and "C-V1 confirmed"; `docs/implementation/platform_refactor/rulings/LEVERS-U-C-D.md` §2.2; #291. The A6 sentence is C-V1's own text.
[^arm-hf]: The Product Owner and the Solution Architect, 25 Nov 2026 (21:56): the arm routines' hands-free breach is accepted until #291's track, which takes #266 with it; named on #291 (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Three rulings"; `docs/design/rig_design.md` `[^arm-hands-free]`).
[^add2]: The Solution Architect, 25 Nov 2026 (the Rig addendum's batch 2, as recommended): decision 2 keeps `evidence` outside the digest and names C-14 as the item that sets it (`docs/design/rig_design_addendum.md` §2). C-V1 re-scoped C-14's taps and the global, not the pointer.
[^pr72]: The Product Owner, 25 Nov 2026, 07:56: lanes are no longer filled with open bugs (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, PR-72).
