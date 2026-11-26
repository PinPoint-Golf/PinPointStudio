# The Rig and calibration: a design proposal (platform refactor, Stage C)

**Status: approved, 24 Nov 2026.** The Solution Architect approved the design and every recommendation in §9 as written, the information-model changes of §8 among them[^sa24-rig]. Stage C builds it, item by item, in §10's order, except the calibration procedures in C++ (§4.8), which are deferred to #291[^c-v1].[^hard]
It answers Stage C of the platform plan[^plan-c] and the plan's Q2[^q2]. This is the third draft: it takes the
independent review of 24 Nov 2026 (round 1: 8 blocking findings, 12 notes; round 2: 3 blocking, 7 notes), whose evidence is copied into Appendix A so the
page stands on committed sources.

**How to read it.** A sentence with a footnote, or a row that cites Appendix A, states something already ruled, or
already in the information model (IM) or the code. Everything else in §2 to §11 is design, and each section that
holds design says so in its first line: "(proposed, for the Solution Architect)" covers every sentence of that
section that carries no footnote. Proposed ids and rule numbers are placeholders; the Solution Architect assigns the
real ones.

Code facts are at `a5089848` (main, 24 Nov 2026). Stage C is built after Stage B, Part 1, which moves files this
page names (`src/Vocabulary/Common`, `Vocabulary/Recording`)[^q15][^plan-b]; §6's checks are re-run at Stage B's
close, before C-1.

---

## 1. What is ruled already

- The Rig (OBJ-RIG) is a domain entity that owns which cameras and sensors there are, where they are, and their
  intrinsic and extrinsic calibration, versioned over time. It is not owned by a device or a shot[^im23].
- Each Recording references the rig state it was captured with[^im23][^im22].
- A calibration *procedure* belongs to Acquisition; its *result* is a source fact owned by the Rig[^im23].
- Acquisition, Diagnostics and Coaching own nothing persistent; Domain + Application owns and stores everything, and
  hands the other contexts "rig state" as typed input[^im2].
- A context reaches Domain + Application only through its contracts (interfaces and value types), never through the
  owner classes[^rctx].
- Rig facts live today in SET-05, SET-09, FILE-SESS-11 and assumptions in code, and no new place may be added
  before this design[^im23].
- The studio profile is not a rig fact[^dec193]. The club-length prior (SET-05) is learned rig state, per machine,
  owned by the Rig[^dec153].
- `RigChanged` (EVT-15) is a decided event whose producer is the Rig; what it carries and recomputes is this
  design's to say[^evt15].
- The shot kind and the rig reference must not assume two kinds of Recording: an external upload will be a
  Recording with declared capabilities and no rig[^guard][^asy].
- Exposure and gain are per-stream capture facts in the Recording, and #183's fields are designed once, from the
  cabin comparison[^q4].
- #74 is a programme of its own. The Rig gives its results a home and does not absorb it[^plan-c].
- The swing document is additive by contract: readers ignore unknown keys[^schema-add].

## 2. Principles

(proposed, for the Solution Architect; each applies an IM principle named in its footnote)

| # | Principle | Applies |
|---|---|---|
| RP-1 | **The Rig is one object, at the application level, per machine.** It is created by the composition root and outlives every session. A session uses the rig; it never owns it | P3, P6[^p]; the studio profile is per machine too[^dec193] |
| RP-2 | **A Recording is self-sufficient.** It carries a copy of the rig facts it was captured with, so analysis never reads the current Rig, a settings store or another machine's files | R-REC-4[^rrec]; the `analysisContext` precedent[^im22] |
| RP-3 | **A rig state is a value with an identity.** A change makes a new value; nothing rewrites an old one, and nothing rewrites a past Recording | P1[^p]; the plan[^plan-c] |
| RP-4 | **Only what describes the capture set-up is versioned.** Labels, machine-wide preferences and learned state are the Rig's too, but changing them makes no new rig state | this design |
| RP-5 | **Absence is never an answer, and a Recording never states what was not so.** "No rig block" (an old swing), "no rig" (an upload) and "this rig" are three written states, none derived from the shot kind. The block lists the devices that have a stream in the shot, not the devices the machine has or the session enabled | the guard[^guard]; R-REC-8's rule that an old file is classified, not backfilled[^rrec8] |
| RP-6 | **A device is named by typed keys, and each persisted map keeps its own spelling.** No setting is migrated or lost | P7[^p] |
| RP-7 | **A procedure decides; a view shows.** Thresholds, maths and step transitions are C++ that takes time and samples as inputs, so a recorded run replays exactly. QML binds and words. Not met by Stage C: the calibration routines keep their logic in QML until #291[^c-v1] | P2, P9[^p] |
| RP-8 | **An assumption is written down as an assumption.** "Fixed cameras are square" and the uncalibrated down-the-line yaw σ become stated entries in the rig state, with their method, never silent constants | the camera design's "degrade explicitly"[^ccd]; #178[^i178] |
| RP-9 | **Stage C changes no analysis input.** The job builder reads what it reads today, so no shot goes stale and nothing is re-run | the plan's "Re-run: none"[^plan-c] |
| RP-10 | **The `rig` block is switched on only when its contents are complete.** No swing is ever written with a block that lacks a record the Rig will later hold | RP-5; a recorded input is never rewritten[^p] |

## 3. What exists today

Facts, counted at `a5089848`. Each row's evidence is the named file or Appendix A.

| Thing | Where | Count |
|---|---|---|
| The Rig | no type. `recording.h` says "There is no rig reference yet"; a Recording keeps each camera stream's `perspective`, `mirrored` and `fixedInPlace`[^code-rec] | 0 |
| `camera*` properties of `AppSettings` | `cameraExcluded`, `cameraTargetFps`, `cameraTriggerMode`, `cameraRoi`, `cameraPerspective`, `cameraIsMirrored`, `cameraBallRoi`, `cameraPreroll`, `cameraSyncEnabled`, `cameraFixedInPlace`, `cameraAlias`, `cameraExposureUs`, `cameraTuning`, and `impactPipRect` (`camera/impactPip`)[^code-set] | 14 |
| `imu*` properties of `AppSettings` | `imuExcluded`, `imuAlias`, `imuCalibration`, `imuRoles`, `imuPlacement`, `imuOutputRateHz`, `imuFusionMode`, `imuMountOrientation`, `imuAutoConnect`, `imuAutoReconnect`, `imuSaveCalibrationToFlash`, `imuDefaultFusionMode`, `imuOrientationFilter`[^code-set] | 13 |
| What the sensor keys really are (Appendix A, table A2) | `imuRoles` maps a **placement key** to a role: a Witmotion's bare device id, or a HackMotion lane, `<id>#lowerArm` or `<id>#palm`. One claim per role is enforced (`imu_roles::claimRole`). `imuExcluded` is a list of bare device ids. `imuAlias` is keyed `description\|deviceId`. `imuPlacement` is a derived, read-only view of `imuRoles`. `imu/mountOrientation` and `imu/calibration` are loaded and never read or written by anything else | 3 spellings; 1 derived; 2 unused |
| What "excluded" means (table A2) | `camera/excluded` and `imu/excluded` are machine-wide switches. Each session is seeded from them and then decides for itself which devices it captures with; its own choice is never written back | 2 |
| `ppcp/*` | `studioName`, `pairings` (SET-09)[^dec140]; also `phoneAlias`, `phoneNames`, `torchDuringCapture`, which SET-09 does not name. `ppcp/pairings/<id>` holds each pairing's key material and nothing else ("the 32 raw bytes of PRK as hex", `ppcp_pairing_store.cpp`). None of the five is an `AppSettings` property | 5 |
| SET-05 | `analysis/clubLenPrior`, keyed `athlete uuid + "\|" + club + "\|" + face-on camera key`, read only for a fixed face-on camera, written on the live path after the analysis by `ShotProcessor` through `AppSettings`[^dec153] (table A2) | 1 |
| A worn sensor's alignment | A and M live on the sensor's live object for the run of the app. Nothing restores them at launch. The live object clears them itself (`zeroOrientation`[^zero], the routines' resets), and a HackMotion's calibration dies with its link (table A2)[^code-hm] | per sensor |
| FILE-SESS-11 | `<session>/trunk_ceremony.json`: the raw samples of every sensor in the ceremony, the step boundaries and each step's result, so the solve replays offline. Written by `TrunkCeremonyRecorder` on a pool thread[^code-cer][^trunk]. **No real file exists**: 0 on the share and 0 on the Mac; the test builds a synthetic one (table A3) | 1 writer; A5 ×7, A6 ×2; 0 files |
| Open-coded device keys (table A1) | `camera_instance.cpp` ×2, `camera_manager.cpp` ×1, `camera_manager.h` ×1, `imu_role_map.h` ×1, `resource_monitor_controller.cpp` ×2, `shot_processor.cpp` ×1. The plan counts 7[^c-key]; `camera_manager.h:216` is the eighth | 8 |
| Places that know a key's shape without building it (table A1) | `camera_manager.cpp:85`, `imu_manager.cpp:183`, `imu_role_map.h:110`, `camera_roi.h:47` (`<cameraKey>#impact`), `capture_context.cpp:348`, `tst_setup_sensors.qml:313` | 6 |
| Perspective types (table A1) | `recording::Perspective` (the one mapping), `CameraInstance::Perspective` (a `Q_ENUM`), a mirror for the set-up tests (`setup_enum_standins.h`), and two bare constants: `kImpact = 4` (`app_settings.cpp:36`) and `pp_camroi::kImpactPerspective` (`camera_roi.h`) | 2 types, 1 test mirror, 2 constants |
| QML that names the perspective enum (table A1) | `CameraInstance.FaceOn` and its siblings: about 120 uses in 19 files | 19 files |
| Calibration QML | 3,978 lines in 10 files[^c-calib]. The state machines are `TrunkRoutine.qml` (863), `HackMotionArmRoutine.qml` (759) and `WitmotionArmRoutine.qml` (553), all on `CalibrationRoutineBase.qml` (188). `ImuCalibrationFlow.qml` (502) holds no state machine since the session-setup refactor: it picks and hosts a routine[^wizard] | 3 machines + 1 base |
| Calibration maths already in C++ | `src/IMU/imu_calibration.h`, `trunk_stand_tall.h`, `trunk_ceremony.h`, `trunk_ceremony_file.h`: Rules ring, Acquisition context[^ring] | 4 headers |
| Per-stream rig facts already in each swing | `streams[].source.serial`, `streams[].setup` (`perspective`, `mirrored`, `fixedInPlace`, `ballDetection`), `streams[].device` (a sensor's calibration snapshot)[^schema] | every captured swing |
| Assumptions in code | a fixed camera is square to the target line; `tuned::dtlCamera::kDtlYawSigmaUncalDeg` (`pp_tuned_constants.h`)[^im23] | 2 |

## 4. The design

### 4.1 The Rig object

(proposed, for the Solution Architect)

`Rig` is a Domain-model entity (`src/DomainModel/rig.*`), one per machine. The Mac and the cabin PC each hold their
own. It is the camera design's "Bay": the persistent, named physical set-up a session attaches to[^ccd]. One name,
the Rig, is used from here on, and the camera design's "per-bay store" is the Rig's store.

It has two parts with different rules (RP-4):

| Part | Holds | A change |
|---|---|---|
| **The rig state** (versioned) | Per camera, by its device key: perspective, mirrored, fixed in place, the hitting-area box, its calibration records. Per sensor placement, by its placement key (§4.6): the role, and the alignment record held for the run (§4.2) | makes a new rig state and emits `RigChanged` |
| **The Rig's other facts** (not versioned) | Display aliases; whether each device is enabled on this machine (today's `excluded` lists); the learned club-length prior (SET-05)[^dec153]; by this design also `ppcp/studioName`, which Stage F moves (§4.3) | is saved; no new state, no event |

**One writer.** Only `Rig` changes either part, by its own commands, and only `Rig` saves, through `RigRepository`
(an interface in the Domain model; its adapter in `src/Stores`)[^p]. Today's writers (`CameraManager`,
`CameraInstance`, `ImuManager`, `ShotProcessor`, QML through `appSettings`) call the Rig's commands instead.

