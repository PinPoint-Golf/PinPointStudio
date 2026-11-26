Taken at `1a083129` (main), `src/Gui/calibration/HackMotionArmRoutine.qml`, 759 lines. C-10b; read, not run.

# Duties of `HackMotionArmRoutine.qml`

`HA` = `src/Gui/calibration/HackMotionArmRoutine.qml`. Line numbers are at `1a083129`; later items match on the symbol. The root type is `CalibrationRoutineBase`. The host is `ImuCalibrationFlow.qml` (C-10c), which loads it when the slot-A holder is a HackMotion (`_isHackMotionRole`, `ImuCalibrationFlow.qml:218–221`).

The device runs the calibration (libwrist, `HmInstance`): this file issues markers, reads the device's phase and state, measures one thing of its own (the forearm's travel), and words the outcome. Nothing is solved or stored host-side. It speaks nothing and makes no recorder call.

**Where each duty goes:**
- **P**: `HackMotionArmProcedure` (Rules, C-13a). Its events: a tick, the device's phase, state and abort reason, link up or down, the result (or refusal) of each device call it requested, the guide finished. Its outputs: stage, codes, requests (the five device calls), then a `sensorAlignment` record with method `deviceHeld` (no A, no M) or a failure code.
- **D**: the HackMotion arm driver (Adapters, C-13b). Timers, the device taps, the five calls, the `deviceHeld` commit (moved from `HmInstance` in C-13b), today's names for QML.
- **Q**: stays in QML as a binding or a word (C-13b). **C-15**, **X**, **NO HOME Fn** as on the Witmotion page (F1–F13 `NOTES-C-10a.md`, F14 on `NOTES-C-10b.md`).

## 1. Thresholds and constants

| Symbol, HA line | Value, unit | Meaning | Goes |
|---|---|---|---|
| `_hmMinRaiseTravelDeg` 208 | 15.0° | The travel gate: the lower-arm unit's rotation between OBSERVING_RAISE and the raise-confirm settle must be ≥ this, or the routine aborts before `a2 01` (before anything is applied). Between §8.2's ~30° separable and 4.2°/7.6° unseparable attempts. No C++ mirror | P; its quaternion input **F19** |
| `_hmQuatAngleDeg` 284–287 | ° | 2·acos(min(1, \|a·b\|)): how far, never which way | P (verbatim) |
| `_hmUnitQuat` 271–279 | n/a | The lower-arm unit's quaternion; exact identity (1,0,0,0) reads as "no sample yet" → null → travel unmeasured, and the raise is confirmed anyway (712–717, deliberately permissive) | P; the read D (**F19**) |
| `defaultPace` 58–67 `hmStart` 1500 | ms | Read window after `guide.ready`, before `beginCalibration` | P (time is an input) |
| `settle` 2000 (`_hmSettleMs` 158) | ms | Pose-0 settle from AWAIT_HORIZONTAL to `confirmHorizontal` (`a2 00`). Timed, not sensor-gated (**F14**) | P |
| `raiseSettle` 500 (`_hmRaiseSettleMs` 174) | ms | After the guide finishes the raise, before the travel gate and `confirmRaise` (`a2 01`) | P |
| `refSettle` 1500 (`_hmRefSettleMs` 176) | ms | After the guide's return, before `confirmReferencePose`. Timed, not sensor-gated (**F14**) | P |
| `presenceWait` 6000 (`_hmPresenceWaitMs` 180) | ms | Bound on the wait for the presence measurement (library averages ≤ 64 samples at ~25 Hz ≈ 2.6 s) | P |
| `raiseAnim` 3000 (`_hmRaiseAnimMs` 173), `returnAnim` 1500 (`_hmReturnAnimMs` 175) | ms | Guide durations. **Functional:** the raise animation paces the motion the device times; 3000 + 500 lands `a2 01` at ~3.5 s, inside the library's 6 s `calibration_raise_limit_us`. Never raise that limit, never add a wall-clock fallback (166–172) | the duration P's (time); the guide call **F2** |
| `scale` 1, and `pace` | × | Tests (W7) | **F8** |
| `guide.hmRaiseDeg` (fallback 30) 708 | ° | Only the "needs about N°" number in the travel-gate sentence | Q word; its source **F2** |
| Library phase codes `calpIdle` 99 … `calpAborted` 107 | 0 IDLE, 1 AWAIT_HORIZONTAL, 2 MARKING_POSE0, 3 OBSERVING_RAISE, 4 MARKING_POSE1, 5 APPLYING, 6 VERIFYING, 7 COMPLETE, 8 ABORTED (`calpAwaitHorizontal` 100, `calpMarkingPose0` 101, `calpObservingRaise` 102, `calpMarkingPose1` 103, `calpApplying` 104, `calpVerifying` 105, `calpComplete` 106) | `wr_calibration_phase`, named once because QML cannot see the C enum | P's input codes |
| Calibration-state codes `calUnknown` 109, `calUncalibrated` 110, `calCalibrated` 111, `calLost` 112 | 0, 1, 2, 3 | `wr_calibration_state`. Read here only through `fullyCalibrated`; the status column reads them for its readout | P's input; the readout Q (**F20**) |
| Abort codes `abortNone` 116, `abortCaller` 117, `abortRaiseTooSlow` 118, `abortStreamLost` 119, `abortLinkLost` 120, `abortNoResult` 121 | 0–5 | `wr_calibration_abort_reason` (exactly six enumerators) | P's input; §2a |
| Refusal codes `errInvalidState` 124, `errLinkDown` 125, `errNoStream` 126, `errBusy` 127 | −2, −12, −14, −18 | `wr_status` values a calibration call can be refused with | P's input; §2b |
| Boundaries and NaN | n/a | The travel gate aborts only at `travel < 15`, so exactly 15.0° confirms; an unmeasurable travel (no unit, or no sample yet) skips the gate and confirms. The verdict needs `fullyCalibrated === true` and step ≥ 4 | P, as is (C-13a tests each side) |
| Retry limits | none | No automatic retry: every stop (step 9) waits for a Recalibrate tap ("Do NOT queue a begin for later and do NOT retry", 347) | P, as is; **F14** |

