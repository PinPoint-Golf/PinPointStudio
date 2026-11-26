Taken at `1a083129` (main), `src/Gui/calibration/ArmCalibrationStatus.qml`, 488 lines. C-10b; read, not run.

# Duties of `ArmCalibrationStatus.qml`

`AS` = `src/Gui/calibration/ArmCalibrationStatus.qml`. Line numbers are at `1a083129`; later items match on the symbol. An `Item`, not a routine: the status column of the arm calibration, hosted by `ImuCalibrationFlow.qml` as `FlowStatus` (`ImuCalibrationFlow.qml:374–380`) in both layouts. It calls nothing and changes nothing; Recalibrate is a signal. Its drawing is `CalibrationStatusColumn` (C-10c) plus five inline parts.

**Where each duty goes:** **Q** stays in QML (a binding or a word chosen by code); **DW**/**DH** a property of the Witmotion or HackMotion driver that the binding reads (C-12b What 3, C-13b What 4); **PW**/**PH** the procedure that computes it; **C-15**, **X**, **NO HOME Fn** as on the routine pages (F1–F13 `NOTES-C-10a.md`, F14 on `NOTES-C-10b.md`).

## 1. Thresholds and constants

The file holds no threshold of its own. What it compares:

| Expression, AS line | Value | Meaning | Goes |
|---|---|---|---|
| `holdProgress` 128–134 | 0..1 | The Witmotion hold bar (below) | PW computes progress; DW exposes it; the bar binds Q |
| `_captureHoldMs` 70, default 1 | ms | The routine's hold, read to divide by; 1 when no Witmotion routine (no division by 0) | X once DW exposes progress |
| The travel tint 437–439 | `_hmRaiseTravelDeg >= _hmMinRaiseTravelDeg` (15°) | Good / warn colour of the "Forearm movement we saw" readout; NaN → muted | PH's verdict as a flag, DH exposes it; Q colour. Until then **F20** |
| `_hmDeg` 191 | `toFixed(2)` + "°", "-" for NaN or undefined | Readout format (state, never a score) | Q |
| `_hmStateText` 192–197 | `calCalibrated` → "Calibrated", `calUncalibrated` → "Uncalibrated", `calLost` → "Lost", else "Not checked" | The calibration-state readout's words, from the device's `calibrationState` | Q; the codes **F20** |
| The HackMotion enum defaults 83–87 | −1 when no HackMotion routine | So no live code compares equal by accident | **F20** |
| Fixed words with numbers: "About 15° before calibration, 0.4–0.8° after…" 424–426 | n/a | The relative-angle readout's explanation | Q, verbatim |

**The hold-progress arithmetic** (`holdProgress`, 128–134), Witmotion only (the HackMotion shows the step bar):
- `calibrationDone` → 1.0;
- `calibPhase` 2 → the routine's `phaseProgress` (= min(`stableAccumMs` / `_captureHoldMs`, 1), WA 336);
- `calibPhase` 1 and not `_armDownCaptured` → min(`phase1AccumMs` / `_captureHoldMs`, 1), **computed here**: the routine exposes no phase-1 progress;
- otherwise 0.
Goes: PW gives progress for both captures; DW exposes it; this binding reads it (BRIEF-C-12b What 3).

## 2. States and transitions

