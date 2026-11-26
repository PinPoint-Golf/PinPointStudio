# The Rig and calibration: addendum 1, the questions the inventories found

**Status: proposed, for the Solution Architect, 25 Nov 2026. Nothing on this page is ruled.** It adds to `docs/design/rig_design.md`, which is approved[^sa24]. Five inventories of today's code (the three calibration ones, the camera properties and the sensor properties) and the first build item found questions that design does not answer[^inv]. This page puts every one of them as **eleven decisions**, so they can be ruled once.

**How to read it.** A sentence with a footnote states something already ruled, already in the approved design, or a call the coordinator has logged. A line of evidence names the file and line it was read from. Every other sentence on this page is design or this page's own reading, proposed for the Solution Architect: that covers the table below, the two paragraphs after it and the last section. Each numbered section says so again in its first line. Names of new types, events and functions are placeholders.

**Where the line numbers come from.** Each was read at the commit its inventory was taken at: the trunk routine and the recorder at `c8d3eb9d`, the arm routines at `1a083129`, the flow at `b3e656cf`, the camera properties at `4a10080c`, the sensor properties at `cc756cfa`. The inventories are working notes and are not committed, so the evidence each decision rests on is copied here.

Short names used in the evidence, all under `src/Gui/calibration/`: **TR** `TrunkRoutine.qml`, **WA** `WitmotionArmRoutine.qml`, **HA** `HackMotionArmRoutine.qml`, **AS** `ArmCalibrationStatus.qml`, **IF** `ImuCalibrationFlow.qml`. "The step machine" is the Rules-ring half of a procedure (`TrunkProcedure` and its two siblings) and "the driver" is its Adapters-ring half[^d48]. "The device object" is `ImuInstance` or `HmInstance`, the live object for one sensor. "The sink" is `RigCalibrationSink`, the two-method contract (`commit`, `clear`) the Rig implements[^d48]. **A and M** are the two rotations of a worn sensor's alignment: A takes the sensor's readings to the body segment's frame, and M is how the sensor sits on the segment. A **lane** is one of a HackMotion's two units, the lower arm or the palm. **The freeze** is the moment a shot's capture window is fixed and its header facts are read. The **VIEW combo** is the perspective picker on Settings → Cameras.

**Two kinds of id share a prefix.** "C-12" and "C-13" in a heading are the plan's two questions[^pr36]. Everywhere else, and always with a letter (C-12a, C-13b), a "C-" id is a build item of the Stage C plan. F-numbers are the inventories' findings.

## The decisions

| # | Question | Recommendation | Items waiting |
|---|---|---|---|
| 1 | How does one piece ask another to act: the managers asking the Rig, and a step machine asking its driver? (C-12, F4, F5) | A named, typed contract each time, made by the piece that decides. The managers get `RigCommands`, an interface the Rig implements. The step machine's list of requests gains the sensor clear and the recorder's bracket | C-5a.1, C-5b, C-6a.2, C-6b.1, C-7; C-11a, C-11b.1, C-11b.2 |
| 2 | Is a calibration record's `evidence` pointer inside the rig state's digest? (C-13) | No. Like `sinceUtc`, it is outside | C-3, C-14 |
| 3 | What does a step machine take in that the design's event list does not name? (F6, F8, F19) | Four entry events, restore among them; the orientation samples its maths needs; the result of each request; and its step timings as a value handed in | C-11a, C-12a, C-13a, C-11b.2 |
| 4 | When does the Rig hold a sensor's calibration record: who commits, what a Witmotion run and a HackMotion run commit, and what ends a record? (F7, F15, F35, F36, F34) | One rule: the record follows the live sensor's alignment, whatever changed it. The device object is the only caller of the sink. This rewrites step 2 of §4.8 | C-3, C-6b.2, C-8b, C-11b.1, C-12b, C-13b |
| 5 | Where do the Witmotion's refine arithmetic and the two mount limits live? (F18) | In `src/IMU/imu_calibration.h` (Rules). `ImuInstance` goes on applying and calls them; the step machine tests the gate with the same limits | C-12a, C-12b |
| 6 | How does the arm status view reach what it reads directly from a device and from a routine's internals? (F20) | Each fact from its owner: the procedure's state from `app.calibration`, "is it calibrated" from `app.rig`, a live readout from the device object as today | C-12b, C-13b, C-15 |
| 7 | How does one driver that lives as long as the app serve two hosts? (F22) | A host attaches while it is active. One run at a time. A vendor change is a stop. The driver keeps its last outcome; "still calibrated" is the Rig's | C-11b.1, C-12b, C-13b, C-15 |
| 8 | What does a manager take from the Rig, and how does it learn of a change to an alias or an enabled flag? (F26, F27, F38) | It is handed both parts as typed values and told of every change to either. A second event covers the unversioned part. It applies only the field that changed, by a table that is today's behaviour | C-5a.1, C-5b, C-6a.2, C-6b.1 |
| 9 | When are two sensor keys equal? (C-1a, PR-52) | When their device ids are, as built. The alias map is looked up by its own spelling, never by key equality | C-1c, C-3 |
| 10 | What is a camera key with no `\|`, as the set-up tests write? (F32) | Nothing: the type gets no rule for it. The fixtures are re-keyed, and the copy keeps what it cannot type | C-5a.2, C-5b |
| 11 | Is `#wide` a spelling of the device key? (PR-52; a call this page thinks was wrong to take without a ruling) | Yes: confirm it as built | none: it is built; C-1a's IM rows wait for their footnote |

**Ruled (25 Nov 2026):** all eleven decisions as recommended[^r1125]. The Solution Architect's framing: the Rig is a collection of devices, as a session is a collection of shots.

**The order matters twice.** Decision 3's restore event and decision 7's "still calibrated" both read the Rig's record, so they assume decision 4. Decision 1's second half and decision 5 assume decision 4's "the driver performs, the device object commits". Each section says what changes if the earlier one is ruled otherwise.

**What does not wait.** C-1b, C-2a, C-2b, C-4, C-6a.1, C-7b, C-8a and the three golden traces (C-9a to C-9c) depend on none of these.

---

## 1. How one piece asks another to act (C-12, F4, F5)

(proposed, for the Solution Architect)

**The question.** The design says today's writers "call the Rig's commands", and also that only three places may include `rig.h`. It names no contract for the commands in between[^pr36]. The same gap exists one level down: a step machine "requests" things of its driver, and the list of requests leaves out two things the trunk routine does today[^pr45].

