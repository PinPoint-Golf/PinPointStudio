Taken at `c8d3eb9d` (main), `src/Gui/calibration/TrunkRoutine.qml`, 863 lines. C-10a; read, not run.

# Duties of `TrunkRoutine.qml`

`TR` = `src/Gui/calibration/TrunkRoutine.qml`. Line numbers are at `c8d3eb9d`; later items match on the symbol.
The routine's root type is `CalibrationRoutineBase` (its page: `docs/implementation/platform_refactor/duties/DUTIES-CalibrationRoutineBase.qml.md`).

**Where each duty goes** (heading 8: the last column of every table). The design is `docs/design/rig_design.md` §4.8, "The shape of a procedure":
- **P**: `TrunkProcedure` (Rules, C-11a). It takes events (a tick with its elapsed time; a sensor summary of gyro, tilt, heading and link; the guide finished; speech finished; and the result of each request). It returns the stage, progress, reason codes, cue codes and requests (start a step, solve now, check the turn). It holds every threshold and transition, and has no clock, no timer and no sentence.
- **D**: the trunk driver (Adapters, C-11b.1, used from C-11b.2). It owns the timers and the sensor taps, feeds P, calls the sink, and exposes P's state under today's names.
- **Q**: stays in QML as a binding or a word (C-11b.2). The words stay verbatim: `spoken_instructions.js` and the routine's and page's `qsTr` lines, chosen by code.
- **S**: the Session's door (C-14).
- **X**: goes away, with the reason.
- **NO HOME Fn**: the design does not place it. Finding *n* is in `NOTES-C-10a.md`.

## 1. Thresholds and constants