None of its own: every value is a binding over the routine's and the device's state. The derived view states (all Q, words chosen by code from DW/DH's state):

| Symbol, AS line | Rule | Goes |
|---|---|---|
| `_wt` 57, `_hmr` 58 | which routine is loaded, by duck typing (`calibPhase` / `hmStep` defined) | Q (or a driver kind); `_r` 56 is the routine |
| `instruction` 93, `_wtInstruction` 94–103, `_hmInstruction` 104–125 | Witmotion by `calibPhase` and `_armDownCaptured`; HackMotion by `hmStep` 0–5, else "stopped" | Q |
| `_wtCapturing` 138 | phase 1 with `phase1AccumMs` > 0 and not captured, or phase 2 | Q, from DW |
| `status` 140, `_wtStatus` 150–161, `_hmStatus` 162–177 | Witmotion: done / phase 0 / phase 1 (captured; lead not connected → "Waiting for sensor"; filling → "HOLD STILL — CAPTURING"; else "Hold still") / phase 2. HackMotion: done / step 9 / 0 / 1 (`hmPhase` MARKING_POSE0 → "Marking position 1") / 2 (MARKING_POSE1 → "Marking position 2") / 3 "APPLYING…" / 4 (`hmAwaitingPresence` → "CHECKING…") | Q |
| `statusBig` 141 | HackMotion steps 1–4; Witmotion `_wtCapturing` | Q |
| `statusTone` 142–149 | done good; HackMotion step 9 warn/error by `hmFailKind`, step ≥ 1 accent; Witmotion capturing accent | Q |
| `message` 182, `messageKind` 184 | HackMotion: `hmFailMsg` at step 9, with `hmFailKind`. Witmotion: `mountFailMsg` when `mountFailed` and not done, kind always "error" | Q |
| `showRecalibrate` 186–188 | `calibPhase` > 0, done, failed, `mountFailed`, or HackMotion step > 0. The flow keeps its own copy of the rule (`_showRecalibrate`, `ImuCalibrationFlow.qml:383–389`) | Q |
| `StatusBadge` 298–315: `_complete` 299, `_calibrating` 300–302, `_failed` 303 | Complete if done; Failed if failed or `mountFailed` and not done (failed takes precedence over calibrating, CW4); Calibrating if `calibPhase` ≥ 1 or HackMotion step 1–4; else Pending | Q |
| `AngleWarning` 324–337, `_show` 325 | Witmotion only: done, lead `calibrated` and not `calibrationAngleValid`. Never true in production: nothing calls `setCalibration` (**F17**) | Q as is; **F17** |
| `NoImuWarning` 341–349, `forHackMotion` 342 | `leadImu` null, shown under the badge (HackMotion) or under the status (Witmotion) | Q |
| `HmStepBar` 358–373 | HackMotion steps 1–5: four bars, done all good, earlier steps accent, current light | Q |
| `HmReadouts` 405–487 | HackMotion steps 4 and 5 only (the relative angle is meaningful only at rest) | Q |

## 3. Timers

None: `grep -n 'Timer\|Connections' src/Gui/calibration/ArmCalibrationStatus.qml` finds nothing. Every value is a binding; nothing in the file runs on a clock.

## 4. Calls out

None: no device call, no sensor call, no log, no speech, no recorder, no navigation. `signal recalibrate()` 49 is emitted by the full column's button (`onRecalibrate` 242); the flow answers it with `begin()` (`ImuCalibrationFlow.qml:422`). Theme and design-system components only: `CalibrationStatusColumn` (and its `BodyText`, `HoldBar`, `StatusLine`, `MessageText`), `PpStatusPill`. Words: every `qsTr` in 96–124, 151–175, 193–196, 216, 311–314, 336, 348, 413–485. Goes: Q, verbatim.

## 5. Paths that set or clear a calibration

None.

## 6. What a host, another routine or a test reads

- **Inputs, set by `FlowStatus`** (`ImuCalibrationFlow.qml:374–380`) and each layout: `mode` 40 ("full" | "compact"), `routine` 41, `isHackMotion` 42, `leadImu` 43, `leadDevice` 44, `calibrationDone` 45, `showHeader` 46, `stepLabel` 47. Goes: Q (host bindings); `routine` becomes the driver (C-12b/C-13b).
- **Outputs:** `recalibrate` 49 (the flow's `onRecalibrate: flow.begin()` in the full layout). Goes: Q; the flow forwards to D (**F12**).
- **Inline parts' own properties:** `label` 379 and `value` 380 of `HmReadout`; `forHackMotion` 342 of `NoImuWarning`. Q.
- **Tests** read what it draws, not its properties: `CalibDriver.qml` `textShown`, `badgeText` (613–627), in `tst_setup_calib_witmotion.qml` and `tst_setup_calib_hackmotion.qml` (messages, badge text, "Waiting for sensor", the readouts).

## 7. Tests that cover it

- `qml_ui.tst_setup_calib_witmotion` (all 20 cases host it through the flow; the message and badge checks in `test_CW03_mountFailsOnHand`, `test_CW04_leadDisconnects`, `test_CW15_panelFollowsMountLimits`, among others).
- `qml_ui.tst_setup_calib_hackmotion` (all 19 cases; `test_CH03_refusals`, `test_CH04_abortReasons` check `textShown` for every message).
- `qml_ui.tst_setup_rolesviz` and `qml_ui.tst_setup_smoke` host the flow, so draw it.
- `design_system_lint_test` reads it (D1–D5). `HmStepBar` draws four `Rectangle`s with no MouseArea (a bar, not a control).
- C-9b and C-9c freeze `holdProgress` at each stage of one set (BRIEF-C-9b What 5, C-9c What 5).

## 8. Where each duty goes

In the last column of every table above. In short: after C-12b and C-13b this file reads driver properties and keeps its words, tones and layout; the phase-1 hold arithmetic leaves for PW; the device readouts and the enum constants are **F20**.

## 9. The recorder

Not used.

## 10. The hands-free rules, as lines

- It names a waited-for sensor only as "Waiting for sensor" when the lead is not connected (156); it never says which sensor is moving (the Witmotion routine watches the lead alone). **F14**.
- It shows the Recalibrate button whenever a run has started, failed or finished: every retry is a tap (**F14**).
- The readouts are state, never a score: "do not re-run to chase it down" (483–485) stays verbatim.
- *One arm instrument per wearing:* the HackMotion and Witmotion halves never show together (`isHackMotion` gates each part).

## Reads of the routines and the devices (brief What 3)

Every property of a routine it reads by name.

Through `d._r` = `armStatus.routine` (56), guarded by `_wt` / `_hmr`:

| Routine property, AS line | Routine | Goes |
|---|---|---|
| `calibrationFailed` 64 | both | DW/DH |
| `calibPhase` 67 | Witmotion | DW |
| `phaseProgress` 68 | Witmotion | DW (progress) |
| `phase1AccumMs` 69 | Witmotion | DW (progress; the arithmetic X) |
| `_captureHoldMs` 70 (an underscore name) | Witmotion | X once progress is exposed |
| `_armDownCaptured` 71 (underscore) | Witmotion | DW |
| `mountFailed` 72 | Witmotion (the base's) | DW |
| `mountFailMsg` 73 | Witmotion | DW (a code; the words Q) |
| `hmStep` 76 | HackMotion | DH |
| `hmPhase` 77 | HackMotion | DH |
| `hmAwaitingPresence` 78 | HackMotion | DH |
| `hmFailMsg` 79, `hmFailKind` 80 | HackMotion | DH (a code; the words Q) |
| `_hmRaiseTravelDeg` 81 (underscore) | HackMotion | DH |
| `_hmMinRaiseTravelDeg` 82 (underscore) | HackMotion | **F20** (a threshold read by a view) |
| `calpMarkingPose0` 83, `calpMarkingPose1` 84, `calUncalibrated` 85, `calCalibrated` 86, `calLost` 87 | HackMotion (library enum values) | **F20** |

From the host, not the routine: `isHackMotion` 42/60, `leadImu` 43/61, `leadDevice` 44/62, `calibrationDone` 45/63 (the flow's, which for a HackMotion also needs the device connected and CALIBRATED).

**Read straight from a device**, not through the routine: the lead unit's `imuConnected` 156, `calibrated` and `calibrationAngleValid` 328; the HackMotion peripheral's `relativeAngleDeg` 414, `calibrationState` 444, `fullyCalibrated` 445, `presenceAngleDeg` 450, `poseSpreadMaxDeg` 454, `presenceNotMeasured` 465, 469, `presenceSamplesUsed` 466–467. Goes: **F20**.

## Grep hits that are not duties

| AS line | Text |
|---|---|
| 21 | `It calls nothing and changes nothing; Recalibrate is a signal the host acts on.` (header comment) |