## 2. States and transitions

`stage` (76–79) is a binding: done → `"hm5"`; not `_running` → `"idle"`; `hmStep` 9 → `"hmStopped"`; else `"hm" + hmStep`. `hmStep` 135: 0 ready, 1 pose 0, 2 raise, 3 applying, 4 reference pose, 5 confirmed, 9 stopped. The base logs each stage change (no reason text). `failed` 80 = `calibrationFailed` 86; `message` 81 = `hmFailMsg` 140; `messageKind` 82 = `hmFailKind` 141 when a message is set. `mountFailed` and `phase` keep the base's false / 0.

The device's phase is followed through `hmPhase` 148 (= `leadDevice.calibrationPhase`) and `onHmPhaseChanged` 151, which reacts only while `active && _running` (R7). Each phase it follows (`_hmOnPhase` 381–457):

| Id | Device phase | What the routine does | HA | Goes |
|---|---|---|---|---|
| H1 | 0 IDLE | nothing (an invalidation drives the phase here: `hm_instance.cpp:1056`) | 381 | P |
| H2 | 1 AWAIT_HORIZONTAL | `hmStep` 1; the horizontal settle re-armed | 385–391 | P |
| H3 | 2 MARKING_POSE0 | nothing; the status column words it ("Marking position 1") | n/a | P (input); Q words |
| H4 | 3 OBSERVING_RAISE | `hmStep` 2; `_hmRaiseStartQuat` = the unit's quaternion, `_hmRaiseTravelDeg` NaN; the guide posed at pose 0 and animated to pose 1 over `raiseAnim`, `_animStage` "hmRaise". The device watches from this instant | 393–410 | P; the quaternion **F19**; the guide **F2** |
| H5 | 4 MARKING_POSE1 | nothing; words "Marking position 2" | n/a | P (input); Q |
| H6 | 5 APPLYING | `hmStep` 3; guide held at pose 1, `_animStage` "" | 412–415 | P |
| H7 | 6 VERIFYING | `hmStep` 4 (transform applied, not yet checked: no tick); guide animated back to pose 0 over `returnAnim`, `_animStage` "hmReturn"; with no guide, the ref settle armed at once | 417–431 | P |
| H8 | 7 COMPLETE | `_hmEvaluate` (the verdict, below) | 433–438 | P |
| H9 | 8 ABORTED | if already stopped (step 9) with reason CALLER: our own abort's echo, message kept, log `aborted reason=caller after our own stop — message kept`. Else `_hmStop` by reason (§2a) | 440–456 | P |