**Commands** (the names are placeholders): `assignPerspective(camera, perspective)`, `setMirrored`,
`setFixedInPlace`, `setHittingArea`, `clearHittingArea`, `claimRole(placement, role, facts)`, `releaseRole`,
`commitCalibration(record)`, `clearCalibration(subject, kind)`; and, for the unversioned part, `setAlias`,
`setEnabledOnThisMachine(device, bool)`, `learnClubLength(key, value)`. There is no `setPlacement` and no
`setMountOrientation`: the first is derived from the roles and the second is unused (Appendix A, table A2).

**Invariants the Rig keeps.** Both exist today, and the Rig takes them over unchanged:

1. **Impact is exclusive.** Assigning it to one camera clears it from every other
   (`CameraManager::assignPerspective`)[^code-cam].
2. **One claim per role.** A second claim of a role is refused or displaces as `imu_roles::claimRole` decides today
   (Appendix A, table A2). The rule needs hardware facts (which device is which instrument, which is present), so
   `claimRole` is handed them as a typed value; the Rig never asks a device. The role rules of `imu_role_map.*`
   (`claimRole`, `loadRoles`, `placementFromRoles`, the role names and the legacy-slot map) move from `src/Gui/imu`
   to `src/IMU` so the Domain model can use them (§5, §6 rule 4). `sensorDisplayName` and `stripDeviceIds` do
   **not** move: they return display labels ("HackMotion wG3", "sensor"), and the Rules ring holds no golfer-facing
   words[^ring]. They stay in the Adapters ring, behind `app.rig`; `sensorDisplayName` is also one of the
   shape-aware sites C-1 rewrites.