**Evidence.**
- The briefs' stand-in route: a manager "raises each change as a request: a signal, or a callback set declared in its own header", and "the composition root connects each request to the Rig's command" (`build/platform/C/BRIEF-C-5b.md`, `BRIEF-C-6b.1.md` and `BRIEF-C-7.md`, "The route to the Rig").
- A command can be refused, and today the caller gets the answer back: `Q_INVOKABLE bool setRoleForDevice(...)` (`src/Gui/imu/imu_manager.h:233`), with a refusal signal that names the holder (`:300`); the rule is `claimRole`, which returns a `ClaimResult` (`src/Gui/imu/imu_role_map.h:263`).
- F4, the recorder's bracket. The trunk routine calls the recorder's `begin()` unless one is already recording, `attachResult` twice, `stepEnd` three times and `end()` on finish or fail, and closes the open step on stop and on clear (TR 273, 352–360, 420–422, 650).
- F5, the clear. It clears both sensors' calibrations at begin, at reset and at every stand-tall attempt (TR 317–328, 375).
- The design's request list is "start a step, solve now, check the turn"[^d48].

**Options for the managers.**
- **A. Requests wired at the composition root**, as the briefs have it. No new header. But a request with no receiver is a setting change that is silently not saved; a refused claim needs a second signal to carry the answer back; and the contract is spread over each manager's own header, so no lint can name it.
- **B. A commands contract.** One header among the Domain model's contracts, `rig_commands.h`, holding three small interfaces (the camera commands, the sensor commands, `learnClubLength`). `Rig` implements them and the composition root hands each manager the one it may call. It is the shape the sink already has.
- **C. The managers include `rig.h`.** It breaks §6 rule 8 and R-CTX-1[^d48].

**Options for the step machine.**
- **D. The step machine requests them.** Its request list gains "clear the sensors' calibration", "open the recording", "attach a result", "close a step" and "close the recording". The driver performs each one and decides nothing.
- **E. The driver works them out from the stage.** Fewer requests, but the driver then holds a table of which stage change clears a sensor or closes a step. That table is transition logic in the Adapters ring, and the golden trace cannot check it.

**Recommendation: B and D, which are one rule.** Whoever decides makes the request, and every request that crosses between two pieces is named in a typed contract that the inner piece owns. For the managers, B gives a refused claim its answer as a return value, makes a missing connection a build error and not a lost write, and gives lint A9 one header to name. For the step machine, D keeps "it holds every threshold and every transition"[^d48] true: the clear and the bracket happen at transitions, so they belong in the trace. "Unless one is already recording" is the step machine's too, since only it opens a recording. Under decision 4, the driver performs the clear on the device object, whose own clear path tells the Rig; the driver does not call the sink.

**Amends.**

Now (§4.1):
> Today's writers (`CameraManager`, `CameraInstance`, `ImuManager`, `ShotProcessor`, QML through `appSettings`) call the Rig's commands instead.

Becomes:
> Today's writers call the Rig's commands instead. The C++ writers (`CameraManager`, `CameraInstance`, `ImuManager`, `ShotProcessor`) call them through `RigCommands`, a set of interfaces among the Domain model's contracts (`rig_commands.h`) that `Rig` implements and the composition root hands to each of them. QML calls them through `app.rig`.

