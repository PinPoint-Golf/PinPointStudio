Taken at `1a083129` (main), `src/Gui/calibration/WitmotionArmRoutine.qml`, 553 lines. C-10b; read, not run.

# Duties of `WitmotionArmRoutine.qml`

`WA` = `src/Gui/calibration/WitmotionArmRoutine.qml`. Line numbers are at `1a083129`; later items match on the symbol. The root type is `CalibrationRoutineBase` (`docs/implementation/platform_refactor/duties/DUTIES-CalibrationRoutineBase.qml.md`). The host is `ImuCalibrationFlow.qml` (C-10c's page), which loads this routine when the slot-A (leadForearm) holder is not a HackMotion.

**Where each duty goes** (heading 8: the last column of every table). The design is `docs/design/rig_design.md` §4.8, "The shape of a procedure":
- **P**: `WitmotionArmProcedure` (Rules, C-12a). Events in (a tick with its elapsed time, a sensor summary, the guide finished, the result of each request), stage, progress, reason codes and requests out, then a `sensorAlignment` record or a failure code. Every threshold and transition; no clock, no timer, no sentence.
- **D**: the Witmotion arm driver (Adapters, C-12b). Owns the timers and the sensor taps, performs P's requests on `ImuInstance`, calls the sink, exposes P's state under today's names.
- **Q**: stays in QML as a binding or a word (C-12b). Words verbatim, chosen by code.
- **C-15**: left for C-15's sweep. **X**: goes away, with the reason.
- **NO HOME Fn**: the design does not place it. F1–F13 are `NOTES-C-10a.md`'s; F14 on are `NOTES-C-10b.md`'s.

This routine speaks nothing (no `ttsController`, no `Spoken.*`), and touches no recorder: the arm page brackets the trunk ceremony itself (`CalibrateArmPage.qml:88–98`, C-10c). So F1 and F4 do not arise here.

## 1. Thresholds and constants

| Symbol, WA line | Value, unit | Meaning | Goes |
|---|---|---|---|
| `_stillThreshDps` 146 | 15.0 °/s | Still: the lead sensor's `angularVelocityDps` ≤ this. **The quaternion-difference rate, not `gyroDps`**, and the lead (A) only; B and C are never tested for stillness. No C++ mirror, no lint | P; the signal itself **F14** |
| `_captureHoldMs` 147 = `_ms("hold")` | 2000 ms | Unbroken stillness needed for each capture (arm-down, T-pose). `captureHoldMs` 75 is its public copy | P |
| `defaultPace` 44–55: `introStart` 3000, `introReady` 2000, `minHold` 2000, `hold` 2000, `captureTransition` 800, `raiseReady` 2000 | ms | Step timings: the wait after `guide.ready` before the intro; the settle after it; the 2 s before phase 1's still-watch; the capture hold; the pause after arm-down; the settle after the raise | P (time is an input) |
| `defaultPace` `tick` 100 | ms | The sampling interval of both hold timers, and the step each still tick adds to the accumulator | D (the timers' interval); the accumulation P |
| `defaultPace` `introAnim` 3000, `raiseAnim` 1500 | ms | Durations handed to the guide (the intro up and back, the raise) | **NO HOME F2** |
| `defaultPace` `scale` 1, and the base's `pace` override | × | Tests scale the clock and override keys (W7) | **NO HOME F8** |
| The slerp average's window | `hold` ÷ `tick` = 20 samples at app pace (consecutive still ticks of the lead; any moving tick empties it). Under `CalibDriver`'s pace (× 0.05, `tick` 400 → 20 ms, `hold` → 100 ms) it is 5 | The samples `_slerpAverage` means | P |
| `_slerpAverage` 179–185, `_quatSlerp` 156–175 | n/a | Iterative mean: acc = samples[0]; acc = slerp(acc, sᵢ, 1/(i+1)). `_quatSlerp`: flips b when dot < 0; when dot > 0.9995 a normalised lerp, else the sine formula. Empty → identity | P (moved verbatim; no `src/IMU` helper exists: `trunk_stand_tall.h` takes a sign-aligned arithmetic mean, a different formula). Its output feeds nothing: **F16** |
| `_phiFromAbduction` 118–128 | ° | φ from a segment's live `anatQuat`: w = min(1, \|q.w\|), s = √(1−w²); s < 1e-4 → 0; with sg = sign(q.w), mx = sg·q.x/s, mz = sg·q.z/s; φ = the smaller in magnitude of atan2(mx, mz) and atan2(−mx, −mz), in degrees | P (verbatim; reads one live sample, not the averaged T-pose: **F16**) |
| Mount gate 360 | `mountDeviationDeg` ≤ 15.0° AND `mountGravityErrorDeg` ≤ 25.0° | Per connected segment, after the refine. Literals mirror `ImuInstance::kMountDeviationMaxDeg` / `kMountGravityErrorMaxDeg` (`imu_instance.h:199–200`), held equal by lint W11; `fullyCalibrated()` (`imu_instance.h:205–214`) already composes the same two | P (the gate). Where the two constants live for a Rules-ring machine (they are in `src/Gui/imu/imu_instance.h`), and whether P re-tests or takes `fullyCalibrated`'s answer: **NO HOME F18**. W11's subject **F9** |
| `refineMountAboutLongAxis` branch | \|φ\| > 25° | Inside `ImuInstance` (`imu_instance.cpp:840–889`), not this file: see §5 | called, never re-derived; **F18** |
| `tPoseQuat` 92–94 | right-handed (0.9948, −0.0105, −0.0011, 0.1012); left-handed (0.9948, −0.0105, 0.0011, −0.1011) | The guide's T-pose (BodyPoseAdapter's seed), chosen by `rightHanded`. Also read by tests (`tst_setup_calib_witmotion.qml:301–303, 721`; `tst_setup_guide_render.qml:236`) | **NO HOME F2** |
| `leadArmDownQuat` (base, RB 87) | (0.7071, 0.7071, 0, 0) | The guide's rest pose | **F2**, **F13** |
| Boundaries and NaN | n/a | Still is `!(angularVelocityDps > 15)`, so exactly 15.0 °/s and a NaN reading both count as still (the trunk counts NaN as moving). The mount gate is `<=`, so exactly 15.0 / 25.0 pass and NaN fails. A capture completes at `accum >= _captureHoldMs` | P, as is (C-12a tests each side) |
| Retry limits | none | There is no automatic retry. A mount fail or a lost lead stays until the golfer taps Recalibrate (`ArmCalibrationStatus`'s button → the flow's `begin()`) | P, as is; the hands-free gap **F14** |

## 2. States and transitions

`stage` (58–70) is a binding, not a `_go`: done → `"done"`; `mountFailed` → `"mountFailed"`; `calibrationFailed` → `"failed"`; not `_running` → `"idle"`; `calibPhase` 0 → `"intro"`; `calibPhase` 1 → `"phase1"` until arm-down is captured, then `"raise"` while `_animStage` is "raise" or `_raiseReadyArmed`, else `"captured"`; `calibPhase` 2 → `"phase2"`. The base writes one log line per change (`stage=<s>`, RB 139) with no reason text. Other outputs: `failed` 71 = `calibrationFailed`; `phase` 72 = `calibPhase`; `message` 73 = `mountFailMsg` when `mountFailed`; `messageKind` 74 "error" when `mountFailed` or `calibrationFailed`.

The ids are for C-9b: every row is to be reached by one scripted input.

| Id | From → to | Trigger and guard | WA | Goes |
|---|---|---|---|---|
| W1 | any → idle | begin() or reset() → `on_Clearing` → `_reset`: both clears on A, B and C (each present segment), then `_resetState` | 216, 254–307 | P; the clear D (**F5**) |
| W2 | idle → intro | begin(): `_running` true, `calibPhase` 0. Nothing waits for the sensors; the base emits `_begun`, which this routine does not handle | RB 142–147 | P (**F12**, **F14**) |
| W3 | intro: wait → introUp | `_introStartTimer` after `introStart`, while `calibPhase` 0, `!_introStarted` and `guide.ready`: `_introStarted` true, `_animStage` "introUp", the guide posed at rest and animated to the T-pose over `introAnim`. Once per run (comment 389–392: a re-raised `guide.ready` must not replay it) | 393–408 | P; `guide.ready` and the guide calls **F2** |
| W4 | introUp → introDown | `on_GuideDone`, `_animStage` "introUp": the guide animated back to rest | 413–419 | P (input: guide finished); **F2** |
| W5 | introDown → settle | `on_GuideDone`, "introDown": `_animStage` "", `_introReadyArmed` | 420–422 | P |
| W6 | intro → phase1 | `_introReadyTimer` after `introReady`: guide held at rest, `calibPhase` 1, `_phase1MinHoldDone` false, `_phase1MinHoldArmed` | 430–445 | P |
| W7 | phase1: settle → watching | `_phase1MinHoldTimer` after `minHold`: `_phase1MinHoldDone` true | 447–454 | P |
| W8 | phase1 → phase1, hold emptied | `_phase1HoldTimer` tick, lead `angularVelocityDps` > 15: `_phase1Samples` [], `phase1AccumMs` 0 | 478–482 | P |
| W9 | phase1 → phase1, hold filling | a still tick: the lead's quaternion appended, `phase1AccumMs` += interval | 483–485 | P |
| W10 | phase1 → captured | `phase1AccumMs` ≥ `_captureHoldMs`: `calibArmDownQuat` = `_slerpAverage`; `_refA`/`_refB`/`_refC` = each present segment's **current** quaternion (`_curQuat`); `setNominalCalibration(ref, false)` on each **connected** one (handMount false for all three: coplanar straps, comment 488–494); `_armDownCaptured`, `_captureTransitionArmed` | 486–506 | P (request: set nominal ×N); D performs; the commit **F15** |
| W11 | captured → raise | `_captureTransitionTimer` after `captureTransition`: `_animStage` "raise", the guide posed at rest and animated to the T-pose over `raiseAnim` | 512–523 | P; **F2** |
| W12 | raise → settle | `on_GuideDone`, "raise": `_raiseReadyArmed`, `_animStage` "" | 423–425 | P |
| W13 | raise → phase2 | `_raiseReadyTimer` after `raiseReady`: guide held at the T-pose, `calibPhase` 2 | 526–536 | P |
| W14 | phase2 → phase2, hold emptied | `_stabilityHoldTimer` tick, lead `angularVelocityDps` > 15: `_phase2Samples` [], `stableAccumMs` 0, `phaseProgress` 0 | 327–332 | P |
| W15 | phase2 → phase2, hold filling | a still tick: appended, `stableAccumMs` += interval, `phaseProgress` = min(`stableAccumMs` / `_captureHoldMs`, 1) | 333–336 | P (progress) |
| W16 | phase2 → refine | `stableAccumMs` ≥ `_captureHoldMs`: `calibArmTPoseQuat` = `_slerpAverage`; for each segment connected **now** (`_connectedSegs`, A first) with a non-null ref: `refineMountAboutLongAxis(ref, _phiFromAbduction(seg), false)`, then the gate | 337–362 | P (request: refine ×N with φ); D performs (**F18**) |
| W17 | → done | every segment passes, and at least one was judged: `mountFailed` false, `calibrationDone` true → `completed()` (211). Log `phase2 captured mount=pass` | 364–367, 374 | P (record or failure); the commit **F15** |
| W18 | → mountFailed | any segment fails: `mountFailed`, `mountFailMsg` names the failing segments ("forearm", "hand", "upper arm"); done stays false. Log `phase2 captured mount=fail <names>`. With no connected segment the gate also fails, naming none (EDGE, by reading) | 368–374 | P (reason code with the segments); words Q |
| W19 | → failed | `_leadLink`: the lead's `imuConnected` goes false while `calibPhase` > 0 and not done: `calibrationFailed`, log `lead disconnected`. Both hold timers stop (`!calibrationFailed`, CW4); the four one-shots are not gated on it and still fire, but the stage binding keeps "failed". A link loss in phase 0, or of B or C at any time, is not watched | 542–552 | P (link down, lead only) |
| W20 | any → idle (unless done) | stop() → `on_Stopping` → `_disarm`; base: `_running` false, guide cancelled; `on_Stopped` → `_resetState` unless done. Nothing on the sensors is cleared | 220–221, 247–252 | P; the stop D (**F12**) |
| W21 | any → done | restore() → `on_Restoring`: if the lead's legacy `calibrated` is true, its `calibArmDown`/`calibArmTPose` are copied (nothing on the arm path sets that flag: **F17**); disarm; `calibPhase` 2, `stableAccumMs` full, `phaseProgress` 1, guide at rest, failures cleared, `calibrationDone` true whatever the sensors hold. The host calls it only when the draft's outcome is still valid (`CalibrateArmPage.qml:77`) | 226–245 | **NO HOME F6** |

## 3. Timers

All are `property Timer` with `running` ANDed with `active && _running` (R3, lint W1). `CalibDriver.qml:211–214` finds each by name and checks its interval against the pace.

| Timer, WA | Interval | Repeat | Runs while | On trigger | Goes |
|---|---|---|---|---|---|
| `_introStartTimer` 394 | `introStart` 3000 | no | phase 0, `!_introStarted`, `guide.ready` | `_introStartTimer.onTriggered` → W3 | D; P |
| `_introReadyTimer` 430 | `introReady` 2000 | no | `_introReadyArmed` | `_introReadyTimer.onTriggered` → W6 | D; P |
| `_phase1MinHoldTimer` 447 | `minHold` 2000 | no | `_phase1MinHoldArmed` | `_phase1MinHoldTimer.onTriggered` → W7 | D; P |
| `_phase1HoldTimer` 463 | `tick` 100 | yes | phase 1, `_phase1MinHoldDone`, `!_armDownCaptured`, `!calibrationFailed`, lead present | `_phase1HoldTimer.onTriggered` → W8–W10 | D; P |
| `_captureTransitionTimer` 512 | `captureTransition` 800 | no | `_captureTransitionArmed` | `_captureTransitionTimer.onTriggered` → W11 | D; P |
| `_raiseReadyTimer` 526 | `raiseReady` 2000 | no | `_raiseReadyArmed` | `_raiseReadyTimer.onTriggered` → W13 | D; P |
| `_stabilityHoldTimer` 312 | `tick` 100 | yes | phase 2, not done, not `mountFailed`, not `calibrationFailed`, lead present | `_stabilityHoldTimer.onTriggered` → W14–W18 | D; P |

The arming flags `_introReadyArmed` 204, `_phase1MinHoldArmed` 205, `_captureTransitionArmed` 206 and `_raiseReadyArmed` 207 stand in for `start()`; `_disarm` 247–252 clears all four. `_introStarted` 393 holds the intro off for the rest of the run. Goes: P's substage state; D's timers follow it.

## 4. Calls out

| Kind | Call, WA line | Goes |
|---|---|---|
| Sensor, set | `setNominalCalibration(_refX, false)` 499, 501, 503 (each connected segment, at arm-down); `refineMountAboutLongAxis(ref, φ, false)` 357 (each connected segment, at the T-pose). Never `setCalibration` or `setFunctionalCalibration` | D performs P's requests, in this order (BRIEF-C-12b check 4); the commit **F15** |
| Sensor, clear | `clearCalibration()` 276 then `clearFunctionalCalibration()` 277, on A, B and C, each guarded by the method existing (an `HmUnit` has neither) | D performs; who requests it **F5** |
| Sensor, read | lead `angularVelocityDps` 327, 478; `quatW/X/Y/Z` 116, 334, 484; `anatQuat` 119; `imuConnected` 111–113, 498–502, 547; `mountDeviationDeg`, `mountGravityErrorDeg` 360 (results of the calls just made); `calibrated`, `calibArmDown`, `calibArmTPose` 228–230 (restore) | D taps them and hands P the summary and the call results. The raw quaternion samples, `anatQuat` (for φ) and the two mount figures are events C-12a What 1 names; the design's sensor summary lists gyro, tilt, heading and link only (**F19**) |
| Guide | `guide.ready` 401; `guide.pose` 238, 293, 405, 418, 437, 520, 532; `guide.animate` 406, 419, 521; `guide.cancel` (base); finished through the base (`on_GuideDone` 413) | **NO HOME F2** |
| Log | the base's stage line (RB 139); `_log` 374 (`phase2 captured mount=…`), 549 (`lead disconnected`); the base's begin/stop/reset/restore lines | **F3** |
| Navigation | none. `completed()` 211 on every done edge, a restore included (the flow tells them apart with `_restoring`) | D exposes the signal; the host Q |
| Words | `qsTr` 350–352 (the segment names), 370 (the mount-fail sentence, with the names) | Q, verbatim, chosen by code |

## 5. Paths that set or clear a calibration

- **Clear.** `_reset` (254–280), on every begin() and reset() (`on_Clearing` 216): `clearCalibration` then `clearFunctionalCalibration`, on every present segment, connected or not. (The trunk clears in the other order.) begin() is the host's call on page entry (`CalibrateArmPage.qml:80`) and on Recalibrate. Goes: through `ImuInstance`, which clears the Rig's record from C-6b.2; who asks **F5**.
- **Set, arm-down.** `setNominalCalibration(refRaw, false)` per connected segment (498–503). In `ImuInstance` (`imu_instance.cpp:801–838`): M = `nominalArmMount()`, A = conj(ref·M), `mountDeviationDeg` 0, and the gravity check from the live accel sets `mountGravityErrorDeg`. `refRaw` is the segment's quaternion at the capture tick, not the averaged one (**F16**).
- **Set, T-pose: `refineMountAboutLongAxis`'s two branches** (`imu_instance.cpp:840–889`), per connected segment:
  - \|φ\| ≤ 25°: M = nominal·Ry(φ), A re-anchored on the same ref, `mountDeviationDeg` = \|φ\|. Passes the gate when \|φ\| ≤ 15° and the arm-down gravity error ≤ 25°.
  - \|φ\| > 25° ("precise refine REJECTED"): M = nominal, A re-anchored, `mountDeviationDeg` = \|φ\|. Since \|φ\| > 25 > 15, this branch always fails this routine's gate.
  - Both apply A and M and stamp the time. The routine's part is only φ and the call; the branch is the device object's (**F18**).
- **Not cleared:** a mount fail, a lost lead, stop() and a leave all leave the A/M already set on every segment (W10, W16). They stay until the next begin (**F15**).
- **restore()** writes nothing to a sensor.

## 6. What a host, another routine or a test reads

- **Inputs**, set by `ImuCalibrationFlow.qml:334–343`: `active`, `guide`, `segments` ({ a, b, c }: forearm, hand, upper arm, each a Witmotion `ImuInstance`; a role held by an `HmUnit` is handed as null), `rightHanded`, `pace` (all the base's). `leadImu` 104, `slotB` 105 and `slotC` 106 resolve them. `_connectedSegs` 108–115 and `_curQuat` 116 read them. Goes: D takes them; the host's bindings Q.
- **Outputs, the host contract:** `stage`, `failed`, `phase`, `message`, `messageKind`, `done`/`calibrationDone`, `mountFailed` (the base's), `completed`. The flow reads `failed`, `mountFailed`, `phase`, `done`, `message`, `messageKind` (`ImuCalibrationFlow.qml:90–93, 145, 277–278, 358`). Goes: D exposes the same names (§4.8); `message` becomes a reason code plus Q words.
- **Internals read by `ArmCalibrationStatus.qml`** (its page, "Reads of the routines and the devices"): `calibPhase` 81, `phaseProgress` 82, `phase1AccumMs` 154, `_captureHoldMs` 147, `_armDownCaptured` 194, `mountFailed`, `mountFailMsg` 134, `calibrationFailed` 200. Goes: driver properties under the same names (BRIEF-C-12b What 3).
- **Internals read by tests** (`CalibDriver.qml:587–592`, `tst_setup_calib_witmotion.qml`, `tst_setup_guide_render.qml`): the 7 timers by name, `_animStage` 191, `calibPhase`, `phase1AccumMs`, `stableAccumMs` 137, `_phase1MinHoldDone` 197, `phase`, `tPoseQuat`, `leadArmDownQuat`, `_p`, and the change signals of `calibPhase`, `_animStage`, `calibrationDone`, `calibrationFailed`, `mountFailed`. Goes: re-pointed at the driver in C-12b.
- **Read by no other file** (each name grepped across `src`): `captureHoldMs` 75 (the public copy), `calibArmDownQuat` 85, `calibArmTPoseQuat` 86 (only written; restore copies into them), `calibArmDownEuler` 88 and `calibArmTPoseEuler` 89 (only nulled, never set): **F16**. Also used by this file alone: `_refA`–`_refC`, `_phase1Samples`, `_phase2Samples`, `_introStarted`.
- **State that goes to P:** `calibPhase`, `phaseProgress`, `phase1AccumMs`, `stableAccumMs`, `_phase1Samples` 150, `_phase2Samples` 151, `_refA` 130, `_refB` 131, `_refC` 132, `_armDownCaptured`, `_phase1MinHoldDone`, `_introStarted`, the four armed flags, `calibrationFailed`, `mountFailMsg` (as a code with the segments). `_animStage` is P's substage, its guide half **F2**.
- **Functions:** `_disarm`, `_reset` (its clears D, **F5**), `_resetState` (its guide pose **F2**), `_connectedSegs`, `_curQuat`, `_phiFromAbduction`, `_quatSlerp` and `_slerpAverage` go to P (the reads to D). `onCalibrationDoneChanged` 211 → `completed()`: D.
- **Connections:** `_leadLink.onImuConnectedChanged` 545–551, enabled while `active`, `calibPhase` > 0 and not done (not ANDed with `_running`). Goes: D, as P's link-down input.

## 7. Tests that cover it

- **`qml_ui.tst_setup_calib_witmotion`: 20 cases (28 functions with the `_data` rows).** Re-pointed in C-12b, never deleted: `test_0_harnessClock`, `test_CW01_happyPath`, `test_CW02_motionResetsHold`, `test_CW03_mountFailsOnHand`, `test_CW04_leadDisconnects`, `test_CW05_guideStallHoldsChain`, `test_CW06_backAtEachStage`, `test_CW07_backFromCheckShowsComplete`, `test_CW08_skipThenBackRunsFresh`, `test_CW09_skipAtEachStage`, `test_CW10_settingsRoundTrip`, `test_CW11_layoutSwitchFollowsGuide`, `test_CW12_slotAChangesVendor`, `test_CW13_noRepeatedTing`, `test_CWr1_fullRun`, `test_CWr2_stallHoldsIntroDown`, `test_CWr3_stopAtEachStage`, `test_CWr4_inactiveThenFreshBegin`, `test_CW14_toolbarPopupCloseIsLeave`, `test_CW15_panelFollowsMountLimits`.
- `qml_ui.tst_setup_smoke` (`test_3_witmotion_chain`: `setNominalCalibration` on A and B, 277–280) and `qml_ui.tst_setup_guide_render` (the guide's targets per stage, 229–237) run the routine. `qml_ui.tst_setup_rolesviz` hosts the flow.
- The fake: `FakeImuInstance.qml`. Its `refineMountAboutLongAxis` returns a scripted `mountResult` and models neither branch; its `setNominalCalibration` never sets `mountGravityErrorDeg` (**F18**).
- `session_setup_lint_test`: W1 (every timer and the connection gated on `active`), W5 (the members used are public), W7 (`pace` only under `tests/`), W11 (the two mount-gate literals equal `ImuInstance`'s constants).
- The maths it calls, not this file: `imu_calibration_test` (`nominalArmMount`, the A·q·M solve). No C++ test calls `setNominalCalibration` or `refineMountAboutLongAxis` (a grep of the two names under `src/*/tests` and `src/Gui/tests` finds only `FakeImuInstance.qml`, `tst_setup_calib_witmotion.qml` and `tst_setup_smoke.qml`).
- C-9b's golden trace is to reach every id in §2.

## 8. Where each duty goes

In the last column of every table above (the key is at the top).

## 9. The recorder

Not this file's: it makes no recorder call. The arm page records the trunk sensors through this routine as step `armRoutine` (`CalibrateArmPage.qml:88–98`; C-10c).

## 10. The hands-free rules, as lines

- **Start: on page entry, not on on-and-still.** The arm page calls `begin()` on entry when the arm outcome is not done (`CalibrateArmPage.qml:76–80`). That clears every segment (W1), and the intro starts `introStart` after `guide.ready` (W3) whatever the sensors read. There is no "on" (worn, upright) test at all. **F14**, with 10a's **F12**.
- **Captures wait for still, on the lead only, from the quaternion-difference rate.** Phase 1 waits `minHold`, then needs `hold` unbroken with the lead's `angularVelocityDps` ≤ 15 °/s (463–506); phase 2 the same (312–377). STANDING 7 names the device gyro (`gyroDps`, 250 ms peak) and forbids this signal. **F14**.
- **On screen:** "Waiting for sensor" only when the lead is not connected (`ArmCalibrationStatus.qml:156`); "Hold still" / "HOLD STILL — CAPTURING" otherwise. It never names a sensor that moves. No wait line is logged.
- **No automatic retry:** a mount fail (W18) or a lost lead (W19) waits for a Recalibrate tap. **F14**.
- No step faces the golfer away from the screen; the guide teaches the T-pose (the intro, W3–W5) before phase 1.
- **Must survive** (brief What 5):
  - *One arm instrument per wearing, never both.* This routine runs only when the slot-A holder is not a HackMotion (`ImuCalibrationFlow.qml:218–221, 296–304`); `_segments` hands it no `HmUnit` (`ImuCalibrationFlow.qml:244–254`); `_reset` clears only what has the methods (269–277). "One claim per role" and "first match" are the rule, not a defect.
  - *The device maths is called, never re-derived.* `setNominalCalibration` and `refineMountAboutLongAxis` (and through them `imu_calibration::nominalArmMount`, gravity-pinned) are called with the same arguments in the same order. The QML maths that is not in `src/IMU` (the slerp average, φ) moves verbatim; the mount gate keeps reading `ImuInstance`'s two constants.

## Grep hits that are not duties

| WA line | Text |
|---|---|
| 213 | `── Entry points (CalibrationRoutineBase: begin, stop, reset, restore) ────` (a section comment) |
| 540 | `has no imuConnected at all, and Connections warns when it cannot find a signal on` (a comment) |
