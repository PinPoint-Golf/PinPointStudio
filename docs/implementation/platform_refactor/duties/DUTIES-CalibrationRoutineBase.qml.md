Taken at `c8d3eb9d` (main), `src/Gui/calibration/CalibrationRoutineBase.qml`, 188 lines. C-10a; read, not run.

# Duties of `CalibrationRoutineBase.qml`

`RB` = `src/Gui/calibration/CalibrationRoutineBase.qml`. It is the root type of `TrunkRoutine.qml`, `WitmotionArmRoutine.qml` and `HackMotionArmRoutine.qml`.

The destinations are the trunk page's: P (`TrunkProcedure`), D (the trunk driver), Q (stays in QML), S (the Session's door), X (goes away), and NO HOME Fn (`NOTES-C-10a.md`).

The base is shared, so each line also says what it is for the arm routines. The design places none of those lines, and BRIEF-C-10b covers other files, so this page is the base's only one: they are marked **F13**. BRIEF-C-11b.2 What 2 keeps the base's host contract working, with the same names, while the trunk's logic moves. So the contract itself stays in QML, and an entry point forwards to the driver. That placement is BRIEF-C-11b.2's, not the design's: §4.8 names no event for begin, stop, reset, restore or inactive (**F12**). What becomes of the base, and of its arm-side duties, after C-13b is **F13**.

## 1. Thresholds and constants

| Symbol, RB line | Value | Meaning | Goes |
|---|---|---|---|
| `leadArmDownQuat` 87 | `Qt.quaternion(0.7071, 0.7071, 0, 0)` | The arm-down rest pose of the 3-D guide. The arm routines only; the trunk never reads it | **F13**. A view pose, not a threshold |
| `defaultPace` 91 | `{ scale: 1 }` | The routine's table of durations; each routine binds its own | The trunk's values: see the trunk page §1 (P / D / F2 / F8) |
| `pace` 92 | = `defaultPace` | The test override. It may set any key (lint W7: only under `tests/`) | **NO HOME F8** |
| `_p` 93–98 | n/a | `defaultPace` merged with `pace`, key by key | **F8** |
| `_ms` 99 | n/a | `round(_p[name] × _p.scale)`, ms | **F8** for the scale. Each duration it reads goes as the trunk page §1 says |

There are no stillness, tilt or angle thresholds and no retry limit in the base.

## 2. States and transitions

| Duty | RB lines | Trunk goes | Arm routines |
|---|---|---|---|
| `stage` "idle" at creation; `substage` ""; the routine binds both | 102–103 | D exposes P's stage and substage under these names (§4.8) | **F13** |
| `_running`: true from begin() to stop() or restore(). It outlives done and the failures | 116, 145, 154, 163, 171 | P (run in hand); D gates its timers on it | **F13** |
| `begin()` | 142–147 | Q forwards to D → P (a fresh run), per BRIEF-C-11b.2 (**F12**) | **F13** |
| `stop(reason)`: a no-op unless running; `_stopping`, then `_running` false, the guide cancelled, `_stopped` | 149–158 | Q forwards to D → P, per BRIEF-C-11b.2 (**F12**) | **F13** |
| `reset()`: `_running` false, the guide cancelled, `_clearing(false, was)` | 160–166 | Q forwards to D → P, per BRIEF-C-11b.2 (**F12**; the clear **F5**) | **F13** |
| `restore()`: `_restoring`, then `_running` false | 168–173 | Q forwards; restore's run **F6** (**F12**) | **F13** |
| `onActiveChanged`: active false → `stop("inactive")` | 175 | D (stop on inactive), per BRIEF-C-11b.2 (**F12**); the binding of `active` is Q | **F13** |

## 3. Timers

None. The base holds one Connections, `_guideFinished` (183–187). `_guideFinished.onFinished` emits `_guideDone()`. It is enabled while `active && _running && _guideWatched`, and the tests find it by name (`tst_setup_trunk.qml:183`). For the trunk, "the guide finished" is an input of P (§4.8), but who taps the QML guide and feeds it is not said: **NO HOME F2**. Arm routines: **F13**.

## 4. Calls out

| Call, RB line | Goes |
|---|---|
| `appLog.info("Calib", logName + " " + text)` in `_log` 130. It is used by `begin` 143 ("begin"), `stop` 157 ("stop reason=… stage=…" + `_stopNote`), `reset` 161, `restore` 172 ("restore done=…" + `_restoreNote`) and `_logStage` 134–136 ("stage=… step=… why"). `onStageChanged` 139 writes the stage line when `logStageChanges` (the trunk sets it false and logs through `_go`) | Trunk: D per BRIEF-C-11b.1 check 2; the design's driver row does not name the log: **F3**. Arm: **F13** |
| `guide.cancel()` in stop 155 and reset 164 | Trunk: **F2**. Arm: **F13** |
| Sensors, recorder, speech, navigation | none in the base |

## 5. Paths that set or clear a calibration

None in the base. It only emits `_clearing` (144, 165), and each routine clears in its handler (the trunk: `on_Clearing` → `_clearSensors`, the trunk page §5).

## 6. What a host, another routine or a test reads

