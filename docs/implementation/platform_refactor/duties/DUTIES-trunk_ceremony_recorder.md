Taken at `c8d3eb9d` (main), `src/Gui/imu/trunk_ceremony_recorder.h` (165 lines) and `.cpp` (459 lines). C-10a; read, not run.

# Duties of `trunk_ceremony_recorder.{h,cpp}`

`RH` = the `.h`, `RC` = the `.cpp`, both in `src/Gui/imu/`. The class is `TrunkCeremonyRecorder`, the QML context global `trunkCeremony` (`main.cpp:1756`).

The destinations are the trunk page's: P, D (the trunk driver), Q, S (the Session's door, C-14), X, and NO HOME Fn (`NOTES-C-10a.md`).

The design, §4.8 "The ceremony file", and §9 Q5 and Q14 (approved as recommended):
- The recorder's **taps and in-memory ceremony stay with the trunk driver** (D).
- **Its file write goes behind a repository**, the `Session`'s (S). The pool-thread write and the file name spelled in `src/Gui` go: A5 ×7 → 0. The A6 ×2 stay while the QML routine calls the recorder, unless lint A6 narrowed to P9's wording counts them out (C-V1, 25 Nov: the taps, the QML API and the `trunkCeremony` global stay until #291; C-14 moves only the file write, with #265's fix).
- The format `pinpoint.trunk_ceremony/1` does not change.
- The `trunkCeremony` global leaves for `app.calibration` at #291; until then it stays (C-V1).

## 1. Thresholds and constants

| Symbol | Value | Meaning | Goes |
|---|---|---|---|
| `kMaxSamplesPerLane` RH:86 | 120 000 samples a lane (≈ 10 min at 200 Hz, ≤ 5.8 MB) | Beyond it the tap drops and counts (`overCap`); one `ppWarn` per lane (RC:93–98) | D |
| The trunk roles RC:132; the wG3's two units RC:151–154 | `pelvis`, `thorax`; `leadForearm` (lower arm), `leadHand` (palm) | Which devices get a lane | D |
| `roleLabel` RC:43–46 | pelvis → "hips", else "chest" | Log words | D (log) |
| Step names, from `trunk_ceremony_file.h` | `standTall`, `turnAway`, `turnThrough`, `armRoutine` | n/a | n/a (format, unchanged) |
| Retry or write limits | none | One write attempt; no retry | S |

## 2. States and transitions

There are three states: empty (`m_recording` false, `m_pending` false), recording (RH:163), and held (pending, RH:164).

| From → to | Trigger | Guard | Lines | Goes |
|---|---|---|---|---|
| any → empty | `begin()` drops a held or recording ceremony (one `ppInfo`), then `clear()` | n/a | RC:104–106, 80–87 | D |
| empty → recording | `begin()`: taps the trunk Witmotions (`deviceForRole`), plus the wG3 if it holds `leadForearm`. Stamps `createdUtc`, `appVersion`, handedness (from `AthleteController`), `beganUs` | some trunk role has a device | RC:102–174 | D |
| → empty, returns false | `begin()` with no manager, or no trunk device | n/a | RC:107–110, 144–147 | D |
| recording → held | `end()`: drain, stop the taps, drain the tail, stamp the end; then, if a session is running, `write(dir)` at once | recording | RC:330–357 | D (hold); the write S |
| recording or held → empty | `discard()` (`SetupContext.qml:626`: setup left without a session) | either | RC:359–366 | D |
| recording → held → written or dropped | `onSessionStarted()`: still recording → `end()` first (warn) | n/a | RC:392–397 | S (the hand-over) |
| held → empty, dropped | `onSessionStarted()`, `solvesStillInForce()` false | n/a | RC:400–405, 371–390 | S (BRIEF-C-14 lists it) |
| held → empty, dropped | `onSessionStarted()`, no session folder (warn) | n/a | RC:406–411 | S (**F10**: a fourth drop) |
| held → empty, then written on a pool thread | `write(dir)` moves the ceremony out and `clear()`s first | n/a | RC:416–420 | S |
| steps | `stepStart(name)`: a new entry; closes an open one of the same name | recording | RC:176–191 | D |
| steps | `stepEnd(name)`: closes the last of that name | recording and open | RC:193–202 | D |
| steps | `mark(name)`: an instant step | recording | RC:204–214 | D. **No production caller** (grep: the hits are the Witmotion tests' own `drv.mark()`). X is not proposed: **F11** |
| results | `attachResult(step, map)`: under `result[role]` if role is pelvis or thorax, else replaces the result | recording or held | RC:216–228 | D |

The **"dropped" cases**:
- The header lists three (RH:61–63): `discard()`, a new `begin()`, and solves no longer in force.
- The code has two more ways to lose a ceremony unwritten. A session started with no folder (RC:406–411). And the app quitting while held: the destructor only stops the taps (RC:55–58).
- A failed write loses the ceremony from memory too: it was cleared at RC:419 before the write (RC:448–450).
- These are **F10**.

`solvesStillInForce` (RC:371–390) keeps the ceremony when some trunk sensor it solved still carries that solve (the `calibratedAt` match on the live `ImuInstance::trunkCalibration()`), or when it solved nothing at all. Goes: S (BRIEF-C-14). Whether, after the Rig, it reads the Rig's record or the live sensor is not said (**F10**).

## 3. Timers

None. The samples arrive through each device's `ImuSampleTap` (`imu_sample_tap.h`), pushed from the I/O thread and drained on the GUI thread by `drain()` (RC:89–100). Drain points: `checkTurn` (RC:252), and `end()` twice (RC:336, 338). Goes: D.

## 4. Calls out

| Kind | Call, lines | Goes |
|---|---|---|
| Sensor, taps | `inst->ceremonyTap()` RC:142; `hm->ceremonyTap(unit)` RC:162; `tap->start(kMaxSamplesPerLane)` RC:167; `tap->take` RC:93; `tap->stop` RC:77. Device facts: `laneSerial`, `deviceId`, `outputRateHz`, `dataRateHz`, `imuConnected` (RC:137–142); `unitLowerArm`/`unitPalm`/`unitIdFor` (RC:153–158) | D |
| Sensor, A/M | `checkTurn` reads each trunk sensor's **current** `alignA()`, `mountM()` and `trunkCalibration()` (RC:277–281) | D (after the Rig, A/M are handed from the rig state, §4.8 step 2) |
| Manager and athlete | `ImuManager::deviceForRole` RC:133, 151; `AthleteController::currentHandedness` RC:114 | D |
| Handedness, two sources, as is | The file's `handedness` is stamped from `AthleteController` at `begin()` (RC:114). `checkTurn` judges with the caller's `leftHanded` (`TrunkRoutine.qml:647`, `!rightHanded`, the page's `ctx.leftHanded`). Nothing checks the two agree. A replay (C-17) reads the file's | D (both) |
| The turn check | `tcf::turnWindowStats` RC:292, `tcf::runTurn` RC:305, `tcf::turnRecordToMap` RC:306 (the maths in `src/IMU/trunk_ceremony_file.h`, called) | D performs P's "check the turn" |
| File write | `write()` RC:416–459. On `QThreadPool::globalInstance()->start` (RC:424; not `QtConcurrent`). The name `SessionFiles::trunkCeremonyFileName()` RC:426 (`trunkCeremonyFileName`); `AtomicWriteOptions::intoExistingFolder()` RC:428. If the file exists, the earlier one is **copied aside** to the first free `trunkCeremonyAsideFileName` n ≥ 2 (RC:429–446): read in 1 MiB chunks through `atomicWrite(aside` (RC:439), never moved first (R-PERS-2). Then `tcf::toJson` and `atomicWrite(path` (RC:448) | S. The pool thread and the spelled name go (C-14) |
| Log | `ppInfo`: begin dropped (105), recording lanes (170), the two turn window lines (293), the turn verdict (320), ended (347), discarded (363), dropped at session start (401), wrote (455). `ppWarn`: no manager (108), no trunk device (145), over cap (96), stepStart/stepEnd/mark/attachResult ignored (179, 197, 207, 220), checkTurn refused (233), end not recording (333), session started while recording (395), no folder (408), aside failed (445), write failed (449) | D for the taps' and check's lines; S for the write's (**F3** for the split) |
| Session | the resolver `m_sessionDir` (RH:93, set at `main.cpp:1022–1024`: the running session's `activeSessionDir()` or ""); `onSessionStarted` connected to `SessionController::sessionStarted` at `main.cpp:1025–1026` | S |

## 5. Paths that set or clear a calibration

None. The recorder sets and clears no calibration. It reads A/M and `trunkCalibration` (RC:277–281, 381–385) and stores whatever `attachResult` is handed.

## 6. What a host, another routine or a test reads (its QML API)

| Member, RH line | Read or called by | Goes |
|---|---|---|
| `recording` (Q_PROPERTY) 79 | `TrunkRoutine.qml` 273, 353, 359; `CalibrateArmPage.qml:93, 97`; `SetupContext.qml:626`; tests | D, through `app.calibration` (Q14) |
| `pendingWrite` (Q_PROPERTY) 80 | `CalibrateArmPage.qml:92`; `SetupContext.qml:626`; `tst_setup_trunk.qml:1182` | D |
| `laneCount` (Q_PROPERTY) 81 | no QML reader (the other `laneCount` hits in QML are unrelated) | D; nobody reads it |
| `currentStep` (Q_PROPERTY) 82 | `TrunkRoutine.qml:354`; `CalibrateArmPage.qml:97` | D |
| `begin` (Q_INVOKABLE) 102 | `TrunkRoutine.qml:273`; `CalibrateArmPage.qml:93` | D |
| `stepStart` (Q_INVOKABLE) 103 | `TrunkRoutine.qml:376, 508`; `CalibrateArmPage.qml:94` (`armRoutine`) | D |
| `stepEnd` (Q_INVOKABLE) 104 | `TrunkRoutine.qml:355, 422, 650`; `CalibrateArmPage.qml:98` | D |
| `mark` (Q_INVOKABLE) 105 | none in production | D (**F11**) |
| `attachResult` (Q_INVOKABLE) 106 (**A6**: takes a `QVariantMap`) | `TrunkRoutine.qml:420–421` | D at #291 (C-V1); until then the QML routine calls it and the A6 line stays |
| `end` (Q_INVOKABLE) 108 | `TrunkRoutine.qml:359` | D |
| `discard` (Q_INVOKABLE) 110 | `SetupContext.qml:626` | D |
| `checkTurn` (Q_INVOKABLE) 133 (**A6**: returns a `QVariantMap`; its keys RH:118–130 are a contract C-9a's fixtures hold) | `TrunkRoutine.qml:647` | D at #291 (C-V1); A6 stays until then |
| `onSessionStarted` (slot, RH:137); `stateChanged` (signal, RH:140); `setSessionDirResolver` RH:93 | `main.cpp:1022–1026`; `stateChanged` notifies the four properties | S for the hand-over and the resolver; `stateChanged` D |

**The nine lint findings.** I derived these by reading `architecture_lint_test.cpp`'s A5 and A6 rules; I did not run the lint. A5 ×7, all in RC:
- the `#include "../../Export/session_files.h"` at 22 (a repository header outside its owner);
- `QDir` at 425;
- `QFile::exists` at 429 and 433;
- `QFile earlier` at 437;
- `atomicWrite` at 439 and 448.

A6 ×2, in RH: 106 (`attachResult`) and 133 (`checkTurn`). On `architecture_allowlist.txt` they are at lines **83** (A5, 7) and **139** (A6, 2), not BRIEF-C-14's 86 and 142.

## 7. Tests that cover it

- **No test exercises `TrunkCeremonyRecorder` itself.** No C++ test includes it.
- The QML tests use `FakeTrunkCeremony.qml` (`src/Gui/tests/setup/fakes/`), which mirrors its API: `qml_ui.tst_setup_trunk` (TR*, TP1, TP4 the wG3 arm-step begin, TP5 leaving setup discards) and `qml_ui.tst_setup_smoke` (context only).
- The format and the check it calls are covered without the device. `trunk_ceremony_file_test` (`src/IMU/tests`) covers the file round trip and the replayed check, "minus the device". `trunk_turn_path_test` (`src/Analysis/tests`) covers the ceremony "as TrunkCeremonyRecorder holds it", through `runTurn`.
- `session_setup_lint_test` W5 checks every member QML uses is public. `architecture_lint_test` checks A5 ×7 and A6 ×2 against the allowlist.
- C-14's `ceremony_store_test` will be the first test of the write, the aside copy and the drops.

## 8. Where each duty goes

In the last column of each table.

## 9. The recorder's parts, as the brief lists them

| Part | Lines | Goes |
|---|---|---|
| Its taps | RC:118–128, 130–167, 74–78, 89–100 | D (§4.8) |
| Its in-memory ceremony (lanes, steps, results, stamps) | RH:161–164; RC:112–115, 168, 176–228, 339–342 | D (§4.8) |
| The session-folder resolver | RH:91–93; `main.cpp:1022–1024`; used at RC:354, 406 | S (the Session knows its own folder) |
| The hand-over at session start | RH:136–137; RC:392–414; `main.cpp:1025–1026` | S (§7: "handed to the session that starts after it") |
| The pool-thread write | RC:416–459 | S, the pool thread goes (C-14) |
| The "earlier ceremony kept aside" copy | RC:429–446 (copy, then rename over; never remove first). **If the copy fails, it only warns (445) and the new write replaces `trunk_ceremony.json` (448): the earlier ceremony is lost** | S; the loss is **F10** |
| The three "dropped" cases | `discard()` RC:359–366; a new `begin()` RC:104–106; solves not in force RC:400–405. Plus no folder (406–411) and quitting while held (55–58) | S for the session-start drops; D for `discard` and `begin`; **F10** |
| The QML API (A6 ×2) | RH:79–82, 102–110, 133 | D through `app.calibration` at #291 (C-V1); the A6 ×2 stay until then, unless the narrowed A6 counts them out |
| `checkTurn` | RC:230–328 | D (it needs the lanes); P requests it |

## 10. The hands-free rules, as lines

The recorder decides nothing for the golfer. Its share of the rules:
- A turn step's window opens when the routine calls `stepStart` at arming, never at the demo (`TrunkRoutine.qml:508`). `checkTurn` judges the whole open step from that point to now (RC:255–267), clamped to the last sample of each lane, so a replay of the file sees the same window.
- Recording begins at the first calibrate step: the arm's, with a wG3 (`CalibrateArmPage.qml:93`), else the trunk's (`TrunkRoutine.qml:273`).

## Grep hits that are not duties

| Line | Text |
|---|---|
| RH:44 | `tapped in HmSessionWorker::writeSample, only samples with a mapped host time (as its ring).` |
| RH:57 | `session starts (SessionController::sessionStarted → onSessionStarted), it is written into the` |
| RH:92 | `(SessionController::running + ShotProcessor::activeSessionDir) so this class needs neither.` |
| RH:136 | `SessionController::sessionStarted — writes a held ceremony into the session folder.` |

The RC hits at 426, 432, 439 and 448 are duties: `trunkCeremonyFileName`, `trunkCeremonyAsideFileName`, `atomicWrite(aside` and `atomicWrite(path`, in §4 and §9.