The rest of the machine (ids for C-9c):

| Id | From → to | Trigger and guard | HA | Goes |
|---|---|---|---|---|
| H10 | any → step 0 | begin() or reset() → `on_Clearing`: `_hmAbortIfActive` (if the device's `calibrationActive`: H32/H33), `_hmResetState`, `_animStage` "", the guide posed at rest on a begin, failed and done false. Nothing to clear host-side | 226–233, 289–301 | P; the abort D |
| H11 | step 0 → begin | `_hmStartTimer` after `hmStart`, while step 0 and `guide.ready` → `_hmBegin`. Not on on-and-still (**F14**) | 633–641 | P; `guide.ready` **F2** |
| H12a | → stopped (error) | `_hmBegin`: no device, or `!imuConnected` | 353–357 | P |
| H12b | → stopped (error) | `_hmBegin`: connected but `!streaming` | 358–363 | P |
| H12c | step 0 → 1 | `_hmBegin`: message cleared, `hmInvalidated` false, step 1, guide posed at pose 0, `beginCalibration()` (log `beginCalibration`) | 364–377 | P (request) |
| H13 | step 1 → `a2 00` | `_hmHorizontalSettleTimer` after `settle`, still step 1: `confirmHorizontal()` (log) | 664–677 | P (request) |
| H14 | step 2: guide done | `on_GuideDone`, "hmRaise": `_hmRaiseConfirmArmed`. The chain advances here and nowhere else | 644–655 | P (input: guide finished) |
| H15 | step 2 → stopped (warn) | `_hmRaiseConfirmTimer` after `raiseSettle`, step 2: travel measured and < 15°: `abortCalibration()` (log `abortCalibration travel=N`), stop "Your forearm only moved N°…" | 683–710 | P (request: abort; code: short raise) |
| H16 | step 2 → `a2 01` | travel ≥ 15°, or not measurable: `confirmRaise()` (log `confirmRaise travel=N\|unmeasured`) | 712–717 | P (request) |
| H17 | step 4: guide done | `on_GuideDone`, "hmReturn": guide held at pose 0, `_hmRefSettleArmed` | 656–660 | P |
| H18 | step 4 → measuring | `_hmRefSettleTimer` after `refSettle`, step 4: `hmAwaitingPresence`, `confirmReferencePose()` (log), the presence wait re-armed. The call returns before the measurement exists | 725–740 | P (request) |
| H19 | the verdict | `_hmEvaluate` (466–534) runs on every `calibrationStateChanged`, on COMPLETE and when the presence wait ends. Idempotent. Returns at once when done or stopped, and **while step < 3** (a previous attempt's state persists until the library moves it: CH07) | 466–476 | P |
| H20 | step ≥ 4 → 5, done | `fullyCalibrated` (state CALIBRATED): presence wait off, step 5, failed false, message "", `calibrationDone` → `completed()` (219). Never from phase COMPLETE alone, never from a small presence angle | 482–490 | P (record `deviceHeld`); the commit D (C-13b) |
| H21 | waiting | not calibrated and phase ≠ COMPLETE: still in flight | 493 | P |
| H22 | → stopped (error) | COMPLETE with step < 4: "finished before the reference pose could be taken" | 495–503 | P |
| H23 | → stopped (warn) | COMPLETE, abort reason CALLER: presence check declined (applied, unverified, not undone) | 505–510 | P |
| H24 | → stopped (error) | COMPLETE, `presenceNotMeasured` (WR_WARN_PRESENCE_NOT_MEASURED): too few readings | 518–524 | P |
| H25 | → stopped (error) | COMPLETE, `presenceSamplesUsed` > 0, not calibrated: the check did not confirm | 529–532 | P |
| H26 | still waiting | COMPLETE, no samples yet: keep waiting (the presence event may follow the phase event) | 533 | P |
| H27 | step 4 → stopped (error) | `_hmPresenceWaitTimer` after `presenceWait`: `_hmEvaluate` once more, then if still step 4 and not done, "never reported a check" | 745–758 | P |
| H28 | → stopped (error) | **link loss**: `_deviceLink.onCalibrationInvalidated` (the device emits it on link-down and on a stream restart, `hm_instance.cpp:1024–1061`) → `_hmInvalidated`: `hmInvalidated` true, "The sensor's calibration is gone…" | 540–546, 602 | P (link down) |
| H29 | → stopped (error) | **link loss**: `_deviceLink.onImuConnectedChanged`, `imuConnected` false while step > 0 or done → `_hmInvalidated` (the transport path, in case it is the only signal that arrives) | 614–618 | P |
| H30 | done → stopped | H28 or H29 after success: `_hmStop` sets `calibrationDone` false and step 9. The connection is live while `active && (_running \|\| calibrationDone)`, so a restored done is also invalidated | 336–337, 597 | P; the Rig's clear stays on the device object (BRIEF-C-13b What 2) |
| H31 | → stopped | a refused call: `_deviceLink.onCalibrationCallRefused(status, call)` → `_hmRefused` (§2b) | 551–567, 599 | P (call result) |
| H32 | any → stopped (warn) | stop() → `on_Stopping`: `_hmDisarm`; if the device's `calibrationActive`, `_hmAbortIfActive`: `abortCalibration()` (log `abortCalibration[ at=verifying]`); at VERIFYING "Presence check declined… NOT undone", else "cancelled before the sensor applied anything"; `_stopNote` " aborted". Exactly once (stop is a no-op unless running) | 239–242, 308–321 | P (request: abort); words Q |
| H33 | → idle (unless done) | `on_Stopped`: step 0, awaiting false, failed false, `_animStage` "". The last message stays in `hmFailMsg` (the flow copies it, `ImuCalibrationFlow.qml:277–278`) | 243–250 | P |
| H34 | → done or step 0 | restore() → `on_Restoring`: disarm; step 5 and done if the device's `fullyCalibrated`, else step 0 and not done. A read of the device, never a replay | 257–262 | **NO HOME F6** |

`_hmStop` (326–339) is every stop's tail: disarm, step 9, awaiting false, kind and message set, `guide.cancel()`, `_animStage` "", `calibrationFailed` true, `calibrationDone` false, log `stopped kind=<kind>`.

### 2a. Every abort reason, by its code

On phase 8 ABORTED (H9), unless it is our own stop's CALLER echo. Kind and words from `_hmAbortText` 569–587; `tst_setup_calib_hackmotion.qml` `test_CH04_abortReasons` covers each row.

| Code | Library name | Kind | Message (verbatim) |
|---|---|---|---|
| 0 | WR_CAL_ABORT_NONE (and any code not below) | error | "The calibration routine stopped before it finished. Tap Recalibrate." |
| 1 | WR_CAL_ABORT_CALLER, while still running | error | "Calibration cancelled." |
| 1 | CALLER, after our own stop (step 9) | (kept) | the earlier message stands (H9; `test_CHr3_ownStopSurvivesAbortEcho`) |
| 1 | CALLER carried on COMPLETE | warn | "Presence check declined…" (H23) |
| 2 | WR_CAL_ABORT_RAISE_TOO_SLOW | warn | "That took too long between the two positions — the sensor needs one continuous raise. Tap Recalibrate and follow the guide." |
| 3 | WR_CAL_ABORT_STREAM_LOST | error | "The sensor's data stream stopped part-way through, so the raise could not be watched. Tap Recalibrate once data is flowing." |
| 4 | WR_CAL_ABORT_LINK_LOST | error | "The sensor's link dropped part-way through. Reconnect it, then run the routine again — a dropped link destroys any calibration." |
| 5 | WR_CAL_ABORT_NO_RESULT | error | "The sensor did not answer in time, so no calibration was applied. Tap Recalibrate." |

The routine's own stop reasons, which are not library aborts: not connected (H12a), not streaming (H12b), short raise (H15, warn), finished before the reference pose (H22), presence declined (H23, warn), presence not measured (H24), check did not confirm (H25), no check reported (H27), calibration gone (H28/H29), cancelled at VERIFYING or before applying (H32, warn). Each becomes one failure code of P; the kind goes with it; the words stay Q.

### 2b. Every refusal, by its status

`_hmRefused` (551–567), log `refused call=<call> status=<n>`; `test_CH03_refusals` covers each.

| Status | Name | Kind | Message (verbatim, `%1` = the call) |
|---|---|---|---|
| −14 | WR_ERR_NO_STREAM | error | "The sensor stopped streaming, so it cannot watch the raise (%1 was refused). Tap Recalibrate once data is flowing." |
| −18 | WR_ERR_BUSY | warn | "The sensor is busy retrieving swing data (%1 was refused) — try again in a moment." |
| −12 | WR_ERR_LINK_DOWN | error | "The sensor's link went down (%1 was refused)." |
| −2 | WR_ERR_INVALID_STATE | error | "The sensor was not in a state to accept %1. Tap Recalibrate to start the routine from the beginning." |
| other | n/a | error | "The sensor refused %1 (status %2)." |

### 2c. What it does on link loss

Three paths can carry one link drop: the library's own abort during a routine (code 4, §2a), the device object's invalidation (H28: phase forced to IDLE, state to UNCALIBRATED, presence surface emptied, `hm_instance.cpp:1056–1061`), and the transport's `imuConnected` (H29). H28 and H29 can both fire for one drop; `_hmStop` then runs twice (two `stopped` lines, two `guide.cancel()`; the message is the same). Whichever lands, the routine stops with step 9 and done false, nothing is resumed or restored, and a re-run needs a Recalibrate tap. An invalidation from a stream restart gets the same "a dropped link destroys it" words (EDGE, by reading). The Rig's `deviceHeld` record is cleared by the device object (C-6b.2), not by this routine.

## 3. Timers

All are `property Timer`, `running` ANDed with `active && _running` (R3, W1). `CalibDriver.qml:215–217` finds each by name and checks its interval.

| Timer, HA | Interval | Repeat | Runs while | On trigger | Goes |
|---|---|---|---|---|---|
| `_hmStartTimer` 633 | `hmStart` 1500 | no | step 0, `guide.ready` (a binding; `_hmResetState` leaves it to the binding, 290–292) | `_hmStartTimer.onTriggered` → `_hmBegin` (H11) | D; P |
| `_hmHorizontalSettleTimer` 664 | `settle` 2000 | no | `_hmHorizontalSettleArmed` 212 | `_hmHorizontalSettleTimer.onTriggered` → H13 | D; P |
| `_hmRaiseConfirmTimer` 683 | `raiseSettle` 500 | no | `_hmRaiseConfirmArmed` 213 | `_hmRaiseConfirmTimer.onTriggered` → H15/H16 | D; P |
| `_hmRefSettleTimer` 725 | `refSettle` 1500 | no | `_hmRefSettleArmed` 214 | `_hmRefSettleTimer.onTriggered` → H18 | D; P |
| `_hmPresenceWaitTimer` 745 | `presenceWait` 6000 | no | `_hmPresenceWaitArmed` 215 | `_hmPresenceWaitTimer.onTriggered` → H27 | D; P |

`_hmDisarm` 264–269 clears the four armed flags. The re-arm idiom (`= false; = true`, 390–391, 429–430, 737–738) restarts a one-shot. Goes: P's state; D's timers follow it.

## 4. Calls out

| Kind | Call, HA line | Goes |
|---|---|---|
| Device (`segments.device`, the `HmInstance`), the five `Q_INVOKABLE`s (`hm_instance.h:547–551`), each queued to the I/O thread, returning nothing | `beginCalibration` 377; `confirmHorizontal` 674; `confirmRaise` 717; `confirmReferencePose` 736; `abortCalibration` 313, 701 | D performs P's requests, the same calls in the same order (BRIEF-C-13b check 4) |
| Device signals | `calibrationCallRefused(status, call)` (`hm_instance.h:579`), `calibrationInvalidated` (586), `calibrationStateChanged`, the transport's `imuConnectedChanged`; the phase through `calibrationPhase` | D taps them and feeds P |
| Device, read | `imuConnected` 353, 615; `streaming` 358; `calibrationPhase` 148, 311, 493; `calibrationActive` 310; `calibrationAbortReason` 448, 454–455, 505; `fullyCalibrated` 260, 482; `presenceNotMeasured` 518; `presenceSamplesUsed` 529 | D; P's inputs |
| Unit (`segments.lowerArm`, an `HmUnit`), read | `quatW/X/Y/Z` 274 (the travel gate). `segments.palm` is handed in and never read | D; as a P input **F19** |
| Guide | `guide.ready` 639; `guide.pose` 230, 371, 407, 414, 425, 658; `guide.animate` 409, 427; `guide.cancel` 334 (and the base's); `guide.hmUpperArm`, `hmForePose0`, `hmForePose1`, `hmRaiseDeg`; finished through `on_GuideDone` 644 | **NO HOME F2** |
| Log | `_log` 312, 338, 376, 449, 552, 673, 700, 715, 735; the base's stage line; `_stopNote` " aborted" 241 | **F3** |
| Navigation | none. `completed()` 219 on every done edge, a restore included | D exposes it; host Q |
| Words | `qsTr` 315–319, 354–361, 499–501, 506–508, 519–522, 530–532, 542–545, 554–566, 573–586, 702–708, 754–756 | Q, verbatim, chosen by code |

## 5. Paths that set or clear a calibration

- **Set:** none host-side. The device applies its transform at `0x94` (APPLYING) for every `a2 01`, rejected attempts included, and only a passed presence check makes its state CALIBRATED. Until C-13b, C-6b.2 commits the `deviceHeld` record from `HmInstance` on "calibrated"; C-13b moves the commit to the driver on P's success (H20), one per success.
- **Clear:** none host-side ("clearCalibration() / setNominalCalibration() do not exist on it", 222–225). `_hmAbortIfActive` and the travel gate's abort write nothing: before `0x94` nothing has been applied; at VERIFYING the transform is applied and stays, unverified (no `deviceHeld` record, since the state is not CALIBRATED). The calibration dies with the link (H28–H30), on the device object, which also clears the Rig's record (C-6b.2; it stays there after C-13b).
- Nothing about it is persisted, ever (29–31).

## 6. What a host, another routine or a test reads

- **Inputs**, set by `ImuCalibrationFlow.qml:344–352`: `active`, `guide`, `segments` ({ device, lowerArm, palm }), `pace`. `rightHanded` is not bound and not read. `leadDevice` 70 and `leadImu` 71 resolve them. Goes: D; the host's bindings Q.
- **Outputs, the host contract:** `stage`, `failed`, `message`, `messageKind`, `done`, `completed`. The flow also reads `hmStep` (`ImuCalibrationFlow.qml:383–389`, `_showRecalibrate`) and copies `message`/`messageKind` on unload. Goes: D, same names.
- **Internals read by `ArmCalibrationStatus.qml`:** `hmStep`, `hmPhase`, `hmAwaitingPresence` 139, `hmFailMsg`, `hmFailKind`, `_hmRaiseTravelDeg` 205, `_hmMinRaiseTravelDeg`, `calibrationFailed`, and the constants `calpMarkingPose0`, `calpMarkingPose1`, `calUncalibrated`, `calCalibrated`, `calLost`. Goes: driver properties under the same names (BRIEF-C-13b What 4); the constants **F20**.
- **Read by tests** (`CalibDriver.qml:215–217, 587–588`; `tst_setup_calib_hackmotion.qml`, through `drv.d()` too; `tst_setup_guide_render.qml:162`): the 5 timers by name, `hmStep`, `hmInvalidated`, `_animStage` 155, `_p`, `stage`, `message`, `messageKind`, `calibrationDone`, and the change signals of `hmStep`, `_animStage`, `calibrationDone`, `calibrationFailed`.
- **Read outside this file only by a test:** `hmInvalidated` 144 (`test_CH06_linkDrop`, `tst_setup_calib_hackmotion.qml:323`). **Read by no other file** (each name grepped across `src`): every `calp*`, `abort*` and `err*` constant except the five the status column reads, and `_hmRaiseStartQuat`. `calUnknown` and `abortNone` are not read even here: the default branches cover them.
- **State that goes to P:** `hmStep`, `hmAwaitingPresence`, `hmFailMsg` (as a code), `hmFailKind`, `hmInvalidated`, `_hmRaiseStartQuat` 204, `_hmRaiseTravelDeg`, the four armed flags, `calibrationFailed`. `_animStage` is P's substage; its guide half **F2**. `hmPhase` and `onHmPhaseChanged` are D's tap of P's phase input.
- **Functions:** `_hmDisarm`, `_hmResetState`, `_hmAbortIfActive`, `_hmStop` (its `guide.cancel` **F2**), `_hmBegin`, `_hmOnPhase`, `_hmEvaluate`, `_hmInvalidated`, `_hmRefused`, `_hmQuatAngleDeg` go to P; `_hmUnitQuat` to D (**F19**); `_hmAbortText` is Q words chosen by code. `onCalibrationDoneChanged` 219: D.
- **Connections** `_deviceLink` (595–619), target the peripheral: `_deviceLink.onCalibrationCallRefused`, `_deviceLink.onCalibrationInvalidated`, `_deviceLink.onCalibrationStateChanged`, `_deviceLink.onImuConnectedChanged`. Goes: D, as P's inputs.

## 7. Tests that cover it

- **`qml_ui.tst_setup_calib_hackmotion`: 19 cases (26 functions with the `_data` rows).** Re-pointed in C-13b, never deleted: `test_CH01_happyPath`, `test_CH02_shortRaiseAborts`, `test_CH03_refusals` (5 statuses), `test_CH04_abortReasons` (codes 0–5), `test_CH05_presenceMissing` (not measured; silent), `test_CH06_linkDrop` (steps 1–4 and done), `test_CH07_staleCompleteIgnored`, `test_CH08_leaveAtEachStep`, `test_CH09_twoHostsMarkersOnce`, `test_CH10_backFromCheckShowsComplete`, `test_CH11_notReadyAtBegin`, `test_L05_wizardSilentAfterStart`, `test_CH12_noRepeatedTing`, `test_CHr1_fullRun`, `test_CHr2_stopAtEachStep`, `test_CHr3_ownStopSurvivesAbortEcho`, `test_CH13_toolbarPopupCloseIsLeave`, `test_CH14_absentWg3StillHackMotion`, `test_CH15_panelFollowsDeviceCalibration`.
- `qml_ui.tst_setup_smoke` (`test_4_hackmotion_chain`) and `qml_ui.tst_setup_guide_render` run it. The fakes: `FakeHmDevice.qml`, `FakeHmUnit.qml`.
- `session_setup_lint_test`: W1, W5, W7.
- No C++ test drives the device's calibration surface (`HmInstance`'s phases, invalidation); `hm_frame_test` and `hm_conjugate_test` cover the HackMotion frame maths, not the routine.
- C-9c's golden trace is to reach every id in §2, every code in §2a and §2b.

## 8. Where each duty goes

In the last column of every table above.

## 9. The recorder

Not this file's (no call). The arm page's `armRoutine` step brackets this routine when trunk sensors are in the session (C-10c).

## 10. The hands-free rules, as lines

- **Start: on page entry, by timer.** `begin()` on page entry (`CalibrateArmPage.qml:80`); `beginCalibration` 1.5 s after `guide.ready` if the device is connected and streaming (H11–H12). No on-and-still test. **F14**.
- **No stillness gate anywhere.** The pose-0 and reference-pose "hold still" waits are timed (`settle` 2000, `refSettle` 1500). The device measures the hold's spread itself (`poseSpreadMaxDeg`), and the presence check reads it; the routine does not. **F14**.
- **The guide paces the raise, and the chain moves only on the guide's finished signal** (H14, H17). A stalled guide gives a legible RAISE_TOO_SLOW, never a marker fired at an arm that did not move.
- **On screen** each step says what to do; a stop says why, by code (§2a, §2b). No automatic retry: a Recalibrate tap (**F14**).
- No step faces the golfer away from the screen.
- **Must survive** (brief What 5):
  - *One arm instrument per wearing, never both.* This routine runs only when the slot-A holder is a HackMotion; its segments are one device's two units. A HackMotion on the wrist with Witmotions on the trunk is allowed. "One claim per role" and "first match" are the rule.
  - *The device maths is called, never re-derived.* The device solves; the host solves nothing. The travel gate is an independent measurement of the stream and must not be replaced by a figure from the `0x94` payload; the presence angle is state, never a score; `calibrationDone` comes from `fullyCalibrated` only; the library's 6 s raise limit is not raised.

## Grep hits that are not duties

| HA line | Text |
|---|---|
| 154 | `the guide's finished() signal and nowhere else.` (a comment) |
| 221 | `── Entry points (CalibrationRoutineBase: begin, stop, reset, restore) ────` (a section comment) |
| 395 | `the signal to start the raise — not a timer of ours — so the` (a comment) |
| 612 | `path exists so the routine still regresses if the transport signal is the` (a comment) |