- **Inputs** `active` 78, `guide` 79, `segments` 80, `rightHanded` 81 and `pace` 92 are set by `CalibrateTrunkPage.qml:109–118`, by `ImuCalibrationFlow.qml` (the arm routines) and by each `tst_setup_*` host. They go: Q (the host's bindings), handed to D.
- **Outputs (the host contract, header 23–45):**
  - `stage`, `substage`
  - `calibrationDone` 104 and `done` 105 (= calibrationDone)
  - `failed` 106, `mountFailed` 107 (arm), `phase` 108 (Witmotion)
  - `message` 109, `messageKind` 110
  - `completed` (signal) 112
  - Readers: `CalibrateTrunkPage.qml`, `ImuCalibrationFlow.qml`, `ArmCalibrationStatus.qml`, and the tests (`CalibDriver.qml` reads `calibrationDone`, `mountFailed`, `phase`, `message`, `messageKind` and `stage`).
  - Trunk: D exposes the same names (§4.8); `message` is Q words by code.
- **Hooks (signals the base emits, routines handle):** `_clearing(forRun, wasRunning)` 119, `_begun` 120, `_stopping` 121, `_stopped` 122, `_restoring` 123, `_guideDone` 124. For the trunk, the handlers' work goes to P and D, as the trunk page §2 says, and the trunk stops handling them in C-11b.2. The base keeps emitting them while the arm routines use them (until C-12b and C-13b). What then becomes of them, and of the base, is **F13**.
- **Notes and switches:**
  - `_stopNote` 125 and `_restoreNote` 126: log suffixes (**F3**).
  - `logName` 129 ("trunk"): **F3**.
  - `logStageChanges` 133: **F3**.
  - `_logStage` 134: **F3**.
  - `_created` 137 and `Component.onCompleted` 138: the stage line is not written for the initial "idle" (**F3**).
  - `onStageChanged` 139: **F3**.
  - `_guideWatched` 182 (the trunk binds it to `_anim !== ""`): **F2**.

## 7. Tests that cover it

Every case in the three routine test files hosts the base, because each routine is a `CalibrationRoutineBase`. All of them are listed; none was picked out by its title.
- `qml_ui.tst_setup_trunk`: all 40 cases, named in `docs/implementation/platform_refactor/duties/DUTIES-TrunkRoutine.qml.md` §7.
- `qml_ui.tst_setup_calib_witmotion`, 20 cases: `test_0_harnessClock`, `test_CW01_happyPath`, `test_CW02_motionResetsHold`, `test_CW03_mountFailsOnHand`, `test_CW04_leadDisconnects`, `test_CW05_guideStallHoldsChain`, `test_CW06_backAtEachStage`, `test_CW07_backFromCheckShowsComplete`, `test_CW08_skipThenBackRunsFresh`, `test_CW09_skipAtEachStage`, `test_CW10_settingsRoundTrip`, `test_CW11_layoutSwitchFollowsGuide`, `test_CW12_slotAChangesVendor`, `test_CW13_noRepeatedTing`, `test_CWr1_fullRun`, `test_CWr2_stallHoldsIntroDown`, `test_CWr3_stopAtEachStage`, `test_CWr4_inactiveThenFreshBegin`, `test_CW14_toolbarPopupCloseIsLeave`, `test_CW15_panelFollowsMountLimits`.
- `qml_ui.tst_setup_calib_hackmotion`, 19 cases: `test_CH01_happyPath`, `test_CH02_shortRaiseAborts`, `test_CH03_refusals`, `test_CH04_abortReasons`, `test_CH05_presenceMissing`, `test_CH06_linkDrop`, `test_CH07_staleCompleteIgnored`, `test_CH08_leaveAtEachStep`, `test_CH09_twoHostsMarkersOnce`, `test_CH10_backFromCheckShowsComplete`, `test_CH11_notReadyAtBegin`, `test_L05_wizardSilentAfterStart`, `test_CH12_noRepeatedTing`, `test_CHr1_fullRun`, `test_CHr2_stopAtEachStep`, `test_CHr3_ownStopSurvivesAbortEcho`, `test_CH13_toolbarPopupCloseIsLeave`, `test_CH14_absentWg3StillHackMotion`, `test_CH15_panelFollowsDeviceCalibration`.
- `qml_ui.tst_setup_smoke`: `test_3_witmotion_chain` and `test_4_hackmotion_chain` run an arm routine end to end.
- `session_setup_lint_test`:
  - W1: `_guideFinished` is gated on `active`.
  - W7: `pace` and `scale` are only under `tests/`.

## 8. Where each duty goes

In the last column of every table, and in each bullet's "goes".

## 9. The recorder

The base does not touch it.

## 10. The hands-free rules, as lines

The base starts nothing by itself: `begin()` is the host's call. Each host makes it on entering its step: the trunk page at `CalibrateTrunkPage.qml:101–103`, and the arm page's `enter()` at `CalibrateArmPage.qml:76–80`, through `ImuCalibrationFlow.qml:127`. So a run's begin (its clear, its recording, its guide pose) happens on page entry today. Only each routine's capture waits for on-and-still (the trunk: `docs/implementation/platform_refactor/duties/DUTIES-TrunkRoutine.qml.md` §10, **F12**). The guide-finished gate (177–187) moves a chain on only when the guide has really finished drawing, never on a parallel clock. The "on and still" waits are each routine's: the trunk's are in `docs/implementation/platform_refactor/duties/DUTIES-TrunkRoutine.qml.md` §10.

## Grep hits that are not duties

| RB line | Text |
|---|---|
| 33 | `done                  = calibrationDone: the routine's word that it finished` (header comment) |
| 37 | `signal completed()    a run finished (each routine says when — see its file)` (header comment) |
