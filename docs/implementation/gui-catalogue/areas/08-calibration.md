# Calibration

*Directory:* `src/Gui/calibration/`  
*6 components · 2,772 lines of QML*

IMU arm calibration routines (HackMotion, Witmotion), camera-calibration stub and the calibration guide, hosted both by the setup flow and the Wrist toolbar panel.

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [ArmCalibrationStatus](#armcalibrationstatus) | 579 | `Item` | 1 | 2 | 13 local comp. |
| [CalibrationGuide](#calibrationguide) | 143 | `QtObject` | 1 | 0 |  |
| [CameraCalibrationFlow](#cameracalibrationflow) | 102 | `Item` | 2 | 2 |  |
| [HackMotionArmRoutine](#hackmotionarmroutine) | 802 | `QtObject` | 1 | 0 |  |
| [ImuCalibrationFlow](#imucalibrationflow) | 538 | `Item` | 2 | 6 |  |
| [WitmotionArmRoutine](#witmotionarmroutine) | 608 | `QtObject` | 1 | 0 |  |

### ArmCalibrationStatus
`src/Gui/calibration/ArmCalibrationStatus.qml` · 579 lines · root `Item`

> The arm-calibration status column (design §4.7) — PRESENTATIONAL ONLY. It reads the
> routine's outputs and the host's resolved lead unit/device, and says what they mean.
> It calls nothing and changes nothing; Recalibrate is a signal the host acts on.
> mode "full"    — the wizard: eyebrow, title, the status, a Recalibrate button
> mode "compact" — the toolbar panel: the status only (the host pins its own buttons)
> Moved out of ImuCalibrationFlow.qml with every string, colour and layout unchanged.
> The `d` object below mirrors the names the sub-components always read, with the
> values a not-yet-loaded routine would show.

- **Properties:** `string mode`, `var routine`, `bool isHackMotion`, `QtObject leadImu`, `QtObject leadDevice`, `bool calibrationDone`, `bool showHeader`, `string stepLabel`
- **Signals:** `recalibrate()`
- **Local components:** `StatusBadge`, `PhaseText`, `ProgressBar`, `StatusLabel`, `AngleWarning`, `NoImuWarning`, `MountFailText`, `HmPhaseText`, `HmStepBar`, `HmStatusLabel`, `HmReadout`, `HmReadouts`, `HmFailText`
- **Uses:** `PpButton`, `PpDisplayText`
- **Used by (1):** `ImuCalibrationFlow`

### CalibrationGuide
`src/Gui/calibration/CalibrationGuide.qml` · 143 lines · root `QtObject` · no direct `Theme.*` refs

> CalibrationGuide — the 3-D guide as the calibration routines see it (design §4.7, R9),
> and the real adapter over ONE BodyVizView.
> ── THE INTERFACE (what a routine may use, and all it may use) ─────────────────────
> readonly property bool ready            the guide can be watched (bvv.fullyLoaded)
> function pose(armQ, foreQ)              show a pose, no motion (foreQ optional)
> function animate(armQ, foreQ, ms)       move to a pose over ms; finished() at the end
> function cancel()                       stop any motion; NO finished()
> signal finished()
> hmUpperArm, hmForePose0, hmForePose1, hmRaiseDeg   — the HackMotion poses
> property bool useGuideCamera
> The test stand-in (tests/setup/fakes/FakeGuide.qml) has the same members. A routine
> keeps its OWN stage ("introUp", "raise", …): the guide reports only "finished".
> ── THE ADAPTER ────────────────────────────────────────────────────────────────────
> The view binds to this object, never the other way round:
> leadArmOverrideRotation:     guide.armTarget
> leadForeArmOverrideRotation: guide.foreTarget   (identity for a Witmotion — the host)
> …

- **Properties:** `Item view`, `bool active`, `readonly bool ready`, `bool useGuideCamera`, `quaternion armTarget`, `quaternion foreTarget`, `bool animating`, `readonly quaternion hmUpperArm`, `readonly quaternion hmForePose0`, `readonly quaternion hmForePose1`, `readonly real hmRaiseDeg`
- **Signals:** `finished()`
- **Functions:** `pose(armQ, foreQ)`, `animate(armQ, foreQ, ms)`, `cancel()`
- **Used by (1):** `ImuCalibrationFlow`

### CameraCalibrationFlow
`src/Gui/calibration/CameraCalibrationFlow.qml` · 102 lines · root `Item`

> Compact in-panel STUB for camera (stereo / ChArUco) calibration. The real
> pipeline and its QML-exposed validity property don't exist yet, so this is a
> friendly placeholder with a disabled Start — the slot the real flow drops into
> later. Mirrors ImuCalibrationFlow's public shape (layoutMode / completed() /
> cancelled()) so PpCameraPanel hosts it the same way. It NEVER navigates.

- **Properties:** `string layoutMode`, `bool showHeader`
- **Signals:** `completed()`, `cancelled()`
- **Functions:** `begin()`, `reset()`, `showCompleted()`
- **Uses:** `PpButton`, `PpDisplayText`
- **Used by (2):** `PpCameraPanel`, `TriangulatePage`

### HackMotionArmRoutine
`src/Gui/calibration/HackMotionArmRoutine.qml` · 802 lines · root `QtObject` · no direct `Theme.*` refs

> The HackMotion (wG3) arm calibration — the DEVICE's routine, driven through libwrist
> (design §4.7). Moved out of ImuCalibrationFlow.qml with its choreography, guards and
> strings unchanged.
> Forearm horizontal → one continuous ~30° raise across the chest → the device applies
> its own transform → reference pose → presence check. The order is fixed and enforced
> BY THE LIBRARY; we issue markers and read state. There is nothing to solve host-side,
> nothing to store, and no mount check to run — so the refinement, the arm-down/T-pose
> captures and the AngleWarning belong to the Witmotion routine ONLY.
> ⚠ Nothing about the HackMotion calibration is persisted, ever: a plain BLE disconnect
> destroys it (measured 0.70° → 18.80° at the same pose, strap untouched), the library
> makes resume un-expressible, and this UI matches that rather than papering over it.
> NON-VISUAL. Inputs are injected: the guide (CalibrationGuide, R9), the segments
> { device, lowerArm, palm } and the pace.
> ⚠ UNIT vs DEVICE. `segments.lowerArm` / `palm` are the VIZ objects (HmUnit) — they
> carry the quaternions; `segments.device` is the PERIPHERAL (HmInstance) — every
> wr_calibration_* call, imuConnected, streaming and the calibration state live there.
> …

- **Properties:** `bool active`, `var guide`, `var segments`, `readonly var defaultPace`, `var pace`, `readonly QtObject leadDevice`, `readonly QtObject leadImu`, `readonly string stage`, `readonly bool done`, `readonly bool failed`, `readonly bool mountFailed`, `readonly int phase`, `readonly string message`, `readonly string messageKind` … (+32 more)
- **Signals:** `completed()`
- **Functions:** `begin()`, `stop(reason)`, `reset()`, `restore()`
- **Used by (1):** `ImuCalibrationFlow`
- **Exercised by tests/tools:** `CalibDriver`

### ImuCalibrationFlow
`src/Gui/calibration/ImuCalibrationFlow.qml` · 538 lines · root `Item`

> Wrist/arm positional calibration — the host-facing composition. The SAME flow runs in
> two hosts:
> • the start-session wizard  (layoutMode "full")
> • the Wrist toolbar IMU panel (layoutMode "compact")
> Since Stage 2 of the session-setup refactor (session_wizard_refactor_design.md §4.7)
> this file holds no state machine. It composes:
> • WitmotionArmRoutine / HackMotionArmRoutine — the two state machines, non-visual;
> • CalibrationGuide                            — the 3-D guide the routines drive;
> • ArmCalibrationStatus                        — the status column;
> and keeps the public API the hosts already use (layoutMode, showHeader, stepLabel,
> calibrationDone, calibrationFailed, mountFailed, phase, leadImu, isHackMotion,
> begin(), reset(), showCompleted(), completed(), cancelled()) plus `active`.
> ── TWO ROUTINES, PICKED AT THE TOP ────────────────────────────────────────────
> Which routine runs is decided from the DEVICE holding the leadForearm role (slot A):
> a Witmotion runs OUR routine, a HackMotion the DEVICE's. Exactly one exists at a time,
> in a Loader. Neither routine's state means anything for the other, so a vendor change
> …

- **Properties:** `string layoutMode`, `bool showHeader`, `string stepLabel`, `bool active`, `var pace`, `readonly bool calibrationDone`, `readonly bool calibrationFailed`, `readonly bool mountFailed`, `readonly int phase`, `readonly QtObject leadImu`, `readonly bool isHackMotion`
- **Signals:** `completed()`, `cancelled()`
- **Functions:** `begin()`, `reset()`, `showCompleted()`
- **Uses:** `ArmCalibrationStatus`, `BodyVizView`, `CalibrationGuide`, `HackMotionArmRoutine`, `PpButton`, `WitmotionArmRoutine`
- **Used by (2):** `CalibrateArmPage`, `PpImuPanel`
- **Exercised by tests/tools:** `CalibDriver`, `tst_setup_smoke`

### WitmotionArmRoutine
`src/Gui/calibration/WitmotionArmRoutine.qml` · 608 lines · root `QtObject` · no direct `Theme.*` refs

> The Witmotion arm calibration — OUR routine (design §4.7). Moved out of
> ImuCalibrationFlow.qml with its maths, holds, gates and strings unchanged.
> Arm-down + T-pose captures, stillness-gated, then the abduction refinement and the
> mount-validation gate. It calls ImuInstance-only methods (clearCalibration,
> setNominalCalibration, refineMountAboutLongAxis, calibArmDown, mountDeviationDeg,
> calibrationAngleValid), none of which exist on a HackMotion — which is why the host
> picks this routine or HackMotionArmRoutine by vendor and never mixes them.
> NON-VISUAL. Inputs are injected: the guide (CalibrationGuide, R9), the segments
> { a, b, c } (the forearm, hand and upper-arm UNITS — for a Witmotion each unit is its
> own device), and the pace. ⚠ R3: every Timer.running and Connections.enabled below is
> ANDed with `active` — never `visible`, never a step index. A routine runs only while
> `active && <begin() called and stop() not>`; active going false is stop("inactive").
> Every stage change and every entry point writes ONE line to the app log (R10).

- **Properties:** `bool active`, `var guide`, `var segments`, `bool rightHanded`, `readonly var defaultPace`, `var pace`, `readonly string stage`, `readonly bool done`, `readonly bool failed`, `readonly int phase`, `readonly string message`, `readonly string messageKind`, `readonly real captureHoldMs`, `int calibPhase` … (+16 more)
- **Signals:** `completed()`
- **Functions:** `begin()`, `stop(reason)`, `reset()`, `restore()`
- **Used by (1):** `ImuCalibrationFlow`
- **Exercised by tests/tools:** `CalibDriver`
