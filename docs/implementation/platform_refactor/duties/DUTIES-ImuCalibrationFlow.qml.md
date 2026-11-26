Taken at `b3e656cf` (main). The files have 502, 236, 206, 82 and 101 lines (`src/Gui/calibration/{ImuCalibrationFlow,CalibrationStatusColumn,CalibrationGuide,CalibrationGuideView,CameraCalibrationFlow}.qml`), and `spoken_instructions.js` has 53, matching the brief. C-10c; read, not run.

# Duties of `ImuCalibrationFlow.qml`, of the four short files, and of the spoken cues

This page has four parts:
- a full page for the flow (§1–§10, C-10a's headings);
- a short entry for each of the four short files;
- the cue list of `spoken_instructions.js`;
- the closing table for C-15.

Prefixes: `IF` = `ImuCalibrationFlow.qml`, `SC` = `CalibrationStatusColumn.qml`, `CG` = `CalibrationGuide.qml`, `GV` = `CalibrationGuideView.qml`, `CC` = `CameraCalibrationFlow.qml`, `SI` = `spoken_instructions.js`, `TR`/`WA`/`HA`/`RB`/`AS` = the routine, base and status files of C-10a and C-10b. Line numbers are at `b3e656cf`; later items match on the symbol.

**Where each duty goes:**
- **Q** stays in QML: a binding, view state, layout or a word (RP-7, §4.9).
- **D** goes to a driver on `app.calibration`, which exposes it "under the names the QML reads today" (§4.8, the driver row).
- **R** goes through `app.rig`: §4.9 turns `appSettings.*` reads into `app.rig.*`, and C-6b.1 does it (What 3).
- **C-15 What n**: BRIEF-C-15 places it, not the design. The design gives the flow's logic to no build item (NOTES-C-plan C-4).
- **U**: Stage C leaves it alone (§4.10: #74 is not in Stage C).
- **X**: it goes away.
- **NO HOME Fn**: the design does not place it. F1–F13 are in `NOTES-C-10a.md`, F14–F20 in `NOTES-C-10b.md`, and F21 on in `NOTES-C-10c.md`.

The source comments' "finding F3", "F13", "R3", "R7", "R9" and "D1" are ids from the session-setup refactor (`session_wizard_refactor_design.md`), not this inventory's.

<!-- COUNT src/Gui/calibration/ImuCalibrationFlow.qml -->
## Part 1 · `ImuCalibrationFlow.qml`

The flow has no step machine of a procedure. What it holds is a **host machine**:
- which routine exists (picked by the device holding `leadForearm`);
- whether a run has been asked for;
- whether the outcome stands after the routine is gone.

Two hosts: the session-setup arm page (`layoutMode` "full") and the toolbar sensor panel ("compact").

### The routine choice by vendor and role

The choice is `isHackMotion` 107–110, which calls `_isHackMotionRole("leadForearm")` 218–221. That function is `imuManager.unitLabelForRole(role) !== ""` (the role's placement key is a HackMotion unit key, `<id>#lowerArm` or `<id>#palm`), or `_vendorOf(imuManager.deviceIdForRole(role)) === "hackmotion"`. `_vendorOf` 207–213 walks `imuManager.imuDeviceList` for the entry's `vendor` and gives "" when the id is not listed. **The key test comes first:** it alone recognises a HackMotion the scan has not yet found (#54, `test_CH14_absentWg3StillHackMotion`). Only slot A (`leadForearm`) decides the choice.

| Slot A holder | `isHackMotion` | Routine (`_routineComp`, 299, 303) | `_segments` 244–254 handed to it |
|---|---|---|---|
| none (no device id, no unit key) | false | Witmotion | `{ a: null, b, c }`: each the role's `ImuInstance`, or null |
| a Witmotion | false | Witmotion | `{ a, b, c }` = `leadForearm`, `leadHand`, `leadUpperArm`. **A role held by a HackMotion unit is handed as null** (`wt` 250–252): an `HmUnit` has no `imuConnected` and no calibration methods |
| a HackMotion unit key, the device listed | true (the key) | HackMotion | `{ device: deviceForRole("leadForearm"), lowerArm: instanceForRole("leadForearm"), palm: instanceForRole("leadHand") }` |
| a HackMotion unit key, the device **not** listed (#54) | true (the key) | HackMotion, which says it is not connected (comment 214–217) | as above, from whatever the manager resolves |
| a bare id whose listed vendor is "hackmotion" | true (the vendor) | HackMotion | as above |

There is no refusal case in the flow: every slot-A state runs one routine. The only filter is the null for a HackMotion unit in a Witmotion run. A second copy of the choice sits outside this directory, at `SetupContext.qml:404–410`. It tests the listed device's `isHackMotion` only, without the key test, and gives "" when the arm group is not in the session. It agrees with the flow whenever the arm group is in the session, because that needs a listed device (by reading). Goes: **C-15 What 1**, by its brief; what the C++ function takes is **F21**.

### 1. Thresholds and constants

| Symbol, IF line | Value | Meaning | Goes |
|---|---|---|---|
| `phase` 92–93 | the routine's `phase`; with no routine, 2 when `_done` and not a HackMotion, else 0 | The Witmotion's "both captures done" phase, kept after the routine is unloaded | **NO HOME F22** (the outcome outlives the routine) |
| `_rightHanded` 184 | `app.athleteController.currentHandedness !== "Left"` | Right-handed unless the athlete is "Left"; an unset handedness counts as right-handed | Q, handed to the routine (WA's `tPoseQuat`, **F2**) and the guide views |
| `calibCompleteTing` 158 | `TingPlayer`, `frequency: 4186.0` Hz (C8, two octaves above the ball ting) | The completion sound | **NO HOME F24** |
| `_showRecalibrate` 383–389 | `phase > 0`, `hmStep > 0` | Recalibrate is offered once anything has happened: done, or `phase` > 0, or failed, or `mountFailed`, or HackMotion `hmStep` > 0. The same rule is written again as `AS` `showRecalibrate` (C-10b, EDGE) | Q (view state); C-15 What 3 |
| `layoutMode` 69 "full", `showHeader` 70 true, `stepLabel` 71 `Theme.caps(qsTr("Calibrate"))`, `active` 78 false, `pace` 81 null | config | Hosts set them. Unbound, the flow is inactive and runs nothing (R3) | Q; `pace` **F8** |
| Layout sizes 403–418, 442–443, 482–495 | `Theme.sp`/`Theme.gap` | Layout | Q |

The flow has no stillness, tilt, angle, hold or retry threshold, and no maths. `grep -nE '[<>]=?\s*-?[0-9.]+|[=!]==?\s*-?[0-9.]+\b'` finds 2 code lines: 387 and 388, both in `_showRecalibrate`.

### 2. States and transitions

**State:**
- `_setUp` 171: false until `Component.onCompleted`.
- `_live` 169: the routine Loader's switch.
- `_unloadPending` 264, `_runRequested` 162, `_restorePending` 164.
- `_done` 166: the outcome, which outlives the routine.
- `_restoring` 155, `_beganThisTurn` 172.
- `_routineComp` 170: which routine.
- `_lastMessage` 174, `_lastMessageKind` 175.
- `_routine` 177 = `routineLoader.item`, or null.

| Id | Transition | Trigger and guard | Log (`flow …`) | IF lines | Goes |
|---|---|---|---|---|---|
| IF1 | created | `Component.onCompleted`: `_routineComp` from `isHackMotion`, `_setUp` true, `_syncActive()` | n/a | 302–306 | **F22** (C-15 What 3) |
| IF2 | inactive → active, no routine | `onActiveChanged` → `_syncActive` 265: `_live` true, so the Loader creates the routine and `onLoaded` 331 calls `_onRoutineLoaded` | `active` | 267–268, 290, 327–332 | **F22**; the entry point **F12** |
| IF3 | active again in the turn it went inactive | `_unloadPending`: cleared, and `_onRoutineLoaded()` on the stopped routine (reused, not re-created) | `active (stopped routine reused)` | 269–273 | **F22** |
| IF4 | routine loaded → fresh run | `_onRoutineLoaded` 308: clears `_lastMessage`/`_lastMessageKind`. If neither `_restorePending` nor `_done`, and `_runRequested`: `r.begin()`, and `_beganThisTurn` true until the next turn | `routine <hackmotion\|witmotion>` | 308–313, 320–324 | **F22**, **F12** |
| IF5 | routine loaded → restored | `_restorePending \|\| _done`: `_restorePending` false, `_restoring` true, `r.restore()`, `_done = r.done`, `_restoring` false | (the routine's `restore` line) | 314–319 | **F22**; restore **F6** |
| IF6 | active → inactive | `_syncActive`, while `_live` and not `_unloadPending`: `_routine.stop("inactive")`, its `message`/`messageKind` kept, `_unloadPending` true, then unload on the next turn (`_unloadIfInactive` 285–289: `_live` false if still inactive). Header 257–263 says why: `active` can go false inside the routine's own timer handler | `inactive` | 274–289 | **F22**; the stop **F12** |
| IF7 | `begin()` | `_restorePending` false, `_runRequested` true, `_done` false. No routine: wait for active. `_beganThisTurn`: no second begin (no double clear). Else `_routine.begin()` | `begin — starts when active` / `begin — already begun on activation` | 119–128 | Q forwards (**F12**); the request-while-inactive **F22** |
| IF8 | `reset()` | `_restorePending`, `_runRequested` false; `_routine.reset()` if one; `_done` false | (the routine's) | 130–135 | Q forwards (**F12**) |
| IF9 | `showCompleted` (the host calls `showCompleted()`) | `_runRequested` false. No routine: `_restorePending` true. Else `_restoring`, `_routine.restore()`, `_done = _routine.done` | `showCompleted — when active` | 140–147 | Q forwards (**F12**); restore **F6** |
| IF10 | slot A changes vendor | `onIsHackMotionChanged`, after set-up: `_routine.stop("vendor")`, `_done` false, `_routineComp` switched, so the Loader replaces the routine. The new one begins fresh if `_runRequested` (IF4). A HackMotion done never carries over into a Witmotion run, or the reverse (header 37–41) | `vendor <hackmotion\|witmotion>` | 294–300 | **NO HOME F22** |
| IF11 | the outcome follows the routine | the anonymous `Connections` on `_routine`, `enabled: flow.active`: `onDoneChanged` → `_done = _routine.done` | n/a | 355–359 | **F22** |
| IF12 | `calibrationDone` 89 | `_done && _outcomeValid`. `_outcomeValid` 225–227: a Witmotion always; a HackMotion only while `_leadDevice` is non-null, `imuConnected`, and `fullyCalibrated === true`. So a HackMotion done dies with the link even with no routine loaded | n/a | 89, 225–227 | **NO HOME F23** |
| IF13 | completion | `onCalibrationDoneChanged` 156: a false → true edge, not `_restoring` → `calibCompleteTing.play()` and `completed()`. A restore never rings or completes ("a restore is not a completion", 149–155) | n/a | 149–158 | **NO HOME F24** |
| IF14 | Cancel (compact) | the second `onClicked` (463): `_routine.stop("cancel")` if one, then `cancelled()`. A HackMotion is aborted once. At VERIFYING the routine words it as declined, not undone | n/a | 455–464 | Q forwards (**F12**) |
| IF15 | Recalibrate | full: `FlowStatus`'s `onRecalibrate` 422; compact: the first `onClicked` (452), shown while `_showRecalibrate`. Both call `begin()` (IF7) | n/a | 422, 448–453 | Q forwards (**F12**) |

Outputs that forward the routine's state: `calibrationFailed` 90 and `mountFailed` 91 (false with no routine), and `phase` 92 (above). Goes: D under the same names (§4.8), and the no-routine fallbacks **F22**. `leadImu` 96–99 is `imuManager.instanceForRole("leadForearm")`. `_leadDevice` 232–235 is `deviceForRole("leadForearm")`. Both re-resolve through `_roleDeps` and go to Q.

### 3. Timers

There is no `Timer` and no clocked `Connections`. There are two event-loop deferrals:
- `Qt.callLater(flow._unloadIfInactive)` 282 (IF6);
- `Qt.callLater(function() { flow._beganThisTurn = false })` 323 (IF4).

Both are part of the host machine: **F22**, C-15 What 3. The routines' own timers are on their pages.

### 4. Calls out

| Call, IF line | Goes |
|---|---|
| **Routine:** `begin` 127, 321; `reset` 133; `restore` 144, 317; `stop("inactive")` 276, `stop("vendor")` 297, `stop("cancel")` 463. Reads: `done` 145, 318, 358; `failed` 90, 387; `mountFailed` 91, 387; `phase` 92, 387; `message`/`messageKind` 277–278; `hmStep` 388 | Q forwards to D, same names (§4.8). The entry points **F12**; `stop("vendor")` **F22** |
| **Device manager:** `imuManager.instanceForRole` 98, 248, 249, 251; `deviceForRole` 234, 247; `deviceIdForRole` 220; `unitLabelForRole` 219; `imuDeviceList` 205, 209; `instances` 205. **Device:** `_leadDevice.imuConnected`, `.fullyCalibrated` 226–227 | The lookups stay a view's reach to the devices (`imuManager`, a context global, R-QML-1). C-6b.1 What 2 makes `ImuManager` read the rig state behind them. The choice: C-15 What 1, **F21**. The validity read **F23** |
| **Settings:** `appSettings.imuRoles` 205, inside `_roleDeps`, as a reactive dependency only (its value is never read) | **R**: `app.rig`'s sensor model (C-6b.1 What 3, which deletes the property, What 4). The dependency must survive the swap. Every binding assigns `_roleDeps` because a bare statement is dropped by the QML compiler (203–204). If nothing replaces it, a role edit no longer re-resolves the routine or its segments |
| **Athlete:** `app.athleteController.currentHandedness` 184 | Q |
| **Guide:** the flow's `CalibrationGuide` 362–371 (`active`, `useGuideCamera: flow.isHackMotion`). It is handed to the routine (338, 348) and to each `CalibrationGuideView` (410, 486; `followForearm: flow.isHackMotion` 412, 488) | **NO HOME F2** |
| **Sound:** `calibCompleteTing.play()` 156 | **NO HOME F24** |
| **Log:** `_log` 186, `appLog.info("Calib", "flow " + text)`, at 123, 126, 142, 268, 271, 281, 296 and 313 (the IF table's log column) | **F3** |
| **Signals:** `completed` 113 (emitted 156) and `cancelled` 114 (emitted 463). The flow does no navigation: the hosts do | Q |
| Recorder, speech | none here: §9 for the arm page's bracket; the flow speaks nothing |

**Every settings read and write:** one read (`appSettings.imuRoles` 205, dependency only) and no write. `grep -n 'appSettings\.' src/Gui/calibration/ImuCalibrationFlow.qml` finds only 205.

### 5. Paths that set or clear a calibration

The flow calls none of them itself.
- `begin()` (and both Recalibrates) reaches the routine's begin, then its `_clearing`. The Witmotion's `_reset` clears every present segment (`clearCalibration`, `clearFunctionalCalibration`; WA page §5). The HackMotion clears nothing host-side (HA page §5).
- `reset()` does the same clear without a run.
- The vendor swap (IF10) stops the routine and clears nothing.
- `_done = false` (IF7, IF8, IF10) drops the outcome, not a calibration.

From C-6b.2 each clear reaches the Rig through `ImuInstance`; who asks for it is **F5**. The HackMotion's outcome is read from the device (IF12, **F23**).

### 6. What a host, another routine or a test reads

**Inputs:** `layoutMode`, `showHeader`, `stepLabel`, `active`, `pace` (forwarded as `pace: flow.pace !== null ? flow.pace : defaultPace`, 341 and 350, the form lint W7 allows).

**Hosts:**
- `CalibrateArmPage.qml:101–123` ("full"). It binds `active: page.active`.
  - `enter()` (76–82) calls `showCompleted()` when the draft's arm outcome is done; otherwise it calls `_ceremonyArmStart()`, then `begin()`.
  - `leave()` calls `_ceremonyArmEnd()`.
  - On `completed()` it records the outcome (`draft.recordOutcome("arm", ctx.groups.arm.routine, _outcomeDevices())`) and ends the ceremony step.
  - Its `Connections` clears the outcome when `calibrationDone` goes false (118–123).
- `PpImuPanel.qml:203–217` ("compact").
  - `active: mode === "calibrate" && visible`.
  - `onActiveChanged: if (active) calibFlow.begin()`: auto-start on entering calibrate mode.
  - `onCompleted` and `onCancelled` set the mode back to "list".

**Outputs:** `calibrationDone` 89, `calibrationFailed` 90, `mountFailed` 91, `phase` 92, `leadImu` 96, `isHackMotion` 107, `completed`, `cancelled`. In production the hosts read only `calibrationDone` (`CalibrateArmPage.qml:122`), and handle `completed` and `cancelled`: `grep -rn 'calibFlow\.' src --include='*.qml' | grep -v /tests/` finds 77, 80, 122 and `PpImuPanel.qml:216`. The rest are read by tests and by `FlowStatus`.

**Into the status column:** `component FlowStatus` 374–380 binds `routine: flow._routine`, `isHackMotion`, `leadImu`, `leadDevice: flow._leadDevice` and `calibrationDone: flow.calibrationDone` (the `calibrationDone` binding at 379). Q, read as `AS` reads it (C-10b).

**Read by tests (underscore names):**
- `_guide` 182 (`readonly property alias`): `CalibDriver.qml:242, 303–319, 439–443, 483, 629`; `SetupDriver.qml:697–699`; `tst_setup_smoke.qml:152, 249, 253, 315`; `tst_setup_guide_render.qml:152–154, 223`; `tst_setup_calib_witmotion.qml:556–570`; `tst_setup_calib_hackmotion.qml:549`.
- `_routine`: `CalibDriver.qml:245, 322, 325`.
- `_runRequested`: `SetupDriver.qml:700`; `CalibDriver.qml:105, 597`.
- `_lastMessage` and `_lastMessageKind`: `CalibDriver.qml:106–123, 561, 605–610`. **No production reader** (grep of `src` outside `tests/`): see Also noted.

**Internal, read only here:**
- `_segments` 244 (to the routine, 339 and 349);
- `_roleDeps` 205, `_vendorOf` 207, `_isHackMotionRole` 218 (the choice, above);
- `_outcomeValid` 225 (IF12);
- `_syncActive` 265, `_unloadIfInactive` 285, `_onRoutineLoaded` 308 (IF2–IF6);
- `onActiveChanged` 290, `onIsHackMotionChanged` 294, `onCalibrationDoneChanged` 156, `onLoaded` 331, `onDoneChanged` 358.

### 7. Tests that cover it

- `qml_ui.tst_setup_calib_witmotion`: every case hosts the flow except `test_0_harnessClock` and `test_CWr1_fullRun`–`test_CWr4_inactiveThenFreshBegin`, which host the routine bare through `hostRoutine`. The flow cases are CW01 to CW15. Those that test the flow itself:
  - `test_CW07_backFromCheckShowsComplete` (IF5, IF13);
  - `test_CW10_settingsRoundTrip` (suspend: IF6; resume: a fresh run, IF2 and IF4);
  - `test_CW11_layoutSwitchFollowsGuide` (the view re-registers: `_guide`);
  - `test_CW12_slotAChangesVendor` (IF10);
  - `test_CW13_noRepeatedTing` (IF13);
  - `test_CW14_toolbarPopupCloseIsLeave` (IF6).
- `qml_ui.tst_setup_calib_hackmotion`: every case except `test_CHr1_fullRun`–`test_CHr3_ownStopSurvivesAbortEcho`. Those that test the flow itself:
  - `test_CH09_twoHostsMarkersOnce` (two hosts);
  - `test_CH10_backFromCheckShowsComplete`, `test_CH12_noRepeatedTing` (IF13);
  - `test_CH13_toolbarPopupCloseIsLeave` (closing the popup is IF6, with one abort; reopening is a fresh run);
  - `test_CH14_absentWg3StillHackMotion` (the key test, #54);
  - `test_L05_wizardSilentAfterStart`.
- `qml_ui.tst_setup_smoke`: `test_1_loads_with_no_devices` (no flow off the Calibrate page), `test_3_witmotion_chain`, `test_4_hackmotion_chain`.
- `qml_ui.tst_setup_sensors`: `test_OC1_linkDropAfterDoneVoidsTheOutcome` (IF12), `test_OC2_recalibrateAfterDone` (the Check page's Recalibrate brings the step back and a fresh run: IF7).
- `qml_ui.tst_setup_guide_render`: `test_RS1_witmotionGuide`, `test_RS2_hackMotionGuide` (through `_guide`).
- `qml_ui.tst_setup_rolesviz`: `test_V5_toolbarChipShowsMountNames` (the panel's flow, inactive in list mode).
- `session_setup_lint_test`:
  - W1: the `Connections` 355 is gated on `active`, never on `visible`.
  - W5.
  - W7: the `pace:` forwards at 341 and 350.
- `tst_setup_flow`'s `test_OVF_*` test the draft's outcome, not this file.

### 8. Where each duty goes

In the last column of every table above. In short:
- the forwards and the layout stay in QML;
- the outputs become D's names;
- the roles dependency goes through `app.rig` (C-6b.1);
- the routine choice is C-15 What 1 (**F21**);
- the host machine is **F22**, the outcome validity **F23**, the ting **F24**;
- the guide is **F2**, the log **F3**, the entry points **F12**.

### 9. The recorder

The flow makes no recorder call. Its full host does, around the flow's run (`CalibrateArmPage.qml:87–99`, which C-10b handed to this item). This brackets the **trunk** sensors' recording while the arm routine runs.
- `trunkCeremony` comes from `page.ctx.trunkCeremony` (87).
- `_ceremonyArmStart` (88–95) does nothing unless the trunk group is in the session. It also does nothing when a trunk ceremony is held for the session (`pendingWrite`) and the trunk outcome is done, so that a fresh arm run cannot drop it. Otherwise: `begin()` unless already `recording`, then `stepStart("armRoutine")`. It is called on `enter()` before `begin()`.
- `_ceremonyArmEnd` (96–99) calls `stepEnd("armRoutine")` when recording and the current step is `armRoutine`. It is called on `leave()` and on `completed()`.

Goes: the requests are **NO HOME F4**, which C-10a filed for the trunk routine's bracket. This is the same gap in the arm page, which no Stage C brief names (`grep -n CalibrateArmPage build/platform/C/BRIEF-*.md`: none). The recorder's side is `docs/implementation/platform_refactor/duties/DUTIES-trunk_ceremony_recorder.md`.

### 10. The hands-free rules, as lines

- **Start.** The flow starts a run only when its host calls `begin()` and it is active (IF2, IF4, IF7). Both hosts call it on entry: the arm page's `enter()` (page entry), and the panel's `onActiveChanged` (entering calibrate mode). So a run's begin, with its clear and its guide pose, happens on page entry, as with the trunk: **F12**, with the arm routines' **F14**.
- **On and still.** The flow has no on-and-still wait. Each routine's waits are on its page; the arm routines have none (**F14**).
- **Which sensor it is waiting for, on screen:** not this file's. The status column words it (`AS` `_wtStatus`, "Waiting for sensor"; `NoImuWarning` when `leadImu` is null).
- **A step that faces away:** none in the arm routines. The trunk is on its own page and does not use this flow.
- **One arm instrument per wearing.**
  - The choice runs exactly one routine, from slot A's holder.
  - `_segments` never hands a HackMotion unit to the Witmotion routine (250–252).
  - The HackMotion routine gets one device's two units.
  - A wrist HackMotion with trunk Witmotions is allowed: the trunk never passes through this flow.
  - "One claim per role" and "first match" are the rule (`imu_roles::claimRole`), not a defect.
- **The device is characterised.** The flow does no sensor maths, so nothing here re-derives an axis.
- **Thorax and pelvis only.** The flow handles the three arm roles only; no thigh role appears anywhere in the directory.
- Headless tests are not evidence that the routine is usable. The studio PC visit is.

### Grep hits that are not duties

| IF line | Text |
|---|---|
| 45 | `reacts to no device signal (finding F3). The semantics, as approved (D1, "stop and` (header comment; the wizard refactor's ids) |
| 200 | `The reactive dependencies, written ONCE: the resolvers are Q_INVOKABLEs, not` (comment on `_roleDeps`) |

Every other count hit has a row above.

<!-- COUNT src/Gui/calibration/CalibrationStatusColumn.qml -->
## Part 2 · The four short files

### `CalibrationStatusColumn.qml` (SC, 236 lines)

**What it is.** The one drawing of a calibration's status column, beside the guide view: presentational only (header 19–42). The host works out every word and hands it in.

**Hosts:**
- `ArmCalibrationStatus.qml:212` (the full column), and its inline parts at 257–278 (`BodyText`, `HoldBar`, `StatusLine`, `MessageText`);
- `CalibrateTrunkPage.qml:204` (`namePrefix: "trunk"`).

**Threshold: none. Timer: none. Transition: none.**
- `grep -nE '\bTimer\b|callLater|Connections|appSettings|imuManager|appLog|ttsController|trunkCeremony|app\.' src/Gui/calibration/CalibrationStatusColumn.qml` finds 0 lines.
- The numeric-compare grep above finds 0.
- It holds three `Behavior` animations: the hold bar's width at 176 (`NumberAnimation { duration: 150 }`), and the status line's size and colour at 219–220 (`Theme.durationNormal`, off when `animated` is false). They draw; nothing waits on them.
- Its one call out is the `recalibrate` signal (77), emitted by the Recalibrate `PpButton`'s `onClicked` (142).

**Properties** (all Q, bound by the host):
- the column (49–76): `showHeader`, `stepLabel`, `title`, `namePrefix` (names the parts `<prefix>Instruction`, `…HoldBar`, `…Status`, `…Message`, `…Recalibrate` through `_name` 81), `instruction`, `large` (the column's, 58), `holdShown`, `holdProgress`, `holdDone`, `status`, `statusBig`, `statusTone`, `statusTick`, `statusAnimated`, `message`, `messageKind`, `messageSize`, `showRecalibrate`;
- `BodyText`: `large` (149);
- `HoldBar`: `progress`, `done`, and `fillFraction` (= `progress`, not clamped: the host clamps, as `AS` `holdProgress` does);
- `StatusLine`: `text`, `big`, `tone`, `tick`, `animated`;
- `MessageText`: `kind`, `large` (227), `size`.

**Tests:**
- `tst_setup_trunk` reads its parts by name: `test_TP6_failBackNextThenRecalibrateMidHold` (`trunkMessage`, `trunkRecalibrate`), `test_TP7_leaveMidTurnThenNext` (`trunkStatus`), `test_TP8_golfTermsAndTheDirection` (`trunkStatus`, `trunkInstruction`).
- The arm cases reach it through `ArmCalibrationStatus` (C-10b §7).

**Goes:** Q, all of it. C-11b.2 reads its component pages first (its brief, before QML is touched).

<!-- COUNT src/Gui/calibration/CalibrationGuide.qml -->
### `CalibrationGuide.qml` (CG, 206 lines)

**What it is.** The adapter between the routines and one `BodyVizView`. It is not a "none": it decides when the routines' input **"the guide finished"** arrives. Hosts:
- `ImuCalibrationFlow.qml:362`;
- `CalibrateTrunkPage.qml:134`;
- the probe `tests/probes/trunk_guide.qml:58`.

Its stand-in is `tests/setup/fakes/FakeGuide.qml`.

**Constants:**
- `armTarget` 75: (0.7071, 0.7071, 0, 0), the arm hanging at the side (= `RB` `leadArmDownQuat`).
- `foreTarget` 76: identity.
- The HackMotion poses with no view (80–83): identity quaternions, and `hmRaiseDeg` 30. The view's own value is `BodyVizView.qml:178` `hmCalRaiseDeg: 30`, which HA 708 words in a sentence.
- `trunkAnimate`'s duration floor, `Math.max(1, ms)` (152).

**State:** `_inFlight` 89, `_trunkInFlight` 162, `_swappedInFlight` 182, `animating` 77, `trunkAnimating` 134, `hipsTargetDeg` 127, `chestTargetDeg` 128, `armsCrossed` 129. Derived from them: `hipsTarget` 132 and `thoraxTarget` 133 (through `_turn` 135 = `view.trunkTurn(deg)`: the view does the quaternion maths, by its handedness), and `ready` 69 (`view !== null && view.fullyLoaded`, the routines' start gate).

**Transitions:**

| Id | Rule | CG lines |
|---|---|---|
| G1 | `animate(armQ, foreQ, ms)`: sets the view's duration; `_inFlight` and `animating` go true, then the targets are set. The view starts its motion on the target change | 104–110 |
| G2 | The view's `leadArmAnimFinished` while `active && _inFlight` (`_viewFinished.onLeadArmAnimFinished`): `_inFlight` false, `finished()` once. A stray finish is never reported. `animating` stays true until the next `pose()` or `cancel()` | 198–205 |
| G3 | `trunkAnimate(hips, chest, crossed, ms)`: `armsCrossed`, the duration, `_trunkInFlight` and `trunkAnimating` true, then the targets. If the pose is unchanged, `view.retargetTrunk()`, so each call reports exactly one `finished()` | 150–159 |
| G4 | The view's `trunkAnimFinished` while `active && _trunkInFlight` (`_viewTrunkFinished.onTrunkAnimFinished`): `finished()` once. `trunkAnimating` is left as G2 leaves `animating` | 164–171 |
| G5 | `pose()` / `trunkPose()`: no motion. In-flight cleared, the view reset, the targets set | 93–99, 138–145 |
| G6 | `cancel()`: every in-flight flag false, the view's arm reset and trunk stopped, `animating` false. **No `finished()`** | 114–121 |
| G7 | `active` false → `cancel()` (`onActiveChanged`) | 173 |
| G8 | A view swapped mid-motion (`onViewChanged`): `_swappedInFlight` while a motion is in flight. Once `ready` (`onReadyChanged`), `_finishSwapped` reports `finished()` once, if still active and in flight. Nothing is replayed (DECISION stage5b) | 175–196 |

**Timer: none** (code lines; the two `FrameAnimation` mentions are comments). **Calls out:** the view only: `resetArmAnimation`, `leadArmAnimDuration`, `stopTrunkAnimation`, `resetTrunkAnimation`, `trunkAnimDuration`, `retargetTrunk`, `trunkTurn`. No settings, device, log or speech.

**Read by:**
- the three routines (their §4 "Guide" rows): `ready`, `pose`, `animate`, `cancel`, `trunkPose`, `trunkAnimate`, `finished`, `hmUpperArm`, `hmForePose0`, `hmForePose1`, `hmRaiseDeg`;
- the flow (`useGuideCamera`, `active`);
- `CalibrationGuideView` (the targets, `animating`, `trunkAnimating`, `armsCrossed`, `useGuideCamera`; it sets `view`).

**Tests:**
- `tst_setup_guide_render`: `test_RS1_witmotionGuide`, `test_RS2_hackMotionGuide`, `test_RC_checkViewFollowsTheSensor`;
- `test_CW05_guideStallHoldsChain`, `test_CWr2_stallHoldsIntroDown`, `test_CW11_layoutSwitchFollowsGuide` (G8);
- the late-completion checks in `test_CW12_slotAChangesVendor`, `test_CW13_noRepeatedTing`, `test_CH12_noRepeatedTing` and `test_L05_wizardSilentAfterStart` (G2's guard);
- `test_TR9_nothingStartsBeforeTheGuideIsReady` (`ready`);
- the probe `trunk_guide.qml` (frames).

**Goes:** **NO HOME F2** for the guide as a whole (C-10a). G1–G8 are what produce P's "guide finished" input: **F25**. The interface header lines are comments (below).

| CG line | Grep hit that is not a duty |
|---|---|
| 23 | `readonly property bool ready            the guide can be watched (bvv.fullyLoaded)` (header comment) |
| 27 | `signal finished()` (header comment) |
| 29 | `property bool useGuideCamera` (header comment) |

<!-- COUNT src/Gui/calibration/CalibrationGuideView.qml -->
### `CalibrationGuideView.qml` (GV, 82 lines)

**What it is.** A `BodyVizView` bound to a guide (its root type, not a wrapper). Hosts:
- `ImuCalibrationFlow.qml:406, 480` (arm);
- `CalibrateTrunkPage.qml:194` (`trunk: true`).

**Threshold: none. Timer: none. Transition: none.** `grep -nE '\bTimer\b|callLater|Connections|appSettings|imuManager|appLog'` finds 0 lines, and the numeric-compare grep finds 0.

What it holds:
- inputs `guide` 43, `trunk` 44, `followForearm` 45;
- `Component.onCompleted` 47, which registers the view with its guide (`guide.view = view`), its one event;
- `_crossed` 59 (= `trunk && guide.armsCrossed`), which picks the view's crossed-arm poses;
- the rest poses: (0.7071, 0.7071, 0, 0) at 63 and 73, and an identity forearm unless `followForearm` (68–70);
- `highlightLeadArm: !trunk`, `animateLeadArm` and `useGuideCamera` only off the trunk (50, 77, 81).

**Tests:**
- `tst_setup_smoke` `test_3_witmotion_chain` ("BodyVizView did not register itself with the flow");
- `test_CW11_layoutSwitchFollowsGuide` (re-registration);
- `tst_setup_guide_render` (all three cases);
- `tst_setup_trunk` through the trunk page.

**Goes:** Q (view bindings). The poses are the guide's: **F2**.

<!-- COUNT src/Gui/calibration/CameraCalibrationFlow.qml -->
### `CameraCalibrationFlow.qml` (CC, 101 lines, a stub)

**What it is.** A placeholder for the stereo calibration, with the flow's shape: `layoutMode` 32, `showHeader` 33, `completed` 35, `cancelled` 36. Hosts:
- `TriangulatePage.qml:91–96` ("full");
- `PpCameraPanel.qml:264–272` ("compact"; `onCompleted`/`onCancelled` set the mode back to "list").

**Threshold: none. Timer: none. Transition: none.**
- `begin` 39, `reset` 40 and `showCompleted` 41 are empty.
- `completed` is never emitted.
- The Start button is disabled (91).
- Cancel's `onClicked` (96) emits `cancelled()`.
- `grep -nE '\bTimer\b|callLater|Connections|appSettings|imuManager|cameraManager|appLog'` finds 0 lines, and the numeric-compare grep finds 0.

**Tests:** none names it or checks it (`grep -rn CameraCalibrationFlow src/Gui/tests`: none).

**Goes:** **U** (§4.10: the camera calibration programme is #74's, outside Stage C).

<!-- COUNT-END -->
## Part 3 · `spoken_instructions.js`: the cues, by routine and step

Only the trunk routine speaks. The arm routines and the flow speak nothing. Every line is spoken only while `speakInstructions` is on; the trunk page binds it to `Experimental/spokenInstructions` (`CalibrateTrunkPage.qml:117`), which is off by default. The words stay here, verbatim (§4.8). Two rules from the file's header (20–31):
- the lines are **instructions only**, never a verdict, status, reading or failure;
- the prepared text and the spoken text are **identical, character for character**, because the voice's cache is keyed by the text.

| SI line, name | Words | Routine, step, and when it is said | Today's cue code (log `speak cue=…`) | The same cue on screen |
|---|---|---|---|---|
| 40 `trunkStartBackswing` | "Start your backswing turn." | TrunkRoutine, step `turnAway`, at arming → armed (T16): `_arm` → `_speak("start", _startLine())`, TR 513, with `_startLine` 242–244 choosing by `_stage`. Every attempt | `start` | 51 `trunkScreenStartBackswing` "START YOUR BACKSWING TURN", shown while armed, or turning before the turn-back cue (`CalibrateTrunkPage.qml:170–172`) |
| 41 `trunkStartFollowThrough` | "Start your follow-through turn." | TrunkRoutine, step `turnThrough`, the same moment (T16) | `start` | 52 `trunkScreenStartFollowThrough`, the same rule |
| 42 `trunkTurnBack` | "Turn back to address." | TrunkRoutine, either turn step, substage `turning`, once per attempt: at \|turn\| ≥ 70° (T19a), or stopped at the top for `returnStill` (T19b). `_cueTurnBack` → `_speak("turnBack", _turnBackLine)`, TR 583. If the voice had to be stopped first, it is said when the voice goes idle while still turning (V7, TR 837) | `turnBack` | 53 `trunkScreenTurnBack`, shown while turning once `turnBackDue` (`CalibrateTrunkPage.qml:173`) |
| 43 `trunk` | the three, in that order | Not spoken: the list prepared in advance, as `TrunkRoutine.spokenPhrases` (TR 248) and the step descriptor's `spokenPhrases` (`SetupSteps.qml:249`), which the session-setup shell has the voice synthesise | n/a | n/a |
| 50 `onScreen(line)` | drops a final "." and upper-cases | Derives the screen form from the spoken one, so screen and voice cannot differ | n/a | 51–53 |

**What the step machine's cue codes must map onto.** Today there are two codes for three lines:
- `start` picks its line by the stage: `turnAway` → line 40, `turnThrough` → line 41.
- `turnBack` is one line for both turns.

So a cue code must either carry its step or be split into three. The screen form does not follow the cue event. It follows the state (substage, and `turnBackDue`, which P exposes), so it shows with speech off too. The skip, stop and wait rules around each cue are V1–V9 on the trunk page (**F1**).

**Tests:**
- `tst_setup_trunk` holds the words (`startAway`, `startThrough`, `turnBack`, 732–734) and the logged lines: `test_TR18_startAtArmingTurnBackAtSeventyDegrees`, `test_TR19_eachTurnAttemptSaysItsCuesOnceNothingElseSpeaks`, `test_TR21_leaveOrRecalibrateStopsWhatWasSaid`, `test_TR22_spokenPhrasesAreTheLinesAndThePreparedOnesAreLogged`, `test_TR23_theTurnBackCueRule`, `test_TR24_aStartLineStillPlayingIsCutOffByTheTurnBack`, `test_TR17_armedDriftDoesNotStartATurn`, and `test_TR20_settingOffOrNoReadyVoiceSaysNothing` (nothing is said).
- The page: `test_TP7_leaveMidTurnThenNext`, `test_TP8_golfTermsAndTheDirection`, `test_TP9_theSettingAndTheAppVoiceReachTheRoutine`, `test_TP10_settingOnPreparesEachPhraseWhenSetupIsCreated`.

The count's one hit in this file (`function onScreen(`) is outside the five QML files the brief counts, and is covered by the row above.

## Part 4 · For C-15: the logic in `src/Gui/calibration` that C-11b.2, C-12b and C-13b will not take

The cut-overs take their routine files' machines, timers, thresholds and arithmetic (TR, WA, HA), and the `AS` reads of each routine's internals together with the Witmotion hold progress (C-12b What 3, C-13b What 4). Everything below stays behind. Whether each line moves or stays as view state is C-15's call (its check 2), unless a brief already says. **A: the files no cut-over edits for logic.**

| File:line | What it does | Placed by |
|---|---|---|
| IF 107–110, 207–221, 294–306 | The routine choice (key test, then vendor) and the swap | C-15 What 1; **F21** |
| IF 244–254 | The segments, with a HackMotion unit nulled for the Witmotion routine | C-15 What 3 |
| IF 155–177, 264–332, 355–359 | The host machine IF1–IF11: request while inactive, restore pending, begin or restore on load, stop and unload on inactive, reuse in the same turn, the outcome outliving the routine, two `Qt.callLater` | **F22**, **F12** |
| IF 89, 225–227 | `calibrationDone` = done and, for a HackMotion, the device's live state | **F23** |
| IF 149–158 | The ting and "a restore is not a completion" | **F24** |
| IF 92–93 | `phase` with no routine | **F22** |
| IF 383–389; AS 186–188 | `_showRecalibrate`, the same rule written twice | C-15 What 3 (C-10b EDGE) |
| IF 186 and its 8 lines | The flow's log | **F3** |
| RB 91–99 | `defaultPace`, `pace`, `_p`, `_ms` | C-15 What 2; **F8** |
| RB 102–175 | The contract and entry points (`stage`, `_running`, begin, stop, reset, restore, inactive), the six hooks | C-15 What 2 keeps the contract; **F12**, **F13** |
| RB 87; RB 183–187 | `leadArmDownQuat`; the `_guideFinished` tap | **F13**, **F2** |
| RB 130–139 | `_log`, `_logStage`, `onStageChanged` | **F3** |
| CG 69–205 | G1–G8, the guide's own rules: they produce "guide finished" | **F25**, **F2** |
| GV 47, 59–81 | Registration and pose bindings | Q (view) |
| SC, CC | none | n/a |
| SI 50 | `onScreen`: words derived from words | Q |

**B: `ArmCalibrationStatus.qml` lines that C-12b What 3 and C-13b What 4 do not name.**

| AS line | What it does | Placed by |
|---|---|---|
| 57–58 | `_wt`/`_hmr`: which routine is loaded, by duck typing | C-15 What 3 |
| 83–87 | The HackMotion enum defaults (−1) | **F20** |
| 156, 328, 414, 444 | Device readouts read straight from the device | **F20** |
| 192–197 | `_hmStateText` by the library's codes | **F20** |
| 299–303 | `StatusBadge`'s complete/calibrating/failed rule | C-15 What 3 |
| 324–337 | `AngleWarning._show` (never true in production) | **F17** |
| 341–349, 358–373, 405–487 | `NoImuWarning`, `HmStepBar` (steps 1–5), `HmReadouts` (steps 4–5) | C-15 What 3 |
| 437–439 | The travel tint's 15° | **F20** |

`PLAN-C.md:45` gives C-15 "the status view's hold progress", but `BRIEF-C-12b` What 3 takes it (`AS` 128–134). The coordinator should settle which.

**C: lines in the three routine files that their cut-over can take only once a finding is ruled.** C-11b.2's check 3 stops on a line with no home, and C-12b and C-13b take the same shape.

| Finding | Lines |
|---|---|
| F1 speech | TR 594–633, 820–840 |
| F2 the guide | TR 274, 301, 364, 377, 446–463, 688, 748, the pace's `demoTurn`/`demoPause`; WA 92–94, 238–532 (guide calls), `introAnim`/`raiseAnim`; HA 230–427, 639–658, 708 |
| F3 the log | TR `_waitTick`, `waitLog`; each routine's `_log` lines |
| F4 the recorder bracket | TR 239, 273, 352–360, 376, 420–422, 508, 650 |
| F5 who asks for the clear | TR 317–328, 375; WA 254–280 |
| F6 restore | TR 290–305; WA 226–245; HA 257–262 |
| F14 hands-free on the arm | WA 146, 327, 478; HA's timed settles |
| F16 the slerp average | WA 156–185 and the two captures (W10, W16) |
| F17 the legacy flag | WA 228–230 |
| F18 the mount gate's constants | WA 360 |
| F19 unnamed inputs | WA 116, 119, 333–334, 360; HA 400, 694 |

**D: the same rules outside the directory**, which C-15's count cannot see:
- `SetupContext.qml:404–410` (the second routine choice, **F21**);
- `CalibrateArmPage.qml:87–99` (the arm-run ceremony bracket, **F4**);
- `CalibrateArmPage.qml:76–82` and `PpImuPanel.qml:216` (begin on entry, **F12**/**F14**).

### Also noted (by reading, not reproduced; EDGE unless the coordinator says otherwise)

- `_lastMessage` and `_lastMessageKind` (IF 174–175, 277–278, 311–312) have no production reader. Only `CalibDriver.qml` reads them. The routine's last word after an inactive stop is shown nowhere.
- There are two sources of handedness. The flow uses `app.athleteController.currentHandedness !== "Left"` (IF 184); the trunk page uses `!page.ctx.leftHanded`. Both reach the guide only (**F2**).
- After a finished motion, `CG` leaves `animating` and `trunkAnimating` true until the next `pose()` or `cancel()` (G2, G4).
- `SC`'s hold-bar duration is a literal 150 ms (176), not a `Theme` token. It is a design-system matter, for the Design Lead and not for Stage C.
- None of these is a loss of a shot, input, setting, pairing or key.