| Symbol, TR line | Value, unit | Meaning | Goes |
|---|---|---|---|
| `_stillDps` 223 | 10.0 °/s | Still: each sensor's `gyroDps` (the device gyro's peak over 250 ms) ≤ this. Gates ready, hold, arming, return and the top-stop. Mirrors `tuned::trunkRoutine::kStillGyroDps` (lint W11) | P, read from the constant |
| `_uprightMaxDeg` 237 | 35.0° | Upright: `longAxisFromUpDeg` ≤ this (sensor +Y from up, live accel). Mirrors `tuned::calib::kTrunkLongAxisMaxFromUpDeg` (W11) | P |
| `_turnStartDeg` 227 | 20.0° | A turn has started: chest \|turn\| ≥ this from the armed heading (`kTurnStartDeg`) | P |
| `_turnReturnDeg` 228 | 15.0° | Returned: \|turn\| ≤ this and still for `returnStill` (`kTurnReturnDeg`) | P |
| `_turnBackCueDeg` 231 | 70.0° | Turn-back cue: \|turn\| ≥ this (`kTurnBackCueDeg`) | P |
| `_armDriftFollowDps` 234 | 0.5 °/s, routine time | While armed and still, the armed heading follows the live one at up to this (`kArmDriftFollowDps`). Arithmetic at 541–542: cap = rate × interval / (1000 × `scale`), then `wrap180` | P |
| `defaultPace` 139–154: `introStart` 1500, `ready` 1000, `settle` 1000, `hold` 2200, `arm` 1000 (`kArmStillMs`), `returnStill` 750 (`kReturnStillMs`), `turnTimeout` 20000 (`kTurnTimeoutMs`), `retry` 2500, `next` 800 | ms | Step timings; `hold` is calibrateTrunk's 2 s plus a tick | P (time is an input) |
| `defaultPace` `tick` 100 | ms | The sampling interval of the ready, hold, arm and turn timers | D (the timers' interval) |
| `defaultPace` `waitLog` 1000 | ms | At most one `trunk wait` log line per this | D with the log: **NO HOME F3** |
| `defaultPace` `demoTurn` 2500, `demoPause` 600 | ms | The guide's demonstration turn, each way, and its pause | **NO HOME F2** |
| demo angles 446, 452 | hips 45°, chest 90°, sign +1 away / −1 through | The guide's demonstration | **NO HOME F2** |
| `defaultPace` `scale` 1, and the `pace` override (base) | × | Tests scale and override single keys (W7) | **NO HOME F8** |
| `holdProgress` 168 | 0..1 | min(1, `_holdMs` / `hold`); 1 when done | P (progress); D exposes it |
| turn arithmetic 525–527 | ° | d = `VizMath.wrap180(heading − _turnRef)`; peak = max \|d\| | P |
| `_ourSteps` 239 | `["standTall","turnAway","turnThrough"]` | The steps `_closeStep` may close on the recorder | D: **NO HOME F4** for the request |
| Retry limits | none | Stand tall retries without limit (438 → 814). A failed turn re-arms without limit (670 → 815). Only a started turn is bounded (`turnTimeout`). An unstarted turn and ready wait without limit, by intent (header 59–61) | P, as is |
| NaN rule | n/a | `!(x <= limit)` (392, 394, 490) counts a non-finite reading as not upright or moving. `isFinite` guards (525–554, 773) count it as "no reading": it neither starts, returns nor arms | P |

The solve's and the turn check's own gates are not here: `calibrateTrunk` (mean gyro 0.5 °/s, long axis 35°, face 40°) and `checkTurn` (`calib::kTrunkTurn*`). They live in `src/IMU`, are called, and are never re-derived (STANDING 7).

## 2. States and transitions

Stages (157): idle, intro, standTall, turnAway, turnThrough, done, failed. Substages (159–161): ready, settle, hold (stand tall); demo, arming, armed, turning, returned, timeout (a turn); passed, retry.

Every transition but restore's goes through `_go` (251–256). It sets the stage and substage, clears `_waitReason`, and writes one line, `trunk stage=<s>[ step=<sub>][ <why>]`. `tst_setup_trunk`'s `stageLines`/`stageShape` compare these lines. The **why** column is that line's reason text, verbatim (`#` = a measured angle).

The ids (T1…) are for C-9a: every row is to be reached by one scripted input.

| Id | From → to | Trigger and guard | Why (log) | TR | Goes |
|---|---|---|---|---|---|
| T1 | any → idle | begin() or reset() → `on_Clearing`: `_closeStep` if a run was in hand, `_stopSpeech`, `_reset` (clears both sensors, then the state) | (base: "begin" / "reset") | 265–269, 326–350 | P; the clear is D (**F5**) |
| T2a | idle → failed | `on_Begun`: no hip sensor | `missing` | 271–272, 310 | P (presence is an input) |
| T2b | idle → failed | no chest sensor | `missing` | 311 | P |
| T2c | idle → failed | hip sensor not connected | `missing` | 312 | P (link down) |
| T2d | idle → failed | chest sensor not connected | `missing` | 313 | P |
| T3 | idle → intro | `on_Begun`: both present and connected. Recorder `begin()` unless already recording; guide `trunkPose(0,0,false)` | "" | 270–276 | P; the recorder call D (**F4**); the guide **F2** |
| T4 | intro → standTall/ready | `_introTimer` after `introStart`, only while `guide.ready`. Then `_startStandTall`: clear both sensors, recorder `stepStart("standTall")`, pose, the counters zeroed, `_waitLogMs` primed | "" | 684–690, 374–383 | P; `guide.ready` is not a P input (**F2**); the clear **F5** |
| T5 | ready → ready, count zeroed | a tick with a reason: not upright (`longAxisFromUpDeg` > 35° or NaN), else moving (`gyroDps` > 10 or NaN) | (wait line) | 700–705, 386–398 | P |
| T6 | ready → settle | `ready` ms unbroken, both upright and still | `upright and still` | 706–709 | P |
| T7 | settle → hold | `_settleTimer` after `settle`; `_holdMs` 0, `_waitLogMs` primed | "" | 713–718 | P |
| T8 | hold → hold, bar emptied | a tick with a reason (same rule as T5) | (wait line) | 728–733 | P |
| T9 | hold → passed | `hold` ms unbroken → `_captureStandTall`: both `calibrateTrunk`, `attachResult` ×2, `stepEnd("standTall")`; both `valid` | `pelvis=pass thorax=pass` | 734–737, 416–431 | P (request: solve now) |
| T10 | hold → retry, `_retryTo` standTall | either record invalid. The message is each failing record's `failure`, else a stand-in line; kind `error` | `pelvis=<pass\|fail> thorax=<pass\|fail>` | 432–438 | P |
| T11 | standTall/passed → turnAway/demo | `_nextTimer` after `next` → `_startTurn("away")`: `_anim` out, the guide posed and animated +45°/+90° over `demoTurn` | "" | 796–801, 444–454 | P; the guide **F2** |
| T12 | demo: out → pause | `on_GuideDone`, `_anim` out | (none: substage unchanged) | 756–758 | P (input: guide finished) |
| T13 | demo: pause → back | `_demoPauseTimer` after `demoPause`; guide animated to 0/0 | (none) | 742–750 | P; **F2** |
| T14 | demo/back → arming | `on_GuideDone`, `_anim` back → `_armTurn`: `_anim` "", pose, `_waitLogMs` primed | "" | 759–761, 458–465 | P |
| T15 | arming → arming, count zeroed | a tick with a reason: a sensor moving, or no chest heading | (wait line) | 771–777 | P |
| T16 | arming → armed | `arm` ms unbroken → `_arm`: `_turnRef` = the heading, `_turnBackDue`/`_saidStart`/`_turnBackPending` reset, recorder `stepStart(<turn>)`, `waitReason` "Waiting for your … turn", the **start cue** | `heading=#` | 778–783, 497–514 | P (request: start a step; cue: start) |
| T17 | armed → armed, drift followed | finite d and both still: `_turnRef` moves toward the heading by at most the cap | (wait line, `turn=… peak=…`) | 536–547 | P |
| T18 | armed → turning | \|d\| ≥ `_turnStartDeg`. The turn-back test runs on the same tick | `started peak=#` | 529–534 | P |
| T19a | turnBackDue false → true | turning, \|d\| ≥ 70° | log `cue turnBack reached 70deg at turn=# peak=#` | 574–575, 580–584 | P (cue: turn back) |
| T19b | turnBackDue false → true | turning, \|d\| ≥ 20° and both still for `returnStill` unbroken (`_topMs`), short of 70° | log `cue turnBack stopped at the top at # turn=# peak=#` | 572–573, 576–577 | P |
| T20 | turning → returned | \|d\| ≤ 15° and both still, `returnStill` ms unbroken (`_backMs`) → `_checkTurn` | `peak=# turn=#` | 554–558 | P |
| T21 | turning → timeout | `_turnMs` ≥ `turnTimeout` → `_checkTurn` | `peak=# turn=#` | 559–561 | P |
| T22 | → turnAway/passed | `_checkTurn`: `checkTurn`, `stepEnd`, `turnResults[dir]`; valid, away | `turn=valid` | 643–659 | P (request: check the turn) |
| T23 | → done | valid, through → `_finish`: done, recorder `end()`, `completed()` | `turn=valid` | 660, 674–679 | P; `end()` D (**F4**) |
| T24a | → retry, `_retryTo` the direction | invalid, cause not backwards or swapped. Kind `warn` (or `error` if noData). Message: the record's `failure`, plus the hand hint if `wrongWay` | `turn=fail cause=<cause>` | 663–671 | P |
| T24b | → retry, `_retryTo` standTall | cause backwards or swapped. Kind `error`, plus the re-stand-tall line | `turn=fail cause=<cause> back=standTall` | 664, 667–671 | P |
| T25 | turnAway/passed → turnThrough/demo | `_nextTimer` → `_startTurn("through")` (−45°/−90°) | "" | 802 | P |
| T26a | retry → standTall/ready | `_retryTimer` after `retry`, `_retryTo` standTall → `_startStandTall` (clears both sensors again) | "" | 808–814 | P; **F5** |
| T26b | retry → arming (no demo) | `_retryTimer`, `_retryTo` away or through → `_armTurn` | "" | 815 | P |
| T27a | → failed | hip link lost while intro, standTall, turnAway or turnThrough (`_watching`). `_fail`: guide cancelled, step closed, message `error`, recorder `end()` | `pelvis disconnected` | 843–853, 362–370 | P (link down) |
| T27b | → failed | chest link lost | `thorax disconnected` | 854–862 | P |
| T28 | any → idle (unless done) | stop() → `on_Stopping` (`_anim` ""), then `on_Stopped`: `_stopSpeech`, `_closeStep`, `_resetState` | (base: `stop reason=… stage=…`) | 281–286 | P; the stop D (**F12**) |
| T29 | any → done | restore() → `on_Restoring`: records from the sensors, the bar full, done, pose | (base: `restore done=true solves=<both\|not both from stand tall>`) | 290–305 | **NO HOME F6** |

**The speech sub-machine.** It runs only with `speakInstructions` on; ids for C-9a.

| Id | Case | TR | Goes |
|---|---|---|---|
| V1 | setting off: nothing said, nothing logged | 595 | **F1** |
| V2 | no voice: skipped, logged `skipped: no voice (text-to-speech is not available)` | 597, 601 | **F1** |
| V3 | voice not ready: `skipped: voice not ready` | 598, 601 | **F1** |
| V4 | start cue while the voice is busy: `skipped: voice busy` (turn-back is never skipped for busy) | 599, 601 | **F1** |
| V5 | turn-back after this attempt's start line was said, or while busy: `stopSpeaking`, logged `stops the voice first (the start line)` / `(busy)` | 602–604 | **F1** |
| V6 | still active after the stop: `_turnBackPending`, logged `waits for the voice to stop` | 605–608 | **F1** |
| V7 | pending, the voice goes idle: said if still turning, else logged `dropped: the turn ended before the voice stopped` | 831–840 | **F1** (input to P: speech finished) |
| V8 | said: logged with the text and `(prepared)` / `(not prepared — synthesising now)`; then the latency is logged once (`_ttsLatency`) | 614–624, 820–827 | **F1** |
| V9 | leave or Recalibrate: `stopSpeaking`, only if this run said something | 627–633 | **F1** |

R3: every Timer `running` (687, 697, 716, 725, 745, 769, 790, 799, 811) and every Connections `enabled` (822, 833, 847, 856) is ANDed with `active && _running`. Goes: D (the driver's timers stop when inactive).

## 3. Timers

All are `property Timer`. The tests reach each by name (`tst_setup_trunk.qml:181–182`).

| Timer, TR | Interval | Repeat | Runs while | On trigger | Goes |
|---|---|---|---|---|---|
| `_introTimer` 684 | `introStart` 1500 | no | intro and `guide.ready` | `_introTimer.onTriggered` → `_startStandTall` | D; the decision P |
| `_readyTimer` 694 | `tick` 100 | yes | standTall/ready, both sensors | `_readyTimer.onTriggered`: reasons → reset and `_waitTick`; else count → settle | D; P |
| `_settleTimer` 713 | `settle` 1000 | no | standTall/settle | `_settleTimer.onTriggered` → hold (`_holdMs` 0, `_waitLogMs` primed) | D; P |
| `_holdTimer` 722 | `tick` 100 | yes | standTall/hold, both sensors | `_holdTimer.onTriggered`: reasons → empty the bar; else fill → `_captureStandTall` | D; P |
| `_demoPauseTimer` 742 | `demoPause` 600 | no | demo, `_anim` pause | `_demoPauseTimer.onTriggered`: `_anim` back, `guide.trunkAnimate(0,0,true,demoTurn)` | D; **F2** |
| `_armTimer` 766 | `tick` 100 | yes | a turn's arming | `_armTimer.onTriggered`: still and heading finite for `arm` → `_arm` | D; P |
| `_turnTimer` 787 | `tick` 100 | yes | a turn, armed or turning | `_turnTimer.onTriggered` → `_turnTick(interval)` | D; P |
| `_nextTimer` 796 | `next` 800 | no | substage passed | `_nextTimer.onTriggered` → `_startTurn` away or through | D; P |
| `_retryTimer` 808 | `retry` 2500 | no | substage retry | `_retryTimer.onTriggered` → `_startStandTall` or `_armTurn` | D; P |

The `_waitLogMs` priming (381, 462, 717; zeroed at 509) makes the first wait log at once: D, with the log (**F3**).

## 4. Calls out

| Kind | Call, TR line | Goes |
|---|---|---|
| Sensor, solve | `calibrateTrunk("pelvis")` 417, `calibrateTrunk("thorax")` 418. Each applies its A/M when valid; it can apply one sensor's and fail the other's | D performs P's "solve now"; the commit **F7** |
| Sensor, clear | `clearFunctionalCalibration()` 321, `clearCalibration()` 322, both sensors, via `_clearSensors` | D performs; the request **NO HOME F5** |
| Sensor, read | `gyroDps` 394, 408, 490; `longAxisFromUpDeg` 392, 408; `trunkHeadingDeg` 480 (NaN = no reading); `imuConnected` 312–313, 849, 858; `trunkCalibration` 292 (restore) | D taps it and hands P the summary; restore **F6** |
| Recorder | `recording` 273, 353, 359; `begin()` 273; `currentStep` 354; `stepStart` 376 (standTall, each attempt), 508 (a turn, at arming); `stepEnd` 355, 422, 650; `attachResult("standTall", …)` ×2 420–421; `checkTurn(dir, !rightHanded)` 647; `end()` 359. With no recorder, a stand-in failure record 648–649 | D performs; P requests start a step and check the turn. The rest (begin unless recording, stepEnd, attachResult, end, closing the open step): **NO HOME F4** |
| Speech | `ttsController.speak` 623; `stopSpeaking` 604, 632; `ttsReady` 598; `ttsActive` 599, 602, 605, 835; `isPrepared` 617; `lastTtsLatencyMs` 825. Rules in `_speak`/`_say`: skip with no voice or not ready; skip start when busy; turn-back stops the start line or a busy voice, then waits for idle (`_turnBackPending`), dropped if the turn ended. `_stopSpeech` on leave or Recalibrate, only if this run spoke | cue due: P (cue codes; speech finished is an input). The call and its words: **NO HOME F1** |
| Guide | `guide.trunkPose` 274, 301, 377, 451, 463; `guide.trunkAnimate` 452, 748; `guide.cancel` 364; `guide.ready` 688; finished through the base | **NO HOME F2** |
| Log | `_logStage` through `_go` 255; `_log` 411, 582, 601, 603, 607, 618, 825, 838; `_restoreNote` 304; the wait lines with both sensors' gyro and long-axis numbers 402–413 | D per BRIEF-C-11b.1 check 2; the design's driver row does not name it: **F3** |
| Navigation | none. `completed()` 678 tells the host; the page records the outcome (`CalibrateTrunkPage.qml:120`) and clears it on `onDoneChanged` (124–131) | D exposes the signal; the host Q |
| Words | `qsTr` at 310–313, 388, 393, 395, 433–434, 488, 491, 518–519, 546, 637–638, 649, 665, 667, 773, 850, 859; `Spoken.*` 243, 245, 248; the C++ records' own `failure` text passed through at 433–434 and 665 | Q, verbatim, chosen by reason code (§4.8: "the words stay where they are") |

## 5. Paths that set or clear a calibration

- **Clear.** Its triggers:
  - `begin()`, which the host calls on entering the step when the trunk outcome is not done (`CalibrateTrunkPage.qml:101–103`), and on Recalibrate (107).
  - `reset()`.
  - Every stand-tall attempt.

  `_clearSensors` (317–324) runs `clearFunctionalCalibration` then `clearCalibration` on both sensors. It is reached from `_reset` (`on_Clearing`, so on every begin() and reset()), and from `_startStandTall` (375) on every stand-tall attempt: the first, each retry, and the return to stand tall after a backwards or swapped turn. Goes: through `ImuInstance`, which clears the Rig's record from C-6b.2. Who asks for it: **F5**.
- **Set.** `calibrateTrunk` per sensor (417–418), on a full hold. If one sensor passes and the other fails, the passing one keeps its A/M until the next attempt's clear (comment 372). Goes: D performs it; the commit **F7**.
- **Not cleared:** stop() (282–286) and `_fail` (362–370) leave both sensors as they are, so a passed stand tall's A/M stays through a turn failure, a link loss or a leave. restore() (290–305) writes nothing to a sensor.
- **restore() marks done** whatever the solves hold. `solved` only goes into the log note (304), and `turnResults` is not reset. As is.

## 6. What a host, another routine or a test reads

- **Inputs, set by `CalibrateTrunkPage.qml:109–120` and by `tst_setup_trunk.qml` `hostRoutine` (153–157):** `active`, `guide`, `segments`, `rightHanded`, `pace` (all from the base), `trunkCeremony` 129, `speakInstructions` 131, `ttsController` 136. Goes: D takes them (the recorder from C-14 through `app.calibration`); the host's bindings Q.
- **Outputs:** `stage` 158, `substage` 162, `failed` 163, `message` 166, `messageKind` 167, `done`/`completed` (base), `holdProgress` 168, `turnDeg` 171, `turnPeakDeg` 172, `turnBackDue` 174, `waitReason` 178, `standTallResult` 180, `turnResults` 181, `spokenPhrases` 248.
  - The page reads stage, substage, holdProgress, done, waitReason, turnBackDue, message, messageKind and completed.
  - The tests read all of them, plus `pelvisImu` 183 and `thoraxImu` 184 (`TrunkGolfer.qml`).
  - `SetupSteps.qml:248` reads `Spoken.trunk`, not the routine.
  - Goes: D exposes the same names (§4.8). `waitReason` and `message` become reason codes plus Q words. `spokenPhrases` is Q.
- **Internal state 187–209:** `_stage`, `_sub`, `_message`, `_messageKind`, `_holdMs`, `_readyMs`, `_turnMs`, `_armMs`, `_backMs`, `_topMs`, `_turnRef`, `_turnDeg`, `_turnPeakDeg`, `_retryTo`, `_turnBackDue` and `_waitReason` go to P. `_waitLogMs` goes to D (**F3**). `_anim` is **F2**. `_saidStart`, `_turnBackPending`, `_spoke`, `_awaitLatency` and `_latencyCue` are **F1**. `_watching` (843, the stages a link loss fails) goes to P.
- **Functions:** `_go`, `_missingSensor`, `_reset`, `_resetState`, `_fail`, `_startStandTall`, `_sensorReasons`, `_captureStandTall`, `_startTurn`, `_armTurn`, `_clearTurn`, `_stillReasons`, `_arm`, `_turnTick`, `_turnBackTick`, `_cueTurnBack`, `_checkTurn` and `_finish` go to P. `_heading` and `_fmt` go to D. `_clearSensors` is D (**F5**). `_closeStep` and `_endCeremony` are D (**F4**). `_waitTick` goes to D (**F3**). `_speak`, `_say` and `_stopSpeech` are **F1**. `_startLine`, `_turnBackLine`, `_waitingForTurn` and `_handHint` are Q words chosen by code.
- **By name (tests):** the 9 timers and the connections `_ttsLatency`, `_ttsIdle`, `_pelvisLink` and `_thoraxLink` (`tst_setup_trunk.qml:181–183`, TR6/TP3: nothing fires after stop). They go with the timers: C-11b.2 re-points the cases at the driver.
- **Connections:** `_ttsLatency.onLastTtsLatencyMsChanged` (820–827) and `_ttsIdle.onTtsActiveChanged` (831–840) are **F1**. `_pelvisLink.onImuConnectedChanged` (845–853) and `_thoraxLink.onImuConnectedChanged` (854–862) go to D, as P's link-down input.

## 7. Tests that cover it

- **`qml_ui.tst_setup_trunk`: 40 cases (47 functions with the `_data` rows' tables).** They are re-pointed in C-11b.2, never deleted.
  - The routine host (25): `test_TR1_passPath`, `test_TR2_standTallFailureNamesTheSensorAndRepeatsStandTallOnly`, `test_TR3_turnFailureRepeatsTheTurn`, `test_TR4_backwardsGoesBackToStandTall`, `test_TR5_wrongWayHintsAtTheHand`, `test_TR6_leaveAtEveryStep`, `test_TR7_restoreShowsTheFinishedRun`, `test_TR8_linkDropFailsTheRun`, `test_TR9_nothingStartsBeforeTheGuideIsReady`, `test_TR10_readyWaitsForUprightAndStill`, `test_TR10b_waitLogIsRateLimited`, `test_TR11_holdEmptiesOnASensorThatMovesOrTips`, `test_TR12_failedStandTallWaitsForUprightAgain`, `test_TR13_aFullTurnCompletesTheStepNoClickNoTimer`, `test_TR14_aTurnThatNeverComesBackTimesOutAndIsChecked`, `test_TR15_aTurnThatNeverStartsWaitsSayingSo`, `test_TR16_motionWhileArmingDoesNotArm`, `test_TR17_armedDriftDoesNotStartATurn`, `test_TR18_startAtArmingTurnBackAtSeventyDegrees`, `test_TR19_eachTurnAttemptSaysItsCuesOnceNothingElseSpeaks`, `test_TR20_settingOffOrNoReadyVoiceSaysNothing`, `test_TR21_leaveOrRecalibrateStopsWhatWasSaid`, `test_TR22_spokenPhrasesAreTheLinesAndThePreparedOnesAreLogged`, `test_TR23_theTurnBackCueRule`, `test_TR24_aStartLineStillPlayingIsCutOffByTheTurnBack`.
  - The page and flow (15): `test_TP1_trunkOnlySession`, `test_TP2_backFromCheckRestoresWithoutARerun`, `test_TP3_leaveMidStepNothingFiresAfter`, `test_TP4_trunkWithWg3_recordingBeginsAtTheArmStep`, `test_TP5_leavingSetupDiscardsTheCeremony`, `test_TP6_failBackNextThenRecalibrateMidHold`, `test_TP7_leaveMidTurnThenNext`, `test_TP8_golfTermsAndTheDirection`, `test_TP9_theSettingAndTheAppVoiceReachTheRoutine`, `test_TP10_settingOnPreparesEachPhraseWhenSetupIsCreated`, `test_TP11_settingOffAtCreationThenSwitchedOn`, `test_TP12_voiceNotReadyAtCreationPreparesWhenReady`, `test_TP13_aVoiceChangePreparesAgain`, `test_TP14_checkWithNoHeadingShowsTheMissingMark`, `test_TP15_aHeadingThatArrivesAfterCheckOpensIsShown`.
- `qml_ui.tst_setup_flow` (its trunk plan at 605) and `qml_ui.tst_setup_mounts` (`test_P2_trunkMountsBringTheTrunkSteps`) name the step in a plan. Neither runs the routine.
- `session_setup_lint_test` reads the source:
  - W1: every timer and connection here is gated on `active`.
  - W5: the recorder and tts members used are public.
  - W7: `pace` is only under `tests/`.
  - W11: the 6 mirrored gates, and `arm`, `returnStill` and `turnTimeout`, equal their constants.
- The maths it calls, not this file: `trunk_stand_tall_test`, `trunk_ceremony_file_test`, `trunk_turn_path_test`.
- C-9a's golden trace is to reach every id in §2.

## 8. Where each duty goes

In the last column of every table above and below (the key is at the top).

## 9. The recorder

Not this file's. See `docs/implementation/platform_refactor/duties/DUTIES-trunk_ceremony_recorder.md`; this file's calls on it are in §4.

## 10. The hands-free rules, as lines

- Stand tall starts only when both sensors are on (upright, ≤ 35° from up) and still (device gyro peak ≤ 10 °/s) for `ready` without a break: 694–711. Each retry starts at ready and waits again (814 → 374–383): never a blind re-run.
- A turn arms only when both are still for `arm` and the chest gives a heading: 766–784. Each retry re-arms the same way (815 → 458–465).
- The stillness signal is `gyroDps`, never `angularVelocityDps` (comment 211–222; reads 394, 490).
- On screen it names the sensor it waits for: `_sensorReasons` (386–398: "Hip sensor isn't upright" / "… is moving") and `_stillReasons` (486–493). It also says when no turn reading has come (546, 773), through `waitReason`. It logs the wait at most once per `waitLog` (402–413).
- The turn-away step is taught first, facing the screen: the guide demo and one line (444–454). Nothing is recorded during the demo. The recording begins at arming (508). The sensors start the step (529–534) and end it (550–558). A started turn that never comes back ends at `turnTimeout`. The verdict is shown once the golfer faces the screen again (643–671).
- **On page entry, as is:** the host calls `begin()` when it enters the step, if the trunk outcome is not done (`CalibrateTrunkPage.qml:101–103`; restore() otherwise). At that moment both sensors' calibrations are cleared (T1), the recorder begins unless it is recording, the guide is posed, and the intro clock starts once `guide.ready` (687–688). The stand-tall capture and each turn's recording wait for on-and-still (T4–T16). Whether that meets STANDING 7's "never on page entry" is **F12**.

## Grep hits that are not duties

| TR line | Text |
|---|---|
| 258 | `── Entry points (CalibrationRoutineBase: begin, stop, reset, restore) ────` (a section comment) |