Stage C adds no other invariant. The camera list has no fixed length, so a third camera is one more entry (#180).

**An unknown perspective code is kept.** Today `perspectiveFromCode` maps a code it does not know to `None`
(`recording.h`). The Rig stores the code as an integer beside the enum and writes it back unchanged, so a file from
a newer build that knows a sixth perspective is not flattened. This is new behaviour, limited to the Rig and the
block.

**The hitting-area box.** Today a stored box is applied only while its camera is fixed in place, and is kept while
it is not (table A2). The rig state holds the box and the fixed flag side by side, both in the digest. A Recording's
block carries `hittingArea` only for a camera that was fixed at capture.

**Lifecycle.** The composition root creates the Rig at launch and it lives for the application's lifetime. It has
no states. On a machine with nothing stored it starts empty: no cameras, no sensors, a new `RigId`. The state loaded
at launch, which holds no sensor alignment (§4.2), is the state in force, and no `RigChanged` is emitted for it.

**Thread and failure.** The Rig lives on the owner thread, the GUI thread in the app[^p]. Its commands and its
atomic save run there; the file is a few kilobytes. A save that fails is logged with `ppWarn`, the state in memory
stands, and the next command saves again. A command is never refused because the disk failed.

**A view reaches it** through `app.rig`: a presenter in the Adapters ring that exposes typed gadgets and list models
(cameras, sensors, and the derived placement view that `imuPlacement` is today) and forwards commands. No view reads
the Rig's file or the settings keys it replaced[^p]. Only the composition root, `app.rig` and the store include
`rig.h` (a lint check, C-4).

**Acquisition reaches it** in two directions, both by contract (§5, §6): devices are handed the current rig state as
a typed value, and a driver hands back a calibration record or a clear.

### 4.2 Identity and the versioned rig state

(proposed, for the Solution Architect)

| Identity | Representation | Minted | Immutable |
|---|---|---|---|
| `RigId` (ID-RIG's first half) | a uuid | by the Rig, the first time a machine has none stored | yes |
| `RigStateId` (its second half) | a digest of the rig state's canonical bytes | computed whenever the versioned part changes | yes: it is a function of the content |
| `RigRef` | (`RigId`, `RigStateId`) | n/a: composed | yes |

A digest is recommended over a counter (open question 3). The same set-up has the same id wherever and whenever it
is computed; an id is never minted for a state no Recording used; and putting a camera back as it was gives back the
earlier id, which is true: it is the same rig state. The canonical bytes fix how every number is written, so the Mac
and the cabin PC compute the same digest; C-3's test pins a fixture's digest as a literal and runs on both.

The rig state also carries `sinceUtc`, the time the state came into force. It is outside the digest.

**`RigChanged` (EVT-15).**

| | |
|---|---|
| Producer | `Rig`, from any command that changes the versioned part; once per command, after the save |
| Carries | the new `RigRef` |
| Recomputes | nothing. No derived fact reads the current Rig. A Recording captured after it references the new state; every earlier Recording keeps its own copy |
| Not emitted for | an alias, an enabled flag, a learned prior; a command that leaves the digest unchanged; the state loaded at launch |

A worn sensor is calibrated at each wearing, so a session will usually begin with one or two `RigChanged` events.
That is intended: the alignment in force is part of the state the shots were captured with.

**A worn sensor's alignment lasts exactly as long as it does today.** It is a `sensorAlignment` record in the
current rig state and in every Recording captured under it. It is **not** saved to `rig.json` and never re-applied
at launch, because nothing restores one today (table A2) and restoring it onto a re-strapped sensor would be a
behaviour change. **Every path that clears a sensor's calibration today calls `Rig::clearCalibration`**:
`ImuInstance::zeroOrientation`[^zero], the routines' reset paths, and a HackMotion's link loss or "calibration invalidated"
signal. The sensors' duties inventory lists each one. So the Rig's record and the live sensor never disagree.

A hitting-area or perspective edit is a command at the end of the edit, never per mouse move.

### 4.3 What the Rig holds, and what it does not

(proposed, for the Solution Architect: SET-09's "…" is classified here for the first time, from the code that reads
each key, Appendix A table A2)

| Key or file | Goes to | Why |
|---|---|---|
| `cameraPerspective`, `cameraIsMirrored`, `cameraFixedInPlace`, `cameraBallRoi` | the rig state | named in SET-09[^set09] |
| `imuRoles` | the rig state, per placement key | named in SET-09[^set09] |
| `cameraExcluded`, `imuExcluded` | the Rig, not versioned: "enabled on this machine" | a machine-wide preference that seeds each session. It is not what a shot was captured with (§4.5) |
| `cameraAlias`, `imuAlias` | the Rig, not versioned | labels[^set09] |
| `imuPlacement` | **not moved: it is derived.** `app.rig` serves the same read-only view from the roles, for its one reader | "a derived, read-only view of imuRoles, not storage" (`app_settings.h`) |
| `imuCalibration`, `imuMountOrientation` | **not moved: unused.** The two properties are deleted; the stored keys are left for Stage E. The mount checks are already part of the alignment record | nothing reads or writes either (table A2). SET-09 names both as rig facts[^set09]; C-0 corrects its row, which is listed in §8 |
| A worn sensor's alignment on its live object | a `sensorAlignment` record in the rig state, for the run | the result is the Rig's[^im23] |
| SET-05 `analysis/clubLenPrior` | the Rig, not versioned | learned rig state, per machine[^dec153]. The value used is already recorded in each Recording's analysis context[^im22] |
| FILE-SESS-11 | the result: a calibration record in the rig state. The raw ceremony: stays a file in the session folder (§4.8, open question 5) | the result is the Rig's[^im23]; the raw lanes are megabytes of evidence kept "with the session" for replay[^trunk] |
| "Fixed cameras are square"; the uncalibrated down-the-line yaw σ | a calibration record of kind `extrinsics`, method `assumed`, on the camera it describes | RP-8. The analysis keeps reading its constants in Stage C (RP-9) |
| `ppcp/studioName` | the Rig's label by this design, but **not moved in Stage C**: the phone link reads and writes it, and Stage F moves it | it stays in SET-09 with the rig facts[^dec193] |
| `ppcp/pairings` | which phones are paired: the rig state, when Stage F gives a phone a device key. The key bytes: never the Rig | SET-09 names it a rig fact[^dec140]; its content is a credential (open question 8) |
| `cameraExposureUs`, `cameraTuning`, `cameraTargetFps`, `cameraTriggerMode`, `cameraRoi` (the sensor crop), `cameraPreroll`, `cameraSyncEnabled` | **not the Rig.** Device configuration: settings (Stage E), recorded per stream in each Recording | exposure and gain are per-stream capture facts[^q4]; the others are how a stream was captured, not where a camera is |
| `impactPipRect` | **not the Rig.** A view preference: where the picture-in-picture box sits | `app_settings.h` |
| `imuOutputRateHz`, `imuFusionMode`, `imuDefaultFusionMode`, `imuOrientationFilter`, `imuAutoConnect`, `imuAutoReconnect`, `imuSaveCalibrationToFlash` | **not the Rig.** Device configuration and connection preferences: settings (Stage E) | the same |
| `ppcp/phoneAlias`, `ppcp/phoneNames`, `ppcp/torchDuringCapture` | **not decided here**: Stage F | SET-09 does not name them |

**The count the plan asks for.** Of the 27 `camera*` and `imu*` properties: 9 go to the Rig (5 versioned: four
camera facts and the roles; 4 not versioned: two enabled lists and two aliases), 2 are unused, 1 is derived and 15
are not the Rig's. Places that hold rig facts today: those 9 properties, the alignment on the sensors' live objects,
SET-05, FILE-SESS-11's results, 2 assumptions in code, `ppcp/studioName` and `ppcp/pairings`' membership half: 16.
After Stage C: 14 are the Rig's, and the two `ppcp` keys wait for Stage F, named here so they are not lost.
`AppSettings` loses 13 properties: 6 in C-5, 6 in C-6 (3 moved, 2 unused, 1 derived) and SET-05's in C-7.

### 4.4 Where the capture machine keeps its rig (the plan's Q2a)

(proposed, for the Solution Architect)

One file per machine, in the application's data folder: `rig/rig.json`, schema `pinpoint.rig/1`. It holds the
`RigId`, the current rig state without sensor alignments, the unversioned part, and which key groups have been taken
from settings. It is written by `Rig` through `RigRepository`, atomically, with the schema checked on read[^p].

A file, and not settings keys, on its own merits: calibration records are structured and carry uncertainty; one file
has one schema and one writer; and it can be schema-checked on read. The studio profile is the precedent for "per
machine" only: its fields are settings keys and only its two pictures are files (`studio_profile.cpp`).

It keeps no history. Every state a Recording used is in that Recording (§4.5).

**Tests never write a real machine's rig.** The store takes its folder from the composition root, with an override
for tests as the studio profile has (`PINPOINT_STUDIO_DIR`), and C-4's test proves that a test run on the Mac or on
the cabin PC's test lane leaves the real folder untouched. `rig.json` joins lint A5's table of file names, so no
other code spells it.

**From settings to the file, one group at a time.** There are three groups: the camera keys, the sensor keys and
the club-length prior. A group is copied into `rig.json` in the same build item that makes the Rig its writer, and
`rig.json` records that the group has been taken (as `imu/rolesMigrated` records an earlier move[^code-set]). Until
its item lands, `AppSettings` stays a group's one writer and the Rig does not hold it. So a group never has two
writers, and no copy goes stale before its cut-over. The copy reads the settings store directly, never an
`AppSettings` instance, each of which caches its own maps. The sensor copy takes the roles through
`imu_roles::loadRoles`, so a machine on which the earlier `imu/placement → imu/roles` move never ran still copies
correctly.

The settings keys are copied, never deleted or rewritten, in Stage C; Stage E's settings split removes them under
its own ruling. A machine with neither a file nor keys gets the empty Rig.

**Going back to an older build, and losing the file.** The cabin's baseline installer is an older build[^q20], so a
downgrade is a planned operation. Three cases follow, and the recommendation handles each (open question 6):

| Case | Without a guard | With a fingerprint of each group's keys kept in `rig.json` at the copy |
|---|---|---|
| The older build runs | it sees the keys as they were at the copy | the same: stated, accepted |
| The older build changes a key (a perspective; the prior, on every shot), then the newer build runs again | the change is silently ignored, because the group is marked taken | the keys no longer match the fingerprint: the group is copied again, with a `ppWarn` line saying an older build changed it |
| `rig.json` is deleted | the stale keys are copied again, silently | the same copy, logged; the `RigId` is new, which a Recording's block shows |
| `rig.json` was written by a newer build (a `pinpoint.rig/N` this build does not read) | n/a | the file is read-only to this build, as `refusesWrite` treats a swing: the Rig runs from the fields it knows, or empty, never saves over the file, and says so with `ppWarn` |

The fingerprint is taken over each group's decoded values, not the settings file's bytes. Its cost is that the last
writer wins for the whole group: when an older build changes one key, the newer build copies the whole group again
at its next launch, which undoes any edit the newer build had made to that group before the downgrade. One case: the
older build seeds an alias for a newly found camera, and a perspective changed under the newer build is reverted.
That is logged, and it is the price of never silently ignoring the older build (open question 6).

### 4.5 The rig reference in each Recording (the plan's Q2b)

(proposed, for the Solution Architect)

**Shape.** A Recording gains one value, `RigReference`, with three states:

| State | Means | On disk |
|---|---|---|
| `Unknown` | the swing was written before the Rig existed, or by a writer that does not know the rig | no `rig` block; also a block whose tag is newer than this build knows, or whose `state` it does not know |
| `None` | the Recording was made with no rig: an external upload | `"rig": { "schema": "pinpoint.rigState/1", "state": "none" }` |
| `Known` | captured on this rig state | the block below |

```json
"rig": {
  "schema": "pinpoint.rigState/1",
  "state": "known",
  "id": "<RigId>", "stateId": "<RigStateId>", "sinceUtc": "…",
  "cameras": [ { "key": "<device key>", "perspective": 2, "mirrored": false, "fixedInPlace": true,
                 "hittingArea": [x, y, w, h], "calibrations": [ { … §4.8's record … } ] } ],
  "sensors": [ { "key": "<placement key>", "device": "<device key>", "role": "pelvis",
                 "calibration": { … } } ]
}
```

The block's tag is `pinpoint.rigState/1`, its own, so `rig.json`'s shape and the block's can change apart.

**The spelling of each key in the block.** A camera's `key` is its `DeviceKey`, spelled as the camera maps spell it
(§4.6). A sensor's `key` is its `PlacementKey`, spelled as `imu/roles` spells it: `deviceId`, `deviceId#lowerArm` or
`deviceId#palm`. A sensor's `device` is its `DeviceKey` spelled with its description
(`persistedWithDescription()`, `description|deviceId`), so the block names the instrument without a lookup.

The block has no slot-letter `placement` field and no `mountOrientation`: a recorded input must not carry a copy of
another field, or a field nothing writes, because neither could ever be removed from old swings.

**The block lists what the shot was captured with.** The Rig knows every device the machine has set up. The block's
entries are exactly the rig devices that have a stream (or, for a sensor, a binding) in this shot's header. Neither
the machine's enabled flag nor the session's enablement is consulted: a camera that is enabled and was disconnected,
or delivered no frames in the window, has no stream and so no entry. The one header writer writes the streams and
the block from the same list, in the same call.

`stateId` is the digest of the Rig's whole state at that moment, so the block is a projection of that state. What
follows (open question 19): an equal `stateId` means the same captured set-up; an unequal one does not mean a
different set-up, because re-calibrating a sensor the shot never used changes the id. The digest cannot be recomputed
from one block, and no reader may check a block against its `stateId`.

**A stream that is not in the rig.** The block lists rig devices only. A phone clip in a captured shot has no entry,
because a phone has no device key before Stage F; a stream with no entry reads as "not in the rig", never as an
error.

The state is never worked out from `shotKind`, and no code switches on the pair. A device-only shot captured in the
studio is `Known` with no entries. A third kind of Recording adds a shot kind (Stage D's concern) and writes
`None`[^guard].

**An embedded copy, with its id.** The block holds the set-up and the records, a few kilobytes, never the ceremony's
raw samples. The reasons:

- Analysis runs on the Mac and on the cabin PC, and a swing is copied alone into the corpus; a library-level store
  would be absent in both cases[^q2].
- "Same Recording + same effective context → same job" holds only if the Recording carries what the job reads[^rrec].
- It is what the analysis context already does: athlete facts are recorded at capture so re-analysis never reads the
  current record[^im22].
- The id still lets a tool group Recordings by rig state without comparing blocks.

**Who writes it.** The one header writer, when the shot is created, from the capture-side snapshot, which reads
`Rig`'s current state once at the window freeze, as it reads the current athlete facts[^im22]. It is a recorded
input (P1): written once, never regenerated, and no late part changes it[^rrec].

| Document kind[^schema] | Rig block |
|---|---|
| Capture; analysis-only (the same header writer) | `Known` |
| Device-only (`writeDeviceOnlySwing`) | `Known`, no entries |
| Phone-only (`ensureSwingDocument`: schema, streams and summary only) | none: reads as `Unknown`, as today. Stage F decides |
| An archived stub | kept: the stub is the whole document with only `analysis` and `streams` thinned (table A4) |
| An external upload, when the coaching track builds it | `None` |

**Backward compatibility: no schema bump, and why that is safe.** The document's schema stays `pinpoint.swing/2`.

- The swing schema is additive by contract, and additive blocks have shipped before with no bump
  (`capture.club.lengthPrior`: "Additive — no schema-version bump")[^schema-add].
- The write guard looks only at the document's `schema` and `analysisContext`'s (`refusesWrite`,
  `swing_doc_writer.cpp`), so an older build still opens a new swing for writing.
- The independent review traced the whole path and found the block passes through untouched (table A4): "libppswing
  is schema-blind"; its encoder writes every member and its decoder keeps every key; the Python reader passes maps
  through; every PPS rewrite loads the whole document and edits only its own keys; and "a build from before Stage C
  also keeps the block when it re-analyses a newer swing, because this code is already in it"[^review]. So
  libppswing needs no change.
- A swing without the block reads as `Unknown`. No old swing is backfilled or rewritten.
- A block with a newer tag, or a `state` this build does not know, reads as `Unknown` and is kept verbatim by every
  writer. It does not make the document read-only, because the block is not an analysis input (open question 16).
  That holds only until a reader of the block enters the analysis input: the change that adds one also adds the
  block's tag to `refusesWrite`, or an older build would re-analyse without the calibration a newer build used and
  overwrite the newer result.

**Which copy is read.** `streams[].setup` and `streams[].device` are still written, from the same values, by the
same writer, and the job builder still reads them. The `rig` block is not in the analysis input stamp. So Stage C
makes no shot stale (RP-9). The overlap is the kind `capture.club` already is beside the analysis context: written
from the same value, as provenance[^im22]. The block's item tests that the two agree for every shot it writes.
Moving a reader onto the block (first, #74's calibration records) is its own information-model change, with a
stage-version bump and a re-run[^plan-c].

**Reading a past Recording's rig on another machine.** The reader returns the embedded block. It never looks up the
local Rig, and a `RigId` that is not this machine's is normal. An `Unknown` Recording gives analysis exactly what it
gets today: the per-stream set-up and the code's assumptions.

### 4.6 Typed device keys, each map with its own spelling

(proposed, for the Solution Architect)

Two value types in the Vocabulary ring (`src/Vocabulary/Rig/`): hashable, ordered, each with a null[^p].

| Type | Is | Built from |
|---|---|---|
| `DeviceKey` | one physical device | a camera: description, serial number, device id. A sensor: description, device id |
| `PlacementKey` | one thing that can hold a role: a `DeviceKey` plus an optional lane. A Witmotion has no lane; a HackMotion has two, lower arm and palm, so one device holds two roles | a `DeviceKey` and a lane, the lane spelled as `hm_unit_id.h` spells it[^schema] |

Each persisted map keeps today's spelling, byte for byte (table A2), so nothing is migrated:

| Map or field | Keyed by | Spelling |
|---|---|---|
| `camera/perspective`, `isMirrored`, `fixedInPlace`, `ballRoi`, `alias`; `camera/excluded` (a list) | `DeviceKey` | `description + "\|" + (serial, or the device id when there is none)`[^code-key] |
| `camera/roi` (not the Rig's) | a `DeviceKey` and a perspective | the camera spelling, plus `#impact` for the impact strip (`camera_roi.h`); the type offers this spelling so `camera_roi.h` stops building it |
| `imu/alias` | `DeviceKey` | `description + "\|" + deviceId`[^code-key] |
| `imu/roles` | `PlacementKey` | the bare `deviceId`, or `deviceId#lowerArm` / `deviceId#palm` |
| `imu/excluded` (a list) | a sensor's `DeviceKey` | the bare `deviceId` |
| SET-05 | (`AthleteId`, club id, the face-on camera's `DeviceKey`) | `athlete uuid + "\|" + club + "\|" + camera spelling` |
| The swing's `source.serial` | unchanged | as written today |
| A phone camera | `peer_id + source_id`[^ccd]: the type has room for it; Stage F builds it | n/a |

So a sensor's `DeviceKey` has two persisted spellings, with and without its description, and the type names them
(`persistedWithDescription()`, `persistedId()`); each map's reader and writer states which it uses. In the Rig and
in the block, a sensor entry is keyed by `PlacementKey`, so a HackMotion appears as two entries.

Sites: 8 open-coded builds → 0, and the 6 places that know a shape → 0.

### 4.7 One perspective type

(proposed, for the Solution Architect)

`recording::Perspective` is the one type[^plan-c]. It gets its own small header, so the rig state can hold it
without including the whole Recording (§6, rule 1). Its home is `src/Recording/perspective.h` today; if Stage B,
Part 1 has made `Vocabulary/Recording` by then[^plan-b], it goes there. `CameraInstance::Perspective`, `kImpact`,
`pp_camroi::kImpactPerspective` and the set-up tests' mirror in `setup_enum_standins.h` are deleted. The codes
stored in settings and swings do not change.

QML needs the enum by name. The header declares it to Qt's meta-object system with `Q_ENUM_NS`, in a small namespace
of its own that holds only the enum (`Q_NAMESPACE` must not be put on all of `recording`; the item greps all of
`src` for the new namespace's name first). The identity types already carry `Q_GADGET` in the same ring[^code-id].
The Adapters ring registers it. The 19 QML files change `CameraInstance.FaceOn` to `Perspective.FaceOn` by a
scripted rename. Nothing a golfer sees changes (§4.9).

`AppSettings::migrateImpactRoi` reads the perspective map at load and uses `kImpact`. Its marker,
`camera/impactRoiMigrated`, is set wherever it has run. C-5 keeps it working with a raw read of the key and the one
enum, and does not retire it. It is named for Stage E's list of settings keys to remove, so it is retired with
`camera/impactRoiMigrated` there.

Types: 2 → 1. Test mirrors: 1 → 0. Bare constants: 2 → 0.

### 4.8 Calibration procedures in C++

(proposed, for the Solution Architect)

**Deferred.** The typed record, the sink and step 1 below are built in Stage C. The procedures themselves (the step machines, their drivers, `app.calibration`, the routines as bindings and the real ceremonies as fixtures) are deferred to one calibration track after the platform programme, built with #266 and #270 (#291). Until then the 3,978 lines of calibration QML keep their thresholds, maths and transitions: a known breach of RP-7, of this section and of the rule that QML holds no rules and only binds, pinned by the three golden traces[^c-v1]. The two arm routines are also not hands-free until then, against UI-14: each starts on a timer when its page opens, and after a failure the golfer presses Recalibrate. That breach is accepted until #291, which builds #266's start on a still arm with the procedures[^arm-hands-free].

**The typed result.** One record, shaped on the camera design's calibration record[^ccd], for cameras and sensors
alike:

```
CalibrationRecord {
    subject       DeviceKey for a camera; PlacementKey for a sensor
    kind          intrinsics | extrinsics | focus | sensorAlignment
    method        factory | per_frame | solved | user_measured | estimated_online | assumed | deviceHeld
    parameters    typed per kind. sensorAlignment: role, A, M, the two mount checks, and the trunk solve's provenance
    uncertainty   mandatory, per kind; "not known" is a stated value, never an omission
    observedAtUtc
    evidence      optional: where the raw run is kept (a session id and a file name)
}
```

`assumed` and `deviceHeld` are additions to the camera design's five methods, and C-3 defines every kind and method Stage C writes, both of these included. `assumed` is RP-8. `deviceHeld` is the
HackMotion: its calibration lives in the device, is never persisted and dies with the link[^code-hm], so its record
says that the device reported "calibrated" and when, and holds no A or M.

`evidence` is best-effort. When one calibration serves two sessions in a run, the second session's shots point at a
ceremony file in the first session's folder, and the pointer dangles if that session is trashed. A reader treats a
missing file as "no evidence kept".

Stage C produces records of kind `sensorAlignment` and `extrinsics` with method `assumed`. Every other kind is
#74's, by its own stages[^ccd].

**How a result commits, in two steps of the build.**

1. *From the sensors cut-over (C-6b), before any procedure moves.* Every path that sets a calibration on the live
   sensor commits a record to the Rig: `ImuInstance::setCalibration`, `setFunctionalCalibration`,
   `setNominalCalibration`, `refineMountAboutLongAxis` (which changes A again after a commit, on two branches) and
   `calibrateTrunk`; and the HackMotion reporting "calibrated", as a `deviceHeld` record. Every clear path clears
   it: `clearCalibration`, `clearFunctionalCalibration`, `zeroOrientation`[^zero], and the HackMotion's link loss
   (Appendix A, table A2). The sensors' duties inventory, which comes before C-6a, is the authority for both lists. So the Rig's records are complete and true from the first build that has
   them, whoever runs the procedure.
2. *When each procedure moves to C++ (C-11 to C-13; deferred to #291[^c-v1]).* Its driver commits the record the step machine returned, and
   the live sensor is handed its alignment from the new rig state, as typed input. Which of today's duties on
   `ImuInstance` (solve, apply) moves where is settled by the duties inventory, not here.

**The sink.** `RigCalibrationSink` is a two-method interface among the Domain model's contracts (`commit`, `clear`);
`Rig` implements it; the composition root joins them. **Only the drivers and the device objects, which are in the
Adapters ring, call it.** A step machine is in the Rules ring, returns a record and includes nothing of the Domain
model (§6 rule 3)[^rctx][^ring].

**The shape of a procedure.** Each routine becomes two pieces:

| Piece | Ring, context | Does |
|---|---|---|
| A step machine: `TrunkProcedure`, `WitmotionArmProcedure`, `HackMotionArmProcedure` (`src/IMU/calibration/`) | Rules, Acquisition | Takes events: a tick with its elapsed time, a sensor summary (gyro rate, tilt, heading, link up or down), "the guide finished", "speech finished", the HackMotion's own phase. Returns its stage, progress, reason codes, cue codes, and requests (start a step, solve now, check the turn). Ends with a `CalibrationRecord` or a failure code. It holds every threshold and every transition. It has no clock, no timer and no sentence |
| A driver per procedure (`src/Gui/imu/` until Stage D) | Adapters | Owns the timers and the sensor taps, feeds the machine, calls the sink, and exposes the machine's state to QML as typed properties under the names the QML reads today |

Because time is an input, a recorded run replays to the same answer. The Rules ring holds no golfer-facing
sentence[^ring]: the machine returns codes and the words stay where they are, verbatim (`spoken_instructions.js`, the
status column). The existing maths in `src/IMU` is called, not rewritten.

The drivers are created by the composition root and reached as `app.calibration`, never created by QML[^p]. The
`trunkCeremony` global leaves with them[^q17]. Both wait for #291; until then the global stays[^c-v1].

**The ceremony file (FILE-SESS-11).** The recorder splits. Its taps and its in-memory ceremony stay with the trunk
driver (until #291, with the recorder itself; Stage C's C-14 moves only the file write, with #265's fix[^c-v1]). Its file write goes behind a repository, so the pool-thread write and the file name spelled in `src/Gui`
both go (A5 ×7 → 0)[^plan-c]. The recorder's two A6 lines, `attachResult` and `checkTurn`, are the QML routine's calls on it: they stay while that routine calls them, unless lint A6 narrowed to P9's wording counts them out[^c-v1]. The format, `pinpoint.trunk_ceremony/1`, does not change. Who the one writer is
is open question 5; the recommendation is the `Session`, because the file lives, moves, archives and is trashed with
its session.

**The duties inventory, before any logic moves.**[^duties] One page per file, checked by the coordinator before any
code. All ten QML files in `src/Gui/calibration` are covered, because the plan counts the directory's logic to
0[^plan-c]: a full page each for `TrunkRoutine.qml`, `WitmotionArmRoutine.qml`, `HackMotionArmRoutine.qml`,
`CalibrationRoutineBase.qml`, `ImuCalibrationFlow.qml` (the routine choice by vendor and role) and
`ArmCalibrationStatus.qml` (it reads the routines' internals and works out the hold progress); a short entry each
for `CalibrationStatusColumn.qml`, `CalibrationGuide.qml`, `CalibrationGuideView.qml` and `CameraCalibrationFlow.qml`
(a stub), saying what, if anything, is a threshold or a transition; and `trunk_ceremony_recorder.*`. Each lists every
threshold with its value, every state and transition, every timer, every call out (sensor, recorder, speech, log),
every path that sets or clears a calibration, every property a host or a test reads, every test that covers it, and where
each duty goes. A test is re-pointed at the new owner, never deleted.

**The equivalence test: old QML against new C++.** No routine has a recorded run today: the arm routines never had a
format, and there are 0 real trunk ceremonies (table A3). So the inputs are made in two stages.

| Stage | Inputs | What it proves | What it does not |
|---|---|---|---|
| **Now: golden traces** (C-9), for all three routines, the trunk included | Before any QML changes, each existing routine runs under the set-up test harness against scripted sensor inputs, and its event log (`CalibDriver.qml`)[^code-test] is frozen with the inputs as a fixture | The new step machine makes the same transitions, verdicts, reason codes and cue codes as today's QML for the same inputs | The harness uses `FakeTrunkCeremony` (`src/Gui/tests/setup/fakes/`), so the trace freezes the QML's transitions only. The solve and the turn check are already C++ in `src/IMU` and keep their own tests (`trunk_stand_tall_test`, `trunk_ceremony_file_test`) |
| **At the cabin visit: real ceremonies** | The visit's written procedure has a step: run the trunk ceremony on the real sensors, three times, on the build that still has the QML routine, and keep each `trunk_ceremony.json`. They become fixtures (C-17) | The lanes replay through `TrunkProcedure` to the same step sequence, the same verdict and cause per step, and the stand-tall A and M the file records | The arm routines: they have no recorded format, and adding one is not proposed |

Three rules make the traces a real test:

- **The comparison is a projection.** The routines run on real timers at a scaled pace, so a log's times do not
  repeat exactly. What is compared is the sequence of stages, verdicts, reason codes and cue codes, with each time
  rounded to the pace tick. C-9 proves the projection is deterministic by running each fixture twice.
- **The inventory comes first** (C-10 before C-9), and C-9 is accepted only when every transition and threshold in
  the inventory is reached by at least one scripted input. Otherwise the test proves only the happy path.
- **No new QML test file.** C-9 extends the three existing files (`tst_setup_calib_witmotion.qml`,
  `tst_setup_calib_hackmotion.qml`, `tst_setup_trunk.qml`); the new tests are C++. It is the plan's own test for
  this stage[^plan-c].

Until the visit, the trunk procedure is proved by its trace and by the existing C++ tests of the solve; the notes of
C-11 say so. C-14's byte-identity fixture for the ceremony file is synthetic, as `trunk_ceremony_file_test`'s is.

### 4.9 QML and the design system

(proposed, for the Solution Architect and the Design Lead)

No look or behaviour change is planned, so no design-system approval is planned for. Three kinds of QML edit happen:
the perspective rename (§4.7), `appSettings.*` reads and writes that become `app.rig.*`, and the three routine files
losing their logic to bindings (deferred to #291[^c-v1]). No control is drawn, no component or token is added. Each item that touches QML
reads `governance/artefacts/design_system/README.md` and the component pages first, and ends with a before/after sheet of each page
it touched as the evidence that nothing changed. **If an item finds it cannot keep the look, it stops and the change
goes to the Design Lead before it is built.**[^hard-ds]

A QML write to a deleted property fails only at run time, so each cut-over item ends with a grep count of 0 for
each removed property name under `src/**/*.qml`, the tests included (`tst_setup_flow.qml` writes
`appSettings.cameraBallRoi` today).

### 4.10 The camera calibration programme (#74)

(proposed, for the Solution Architect)

The Rig is #74's "Bay", `CalibrationRecord` is its record, and `rig.json` is its "per-bay store"[^ccd]. So #74's
stage 2 no longer creates a store; it adds record kinds and their producers. Its "recorded on the swing"[^ccd] is the
`rig` block. Its rule that a violated calibration is reported and never silently repaired[^ccd] fits: a verification
result is a per-shot fact, not a Rig change. Nothing else of #74 is in Stage C. `camera_calibration_design.md` gets a
short note saying so when this design is approved.

## 5. Rings and contexts

(proposed, for the Solution Architect)

| New or moved type | File | Ring | Context |
|---|---|---|---|
| `DeviceKey`, `PlacementKey` | `src/Vocabulary/Rig/device_key.h` | Vocabulary | Acquisition (part of its contract) |
| `RigState`, `CameraSetup`, `SensorSetup`, `CalibrationRecord`, `RigReference` | `src/Vocabulary/Rig/` | Vocabulary | Acquisition (part of its contract) |
| `RigId`, `RigStateId`, `RigRef` | `src/Vocabulary/Identity/identity.h`, with the other identity types[^im52] | Vocabulary | Shared |
| `recording::Perspective` | its own header (§4.7) | Vocabulary | Acquisition |
| The role rules of `imu_role_map.*` (moved) | `src/IMU/` | Rules | Acquisition |
| `sensorDisplayName`, `stripDeviceIds` (the display labels; they stay) | `src/Gui/imu/`, behind `app.rig` | Adapters | Domain + Application |
| `Rig`; `RigRepository`; `RigCalibrationSink` | `src/DomainModel/rig.*`, `rig_repository.h`, `rig_calibration_sink.h` | Domain model | Domain + Application |
| The Rig's store (`rig.json`) and the copies from settings | `src/Stores/rig_store.*` | Adapters | Domain + Application |
| The `rig` block's writer and reader | `src/Stores/recording_header.*` (the one header writer) | Adapters | Domain + Application |
| The ceremony file's door | `src/Stores/` (name with open question 5) | Adapters | Domain + Application |
| The three step machines (deferred, #291[^c-v1]) | `src/IMU/calibration/` | Rules | Acquisition |
| The three drivers (deferred, #291[^c-v1]) | `src/Gui/imu/` until Stage D splits capture[^ring] | Adapters | Domain + Application until Stage D, then Acquisition |
| `app.rig`, `app.calibration` (`app.calibration` deferred, #291[^c-v1]) | `src/Gui/app/` | Adapters | Domain + Application |
| Creating the Rig and the drivers | `src/App/` (the composition root) | Application | Domain + Application |

`Vocabulary/Rig` is tagged Acquisition, not Shared. Diagnostics may read it, because a later context may depend on an
earlier context's contracts[^rctx]. `SegmentRole`, which a sensor entry holds, and `TrunkCalibProvenance`, which its
record holds, both go to `Vocabulary/Common` in Stage B, Part 1[^q27][^plan-b].

## 6. Dependency rules, each checked against the include graph

(proposed, for the Solution Architect; the checks, with each grep, are in Appendix A, table A5)

| # | Rule | What the include graph says at `a5089848` |
|---|---|---|
| 1 | `Vocabulary/Rig` includes only Platform, `Identity`, `Vocabulary/Common`, the perspective header and Qt value types. It never includes `recording.h` | `recording.h` includes `Buffer/types.h`, `Vocabulary/Analysis/swing_analysis.h` and `Vocabulary/Club/club_standards.h`, so the enum gets its own header (A5-4) |
| 2 | `recording.h` includes `Vocabulary/Rig` (for `RigReference`); `Vocabulary/Rig` never includes `recording.h` | no cycle is made. One file under `Vocabulary/` names `Recording/` today (`analysis_versions.h`); the rig types do not include it (A5-5) |
| 3 | **The step machines (Rules) include nothing of the Domain model: they return a record** (when built: #291[^c-v1]). Only the drivers and device objects (Adapters) include `rig_calibration_sink.h`, and none includes `rig.h`. The sink joins lint A9's named contract headers[^q26] | 0 files under `src/IMU`, `src/Buffer`, `src/Video`, `Gui/imu`, `Gui/cameras` or `Gui/calibration` include `DomainModel/` today (A5-6). The check is extended to `src/IMU/calibration` and stays at 0 for `src/IMU` |
| 4 | `rig.*` includes only `Vocabulary/Rig`, `Identity`, Platform and the pure role rule (`IMU/imu_role_map.h`, Rules) | the Domain model's production files include `Analysis`, `Coaching`, `Core`, `Diagnostics`, `Identity`, `LaunchMonitor`, `Metrics`, `Recording` and `Vocabulary`; none includes `IMU`, `Buffer` or `Gui` (A5-7). `imu_role_map.*` includes only Qt, `segment_role.h` and `hm_unit_id.h` (A5-8), so its role rules can move to `src/IMU` and be the Domain model's first include of that directory |
| 5 | The Rig never includes `app_settings.h`; the copies from settings are made in the Rig's store, which reads the settings store itself | `app_settings.h` is included by 18 production files and 4 tests under `src/Gui`, and 4 files under `src/Update` (allowlisted, A1), and by no inner ring (A5-9) |
| 6 | `CameraInstance`'s enum can be deleted without touching an inner ring | `camera_instance.h` is included by 5 files, all in `Gui/cameras` and `Gui/shot` (A5-1) |
| 7 | The ceremony format stays where it is | `trunk_ceremony_file.h` is included by `Gui/imu` (1) and two tests; `trunk_ceremony_recorder.h` by 2 files in `Gui` (A5-10) |
| 8 | Only the composition root, `app.rig` and the store include `rig.h` | n/a: a new file. Checked by a lint line in C-4 |

**Re-run at Stage B's close.** Stage B, Part 1 splits `swing_analysis.h` and moves the Recording contract's types to
`Vocabulary/Recording`[^plan-b]. After it, `recording.h`'s include of a Diagnostics-tagged header is gone, part of
rule 1's reason goes with it, and the perspective header's home is `Vocabulary/Recording` (§4.7). The coordinator
re-runs table A5 on Stage B's closing commit before C-1 is briefed, and the rules are restated against that graph.

## 7. Ownership (IM §10)

(proposed, for the Solution Architect)

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived | 5. Persisted where; the one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| The Rig (OBJ-RIG) | source | itself; application, per machine | the composition root at launch; never ended; `RigId` | n/a | `rig/rig.json`; `Rig`, through `RigRepository` | domain (`rig.*`), adapter (`rig_store.*`) | `app.rig` |
| The rig state and its id (ID-RIG) | source, versioned | `Rig`; application | a new value on each versioned command; `RigRef` | n/a | in `rig.json` (current only, without sensor alignments) and, projected, in each Recording | vocabulary | `app.rig`'s models; a shot's through the shot |
| `RigChanged` (EVT-15) | event | `Rig` | emitted after the save | recomputes nothing | n/a | domain | views bind to `app.rig` |
| The rig reference (OBJ-RECORDING; FILE-SHOT-01's new `rig` block) | recorded input | the `Shot`; shot | written at capture; never changed | n/a | the swing file; capture, through the one header writer and `RecordingRepository`[^rrec] | vocabulary (`RigReference`), adapter (the block's writer) | through the shot |
| SET-09's 5 versioned keys (four camera facts, the roles) | source (rig) | `Rig`; application | the Rig's commands | n/a | `rig.json`; `Rig` (today: device and settings code) | n/a | `app.rig` |
| SET-09's 4 unversioned keys (two aliases, two enabled lists) | source (rig) | `Rig`; application | the Rig's commands | n/a | `rig.json`; `Rig` | n/a | `app.rig` |
| The placement view (`imuPlacement` today) | derived | `Rig`; application | n/a | inputs: the roles; no rule version; changes with them | not persisted | adapter (`app.rig`) | `app.rig` |
| SET-05, the club-length prior | learned rig state[^dec153] | `Rig`; application, per machine | `Rig::learnClubLength`, called by the capture service after a live shot's analysis | n/a (learned, not re-derivable) | `rig.json`; `Rig` (today `ShotProcessor` through `AppSettings`) | n/a | not shown |
| FILE-SESS-11, the raw ceremony | source (a calibration's evidence) | recommended: the `Session`; session (today: Studio/Rig, outside the aggregate) | handed to the session that starts after it; ends with the session | n/a | the session folder, unchanged format; recommended writer: `Session`, through `SessionRepository` | adapter | not shown; tools replay it |
| A calibration result | source | `Rig`; application | `commitCalibration`; replaced by the next of its subject and kind; ended by `clearCalibration`; a `sensorAlignment` also ends with the run of the app | n/a | in the rig state, and so in each Recording; in `rig.json` but for `sensorAlignment` | vocabulary (`CalibrationRecord`) | `app.rig` |
| A procedure's running state (until #291, in the QML routine[^c-v1]) | application state | its driver; application | the composition root; reset at each run | n/a | not persisted | rules (machine), adapter (driver) | `app.calibration` |
| `DeviceKey`, `PlacementKey` | identity | n/a: values | built from a device's own facts | n/a | wherever a device is named; each map's spelling unchanged | vocabulary | as text, through a model |
| `recording::Perspective` (A-13) | vocabulary | n/a | n/a | n/a | codes unchanged in settings and swings | vocabulary | `Perspective.*` in QML |

Information-model change? **Yes**: C1, C2, C3, C4, C5, C7, C8, C9, C11 (§8). Does §11 still match the code? It is
updated in the commit of each item that changes it.

## 8. The information-model changes, as IMC items

(proposed, for the Solution Architect; each is approved or refused on its own)

| IMC | Ids | Before | After |
|---|---|---|---|
| **C1** an object and what it contains | OBJ-RIG, CLS-RIG, §2.3 | "Named; designed in the platform refactor (capture)" | The Rig as §4.1: an application-level entity, per machine, in two parts; the camera design's Bay |
| **C1** an owner of facts | FILE-SESS-11 | Owner Studio/Rig, in the session folder "but outside the aggregate" | If open question 5 is ruled as recommended: inside the `Session` aggregate; the Rig owns the result and points at the file |
| **C2** a kind and level of fact | OBJ-RECORDING | The Recording's parts end at the analysis context | A new recorded input at the shot level: the rig reference with its embedded block |
| **C3** files | a new FILE-APP entry (`rig/rig.json`, `pinpoint.rig/1`); FILE-SHOT-01 (the optional `rig` block, `pinpoint.rigState/1`) | Rig facts in settings; no rig block | as §4.4 and §4.5 |
| **C3** settings keys: a move | SET-09, SET-05 | Rig facts in settings | 9 of SET-09's keys and SET-05 leave settings for the Rig; the two `ppcp` keys follow in Stage F |
| **C3** settings keys: **a correction to SET-09's row** | SET-09 | names `imu/placement`, `imu/mountOrientation` and `imu/calibration` as rig facts, and ends its camera list with "…" | `imu/placement` is a derived view of the roles; `imu/mountOrientation` and `imu/calibration` are unused and hold no fact; the "…" is replaced by §4.3's list; `camera/excluded` and `imu/excluded` are machine-wide preferences, not what a shot was captured with |
| **C4** the one writer | SET-09, SET-05, FILE-SESS-11 | "device and settings code"; `ShotProcessor` through `AppSettings`; `TrunkCeremonyRecorder` on a pool thread | `Rig` through `RigRepository`; `Rig`; the ceremony's door (open question 5) |
| **C5** identities | ID-RIG; new device-key rows in §5.2 | "design: the platform refactor (capture)"; no device identity | `RigId`, `RigStateId`, `RigRef`; `DeviceKey` and `PlacementKey` with each map's spelling |
| **C7** an event | EVT-15 | Producer Studio/Rig; carries and recomputes "design" | §4.2's table |
| **C8** a lifecycle | OBJ-RIG's lifecycle row | "design" | Created by the composition root; no states; the commands and the two invariants of §4.1 |
| **C9** a ring's directories | RING-VOCABULARY, RING-RULES | `imu_role_map.*` in `Gui/imu` (Adapters) | `src/Vocabulary/Rig/` (Vocabulary, Acquisition); `src/IMU/calibration/` and the role rules of `imu_role_map.*` in `src/IMU` (Rules, Acquisition); its two display-label functions stay in Adapters; the perspective header |
| **C11** rules | R-REC-8; new R-RIG rules | R-REC-8 names two kinds | R-REC-8 gains one sentence: the kinds are an open list, and the rig reference is never derived from the kind. New rules: R-RIG-a "a rig state is never altered; a change makes a new one; an equal state id means the same set-up, and an unequal one need not mean a different set-up for a given shot"; R-RIG-b "a Recording's rig is read from the Recording, never from the current Rig"; R-RIG-c "only the Rig's commands change a rig fact, and every clear of a calibration goes through the Rig"; R-RIG-d "a step machine returns its record; only an adapter calls the sink" (its step-machine half applies from #291[^c-v1]); R-RIG-e "a Recording's block lists exactly the rig devices that have a stream or a binding in that shot; no enablement is consulted" |

A-13 closes with C9's perspective header and is not itself an information-model id (it is the audit's
finding[^a13]). No C10 change: nothing domain-scoped is created by a view, and three QML state machines stop holding
procedure state (when #291 builds the procedures; until then they keep it[^c-v1]). No C6 change: no level reads another differently.

If the IM walkthrough deck shows the Studio/Rig as "designed later", C-0 says so in its commit and on the
dashboard's "Needs you", for the Product Owner to regenerate it.

## 9. Open questions for the Solution Architect

(proposed, for the Solution Architect: every recommendation and reason in this table is this design's, and none is
ruled. Questions 1 and 2 are the plan's Q2[^q2].)

| # | Question | Recommendation | Reason |
|---|---|---|---|
| 1 | Q2a: where does the capture machine keep its rig? | One file per machine, `rig/rig.json`, written only by `Rig` | Calibration records are structured and need a schema checked on read; one file has one writer. "Per machine" follows the studio profile; "a file" does not, and stands on these merits |
| 2 | Q2b: how does a past Recording's rig reach analysis on another machine? | An embedded block in each Recording, with its `RigRef` | The two machines do not share one library, and corpus swings travel alone; the analysis context already works this way |
| 3 | Is a rig state's id a content digest or a counter? | A digest | No minting, no counter to persist, equal set-ups compare equal across machines. A counter orders states, which `sinceUtc` does anyway |
| 4 | A worn sensor's alignment is redone at each wearing and, today, does not survive a relaunch. Is it part of the rig state, or a session-level fact? | Part of the rig state, held for the run of the app and never restored at launch (§4.2) | IM §2.3 gives every calibration result to the Rig; set-up runs before the session exists; one calibration can serve two sessions in a run. The cost is a new state per wearing. The alternative is a second owner of calibration results |
| 5 | Who writes FILE-SESS-11? (a) the `Session`, handed the ceremony when it starts; or (b) the Rig, writing into a session's folder | (a) | The file is in the session's folder and shares its life. (b) has an application-level object writing inside another aggregate, which P3 forbids. (a) moves the owner, so it is IMC C1 and C4 |
| 6 | Do the settings keys move into `rig.json` by a copy, or does the repository keep using the settings keys? And is the downgrade guard wanted? | Copy each group once, in the item that cuts it over; leave the keys untouched until Stage E; keep a fingerprint of each group's keys so a change made by an older build is copied again and logged (§4.4) | One store, never two writers of a group. Without the fingerprint, what an older build changes is silently ignored after the next upgrade, and going back to the baseline installer is planned[^q20]. `imu/rolesMigrated` accepted that cost once; the fingerprint is a few lines in the store. Its own cost: a re-copy takes the whole group, so the last writer wins (§4.4) |
| 7 | Is §4.3's classification right, and is SET-09's row corrected as §8 lists? | Approve both | It follows the code that reads each key (table A2), and one test: does the fact say where a device is and what it is for (Rig), or how a stream was captured (settings and the Recording)? |
| 8 | SET-09 names `ppcp/pairings` a rig fact, but the key holds each pairing's secret bytes. Does the Rig hold them? | No. The Rig holds which phones are paired, from Stage F; the bytes stay in the pairing store and follow Stage E's secrets work | A credential in the rig state would be copied into Recordings |
| 9 | One Rig per machine, or several? | One now; `RigId` lets a machine hold more later | Each machine has one bay today. Choosing between rigs is a feature, and it would need a screen |
| 10 | How does QML get the one perspective enum? | `Q_ENUM_NS` on the Vocabulary header, in a namespace of its own, registered by the Adapters ring | It keeps one type. The identity types already carry Qt's meta-object macros in this ring. A mirror enum in an adapter is the second type A-13 removes |
| 11 | Are "fixed cameras are square" and the down-the-line yaw σ written into the rig state in Stage C? | Yes, as `extrinsics` records with method `assumed`; the analysis keeps its constants | The assumption becomes visible per Recording now, and #74 replaces the record, not a constant. Changing what the analysis reads is a later change with a re-run |
| 12 | Does a device-only shot carry a rig reference? Does a phone-only document? | Device-only: yes. Phone-only: not in Stage C | The device-only writer goes through the repository at creation. The phone-only writer is the phone link's, which Stage F reworks |
| 13 | Does `RigChanged` recompute anything? | Nothing | No derived fact reads the current Rig. If one ever does, that is its own change |
| 14 | Does `trunkCeremony` leave the QML globals in Stage C? | Yes, for `app.calibration` (overtaken: it stays until #291[^c-v1]) | It is the plan's recommendation for the globals[^q17]: each leaves with the stage that gives it an owner |
| 15 | May Stage C add `assumed` and `deviceHeld` to the calibration methods the camera design took from the phone protocol? | Yes, as internal values that the phone link maps or omits | Without them an assumption and a device-held calibration would be recorded as something they are not |
| 16 | Does a `rig` block with a newer tag make the document read-only to this build, as a newer `analysisContext` does? | No, **until a reader of the block enters the analysis input**: until then it reads as `Unknown` and is kept verbatim. The change that makes analysis read the block also adds the block's tag to `refusesWrite` | While the block is not an analysis input, re-analysing under an older build cannot produce a wrong result from it, and refusing would stop a downgraded cabin re-analysing its own swings. Once analysis reads it, an older build would overwrite a newer result computed with a calibration it cannot see |
| 17 | Is the machine-wide "enabled" flag versioned? | No | It is a preference that seeds a session. What matters to a Recording is what the shot was captured with, which the block states (§4.5) |
| 18 | Does the Rig keep an unknown perspective code as its number? | Yes, in the Rig and the block only | It stops a newer file being flattened to `None`. It is new behaviour, so it is asked, not assumed |
| 19 | The block is a projection, so `stateId` is the whole rig state's digest and cannot be recomputed from a block. Is that enough, or does the block also carry a second digest of its own entries (a `setupId`)? | `stateId` only, for now; the meaning is written into R-RIG-a | An equal `stateId` implies the same set-up, which is what grouping needs. The reverse fails: re-calibrating a sensor a shot never used changes the id. A `setupId` would let a tool compare captured set-ups exactly, and it can be added later as an additive field; no tool needs it yet. No reader may check a block against its `stateId` |

For the Product Owner: the plan's Q23 (Stage E's part 1 behind this design's approval) is unchanged by this design;
the cabin visit's procedure gains one step, the three recorded trunk ceremonies (§4.8). For the Design Lead: nothing,
unless an item reports that it cannot keep a page's look (§4.9).

## 10. Build order

(proposed, for the Solution Architect and the Product Owner)

One agent per item. Every item is independently reviewed[^plan-c]. Items that share a file run in sequence. **The
`rig` block (C-8) is switched on only after every record it holds has a producer** (RP-10): the assumed records
(C-7b) and today's alignment results and clears, the HackMotion's `deviceHeld` record among them (C-6b). The procedures' move to C++ (C-11 to C-13) changes who
commits a record, not what the block holds, so it may follow C-8.

| Item | What | Needs | Its test |
|---|---|---|---|
| C-0 | The approved IMC items applied to the IM, SET-09's correction included; `camera_calibration_design.md`'s note; the walkthrough-deck note | approval | every cited id resolves through `im_excerpt.py`; both lints |
| C-1 | `DeviceKey` and `PlacementKey`; the 8 open-coded keys and the 6 shape-aware sites use them; table A5 re-run first | Stage B, Part 1 | `device_key_test`: from a settings fixture that holds two cameras (one with no serial), a Witmotion and a HackMotion, every spelling of §4.6's table is reproduced byte for byte, and the alias, role, excluded and prior maps resolve unchanged. Grep counts of 0 |
| C-2 | The perspective header; `Q_ENUM_NS` in its own namespace; the scripted QML rename; the two types become one; the test mirror and the two constants go | C-1 (both edit `CameraInstance`) | `perspective_test`: the codes; the existing `tst_setup_*` files pass unchanged; a before/after sheet of each page |
| C-3 | `Vocabulary/Rig` types, the ids, the canonical bytes and the digest. `CalibrationRecord` with every kind and method Stage C writes: `sensorAlignment`, `extrinsics`, `assumed` and `deviceHeld` | C-1, C-2 | `rig_state_test`: the digest ignores list order, changes with each versioned field, ignores `sinceUtc` and a record's `evidence`, and ignores aliases, enabled flags and the prior, which are held beside the rig state, never in it (IM §2.3; the addendum's decisions 2 and 8)[^c3-row]; a fixture's digest pinned as a literal, on the Mac and the cabin PC. **Scratch: a third camera** |
| C-4 | `Rig`, `RigRepository`, the store with its test override, `app.rig` read-only. The file holds the `RigId` and no group yet | C-3 | `rig_store_test`: schema checked on read; atomic write; a failed save keeps the state and warns; no event at load; a test run leaves the real folder untouched. Lint: `rig.json` in A5's table; only three places include `rig.h`. **Scratch: a machine with no rig stored yet** |
| C-5a | The camera half of the Rig in C++: the copy with its fingerprint, the commands, Impact exclusive, `RigChanged`, the unknown code kept | C-4; the duties inventory of the 6 camera properties first | `rig_commands_test`; `rig_import_test`: a settings fixture copies to the same values; keys untouched; no second copy; a key changed after the copy is copied again and logged; a `rig.json` with a newer schema is never saved over; **the no-rig machine with keys, and with none** |
| C-5b | The cameras cut over: `CameraManager`, `CameraInstance`, `migrateImpactRoi` and the QML use the Rig; 6 `AppSettings` properties go | C-5a | the camera tests re-pointed; grep 0 for each removed property in QML; a before/after sheet of each page |
| C-6a | The sensor half in C++: the role rules of `imu_role_map.*` moved to `src/IMU`, the two display-label functions left in the Adapters ring; the copy through `loadRoles`; `claimRole` with facts handed in; the enabled flag; the placement view | C-5b; the sensors' duties inventory first | the same two suites, for sensors: a second claim of a role is refused, as today; a HackMotion holds two roles; an un-migrated machine copies correctly |
| C-6b | The sensors cut over: `ImuManager` and the QML use the Rig; **today's calibrate paths commit to the Rig (the HackMotion's `deviceHeld` record included) and every clear path clears it**; 6 `AppSettings` properties go (3 moved, 2 unused, 1 derived) | C-6a | `tst_setup_sensors` re-pointed; each set path and each clear path of the inventory is run, and after each the Rig's record equals the live sensor's state; **a HackMotion reports calibrated and the Rig holds a `deviceHeld` record; its link is lost and the record is cleared**; no alignment at launch; grep 0 in QML; a before/after sheet |
| C-7 | SET-05 becomes `Rig::learnClubLength`: copied and marked taken in this item | C-4 | the prior round-trips under its typed key with today's spelling; no event; the capture-side snapshot reads the same value; a prior learned on the shot before the cut-over is in the copy |
| C-7b | The two assumptions as `assumed` records | C-5b | the records appear for a fixed camera and for an uncalibrated down-the-line camera; the analysis output of a fixture swing is byte-identical |
| C-8 | The `rig` block: writer, reader, `Recording::rig`; the schema reference. No libppswing change (table A4) | C-6b, C-7b | `rig_reference_test`. **Scratch: a swing with no rig reference** reads `Unknown`; `None` and `Known` round-trip; a newer tag reads `Unknown` and is kept. The block's per-camera set-up and per-sensor calibration equal `streams[].setup` and `streams[].device` of the same shot. **The block's entries match the shot's rig-device streams one to one**: a camera the session switched off has no entry, and neither has a camera that is enabled but absent (disconnected, or no frames in the window). **Two Recordings either side of a calibration change** hold two ids and the earlier is byte-identical afterwards. The block passes untouched through `writeDocument` (live and write-back), `editReview`, `updateStreamOrigin`, `updateLaunchMonitor`, `recordBackfilledContext`, `writeArchiveStub` and `writeOutsideLibrary`. The input stamp of a fixture swing is byte-identical with and without the block. libppswing's Python reader reads both kinds (a fixture check) |
| C-10 | The duties inventories of §4.8 (documents; the coordinator checks them) | none | a scripted count: every function, timer, `readonly property` number, call out, and set or clear path of a calibration that a grep finds in each file has a line in its inventory; every test that covers each file is listed |
| C-9 | Golden traces of the three routines as they are now, the trunk included, before any QML changes | C-10 | the fixtures; each run twice gives the same projection; every transition and threshold in C-10's inventories is reached; the three existing set-up test files extended (no new QML test file) |
| C-11a (deferred, #291[^c-v1]) | `TrunkProcedure` in C++ with its record | C-9 | the equivalence test on C-9's trunk trace |
| C-11b (deferred, #291[^c-v1]) | The trunk driver, the sink call, `TrunkRoutine.qml` as bindings | C-11a, C-6b | `tst_setup_trunk` re-pointed; a before/after sheet |
| C-12 (deferred, #291[^c-v1]) | `WitmotionArmProcedure` and its driver | C-11b | the equivalence test on C-9's trace; `tst_setup_calib_witmotion` re-pointed; a sheet |
| C-13 (deferred, #291[^c-v1]) | `HackMotionArmProcedure` and its driver; the driver commits the `deviceHeld` record that C-6b's path committed until now | C-12 | the equivalence test, each abort reason included; `tst_setup_calib_hackmotion` re-pointed; a sheet |
| C-14 | The ceremony file behind its door; the pool-thread write and the `trunkCeremony` global go (re-scoped: the file write only, with #265's fix; the global stays[^c-v1]) | C-11b; open question 5 (C-11b deferred: C-14 follows C-6b[^c-v1]) | `ceremony_store_test`: the same bytes as today for a synthetic fixture ceremony; an earlier ceremony kept aside; the three "dropped" cases; a test for each of #265's loss paths[^c-v1]; A5 ×7 at 0, the A6 ×2 as §4.8 says[^c-v1] |
| C-16 | The close: the counts of §4.3, §4.6 and §4.7; IM §11; the written procedure for the cabin visit, with its trunk-recording step; an installer | all above | both lints; the full suite on the cabin PC |
| C-17 (deferred, #291[^c-v1]) | After the visit: the three real ceremonies become fixtures | the cabin visit | the lanes replay through `TrunkProcedure` to the file's step sequence, verdicts and stand-tall A and M |

**The scratch cases**[^plan-c], and where each is proved: a swing with no rig reference (C-8); a third camera (C-3,
and again through the commands in C-5a); two Recordings either side of a calibration change (C-8); a machine with no
rig stored yet (C-4, and with settings keys in C-5a).

**The cabin visit.** Stage C has no cabin visit of its own (proposed, for the Solution Architect, after the two
26 Nov rulings[^visits-26nov][^visits-combined]). A visit holds only steps that need clubs and a ball being hit, so
Stage C's one such check, End and quit while a shot is analysing (#280, #281), runs at Stage D's visit, on its
installer of main, which still has the QML routines[^c-v1], and Stage C's close waits for that visit. Its written
procedure is Stage D's visit pack, not C-16's. The rest is not a visit step: the three trunk ceremonies,
recorded on the real sensors and kept for #291, where they become its fixtures, and #270's two HackMotion attempts
in a row, on today's arm routine, whose timed start and Recalibrate after a failure are expected, not faults, until
#291[^arm-hands-free], are desk checks by the Product Owner on a development machine; #105's check is its two tests
and the real connect's log lines, and the sensors' `rig` blocks on real shots are read from the same visit, which
wears the sensors. The procedures in C++, and their proof on the real sensors, go with #291[^c-v1].

## 11. What Stage C does not do

(proposed, for the Solution Architect)

- It adds no third shot kind; it only stops the rig reference depending on there being two[^guard].
- It builds none of #74's stages[^ccd], and no camera calibration ceremony.
- It designs none of #183's per-stream fields[^q4].
- It does not change what the analysis reads, so it bumps no stage version (RP-9).
- It does not split `AppSettings`; it removes 13 properties from it. The rest is Stage E's[^plan-c].
- It does not move `ppcp/studioName` or `ppcp/pairings`; Stage F does (§4.3).
- It does not move `Gui/cameras` or `Gui/imu`; Stage D does[^ring].
- It does not restore a worn sensor's alignment at launch, and it does not change which devices a session captures
  with.
- It measures no set-up aim. #178's reference (the alignment stick, the cameras) will arrive as calibration records;
  Stage C only makes sure an assumption is never recorded as a measurement (RP-8)[^i178].

---

## Appendix A. The evidence

Checked at `a5089848` by this design's author and by the independent review of 24 Nov 2026[^review]. The review file
is not committed, so what the page relies on from it is copied here. Each grep runs under `src` unless it says
otherwise; tests are counted apart.

**A1. Device keys and perspective types.**

| Fact | How it was found | Result |
|---|---|---|
| Open-coded device keys | `grep -n 'QStringLiteral("\|")'` in the six files | `camera_instance.cpp:122, 925`; `camera_manager.cpp:969`; `camera_manager.h:216`; `imu_role_map.h:82`; `resource_monitor_controller.cpp:196, 342`; `shot_processor.cpp:1005`: 8 |
| Places that know a key's shape | the same grep for `'\|'`, and a read of each | `camera_manager.cpp:85` (replaces `\|` for a seeded alias); `imu_manager.cpp:183` (the same, for a sensor); `imu_role_map.h:110` (matches a `"\|<id>"` tail); `camera_roi.h:47` (`cameraKey + "#impact"`); `capture_context.cpp:348` (SET-05's key); `tst_setup_sensors.qml:313`: 6 |
| Perspective types and constants | `grep -rn 'Perspective\b\|kImpact\b\|kImpactPerspective'` | `recording.h:56`; `camera_instance.h:145` (`Q_ENUM`); `setup_enum_standins.h` (a test mirror); `app_settings.cpp:36`; `camera_roi.h`, tied by a `static_assert` at `camera_instance.cpp:22` |
| QML naming the enum | `grep -rn 'CameraInstance\.\(FaceOn\|DownTheLine\|Impact\|Other\|None\)\b' --include='*.qml'` | about 120 uses in 19 files |
| Qt meta-object macros in the Vocabulary ring | `grep -rln 'Q_NAMESPACE\|Q_ENUM_NS\|Q_GADGET' src/Vocabulary src/Vocabulary/Recording src/Vocabulary/Identity` | `src/Vocabulary/Identity/identity.h` (`Q_GADGET` ×3) only |

**A2. What each key means, from the code that reads it** (the review's Part 1, table 3).

| Key | What the code does with it |
|---|---|
| `camera/perspective` | camera key → code. `assignPerspective` makes Impact exclusive (`camera_manager.cpp:732–746`). `migrateImpactRoi` reads it at load (`app_settings.cpp:31–50`) |
| `camera/isMirrored`, `camera/fixedInPlace` | camera key → bool, restored onto the instance (`camera_manager.cpp:997–1003`) |
| `camera/ballRoi` | camera key → box. Restored only for a fixed camera; the entry is kept while the camera is not fixed (`camera_manager.cpp:1001–1019`) |
| `camera/excluded` | a list of camera keys. "Global enablement is owned by Settings"; each session is seeded from it and the session's own exclusion is never written back (`camera_manager.h:51–56`, `SetupContext.qml:616`) |
| `camera/alias` | camera key → label, seeded on discovery (`camera_manager.cpp:85`) |
| `camera/impactPip` | where the picture-in-picture box sits (`app_settings.h:239–242`) |
| `imu/roles` | placement key → role; the key is a Witmotion's bare device id, or `<id>#lowerArm` / `<id>#palm` (`imu_role_map.h:32–36`). One claim per role: `ImuManager::setRoleForDevice` → `imu_roles::claimRole(currentRoles(), deviceId, role, factsFn())`. `loadImuRoles` runs the once-only `imu/placement → imu/roles` move (`app_settings.cpp:56–75`) |
| `imu/placement` | "A DERIVED, READ-ONLY VIEW OF imuRoles, NOT STORAGE … The stored `imu/placement` key is never written again" (`app_settings.h:252–259`). One reader, labelling old swings in the data viewer |
| `imu/mountOrientation` | loaded and never read or written outside `AppSettings` (0 uses in the repo) |
| `imu/calibration` | unused, confirmed: the key appears twice in `src` and `tools`, both in `AppSettings` (the load and the setter); nothing calls the setter and nothing reads the property. A and M live on `ImuInstance` and nothing restores them at launch |
| `imu/excluded` | a list of bare device ids (`ImusPanel.qml:54, 153–157`; `SetupContext.qml:619`), seeding the session's exclusion |
| `imu/alias` | `description\|deviceId` → label (`imu_role_map.h:80–83`), seeded on discovery (`imu_manager.cpp:178–184`) |
| `analysis/clubLenPrior` | key `athlete.uuid\|sessionClub\|faceOnCameraKey`, read only for a fixed face-on camera (`capture_context.cpp:343–349`); written on the live path only, by read-modify-write of the whole map (`shot_processor.cpp:1594–1619`) |
| Setting a calibration | `ImuInstance::setCalibration`; `setFunctionalCalibration` (`imu_instance.cpp:765`); `setNominalCalibration` (`:801`); `refineMountAboutLongAxis` (`:840`, changing A on two branches, `:855` and `:874`); `calibrateTrunk`; the HackMotion's own calibrated state |
| Clearing a calibration | `clearCalibration` (`imu_instance.cpp:755`); `clearFunctionalCalibration` (`:891`); `zeroOrientation` calls `clearCalibration()` (`:581–584`)[^zero]; the routines' `_reset` paths call both clears; a HackMotion's calibration dies with its link |
| `ppcp/pairings/<id>` | "the 32 raw bytes of PRK as hex. Nothing else" (`ppcp_pairing_store.cpp:155–157`) |

**A3. Real trunk ceremonies.** A read-only `find` for `trunk_ceremony*.json` over the share found 0, on a walk that
saw 93 `session.json` files; a search of the Mac found 0 outside the source and build trees; the repo holds none
(`trunk_ceremony_file_test` builds a synthetic one). The set-up harness uses `FakeTrunkCeremony`
(`src/Gui/tests/setup/fakes/`).

**A4. An unknown top-level block passes through** (the review's Part 1, item 1).

| Where | Evidence |
|---|---|
| libppswing's specification | "The format knows nothing about the swing schema. `swing.json`'s own `schema` keys … travel inside the tree unchanged. A new `analysis.*` block therefore needs no format change." (`docs/specification/ppsw-format.md:24`, in the libppswing repo) |
| libppswing's codecs | the encoder writes every member of an object (`src/encode.cpp:243–250`); the decoder keeps every key it reads (`src/decode.cpp:153–167`); the Python reader's `to_plain` converts arrays and tables only and passes maps through (`python/ppswing/reader.py:524`) |
| Loading | `SwingStore::loadPpswFile` loads the whole tree (`swing_store.cpp:172–173`); `ppswqt::fromQt` copies every key with no whitelist (`ppsw_qt.cpp:64–65`) |
| Rewriting | each rewrite loads the whole document and edits only its own keys; re-analysis's write-back hands the document on disk to `writeDocument` (`swing_store_repository.cpp:283`), which starts from `QJsonObject root = rawManifest;` (`swing_doc_writer.cpp:427`) |
| The archive stub | `QJsonObject stub = doc;`, thinning only `analysis` and `streams` (`session_archiver.cpp:135`) |
| The phone path | writes only when no document exists (`ppcp_clip_filer.cpp:125, 265`) |
| `swinglab_run` | hands `writeOutsideLibrary` the source document (`swinglab_run.cpp:1315, 1349`) |
| An older build | "also keeps the block when it re-analyses a newer swing, because this code is already in it" |
| The write guard | `refusesWrite` looks only at the document's `schema` and `analysisContext`'s (`swing_doc_writer.cpp:405–416`) |
| The contract | "Additive by contract. Readers ignore unknown keys" (`swing_json_schema.md:39`) |

**A5. The include graph.** Each is `grep -rl '#include.*<name>' src --include='*.h' --include='*.cpp'`, grouped by
directory.

| # | Check | Result |
|---|---|---|
| A5-1 | Who includes `camera_instance.h` | 5: `Gui/cameras` 3, `Gui/shot` 2 |
| A5-2 | Who uses `recording::Perspective` or `perspectiveFromCode` | `Analysis/tests` 3, `Export` 1, `Gui/shot` 1, `Recording` 1 |
| A5-3 | Who includes `imu_role_map.h` | `Gui/app` 1, `Gui/imu` 3, `Gui/tests` 1 |
| A5-4 | What `src/Vocabulary/Recording` includes | `Buffer/types.h`, `Vocabulary/Analysis/swing_analysis.h`, `Vocabulary/Club/club_standards.h` |
| A5-5 | What under `src/Vocabulary` names `Recording/` | 1: `Vocabulary/Analysis/analysis_versions.h` → `Vocabulary/Recording/analysis_context.h` |
| A5-6 | `DomainModel/` included from `src/IMU`, `src/Buffer`, `src/Video`, `Gui/imu`, `Gui/cameras`, `Gui/calibration` | 0 in each |
| A5-7 | What the Domain model's production files include (`src/DomainModel/*.h`, `*.cpp`) | `Analysis` 1, `Coaching` 24, `Core` 13, `Diagnostics` 15, `Identity` 13, `LaunchMonitor` 1, `Metrics` 4, `Recording` 2, `Vocabulary` 2; no `IMU`, `Buffer` or `Gui` |
| A5-8 | What `imu_role_map.h` and `.cpp` include | Qt core types, `<functional>`, `Vocabulary/Common/segment_role.h`, `IMU/hm_unit_id.h` |
| A5-9 | Who includes `app_settings.h` | `src/Gui`: 18 production files and 4 tests; `src/Update` 4; no inner ring |
| A5-10 | Who includes `trunk_ceremony_recorder.h` / `trunk_ceremony_file.h` / `imu_calibration.h` | 2 (`Gui`, `Gui/imu`) / 3 (`Gui/imu`, two tests) / `IMU` 5, `Gui/imu` 1, tests 6 |
| A5-11 | Uses of the moved properties outside `app_settings.*` (QML / C++) | perspective 2/14, mirrored 3/3, fixed 6/3, ballRoi 0/5, cameraAlias 0/9, cameraExcluded 2/4, imuRoles 7/2, imuPlacement 1/0, imuAlias 0/11, imuExcluded 6/2, clubLenPrior 0/3 |

---

## Notes

[^c3-row]: Corrected 26 Nov 2026 in C-3's fix round to match the rulings it lagged (AG-4): IM §2.3 keeps the Rig's other facts apart from the versioned rig state, and the Rig design addendum's decision 8 ("outside the rig state") and decision 2 (`evidence` outside the digest), ruled by the Solution Architect on 25 Nov 2026. The "ignores" cases run against the rig state and those facts held side by side.
[^hard]: `CLAUDE.md`, "Information model and architecture"; IM §1.2 (`governance/artefacts/information_model.md`).
[^hard-ds]: `CLAUDE.md`, "UI and design system": any design-system change needs the Design Lead's approval before it is built.
[^plan-c]: `docs/implementation/platform_refactor_assessment.md`, "Stage C · The Rig and calibration", with its Closes, Deletes, Test, Re-run, Tier, Accept, Scratch cases and Cabin visit rows.
[^plan-b]: The same page, "Stage B", Part 1: `swing_analysis.h` is split and the Recording contract's types move to `Vocabulary/Recording`; `SegmentRole` (`segment_role.h`) and `trunk_calib_provenance.h` move to `Vocabulary/Common`.
[^q2]: The same page, §9 Q2, split in two after the outside review of 23 Nov 2026 (§10, row 2.4).
[^q4]: The same page, "Rulings of 24 Nov": Q4 (Product Owner), "#183 is held until its comparison at the cabin, so that its settings change and its per-stream fields (a format change) are designed once, from evidence"; and Stage C's "Exposure and gain are per-stream capture facts in the Recording".
[^q15]: The same page, "Rulings of 24 Nov": Q15 (Solution Architect), the Vocabulary area names.
[^q17]: The same page, "Mandatory carry-ins" (iii) and §9 Q17: `trunkCeremony` leaves in Stage C; "a calibration procedure, whose result belongs to the Rig".
[^q20]: The same page, "Rulings of 24 Nov": Q20 (Product Owner), "the `94b3b9a1` installer stays the baseline".
[^q26]: The same page, "Rulings of 24 Nov": Q26 (Solution Architect), contracts, not owners; lint A9 checks a named set of contract headers.
[^q27]: The same page, "Rulings of 24 Nov": Q27 (Solution Architect), shared Vocabulary; `trunk_calib_provenance.h` moves to `Vocabulary/Common`.
[^guard]: The same page, Stage C, "A guard from the four contexts" (ruled 24 Nov 2026 by the Solution Architect and the Product Owner; PR-4 in the platform refactor's judgement calls).
[^duties]: The same page, Stage C's Tier row: "A duties inventory of each calibration QML file before its logic moves".
[^review]: The independent review of this proposal's first draft, 24 Nov 2026 (the platform refactor's Stage C working notes, not committed). What this page relies on from it is quoted or copied in Appendix A, with the file and line each fact was read from.
[^im23]: IM §2.3, "Studio/Rig" (v1.10.1); DEC-17 (Solution Architect, 14 Nov 2026).
[^im22]: IM §2.2, "The Recording contract": the Rig row, the analysis-context rows and "`capture.club` stays, written from the same value, as provenance".
[^im2]: IM §2, the contexts table.
[^im52]: IM §5.2: the identity types live in `src/Vocabulary/Identity`, in the Vocabulary ring (DEC-148).
[^rctx]: IM §2, R-CTX-1 (Solution Architect and Product Owner, 24 Nov 2026; Q26's wording).
[^rrec]: IM §2.2, R-REC-1, R-REC-2 and R-REC-4.
[^rrec8]: IM §2.2, R-REC-8 (DEC-44, DEC-87, DEC-147).
[^p]: IM §3: P1 (source, recorded input, derived), P2 (three layers), P3 (one writer), P4 (repositories), P6 (levels), P7 (identities are types), P8 (one owner thread), P9 (the typed QML boundary), P11 (persistence is the owner's concern); lint A2 for QML-created scoped types.
[^ring]: IM §4 and §4.1: the ring table; RING-RULES places `imu_calibration`, `trunk_stand_tall`, `trunk_ceremony` and `trunk_ceremony_file` in Rules, Acquisition; the Rules ring never holds "golfer-facing sentences"; `Gui/cameras` and `Gui/imu` are Domain + Application "until Stage D splits capture".
[^evt15]: IM §7.4: EVT-15's row, and "`RigChanged` [is] decided" (DEC-22).
[^set09]: IM §8.2, SET-09: "the per-camera `camera/*` keys (`perspective`, `isMirrored`, `fixedInPlace`, `alias`, `ballRoi`, …), `imu/calibration`, `imu/roles`, `imu/placement`, `imu/mountOrientation`, `ppcp/studioName`, `ppcp/pairings`".
[^dec140]: DEC-140 (Solution Architect, 15 Nov 2026): SET-09's four rig keys.
[^dec153]: DEC-153 (Solution Architect, 15 Nov 2026): SET-05 is learned rig state, per machine, keyed by `AthleteId`, club and camera; owner Studio/Rig.
[^dec193]: DEC-193 (Solution Architect, 21 Nov 2026; IM v1.9.12): the studio profile is not a rig fact; `ppcp/studioName` stays in SET-09 with the rig facts; the profile is held per machine.
[^asy]: `docs/design/coaching_layer.md`, ASY-R1: external video files are ingested with a declared analysis capability.
[^ccd]: `docs/design/camera_calibration_design.md`: §2 (principles 1, 5, 6), §3.1 (the Bay), §3.2 (the calibration record and camera identity), §7.3 (declaring a violated calibration), §10 (the stages; stage 2 is "Calibration record + per-bay store").
[^schema]: `docs/reference/swing_json_schema.md`: "Top-level structure", the document kinds by writer, "`streams`" (a HackMotion lane's `source.serial` is `<deviceId>#lowerArm` or `#palm`, `hm_unit_id.h`), and "`archive`".
[^schema-add]: `docs/reference/swing_json_schema.md`: "Additive by contract. Readers ignore unknown keys"; and `capture.club.lengthPrior`'s entry, "Additive — no schema-version bump".
[^trunk]: `docs/design/trunk_imu_sequence_grf_design.md` §6.7; the header of `src/IMU/trunk_ceremony_file.h`.
[^wizard]: `docs/design/session_wizard_refactor_design.md` §4.7; the header of `src/Gui/calibration/ImuCalibrationFlow.qml`.
[^i178]: Issue #178, "Alignment: set-up aim is measured, never assumed".
[^a13]: `docs/implementation/swe-audit/02-findings.md`, A-13; `05-deferred-worklist.md` §1.
[^c-key]: The plan's footnote `[^c-key]`, which also notes `camera_manager.h:216`.
[^c-calib]: The plan's footnote `[^c-calib]`: the production `.qml` files under `src/Gui/calibration`.
[^code-rec]: `src/Vocabulary/Recording/recording.h`, `CameraStream`.
[^code-set]: `src/Gui/app/app_settings.h`, the `Q_PROPERTY` list; `app_settings.cpp` for `imu/rolesMigrated`.
[^code-cer]: `src/Gui/imu/trunk_ceremony_recorder.h` (its header comment) and `.cpp` (`QThreadPool::globalInstance()->start`); `src/Gui/tests/architecture_allowlist.txt`.
[^code-cam]: `src/Gui/cameras/camera_instance.h`, the comment on `Perspective`; `CameraManager::assignPerspective` (`camera_manager.cpp`): "Impact is exclusive".
[^code-key]: `src/Gui/cameras/camera_instance.cpp` (`cameraKey()`); `src/Gui/imu/imu_role_map.h` (`aliasKey()`).
[^code-id]: `src/Vocabulary/Identity/identity.h`.
[^code-hm]: The header of `src/Gui/calibration/HackMotionArmRoutine.qml`: "Nothing about the HackMotion calibration is persisted, ever".
[^code-test]: `src/Gui/tests/setup/support/CalibDriver.qml`; `tst_setup_trunk.qml`, `tst_setup_calib_witmotion.qml`, `tst_setup_calib_hackmotion.qml`.
[^sa24-rig]: The Solution Architect, 24 Nov 2026: "approve as recommended" (`build/platform/JUDGEMENT-CALLS.md` PR-35). §9's questions 10, 14, 17 and 18 are implementation choices, taken as recommended by the coordinator and logged there too.
[^arm-hands-free]: The Product Owner and the Solution Architect, 25 Nov 2026: the arm calibration routines' breach of the hands-free rule (`governance/controls/product/ui-behaviour.md`, UI-14) is accepted until #291's track, which takes #266 with it; named on #291. Only the trunk routine is hands-free today.
[^c-v1]: The Product Owner and the Solution Architect, 25 Nov 2026 (the lever pass on #224-U, Stage C and Stage D, lever C-V1, as recommended; the Solution Architect agreed to defer this part of his approved design; the Product Owner confirmed it the same night: the calibration QML is moved all at once or not at all). The procedures' move to C++ (C-11 to C-13, C-15, C-17; 22–23 h) is deferred, with a trigger: one calibration track after the platform programme, built with #266 and #270; home #291. The 3,978 lines of calibration QML are a known breach of RP-7, §4.8 and the rule that QML holds no rules and only binds, pinned by the three golden traces (C-9). C-14 stays, re-scoped to the ceremony file's write behind the Session's door, with #265's fix and a test for each of its loss paths; the recorder's two A6 lines stay unless lint A6 narrowed to P9's wording counts them out (the lever's own text). The three real ceremonies are still recorded at visit 2.

[^visits-26nov]: The Product Owner, 26 Nov 2026 (governance BT-43, tightened): a visit holds only steps that need golf clubs and a ball being hit; checks that can be automated or done at the desk are not run there. The 25 Nov wording above (`[^c-v1]`, "still recorded at visit 2") is kept as that ruling's record.

[^visits-combined]: The Product Owner, 26 Nov 2026: Stage C's visit and Stage D's visit are combined into one, at Stage D, to save time; Stage C's close waits for it (the platform programme's decision log, `build/platform/JUDGEMENT-CALLS.md`). The C-16 row above, "the written procedure for the cabin visit, with its trunk-recording step", is the 24 Nov design's record.
[^zero]: The Product Owner and the Design Lead, 26 Nov 2026 (`build/platform/JUDGEMENT-CALLS.md`, "C5, and the battery colours"): the Zero button is removed, with `ImuInstance::zeroOrientation` and `ImuManager::zeroAll`, after this page's code facts were taken. The trunk calibration sets "straight". Where this page lists `zeroOrientation` as a path, that path no longer exists.