Now (§4.8, the step machine's row):
> and requests (start a step, solve now, check the turn)

Becomes:
> and requests (start a step, solve now, check the turn, clear the sensors' calibration, apply or refine an alignment, open or close the recording, attach a result, close a step, and each call a HackMotion's own calibration needs)

§5's table gains a row for `rig_commands.h` (Domain model, Domain + Application), §6 rule 3 gains "`rig_commands.h` joins lint A9's named contract headers", and OBJ-RIG's lifecycle row names the contract (C8, C9).

**If ruled A:** no brief changes; C-5a.1 adds a test that every request has a receiver.

**Waits:** C-5a.1 (it would declare the header), C-5b, C-6a.2, C-6b.1, C-7 (the route); C-11a (the request list), C-11b.1 and C-11b.2 (who performs).

**Ruled:**

---

## 2. Is `evidence` inside the digest? (C-13)

(proposed, for the Solution Architect)

**The question.** A calibration record may carry `evidence`: "where the raw run is kept (a session id and a file name)"[^d48]. The design does not say whether that pointer is part of the bytes the rig state's id is computed from[^pr36].

**Evidence.**
- The pointer is set after the record exists. The ceremony file is written by the `Session`, which is handed the ceremony when it starts[^q5], and set-up runs before the session exists. `BRIEF-C-14.md` What 4 sets the pointer then.
- The design already has one field outside the digest: "The rig state also carries `sinceUtc`, the time the state came into force. It is outside the digest."[^d42]
- `evidence` is best-effort: "A reader treats a missing file as 'no evidence kept'."[^d48]

**Options.**
- **A. Outside.** Setting the pointer makes no new rig state and no `RigChanged`.
- **B. Inside.** Every session start that writes a ceremony file makes a new rig state and an event, for a fact that describes no set-up. The same sensors, calibrated the same way, would get a different id depending on whether a file was kept.

**Recommendation: A.** The id answers "was this the same captured set-up?"[^q19]. Where a file is kept is not part of the set-up. The cost is stated: two Recordings with one `stateId` can carry different `evidence` values in their blocks.

**Amends.**

Now (§4.2):
> The rig state also carries `sinceUtc`, the time the state came into force. It is outside the digest.

Becomes:
> The rig state also carries `sinceUtc`, the time the state came into force, and a calibration record may carry `evidence`, where its raw run is kept. Both are outside the digest: setting or changing `evidence` makes no new rig state and emits no `RigChanged`.

ID-RIG's row in the IM gains the same words (C5).

Now (§10, C-3's test):
> ignores aliases, enabled flags and the prior

Becomes:
> ignores aliases, enabled flags, the prior and a record's `evidence`

**Waits:** C-3 (the canonical bytes; C-4 and every item of the Rig's lane after it follow C-3), C-14.

**Ruled:**

---

## 3. What a step machine takes in (F6, F8, F19)

(proposed, for the Solution Architect)

**The question.** The design lists the step machine's events as "a tick with its elapsed time, a sensor summary (gyro rate, tilt, heading, link up or down), 'the guide finished', 'speech finished', the HackMotion's own phase"[^d48]. Today's routines also take three things that list does not name[^pr45][^pr48].

**Evidence.**
- **F6, restore.** When a host shows a finished calibration again, the trunk routine reads each sensor's `trunkCalibration` and marks the run done, even when the two solves are not both valid (TR 290–305; the read at 292). The flow calls it on re-entry (IF 140–147, 314–319). No event carries it.
- **F8, the pace.** The trunk routine's step timings are one map, `defaultPace` (TR 139–154: `introStart` 1500, `settle` 1000, `hold` 2200, `turnTimeout` 20000, `retry` 2500 and others), with a `scale`. The tests scale the clock by 0.05 and override single keys (`src/Gui/tests/setup/tst_setup_trunk.qml:129–394`).
- **F19, orientation.** The Witmotion routine needs the raw quaternion samples, each segment's anatomical quaternion (for the roll angle φ), and the two mount figures after each call (WA 116, 119, 333–334, 360). The HackMotion routine's 15° travel gate needs the lower-arm unit's quaternion at two moments (HA 400, 694).
- The coordinator has already placed begin, stop and reset: QML forwards them to the driver[^pr45].

**Options.**
- **A. Name them all as events and inputs of the step machine.**
- **B. Leave restore and the pace to the driver.** The driver would then decide "done" on restore and hold the timings: a transition and thresholds outside the step machine, which RP-7 rules out[^rp7].
- **C. Drop restore.** A golfer who goes back a page would see a finished calibration as not done: a visible change, which §4.9 rules out[^d49].

**Recommendation: A.**
- **Restore is an event.** It carries the calibration records the Rig holds for the run's sensors (the coordinator's call is that "still calibrated" is read from the Rig[^pr50]; under decision 4 the Rig's record and the sensor agree). The step machine goes to done as today, both solves valid or not: today's behaviour is kept exactly[^pr48].
- **The pace is a value handed to the step machine when it is made**: its step timings, with today's numbers as the defaults, defined beside it in `src/IMU/calibration/`. The step machine still "has no clock, no timer"[^d48]: the timings are thresholds it compares elapsed time against. The driver keeps only what is its own (the tick interval, the log's rate limit) and passes a test's pace through.
- **Orientation and results are named.** "The maths is the step machine's" only holds once its inputs are named.

**Amends.**

Now (§4.8, the step machine's row):
> Takes events: a tick with its elapsed time, a sensor summary (gyro rate, tilt, heading, link up or down), "the guide finished", "speech finished", the HackMotion's own phase.

Becomes:
> Is made with its step timings (its pace), whose defaults are today's values. Takes events: begin, stop with a reason code, reset, and restore with the calibration records the Rig holds for its sensors; a tick with its elapsed time; a sensor summary for each sensor (gyro rate, tilt, heading, link up or down, and the orientation samples its maths needs); the result of each request it made (a solve's result, the two mount figures after an alignment is applied or refined, a device call accepted or refused); "the guide finished"; "speech finished"; the HackMotion's own phase, state and abort reason.

**If decision 4 is ruled otherwise** (the record need not match the sensor), restore carries what the driver reads from the live sensors, as today.

**Waits:** C-11a (restore, the pace), C-12a and C-13a (orientation, results), C-11b.2 (the pace its re-pointed tests set).

**Ruled:**

---

## 4. When the Rig holds a sensor's record (F7, F15, F35, F36, F34)

(proposed, for the Solution Architect)

**The question.** The design gives two lists, the calls that commit a record and the calls that clear one, and says the driver commits once a procedure is in C++[^d48]. The sensors' inventory, which the design names as "the authority for both lists"[^d48], found that the lists do not match the code, and that a calibration can begin, change and end where no listed call is made[^pr45][^pr48][^pr53].

**Evidence.**
- **Two halves on one `ImuInstance`.** An older half (`calibrated` and a transform) and the anatomical half (A, M, a flag, two mount checks). `setCalibration` and `clearCalibration` touch only the older half (`src/Gui/imu/imu_instance.cpp:720–753`, `755–763`). `fullyCalibrated()`, which a saved shot records as `calibrated`, reads only the anatomical half (`src/Gui/imu/imu_instance.h:212–213`). `setCalibration` has no production caller.
- **`zeroOrientation`** (`imu_instance.cpp:570–585`) says a calibration "is now invalid" and then clears only the older half, and only if it was set. A and M stay applied and `fullyCalibrated` stays as it was (F35).
- **A Witmotion run** sets each connected segment twice: `setNominalCalibration` at arm-down (WA 499, 501, 503; `imu_instance.cpp:801–838`) and `refineMountAboutLongAxis` at the T-pose (WA 357; `imu_instance.cpp:840–889`). A mount fail or a lost lead leaves every segment's A and M applied until the next begin (F15).
- **A trunk pass can be partial.** One sensor's solve can pass while the other fails (`imu_instance.cpp:909–991`; TR 372, 417–418), and the next attempt clears it (F7).
- **A HackMotion has no single "calibrated" moment.** Five handlers write its state, each with whatever the library reports (`src/Gui/imu/hm_instance.cpp:285–335`, `337–373`, `375–398`, `400–409`, `1024–1080`). Only the last emits `calibrationInvalidated`. A refresh that reads ABSENT drops "calibrated" with no such signal (`src/IMU/hm_session_worker.cpp:962–967`) (F36).
- **A HackMotion unit's M is not empty.** Each unit's binding records M = `hm_frame::mountM()` and A = identity (`src/Gui/shot/capture_context.cpp:172–175`). The design says the record "holds no A or M"[^d48], and C-8's test is that the block's per-sensor calibration equals `streams[].device`[^d10].
- **Teardown ends a calibration with no clear call.** `ImuManager::setSelected(false)` stops and deletes the device object, and its A and M with it (`src/Gui/imu/imu_manager.cpp:441–511`). It is reached from Disconnect (`src/Gui/settings/ImusPanel.qml:413`), the Capture page chip (`src/Gui/imu/CapturePage.qml:105`), a session or machine-wide disable (`imu_manager.cpp:355–379`) and `disconnectAll` (`imu_manager.cpp:513–527`). The next Connect builds a new, uncalibrated object (F34).
- **A role released or moved leaves A and M on the sensor** (`imu_manager.cpp:759–787`; `src/Gui/imu/imu_role_map.cpp:121–127`), so a forearm alignment stays on a sensor now worn on the thorax (F34).
- The briefs pull two ways: `BRIEF-C-11b.1.md` keeps the commit on `calibrateTrunk`'s own path, `BRIEF-C-12b.md` asks for "one commit per successful calibration", and `BRIEF-C-13b.md` moves the HackMotion's commit to the driver.

**Options.**
- **A. One rule: the record follows the live sensor.** The device object commits whenever its anatomical alignment is set or changes, and clears whenever it ends, whatever the cause. Nobody else calls the sink.
- **B. Keep the two lists and add the missing paths to them** (teardown, the three unlisted HackMotion handlers), and keep the driver's commit from C-11 on. Every new path found later is a new disagreement, and from C-11 a sensor has two committers: the driver for a set, the device object for a clear.
- **C. One commit per successful run, by the driver.** Between a Witmotion's arm-down and its T-pose, and after any failed run until the next begin, the sensor has A and M applied and the Rig has no record. That is the disagreement §4.2 says never happens[^d42], and a shot captured in that state would fail C-8's test of the block against `streams[].device`[^d10].

**Recommendation: A.** It is the design's own sentence, "the Rig's record and the live sensor never disagree"[^d42], made the rule and not the result of two lists. Only the device object sees every way a calibration ends: a link loss, a state refresh and a teardown happen outside any run, where no driver is listening. What follows from it:

1. **Who commits (F7).** The device object, always. From C-11 on, the driver performs the step machine's requests on the device object, whose own paths go on calling the sink. The driver never calls it. The record the step machine returns is the run's outcome, and the equivalence test checks it against the record the Rig then holds.
2. **Which half (F35).** The record follows the anatomical half only: A, M and the two mount checks. `setCalibration`, `clearCalibration` and `zeroOrientation` touch only the older flag, so they commit and clear nothing. Stage C keeps today's behaviour exactly[^pr48], so it does not make `zeroOrientation` do what its comment says; the coordinator files that mismatch as its own item[^zero].
3. **What a run commits (F15, F7).** Every change, when it happens: a Witmotion run commits each segment at arm-down and again at the T-pose; a partial trunk pass commits the one sensor that passed. A failed run leaves its record, because the alignment is still applied and shots captured then record it. A record with a failed mount check is a true record. "Fully calibrated" is worked out from the record by the one gate function (decision 5), never from the record being present.
4. **A HackMotion (F36).** One place in `HmInstance` compares "calibrated" before and after every state write, and commits or clears on the change. It commits one record for each of its two placement keys, since a sensor entry is keyed by `PlacementKey` and "a HackMotion appears as two entries"[^d46]. Each record holds what the host applies: A = identity and M = the unit's fixed mount, exactly as `streams[].device` writes them. The method, `deviceHeld`, says the calibration itself is in the device.
5. **What ends a record (F34).** A clear of the anatomical half; the device object's teardown; a HackMotion leaving "calibrated" for any reason; the end of the run of the app. Within a run, a new device object starts with no record, as it starts uncalibrated today: nothing is handed down to it.
6. **A role released or moved (F34) ends nothing.** The alignment is still applied, so the record stays, and its `role` parameter goes on naming the role it was solved for. The block then shows a sensor whose role and whose record's role differ, which is what happened. Whether a role move should clear a live alignment is a behaviour change, for the Product Owner and the Coaching Lead after Stage C.

The cost: a wearing makes more rig states than "one or two"[^d42]. A three-segment Witmotion run makes up to six, plus its clears. Nothing recomputes on `RigChanged`[^d42], so the cost is events and digests, not work.

What this gives up: step 2 of the approved design made the Rig the source and the sensor the follower ("the live sensor is handed its alignment from the new rig state"). That direction matters only when an alignment is restored onto a sensor, which §4.2 rules out[^d42]. It can be added by its own change if #74 or a later stage needs it.

**Amends.**

Now (§4.2):
> **Every path that clears a sensor's calibration today calls `Rig::clearCalibration`**: `ImuInstance::zeroOrientation`, the routines' reset paths, and a HackMotion's link loss or "calibration invalidated" signal. The sensors' duties inventory lists each one. So the Rig's record and the live sensor never disagree.

Becomes:
> **The Rig's record follows the live sensor's alignment, whatever changed it.** The device object commits a record whenever its anatomical alignment (A and M) is set or changes, and clears it whenever that alignment ends: a clear of it, the device object's teardown, or a HackMotion leaving its "calibrated" state for any reason. `ImuInstance::clearCalibration` and `zeroOrientation` touch only the older `calibrated` flag and leave A and M applied, so they clear nothing in the Rig. So the Rig's record and the live sensor never disagree.

Now (§4.8, step 1):
> Every path that sets a calibration on the live sensor commits a record to the Rig: `ImuInstance::setCalibration`, `setFunctionalCalibration`, `setNominalCalibration`, `refineMountAboutLongAxis` (which changes A again after a commit, on two branches) and `calibrateTrunk`; and the HackMotion reporting "calibrated", as a `deviceHeld` record. Every clear path clears it: `clearCalibration`, `clearFunctionalCalibration`, `zeroOrientation`, and the HackMotion's link loss (Appendix A, table A2).

Becomes:
> Every path that sets or changes a live sensor's anatomical alignment commits a record to the Rig: `ImuInstance::setFunctionalCalibration`, `setNominalCalibration`, `refineMountAboutLongAxis` (which changes A again after a commit, on two branches) and a valid `calibrateTrunk`; and a HackMotion entering its "calibrated" state, as a `deviceHeld` record for each of its two placement keys. Everything that ends that alignment clears it: `clearFunctionalCalibration`, the device object's teardown, and a HackMotion leaving "calibrated". `setCalibration`, `clearCalibration` and `zeroOrientation` do neither: they touch only the older `calibrated` flag.

Now (§4.8, step 2):
> Its driver commits the record the step machine returned, and the live sensor is handed its alignment from the new rig state, as typed input. Which of today's duties on `ImuInstance` (solve, apply) moves where is settled by the duties inventory, not here.

Becomes:
> Its driver performs the step machine's requests on the device object, whose own paths go on committing and clearing as in step 1, so each change on a sensor commits once. The driver never calls the sink. The record the step machine returns is the run's outcome, and the equivalence test checks it against the record the Rig holds. No alignment is handed from the rig state to a sensor in Stage C. The solve stays in `src/IMU` and applying stays on `ImuInstance`.

Now (§4.8, the `deviceHeld` sentence):
> so its record says that the device reported "calibrated" and when, and holds no A or M.

Becomes:
> so its record says that the device reported "calibrated" and when. Its A and M are the host's fixed frame for that unit (A = identity, M = the unit's mount), as each shot's `streams[].device` records them; they are not a solved alignment.

Now (§4.8, the sink):
> **Only the drivers and the device objects, which are in the Adapters ring, call it.**

Becomes:
> **Only the device objects, which are in the Adapters ring, call it.**

Now (§4.8, the driver's row):
> Owns the timers and the sensor taps, feeds the machine, calls the sink, and exposes the machine's state to QML as typed properties under the names the QML reads today

Becomes:
> Owns the timers and the sensor taps, feeds the machine, performs its requests on the device objects, and exposes the machine's state to QML as typed properties under the names the QML reads today

Now (§7, the calibration result's row):
> a `sensorAlignment` also ends with the run of the app

Becomes:
> a `sensorAlignment` also ends with its live device object, and so with the run of the app

Now (§4.2):
> A worn sensor is calibrated at each wearing, so a session will usually begin with one or two `RigChanged` events.

Becomes:
> A worn sensor is calibrated at each wearing, so a session will usually begin with several `RigChanged` events: one for each change of each sensor's alignment.

Now (§6, rule 3):
> Only the drivers and device objects (Adapters) include `rig_calibration_sink.h`, and none includes `rig.h`.

Becomes:
> Only the device objects (Adapters) include `rig_calibration_sink.h`, and none includes `rig.h`.

Now (§3, "A worn sensor's alignment", a correction of fact):
> The live object clears them itself (`zeroOrientation`, the routines' resets)

Becomes:
> The live object clears them itself (the routines' resets; `zeroOrientation` clears only the older `calibrated` flag and leaves A and M applied)

Now (§10, the paragraph before the table):
> changes who
> commits a record, not what the block holds, so it may follow C-8.

Becomes:
> changes who decides a run's steps, not who commits a record or what the block holds, so it may follow C-8.

Now (§10, C-6b's row):
> **today's calibrate paths commit to the Rig (the HackMotion's `deviceHeld` record included) and every clear path clears it**

Becomes:
> **every change of a live sensor's alignment commits to the Rig (the HackMotion's `deviceHeld` records included), and everything that ends one clears it, a teardown included**

Now (§10, C-11b's row):
> The trunk driver, the sink call, `TrunkRoutine.qml` as bindings

Becomes:
> The trunk driver, `TrunkRoutine.qml` as bindings

Now (§10, C-13's row):
> `HackMotionArmProcedure` and its driver; the driver commits the `deviceHeld` record that C-6b's path committed until now

Becomes:
> `HackMotionArmProcedure` and its driver

The last clause of §4.1's "Acquisition reaches it" sentence changes too; its replacement is written once, in decision 8. Appendix A's table A2 is right as far as it goes (`zeroOrientation` does call `clearCalibration()`), and gains "which clears only the older flag".

**The rules stand.** R-RIG-3 ("every clear of a calibration goes through the Rig") holds: every end of an alignment the Rig holds reaches it through the sink, a teardown included, and the older flag is not a calibration the Rig holds. R-RIG-4 ("only an adapter calls the sink") holds: the device objects are adapters. OBJ-RIG's calibration row and its lifecycle row change with these amendments (C1, C8).

**If ruled B or C:** decision 3's restore reads the live sensors, decision 7's "still calibrated" cannot be read from the Rig, and the coordinator's F23 call[^pr50] has to be taken back.

**Waits:** C-3 (what a `deviceHeld` record's parameters hold), C-6b.2 (the sink and both lists), C-8b (the block against `streams[].device`), C-11b.1, C-12b and C-13b (whether a driver commits).

**Ruled:**

---

## 5. Where the refine arithmetic and the mount limits live (F18)

(proposed, for the Solution Architect)

**The question.** A Witmotion's T-pose step refines its mount about the arm's long axis. The arithmetic, its two branches and the two limits that decide "mounted correctly" are in `ImuInstance`, in `src/Gui`. A Rules-ring step machine may not include that file, and the design left "which of today's duties on `ImuInstance` (solve, apply) moves where" to the inventory[^d48]. The inventory found no home[^pr48].

**Evidence.**
- The two branches: |φ| ≤ 25° refines M (`src/Gui/imu/imu_instance.cpp:868–888`); |φ| > 25° keeps the nominal mount and reports |φ| as the deviation, which always fails the gate (`:853–866`).
- The limits: `kMountDeviationMaxDeg = 15.0` and `kMountGravityErrorMaxDeg = 25.0` (`src/Gui/imu/imu_instance.h:199–200`), read by `fullyCalibrated()` (`:212–213`). The routine also tests the gate itself after the call (WA 360).
- The test fake models neither branch: it returns a scripted `mountResult` (`src/Gui/tests/setup/fakes/FakeImuInstance.qml`), so the golden trace reaches the branches only as a scripted answer.
- `src/IMU/imu_calibration.h` already holds the calibration maths in the Rules ring[^d3].

**Options.**
- **A. The arithmetic and the limits move to `src/IMU/imu_calibration.h` as pure functions.** `ImuInstance` goes on applying and calls them. The step machine requests "apply" and "refine", takes the two mount figures back as the result (decision 3), and tests the gate with the same limits.
- **B. They stay in `ImuInstance`, and the step machine takes `fullyCalibrated`'s answer.** The gate, a threshold, is then decided in the Adapters ring, which RP-7 rules out[^rp7], and no test below the app can reach either branch.
- **C. The step machine computes A and M itself and the driver commits them.** This is the approved step 2; it needs decision 4 ruled B or C.

**Recommendation: A.** One home for the limits, which both rings can reach because `src/Gui` may include `src/IMU` and not the reverse[^ring]. The arithmetic is moved, not changed, and a C++ test pins both branches for the first time. The gate function is also what "fully calibrated" is worked out with from a record (decision 4, point 3).

**Amends.** Decision 4's replacement for step 2 gains one sentence:
> The refine arithmetic of `refineMountAboutLongAxis` and the two mount limits move to `src/IMU/imu_calibration.h`; `ImuInstance` and the step machine both call them.

RING-RULES' row for `imu_calibration.h` gains the two functions (C9).

**Waits:** C-12a, C-12b.

**Ruled:**

---

## 6. How the arm status view reaches what it reads (F20)

(proposed, for the Solution Architect)

**The question.** The arm calibration's status column reads below the routine's contract: a routine's private properties, and a device's properties directly. Once the routines are bindings on a driver, where does each read go[^pr48]?

**Evidence.**
- Private routine properties: `_captureHoldMs` (AS 70), `_armDownCaptured`, `_hmRaiseTravelDeg` (AS 81) and `_hmMinRaiseTravelDeg`; the travel tint compares the last two (AS 437–439).
- Read straight from a device: the lead unit's `imuConnected` (AS 156), `calibrated` and `calibrationAngleValid` (AS 328); the HackMotion's `relativeAngleDeg` (AS 414), `calibrationState` (AS 444), `fullyCalibrated` (AS 445), `presenceAngleDeg` (AS 450) and `poseSpreadMaxDeg` (AS 454).
- The design says a view reaches a procedure's running state through `app.calibration` and a rig fact through `app.rig`[^d7]. It does not say where a live readout comes from.

**Options.**
- **A. Each fact from its owner.** A procedure's state, progress and verdicts from `app.calibration`. "Is it calibrated" from `app.rig`. A live readout (an angle now, the link now, the device's own state code) from the device object, as today.
- **B. The driver re-exposes everything**, device readouts included, so the view reads one object. The driver becomes a second door onto each device, and every new readout needs a driver edit.

**Recommendation: A.** It is the ownership rule applied three times[^p]. In detail:
- the private properties go: the driver exposes the hold progress, "arm-down captured", the travel seen, and a "travel is enough" flag, so the 15° limit stays in the step machine;
- `fullyCalibrated` is read from `app.rig`, as the coordinator has called for the hosts[^pr50];
- the live readouts stay on the device object: they are not rig facts and not a procedure's state;
- `calibrated` and `calibrationAngleValid` are the dead older flag, which the coordinator has left for clean-up after Stage C[^pr48].

**Amends.** §7's row "A procedure's running state" gains, in "How a view reaches it": "`app.calibration`; a live device readout is read from the device object, and 'is it calibrated' from `app.rig`".

**Waits:** C-12b, C-13b, C-15.

**Ruled:**

---

## 7. One driver, two hosts (F22)

(proposed, for the Solution Architect)

**The question.** The design makes one driver per procedure, created by the composition root, living as long as the app[^d48]. Today each host (the set-up page and the session toolbar's panel) has its own flow, which creates a routine while it is active and destroys it when it is not. The design does not say how two hosts share one driver, what a vendor change under a run is, who holds an outcome after its run, or whether the load-and-unload stays[^pr50].

**Evidence.**
- Each flow loads its routine when it becomes active (IF 267–268, 290, 327–332) and stops and unloads it when it goes inactive (IF 285–289).
- "A host that is not active holds no routine, so the panel's flow must send nothing" (`src/Gui/tests/setup/tst_setup_calib_hackmotion.qml:421–422`); `test_CH09_twoHostsMarkersOnce` (`:423`) hosts both and checks each device call is made once.
- A vendor change on the lead-forearm role stops the routine with `stop("vendor")`, drops the outcome and swaps the routine (IF 294–300).
- The flow's `_done` (IF 166) outlives the routine and feeds `phase` 2 with no routine loaded (IF 92–93).
- The coordinator has placed the routine choice in the C++ procedure's side (C-15)[^pr50] and the entry points as "QML forwards to the driver"[^pr45].

**Options.**
- **A. Hosts attach to the one driver.**
- **B. One driver per host**, created by `app.calibration` on demand. Two drivers could then run on one sensor at once, and each would be created by a view's request, which the design rules out[^d48].

**Recommendation: A**, in five parts:
1. **A host attaches while it is active and detaches when it is not.** Only an attached host's begin, stop and reset are forwarded. That is today's "an inactive host holds no routine", without creating or destroying anything.
2. **One run at a time.** A driver has at most one run. When the host that began it detaches, the driver gets `stop` with the reason `inactive`, as today. If a second host attaches during a run, it shows that run, and its begin is ignored and logged. Today two active hosts would mean two routines on one sensor; the driver makes that impossible.
3. **A vendor change is a stop.** `app.calibration` owns the choice[^pr50]. When the choice changes under a run it stops that driver with the reason `vendor` and drops its outcome, as today. The step machine sees a stop with a reason code (decision 3) and needs no new event.
4. **The outcome stays with the driver.** It outlives its hosts, so it keeps its last finished stage until the next begin, reset or vendor stop. The flow's `_done` goes. The outcome is never the answer to "is it still calibrated": that is the Rig's record[^pr50], and a host showing a finished calibration again sends restore (decision 3).
5. **Loading a page is view state.** The Loader decides which page is on screen and nothing else.

**Amends.**

Now (§4.8):
> The drivers are created by the composition root and reached as `app.calibration`, never created by QML[^p].

Becomes:
> The drivers are created by the composition root and reached as `app.calibration`, never created by QML[^p]. `app.calibration` chooses which driver serves a role. A host attaches to that driver while it is active; a driver has one run at a time, stops when the host that began it detaches or the choice changes, and keeps its last outcome until its next run.

§7's row "A procedure's running state" changes "reset at each run" to "reset at each run; the last outcome is kept until the next run, a reset or a vendor change" (C8).

**If decision 4 is ruled B or C,** part 4's "still calibrated" is read from the live device, as today.

**Waits:** C-11b.1 (it creates `app.calibration`), C-12b, C-13b, C-15.

**Ruled:**

---

## 8. What a manager takes from the Rig, and how it learns of a change (F26, F27, F38)

(proposed, for the Solution Architect)

**The question.** The design says "devices are handed the current rig state as a typed value"[^d41]. It puts aliases and the enabled flags outside the rig state, and says changing one emits no `RigChanged`[^d42]. So it does not say how `CameraManager` and `ImuManager` read an alias or an enabled flag, how they learn that one changed, or which live effect follows which change[^pr51][^pr53].

**Evidence.**
- **Each camera fact reaches a live camera its own way today.** A perspective change sets the live controller, sets the camera that loses Impact to None, and reconnects every selected camera whose Impact-ness changed (`src/Gui/cameras/camera_manager.cpp:732–746`, `762–795`). The reconnect is why the new instance reads its role's crop, rate and exposure, and it relies on the VIEW combo having written those settings first (`src/Gui/settings/CamerasPanel.qml:536–561`). Mirrored is set live only when there is a live instance. Fixed in place has no live effect. The box is set live before it is saved. The alias is pushed to the live controller.
- **A stored box is applied only at connect, and only for a fixed camera** (`camera_manager.cpp:1005`, in `createController`[^a2]). The set-up Ball step uses a saved box on a camera that is not fixed, on purpose.
- **The managers read the unversioned facts.** A camera's alias at construction (`src/Gui/cameras/camera_instance.cpp:125`) and in `cameraList` (`camera_manager.cpp:154`); the enabled flag at construction, on discovery and in the list (`:66`, `:545`, `:214`). The live alias names each clip file and the swing's `alias` (`src/Gui/shot/shot_processor.cpp:841–855`).
- **They react to changes of them.** `setCameraAlias` pushes to the live controller and emits `cameraListChanged` (`camera_manager.cpp:1034–1051`); `setExcluded` updates the flag, mirrors it into the session's list and selects or deselects the camera (`:559`). `ImuManager` seeds its session list from the machine-wide list (`src/Gui/imu/imu_manager.cpp:80–86`), and on a change disconnects each newly excluded sensor (`:88–113`); `imuList` and `imuDeviceList` carry each alias (`:264–348`), and `setImuAlias` emits both list signals (`:1032–1050`).

**Options for learning of an unversioned change.**
- **A. A second event from the Rig**, for the unversioned part: it carries the device key and which fact changed. `RigChanged` stays as approved.
- **B. `RigChanged` for these too.** It contradicts the approved "Not emitted for" row, and there is no new `RigRef` for it to carry[^d42].
- **C. Only a manager may issue these two commands**, so it applies its own effect. The effect is then tied to the caller, not the change: the copy from settings at launch, or any other caller, changes the Rig and no live object hears of it. QML also keeps two doors, the manager and `app.rig`.

**Options for what a live device applies.**
- **D. Only the field that changed, by a table.**
- **E. Every field, on every change.** Toggling the fixed switch would then start applying or withholding a live box: a change the golfer can see[^pr51].

**Recommendation: A and D, one rule.** A manager is handed both parts for its own devices as typed values, is told of every change to either, and applies only what changed. `app.rig`'s list models need the second event too, or an alias edit would not show. The table is today's behaviour:

| What changed | What the manager does to live objects |
|---|---|
| Perspective | sets it on the live controller; reconnects each selected camera whose Impact-ness changed |
| Mirrored | sets it, if there is a live instance |
| Fixed in place | nothing. In particular it never applies or withholds the box |
| The hitting-area box | sets it, if there is a live instance. At connect, a stored box is applied only for a fixed camera |
| A camera's alias | pushes it to the live controller; the list changes |
| A camera's enabled flag | updates its own flag and the session's list; selects or deselects the camera; the list changes |
| A sensor's role | as today: the role views are recomputed. A and M are not touched (decision 4, point 6) |
| A sensor's alias | the two lists change |
| A sensor's enabled flag | each newly disabled sensor is switched off for the session, which disconnects it; each re-enabled one is switched on |

**The Impact reconnect is the manager's**, as its reaction to the change. Its ordering is a stated precondition of `assignPerspective`: a caller that moves a camera to or from Impact writes that role's device settings (exposure, rate, the `#impact` crop) first. Those settings are not the Rig's[^d43], so the Rig cannot order them. The VIEW combo's handler keeps its order, and C-5b's test pins it.

The learned club-length prior gets no event: nothing shows it[^d7], and it changes on every shot.

**Amends.**

Now (§4.1):
> devices are handed the current rig state as
> a typed value, and a driver hands back a calibration record or a clear.

Becomes:
> devices are handed the current rig state and, for their own devices, the alias and the enabled flag, each as a typed value, and are told of every change to either; and a device object hands back a calibration record or a clear.

(The last clause, "a device object" for "a driver", is decision 4's and stands or falls with it.)

Now (§4.1, the table's row for the Rig's other facts):
> is saved; no new state, no event

Becomes:
> is saved; no new state and no `RigChanged`. A change to an alias or an enabled flag emits `RigDeviceLabelChanged` (§4.2)

Now (§4.2, EVT-15's table):
> an alias, an enabled flag, a learned prior; a command that leaves the digest unchanged; the state loaded at launch

It stays as it is. A new row follows the table:
> A change to an alias or an enabled flag emits a second event (placeholder name `RigDeviceLabelChanged`) from the Rig, after the save. It carries the device key and which of the two changed. It recomputes nothing and makes no new rig state. A learned prior emits nothing.

The IM gains the event's row in §7.4 (C7), and CLS-RIG's "hands devices the current rig state as a typed value" gains the two facts.

**Waits:** C-5a.1 (the event; the commands' effects), C-5b (the table for cameras), C-6a.2 (the enabled flag), C-6b.1 (the table for sensors).

**Ruled:**

---

## 9. When two sensor keys are equal (C-1a, PR-52)

(proposed, for the Solution Architect)

**The question.** `DeviceKey` is built (C-1a). A sensor's key has two persisted spellings, with and without its description[^d46]. §4.6 does not say whether two keys for one device id with different descriptions, or one with a description and one without, are the same key (C5)[^pr52].

**Evidence.**
- `imu/roles` and `imu/excluded` spell a sensor by its bare device id; `imu/alias` spells it `description|deviceId`[^d46].
- As built, two sensor keys are equal when their device ids are, whatever their descriptions; a camera is compared by its description and its identifier (`src/Vocabulary/Rig/device_key.h`, the `==` at line 197, in C-1a's worktree, not yet committed).
- A sensor key read from a bare-id map spells `"|<id>"` with its description, which matches no stored alias[^c1a].

**Options.**
- **A. By device id alone**, as built.
- **B. By both parts, with a second "same device" test.** A key read from `imu/roles` would never equal the key the enumerator builds for the same sensor, and every lookup would have to pick the right test.

**Recommendation: A**, with the rule that comes with it: `imu/alias` is never looked up by key equality, only by its own spelling (`persistedWithDescription()`), or two descriptions of one id would merge. Stated cost: two sensors with empty device ids compare equal.

**Amends.** §4.6 gains, after "each map's reader and writer states which it uses":
> Two sensor keys are equal when their device ids are; the description is carried only for the alias spelling, and `imu/alias` is looked up by that spelling, never by key equality. A camera key is equal when both its parts are.

ID-DEVICE-KEY's row in the IM gains the same sentence (C5).

**Waits:** C-1c (how the alias map is keyed), C-3 (the rig state's ordering and digest use the keys' equality).

**Ruled:**

---

## 10. A camera key with no `|` (F32)

(proposed, for the Solution Architect)

**The question.** The set-up tests name their cameras with bare keys. §4.6 gives no `DeviceKey` for a camera key without a `|`, and the copy from settings "parses each map key into a `DeviceKey`" (`build/platform/C/BRIEF-C-5a.2.md`). What is such a key (C5)[^pr51]?

**Evidence.**
- The fixtures: `"FO1"`, `"DTL1"` and the like (`src/Gui/tests/setup/tst_setup_indicator.qml:234–235`, through `src/Gui/tests/setup/fakes/FakeCameraManager.qml`).
- Production always writes `description|identifier`. With an empty description it writes `"|id"`, never a bare id[^c1a].

**Options.**
- **A. No rule in the type.** The fixtures are re-keyed to `description|identifier` in C-5b: an edit to existing tests, none deleted. The copy keeps any entry it cannot type, verbatim, as the coordinator has already called for the sensor maps[^pr53].
- **B. A third camera spelling** that round-trips a bare string. An identity type would carry a representation that only tests write.
- **C. A bare key parses to null.** The copy would then drop such an entry, against "no setting is migrated or lost"[^rp6].

**Recommendation: A.** The type states what production writes and nothing else, and nothing stored is lost.

**Amends.** §4.6 gains: "A key that fits no spelling is not a `DeviceKey`. The copy from settings keeps such an entry verbatim and matches it to no device."

**Waits:** C-5a.2 (what the copy does with a key it cannot type), C-5b (the fixtures).

**Ruled:**

---

## 11. `#wide` (PR-52)

(proposed, for the Solution Architect)

**The question.** Is `<camera key>#wide` one of the device key's spellings? The coordinator had C-1a build it, on the grounds that it "doesn't need a new ruling"[^pr52]. This page asks it anyway; the last section says why.

**Evidence.**
- `camera/exposureUs` and `camera/tuning` spell every role but Impact as `<camera key>#wide` (`src/Gui/cameras/camera_roi.h:92–98`; pinned by `camera_roi_test`).
- §4.6's table lists `#impact` for `camera/roi` and omits `#wide`[^d46].
- C-1a added the spelling to the type, a sentence to ID-DEVICE-KEY's row and a row to the IM's spelling table, each citing the approval of 24 Nov (C-1a's worktree, not yet committed).

**Options.**
- **A. Confirm it as built.**
- **B. Leave `camera_roi.h` building `#wide` itself.** One place would still know a key's shape without building it, where the design's count is 6 → 0[^d46].

**Recommendation: A.** The spelling exists today, and the rule is "each persisted map keeps today's spelling, byte for byte"[^d46]. The two maps are not the Rig's[^d43]; the type offers the spelling so `camera_roi.h` stops building it, as it does for `#impact`.

**Amends.** §4.6's spelling table gains a row:
> | `camera/exposureUs`, `camera/tuning` (not the Rig's) | a `DeviceKey` and a role | the camera spelling for Impact, plus `#wide` for every other role (`camera_roi.h`) |

**Waits:** nothing is held: it is built. C-1a's IM rows wait for a footnote that is true.

**Ruled:**

---

## The coordinator's calls: one this page thinks was wrong, and two cautions

The calls logged in PR-36, PR-45, PR-48, PR-50, PR-51, PR-52 and PR-53 were read against the approved design. None of the others is re-asked above.

- **Wrong, in one respect: `#wide` "doesn't need a new ruling" (PR-52).** Building it was right. Calling it not an information-model change was not. The definition counts any edit that "changes what this page says about" an identity (C5)[^im12]. The call's reason, that the spelling is not a new representation, answers a different clause. C-1a changed ID-DEVICE-KEY's row and added a row to the spelling table, so by the letter it needed the Solution Architect before it was built. The rows also cite the approval of 24 Nov, which never saw `#wide`. The harm is small, because the spelling is today's and nothing stored changes; the remedy is decision 11, and until it is ruled the rows should cite the coordinator's call.
- **A caution on F23, "still calibrated is read from the Rig" (PR-50).** It holds only if decision 4 is ruled A. Even then the read must apply the mount gate to the record's two mount checks (decision 5): a Witmotion with a failed mount has a record and is not fully calibrated. Testing for "a record is present" would show such a sensor as calibrated.
- **A caution on F37, the role functions that feed the freeze (PR-53).** The call covers the two automatic writers and is silent on `resolveRole`, `roleHolder`, `rolesInSession`, `ownerOfKey` and `parseUnitKey` (`src/Gui/imu/imu_role_map.h:213–235`). That needs no ruling: the approved change is "the role rules of `imu_role_map.*` in `src/IMU`; its two display-label functions stay in Adapters"[^d8], so everything in the file but those two moves. `BRIEF-C-6a.1.md` What 1 lists five things and should say so.

---

## Notes

[^sa24]: The Solution Architect, 24 Nov 2026: "approve as recommended", the design, its information-model changes and all 19 answers of §9 (the platform refactor's judgement calls, PR-35; `docs/design/rig_design.md`, Status).
[^inv]: The platform refactor's Stage C working notes, not committed: the duties inventories C-10a, C-10b, C-10c, C-5i and C-6i, and the notes of C-1a. What this page relies on from them is copied into each section's evidence, with the file and line each fact was read from.
[^pr36]: The platform refactor's judgement calls, PR-36 (24 Nov 2026): C-12 and C-13 are for the Solution Architect; the briefs route the managers' requests through the composition root until then.
[^pr45]: The same, PR-45: of C-10a's thirteen findings, F4, F5, F6, F7 and F8 are for the Solution Architect. The coordinator's calls: speech and the guide stay in the QML views (F1, F2); the entry points are placed by C-11b's briefs (F12).
[^pr48]: The same, PR-48: the refactor keeps today's calibration behaviour exactly (F14); the unused average and the dead older flag are left for clean-up after Stage C (F16, F17); F15, F18, F19 and F20 are for the Solution Architect.
[^pr50]: The same, PR-50: the routine-choice rule has one home, the C++ procedure's side (F21); "still calibrated" is read from the Rig, not the live device (F23); F22 is for the Solution Architect.
[^pr51]: The same, PR-51: F26, F27's alias and F32's bare test keys are for the Solution Architect; F27's enabled flag is settled by §9's question 17.
[^pr52]: The same, PR-52: `#wide` is reproduced in C-1a (the coordinator's call); sensor-key equality is for the Solution Architect.
[^pr53]: The same, PR-53: F34, F35, F36 and F38 are for the Solution Architect; the sensor copy keeps every entry verbatim, including ones the typed keys cannot hold (L4).
[^c1a]: C-1a's notes, call 4: "Empty parts still give `"|id"`, as today does."
[^d3]: `docs/design/rig_design.md` §3, "Calibration maths already in C++".
[^d41]: The same, §4.1, "Acquisition reaches it".
[^d42]: The same, §4.2: the `sinceUtc` sentence; EVT-15's table ("Recomputes: nothing"; "Not emitted for"); "a session will usually begin with one or two `RigChanged` events"; "the Rig's record and the live sensor never disagree"; a worn sensor's alignment is never re-applied at launch.
[^d43]: The same, §4.3: `cameraExposureUs`, `cameraTuning` and `cameraRoi` are "not the Rig", device configuration.
[^d46]: The same, §4.6: "Each persisted map keeps today's spelling, byte for byte"; the spelling table; a sensor's two persisted spellings; "a HackMotion appears as two entries"; "the 6 places that know a shape → 0".
[^d48]: The same, §4.8: the record and its `evidence`; "How a result commits, in two steps of the build"; the sink; "The shape of a procedure"; "The drivers are created by the composition root". §6 rules 3 and 8 for what may include `rig.h` and the sink.
[^d49]: The same, §4.9: "No look or behaviour change is planned".
[^d7]: The same, §7: the rows "A calibration result", "A procedure's running state" and "SET-05" ("not shown").
[^d8]: The same, §8, the C9 row.
[^im12]: IM §1.2, "What counts as an information-model change", C5 (`governance/artefacts/information_model.md`).
[^d10]: The same, §10, C-8's test: "The block's per-camera set-up and per-sensor calibration equal `streams[].setup` and `streams[].device` of the same shot."
[^a2]: The same, Appendix A, table A2, `camera/ballRoi`.
[^q5]: The same, §9 question 5, approved as recommended: the `Session` writes the ceremony file, handed the ceremony when it starts.
[^q19]: The same, §9 question 19 and §4.5: "an equal `stateId` means the same captured set-up".
[^rp6]: The same, §2, RP-6: "No setting is migrated or lost".
[^rp7]: The same, §2, RP-7: "A procedure decides; a view shows. Thresholds, maths and step transitions are C++ that takes time and samples as inputs".
[^ring]: IM §4: rings point inward only; `src/IMU`'s calibration headers are Rules, `src/Gui/imu` is Adapters.
[^p]: IM §3, P3 (one writer) and P9 (the typed QML boundary); IM §10's "how a view reaches it".

[^r1125]: The Solution Architect, 25 Nov 2026, in the platform refactor's coordination session.
[^zero]: The Product Owner and the Design Lead, 26 Nov 2026 (`build/platform/JUDGEMENT-CALLS.md`, "C5, and the battery colours"): the Zero button is removed, with `ImuInstance::zeroOrientation` and `ImuManager::zeroAll`, after this page's code facts were taken. The trunk calibration sets "straight". Where this page lists `zeroOrientation` as a path, that path no longer exists.
