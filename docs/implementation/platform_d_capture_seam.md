# Platform refactor, Stage D: the capture seam. A test-rig question, ten design questions and two scope levers, classified

**Status: proposed, 24 Nov 2026; every row ruled as recommended on 25 Nov 2026[^d-ruled-25nov], and the scope then cut by the lever pass ("The lever pass of 25 Nov", below)[^d-lever]; its Buffer cut (D-V3) was reversed the same night[^d-v3].** Each row ends with an empty **Ruled** line: the
coordinator writes the ruling, the role and the date there, and the briefs that wait read it. Until a row is ruled,
today's behaviour stays and the items that wait on it are not built[^hard]. Every recommendation is **(proposed)**
for the role its **Class** line names.

**Why this page exists.** Stage D of the platform plan splits the shot processor, gives the shot's allocation to the
session, and moves the capture code out of `Gui`[^plan]. The plan marks ten of its structure questions "proposed",
and Stage D's item plan (38 items, about 98 hours) is provisional until they are ruled[^pland]. One more question
came out of the item plan and goes first, because the whole shot chain stands on it: can the shot processor be driven
in a test at all?

**How each row is classified.** By the information model's definition of a change, §1.2 (C1–C11), read through
`tools/method/im_excerpt.py`[^imc]. A row that only makes code match what the information model already says is not
a change (§1.2's own exclusion); a row that moves a file the model names, adds an interface that needs a ring, or
changes a lifecycle or a lint rule's scope is.

| Class | Meaning | Count | Rows |
|---|---|---|---|
| **HARD: the Solution Architect** | An information-model change. Nothing that depends on it is built before the ruling | **10** | T2, S1, S2, S4, S5, S6, S7, S8, S9, V2 |
| **HARD: the Design Lead** | A design-system change | **0** | none: see "No design-system change" below |
| **Coordinator's call** | A build-structure or test-rig choice with no C1–C11 change. Decided and logged as a judgement call | **2** | T1, S3 |
| **Product Owner** | Scope and order | **2** | S10, V1 |

Fourteen rows: the rig in two parts (T1, T2), the ten questions (S1–S10) and the two levers (V1, V2).

**Three places where this page differs from the item plan**, each explained in its row:
1. The item plan calls all ten questions information-model changes. **S3 is not, if the handle is defined in a header
   that is already Vocabulary**, and **S10 is a question of order** that the Product Owner has already ruled once.
2. **The rig needs a seam for at least 4 of its 11 pinned cases, and perhaps 7** (T1, T2). The item plan left
   that open. It adds one item, and SD-2a itself grows from about 3.5 to about 5 hours.
3. **Lever 2 saves less in Stage D than the forecast says**: 3 to 4.5 hours, not 6, because the launch-monitor
   board is a presenter and stays inside lint A6 under P9's wording (V2).

**How to read the counts.** Taken on main at `cb9f376c`, each with its command, run from the repository root in
`bash`. "Product files" leaves out `tests/` directories. The briefs re-take every figure at run time.

**Include check, as the method asks before a dependency rule is proposed**[^inc]. S1 and S8 each propose one.
The capture files in `Gui` include Diagnostics or Rules headers 4 times in `Gui/cameras`, 2 in `Gui/imu` and 14 in
`Gui/shot`, in 10 files (`grep -nE '^#include ".*(pose_estimator|ball_detector|/Pose/|/Ball/|/Analysis/|/Club/|/Timeline/|/Diagnostics/|/Wrist/|/Kinematics/|/Body/)' src/Gui/{cameras,imu,shot}/*.{h,cpp}`).
The 2 in `Gui/imu` and 3 of the 14 are Vocabulary headers, which any ring may include. The shot processor's own
includes reach the device classes 7 times (`grep -cE '^#include "(camera_|imu_|hm_|device_enumerator)' src/Gui/shot/shot_processor.{h,cpp}`).
Thirteen product files in seven directories include the device enumerator.

**What can start with no HARD ruling.** The five duties inventories (SD-1a to SD-1e, as Stage C's cut-overs land;
SD-1c now), the camera-less rig (SD-2a, under T1), the 4 to 7 of 11 pins it reaches (parts of SD-2b and SD-2c),
the pixel-format mapping (SD-5), and, if S3 is taken as the coordinator's call, the handle and Buffer's move (SD-3,
SD-4, after B2.5). Everything else waits for a row below.

## The capture test rig (SD-2a), first

### T1 · Can the shot processor be built and driven in a test with no product seam?
- **Today.** No test builds a `ShotProcessor`, a `CameraManager`, an `ImuManager` or a `CameraInstance`
  (for each class `C`: `grep -rlE '(\bC\s+\w+\s*[({;]|new C\b|make_unique<(pinpoint::)?C>)' src --include='*test*.cpp' | wc -l`,
  which finds constructions only: 0 files each). Its constructor takes seven concrete objects
  (`src/Gui/shot/shot_processor.h:147`). Read one by one:

  | Dependency | Built in a test today | What its constructor reaches | What the shot processor asks of it |
  |---|---|---|---|
  | `EventBuffer` | yes, 7 test files; `shot_controller_decline_test` runs one with no session | memory only | `isCapturing`, `state`, `snapshot`, `makeRingPayloadSource` (4 calls) |
  | `CameraManager` | **no** | camera enumeration, unless `PINPOINT_HEADLESS_PROBE` is set (`camera_manager.cpp:48`); the enumerator singleton; the settings | `pauseBuffer`, `liveCameraInstances`, `applyCaptureIntent` (3 calls, **none guarded against null**: `:345`, `:511`, `:2100`) |
  | `ImuManager` | **no** | the Bluetooth adapter list and scan, unless the same switch is set (`imu_manager.cpp:42`); it always starts its I/O thread | `instances`, `roleForDevice` (every use guarded) |
  | `AppSettings` | yes, 5 test files, on a scratch INI (`QSettings::setPath`) | the settings store | 4 getters; a null one falls back to a fresh object on the real store (3 of the 24 fallbacks) |
  | `AthleteController` | yes, 3 test files, on a scratch library | none until a library is set | the current athlete, through helpers that accept null |
  | `SessionController` | yes, 2 test files | none | passed to the capture snapshot only |
  | `ShotListModel` | yes, 5 test files | none ("starts empty, over no session") | `landShot`, `ordinalForId` (guarded) |

  Four findings follow.
  1. **Constructed: yes.** The constructor stores the seven pointers, sets up two timers and starts a third (the stall watchdog); it calls none of the seven
     (`shot_processor.cpp:174–227`). Both managers build with no device when the headless switch is set, a switch the
     product already has.
  2. **Linked: not in the test tree as it stands.** `shot_processor.cpp` calls the exporter, the analysis service and
     the camera instance directly, and those bring the video encoder (FFmpeg), ONNX Runtime, OpenCV and the camera
     back-ends. The test tree builds none of them: of its 30 build files, 0 name `shot_processor.cpp`,
     `capture_context.cpp`, `camera_manager.cpp`, `camera_instance.cpp`, `imu_manager.cpp`, `swing_exporter.cpp`,
     `shot_analyzer.cpp` or `analysis_service.cpp`. `src/Analysis/tests/CMakeLists.txt:526` says so in words: it
     is "why the busiest state machine in the app had no tests at all".
  3. **Driven through a camera-less shot: yes, by reading, as far as the allocation. To its first write: not
     settled.** With synthetic streams registered on the buffer (as the decline test does), an empty camera list
     and no sensor, a trigger runs the post-roll, the pause, the window, the analysis and the allocation:
     `startSwingSave` calls `addShot`, then takes its "no exportable cameras — analysis-only swing" path (`:752`).
     The join then writes a document only if the analysis succeeded with a result
     (`persistPathFor`, `shot_outcome.h:142–149`). Whether the analysis succeeds on a window with no camera and
     no bound sensor is not settled by reading. This page builds nothing: SD-2a's first hour settles it.
  4. **Driven with a camera or a late sensor: no.** A camera instance exists only when the manager creates one from
     an enumerated device, and it opens a real back-end (`camera_instance.cpp:116`: Spinnaker, Aravis, Apple or a
     phone). A deferred source exists only as a `HmInstance` the sensor manager created over Bluetooth
     (`shot_processor.cpp:327–334`, `imu_manager.cpp:867`). No synthetic device of either kind exists
     (`grep -rliE 'synthetic|fake|mock' src/Video src/Gui/cameras src/Gui/imu`: one QML file and two health scripts, no back-end).
- **Options, for where the rig is built.**
  (a) An executable in the app's build tree, run by `ctest` and off by default, as `swing_window_parity_test` is
  (`CMakeLists.txt:3639`, under `PINPOINT_BUILD_TOOLS`). That test links the offline source list; the rig needs
  the app's, which is written inline in the app's own target (`CMakeLists.txt:711`). So the list becomes a
  variable and the rig compiles those sources a second time.
  (b) The same, but with the app's sources in a library that the app and the rig both link.
  (c) Teach the test tree to find ONNX Runtime and FFmpeg for this one target and list its sources by hand.
  (d) Link-time stubs in the test tree for the managers, the camera instance, the exporter and the analysis.
  (e) Drive the built app itself, as `headless_probe_test` does, with a new mode that injects streams and a trigger.
- **Recommendation: (a).** It links the code that ships and leaves the app's own link alone. (b) is the better end
  state, but it changes how the product links on three platforms, and Part 2 of Stage B ruled no library for this
  code yet[^l11]; it comes for free after SD-13. (c) hand-lists well over a hundred sources while Part 2 removes
  hand-listed sources. (d) replaces whole classes with copies that drift, and a stubbed exporter cannot pin a write.
  (e) puts a test-only mode in the product.
- **Cost of (a).** A second compile of the app's sources wherever the option is on; its time is not measured here.
  The cabin's `tests` mode builds only the test tree today, so `tools/studio/studio-build.cmd` changes, and a
  change to that script cannot be checked through the cabin runner (its exit 21): it is checked at a visit or by
  an off-hours run. SD-2a's 3.5 hours becomes about 5.
- **Two limits to state now.** The gather deadline reads the wall clock (`QDateTime::currentMSecsSinceEpoch`,
  `:393`) and the post-roll is a `QTimer`, so a deadline pin takes its real 6 to 7 seconds. "Two runs equal" (SD-2a's
  Accept) can only mean the two documents less their timestamps and paths: the brief names the fields left out.
- **Class: Coordinator's call.** C1–C11: none (a test executable and its build). It touches the real-store rule:
  the rig runs on a scratch INI and a scratch library, with the headless switch set in the test's environment.
- **Waits:** SD-2a, and through it the camera-less pins. Four for certain: the post-roll per source and the
  bounds frozen at the pause (SD-2b); which session a shot in flight is allocated in when End is pressed, and a
  card with no document (SD-2c). Three more only if a camera-less analysis succeeds, because each needs a
  document: the first write, the staged writes and a late reading (SD-2c).
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-41, for the Product Owner's review).

### T2 · The seam the other four pins need
- **Today.** At least four of the eleven cases SD-2b and SD-2c pin cannot be reached by T1's rig. Two need a late
  sensor: the two ordered deadlines, and "reserve at detection, collect at the gather". Two need a camera:
  back-to-back swings and a swing just before End, each "a full clip on every camera" (the plan's Test row, and
  #53's cases). The three that need a document (T1) join them if a camera-less analysis fails.
  The plan asks for pins "passing on today's code, before any of it moves"[^plan], so any seam is itself a change
  made before its pin.
- **Options.**
  (a) No seam. Pin 4 to 7 of 11; leave the rest to the cabin visit.
  (b) Two additive seams at the device edge. **A synthetic camera back-end** beside Spinnaker, Aravis and Apple,
  compiled only under a build option as two of those are, registered through the enumerator's existing
  `registerDevice`: the manager and the shot processor are not edited. **A deferred-source interface** (reserve,
  poll, collect) that `HmInstance` implements, with the shot processor handed its sources through a setter, as it
  is already handed the repository and the unassigned athlete (`shot_processor.h:160`, `:171`): about four call
  sites in the class change. Faking the HackMotion sensor below `HmInstance` instead would mean emulating its
  Bluetooth protocol.
  (c) Bring SD-8a forward: put the shot processor on its narrow interfaces first, and pin through them.
  (d) Lift `postRollMsFor` and the two deadline constants word for word into `shot_source_map.h`, which a test
  already covers, as the launch and join decisions were lifted into `shot_outcome.h`. It pins the values and their
  order, not the behaviour.
- **Recommendation: (b) and (d).** (d) is cheap, runs in the ordinary test tree on every cabin run, and has a
  precedent twice over. (b) leaves the class under test almost untouched and its pins stay valid while the class is
  split, because they drive devices, the buffer and signals, not interfaces the split will reshape. (c) writes the
  pins against the very interfaces SD-8b to SD-8f change, so "pins unedited" could not hold. (a) leaves #53's two
  cases, half the timing invariants, and perhaps the R-REC-1 write itself, to a visit that happens two-thirds of
  the way through the stage.
- **Cost.** One new item, about 3 hours, after SD-2a and before the four pins. The synthetic camera also gives
  `CameraInstance` and the two managers their first test, which SD-11 and SD-12 have none of today. Not checked
  here: whether a synthetic back-end meets all seven pure virtuals of `VideoInputBase` without a real clock; the
  item's first hour does.
- **Class: HARD: the Solution Architect, for the deferred-source interface.** It is the first of the capture
  interfaces S1 asks about, and a new interface needs a ring (C9). **The synthetic camera back-end alone is the
  coordinator's call**: a file in an Adapters directory behind a build option; no capture key records the back-end
  (`grep -n backend src/Export/capture_export_facts.h src/Gui/shot/capture_context.cpp`: 0), so nothing persisted
  changes. **(d) is the coordinator's call**: no file is added and none changes ring. For the Product Owner to know:
  (b) edits about four lines of the shot processor before their pin exists.
- **Waits:** the two deadline pins and the reserve-and-collect pin (SD-2b); the back-to-back and near-End cases
  (SD-2c); and the three document cases if T1's rig cannot write one. SD-8a and everything after it wait for all
  eleven.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

## The ten questions

### S1 · The capture service's ring, and where its device half goes
- **Today.** `shot_processor.cpp` and `.h` are Application by the lint's per-file entries and carry 17 A1 findings
  (13 + 4: `grep -E 'Gui/shot/shot_processor\.(h|cpp) \| A1' src/Gui/tests/architecture_allowlist.txt`). The
  information model already says the capture service is Application, in `Gui/shot` "until Stage D", and that its
  device half leaves in this refactor (RING-APPLICATION, RING-ADAPTERS).
- **Options.** (a) The service stays Application. Its device half (the pause, the live cameras' facts, the
  deferred sources, the capture intent) moves to a collaborator in the Adapters ring, behind interfaces the
  Application ring defines. (b) Re-tag the whole service Adapters, with a thin Application command in front.
  (c) Leave it, and re-tag the 17 findings as kept.
- **Recommendation: (a)**, as the plan proposes[^plan]. The service decides what a shot is and writes its first
  document, which is not a device's job; (b) would put the R-REC-1 write in the Adapters ring. The interfaces carry
  the three guards (below): a trigger with a kind, not "a ball impact"; a shot kind that is not two-valued.
- **Class: HARD: the Solution Architect.** Keeping the service in Application matches the page. The new interfaces
  and the collaborator's directory each need a ring (C9).
- **Waits:** SD-8a to SD-8f, SD-9a, and T2's interface.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov]. Whether the split that builds it (SD-8a to SD-8c, SD-8f) is built is decided with the pins' results[^d-v4].

### S2 · When the shot is allocated, and when its session is bound
- **Today.** `Session::addShot` is called in two places (`grep -n 'addShot(' src/Gui/shot/shot_processor.cpp`:
  `:1234` for a captured shot, `:1403` for a device-only one). The captured shot's call sits in
  `buildSwingExportJob`, which runs when the export starts, after the analysis has finished: by the file's own note
  12 to 37 seconds after the trigger. The session is read at that same moment (`m_shotSession = m_openSessionObject`,
  `:1232`). So neither of the plan's two answers describes today: it is not at the freeze, and it is before the
  export, not after it. By reading, End pressed inside that gap clears the open session (`:1352`) before the shot
  has one; #188 is that case, and it has not been observed since step 3.4a-ii. SD-2c observes it first.
- **Options.** (a) Today's moment. (b) Bind the session at the trigger; allocate at the freeze, when a window
  exists. (c) Bind and allocate at the trigger.
- **Recommendation: (b).** Binding at the trigger is what makes a shot in flight land in the session it was hit in.
  Allocating at the freeze, not the trigger, means a trigger that dies in the post-roll (the buffer torn down)
  uses no swing number and leaves no folder; a number is never reissued (LC-SHOT). The order DEC-142 rules, allocate
  then create then write, is unchanged[^dec142].
- **What the ruling must also say.** Under (b) a `Shot` exists for the length of the analysis before its document
  does. LC-SHOT's first state is "capturing (late parts pending)", which assumes a document. What is a shot whose
  document never lands: a state of its own, shown with a warning, or an allocation undone? That is #235's half (1).
- **Class: HARD: the Solution Architect** (C8: who creates the shot and when, and its states).
- **Waits:** SD-8d, SD-8e, SD-10a.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov]. S2 (SD-8d) is built on today's shot processor straight after the pins, before any split[^d-v4].

### S3 · The window's handle
- **Today.** `swing_window.h` and `swing_payload_source.h` are Vocabulary by the lint's per-file entries and include
  `source_ring.h` (Adapters) for one nested type, `SourceRing::ReadHandle`: a pointer, a size, a timestamp and a
  generation number (`grep -n 'source_ring.h\|ReadHandle' src/Buffer/swing_window.h src/Buffer/swing_payload_source.h`:
  2 includes, 3 uses). That is their 2 A1 lines. `ReadHandle` is named 82 times in 29 files
  (`grep -rn ReadHandle src --include='*.h' --include='*.cpp' | wc -l`).
- **Options.** (a) Define the handle as a plain value in `swing_payload_source.h`, which is already Vocabulary;
  `SourceRing::ReadHandle` becomes a name for it, and its `validate` moves onto the ring. (b) A new header for it.
  (c) The window copies its payloads, so it holds no pointer into ring memory.
- **Recommendation: (a).** It closes both lines with no new file. (c) gives up the zero-copy read the buffer is
  designed around and is a performance change nobody asked for. The handle still points into ring memory, so the
  window is still valid only while the buffer is paused: the guide says so today and must keep saying so.
- **Class: Coordinator's call for (a).** It makes two Vocabulary headers obey the rule they are already under,
  which §1.2 excludes; no file is added, moved or re-tagged, and the lint's tables are not edited. **Option (b)
  adds a per-file ring entry (C9) and would be HARD.** IM §4.1 names neither the handle nor `source_ring`. The plan marks this row "for the Solution
  Architect"[^plan], so it is shown in the same sitting; SD-3 cannot start before B2.5 in any case.
- **Waits:** SD-3, then SD-4 (Buffer's seven headers to `src/Vocabulary/Buffer/`, already ruled[^j109]).
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-41, for the Product Owner's review).

### S4 · The shot's source as a persisted type
- **Today.** `ShotController::Source` is `{ Manual, Imu, Pose, Ball, Acoustic, Ppcp }`
  (`src/Gui/shot/shot_controller.h:89`), and its position in that list is what the swing file keeps.
  `capture.shotSource` is written as a number for a captured shot (`recording_header.cpp:188`) and as the word
  `"launchMonitor"` for a device-only one (`swing_doc_writer.cpp:999`). It is read as a number in two places, one of
  which gates the microphone correction on the value 4 (`recording_loader.cpp:754`, `:789`), and as text in two
  (`club_vocabulary.h:173`, `swing_store.cpp:116`)
  (`grep -rn 'shotSource"' src --include='*.h' --include='*.cpp' | grep -v /tests/`). So one key holds two shapes, and inserting a source before
  `Acoustic` would silently change what every later re-analysis believes.
- **Options.** (a) A Vocabulary enum with each value's number written out, the file unchanged, and one reader that
  accepts both shapes. (b) Write names from now on and read both. (c) Leave it, with a compile-time check on the
  numbers.
- **Recommendation: (a).** No file written so far changes meaning and no schema version moves. (b) is the tidier
  file, but it is a format change with nothing waiting on it. The type must not be called `ShotSource`:
  `src/App/shot_source.h` already defines that name as the carousel's port. `TriggerSource` is proposed.
- **Class: HARD: the Solution Architect.** A new Vocabulary type and its home (C9). Option (b) would also change a
  persisted field (C3).
- **Waits:** SD-9b, then SD-9c (#78's two-phones case); SD-1c lists every reader first.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S5 · Who owns the launch-monitor board (D-6)
- **Today.** The board's interim owner is `SessionReviewController`. `lmBoard` is named 56 times in 10 files, 53
  in 9 product files (`grep -rn lmBoard src --include='*.h' --include='*.cpp' --include='*.qml' | wc -l`). Three
  allowlist lines tagged `platform refactor (#224)` hang on it: `lm_club_art.h` A6 ×2 and `lm_session_model.h`
  A6 ×1. `LmSessionModel` is on lint A2's list of session-scoped types.
- **Options.** (a) A presenter over `SessionSeries`, owned by the session and reached through it; the "No launch
  monitor connected" state comes from the device. (b) An application-level owner. (c) Leave the interim owner, and
  move D-6 to #224-U (the forecast's lever 10, 3 hours, not proposed here).
- **Recommendation: (a)**, as the plan proposes[^plan]. The board's rows are one session's shots.
- **Class: HARD: the Solution Architect** (C1: an owner of facts).
- **Waits:** SD-10b, SD-10c.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S6 · The device enumerator's home, and its `Device` struct
- **Today.** `src/Core/device_enumerator.{h,cpp}` (159 + 490 lines) is the one named Adapters exception left in
  `Core`[^b1r]. Thirteen product files include it: `Gui/imu` 3, `Video` 3, `Audio` 2, `Gui/cameras` 2, and one each
  in `Core`, `Gui/shot` and `Ppcp` (`grep -rlE '#include "(.*/)?device_enumerator\.h"' src | grep -v /tests/`). Its `Device` struct holds a video back-end, camera capabilities, a sensor
  transport, a sensor vendor, sensor capabilities and a Bluetooth handle in one value, so every includer compiles
  the Video and IMU headers. It has no thread check (`grep -n 'currentThread' src/Core/device_enumerator.cpp`: 0).
- **Options.** (a) Move it to the Adapters capture directory S9 names; split `Device` into a common part (kind, id,
  description) and one part each for a camera and a sensor. (b) Move it whole. (c) Leave it in `Core`.
- **Recommendation: (a).** After it, `Core` is one ring. The GUI-thread assert the Rig design asks for goes in
  with the move.
- **Class: HARD: the Solution Architect** (C9: RING-ADAPTERS names the file by its path).
- **Waits:** SD-6. It also needs B1.11.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S7 · The motion-capture probe's home
- **Today.** `src/Pose/motion_capture_probe.{h,cpp}` (150 + 331 lines) is Adapters by a per-file entry in a Rules
  directory. It includes `session_controller.h` to hold off while a session runs (A1 ×1) and hands QML an untyped
  map (A6 ×1) (`grep -n motion_capture_probe src/Gui/tests/architecture_allowlist.txt`). Its only user outside
  `src/Pose` is `main.cpp` (`grep -rlE 'motion_capture_probe\.h|MotionCaptureProbe' src`).
- **Options.** (a) Move it beside the enumerator, and hand it "is a session running?" as a question it is given, not
  a controller it includes. (b) Leave it in `src/Pose` and inject the question only.
- **Recommendation: (a).** It probes hardware; it is not a pose rule. #86's two defects are fixed in the same item.
- **Class: HARD: the Solution Architect** for the move (C9). The injected question alone makes code match the page
  and is not a change.
- **Waits:** SD-7.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S8 · Live pose and ball triggering across the Acquisition boundary
- **Today.** The camera instance includes the pose estimator's base, the MoveNet estimator and the ball detector
  (4 include lines in `camera_instance.h` and `.cpp`), to trigger a shot live. Those are Diagnostics' rules.
  R-CTX-1 says Acquisition "knows nothing of" Diagnostics, so the capture directories are tagged Domain +
  Application until this stage decides.
Both detectors take an OpenCV frame (`estimatePose(const cv::Mat &)`, `detect(const cv::Mat &, qint64)`).
- **Options.** (a) Acquisition declares a small "live detector" interface beside the camera code (frames in, a
  trigger or a ball sample out; Adapters ring). A Rules file may not include an Adapters header, so the detectors
  do not implement it themselves: a thin wrapper in the hub does (Adapters ring, Domain + Application), over the
  estimator and the detector as they are, and the composition root joins the two. No Rules file changes. (b) Tag the two
  detectors Acquisition, or Shared. (c) Keep the capture code tagged Domain + Application for good.
- **Recommendation: (a).** It is R-CTX-1 as written: the earlier context names nothing later. (b) puts pose
  inference in Acquisition, where the analysis could no longer use it without a carve-out. (c) leaves the three
  largest capture classes outside the context they are. The interface speaks of a trigger and its kind, never of
  "impact", so a rehearsal swing fits it (the second guard).
- **Class: HARD: the Solution Architect** (C9: the ring and context of the interface, of the wrapper and of the
  capture files; C11 if R-CTX-1 is to be read any other way).
- **Waits:** SD-11c, and B1.21's lint A9 for the check.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S9 · Where the capture files in `Gui` go
- **Today.** `src/Gui/cameras`, `src/Gui/imu` and `src/Gui/shot` hold 47 C++ files and 20,695 lines (10, 13 and
  24 files; `find src/Gui/{cameras,imu,shot} -maxdepth 1 \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \) | wc -l`),
  and 20 QML files, which stay. They have no test directory of their own. The Rig design and Part 2 of Stage B both
  leave these moves to this stage[^rig].
  By the lint today (`grep -n 'Gui/shot/' src/Gui/tests/architecture_lint_test.cpp`), four of the 28 file stems
  are Application (`shot_processor`, `shot_controller`, `reanalysis_controller`, `shot_source_map`), two are Rules
  (`shot_arbiter`, `shot_outcome`), and the other 22 are Adapters.
- **Options.** (a) By ring, one ring to a directory. (b) One directory for all of it, spanning three rings.
  (c) Leave them, and keep the per-file ring entries.
- **Recommendation: (a)**, placed as below. A directory then has one ring, which is what Part 2's libraries need.
  The names are a proposal only; `src/Acquisition/` reads better against the four contexts, but `Buffer`, `Video`
  and `Ppcp` are Acquisition too and are not moving. SD-13's map confirms each file by its includes before it moves.

  | To | Ring | File stems |
  |---|---|---|
  | `src/App/capture/` | Application | what `shot_processor` becomes under S1; `shot_controller`; `shot_source_map` |
  | `src/Capture/rules/` | Rules | `shot_arbiter`, `shot_outcome` |
  | `src/Capture/devices/` | Adapters | `camera_instance`, `camera_manager`, `buffer_controller`, `camera_role_probe`, `imu_instance`, `imu_manager`, `imu_device`, `hm_instance`, `paced_connect_queue`, `capture_context`; the enumerator (S6) and the probe (S7); S1's collaborator |
  | stays in `Gui` | Adapters | `viz_frame`, `shot_list_model`, `shot_filter_proxy_model`, `shot_replay_controller`, `replay_source`, `disk_replay_source`, `qml_map_ref`, `qml_payload`, and the 20 QML files: presentation |
  | not this stage's to place | — | `camera_roi`, `imu_role_map` and `trunk_ceremony_recorder` (the Rig's, Stage C); `ppcp_clip_filer` (the phone link, Stage F); `reanalysis_controller` (re-analysis, not capture) |
- **Class: HARD: the Solution Architect** (C9). Whether the stage's five QML globals leave with these files is
  V1's question, not this row's.
- **Waits:** SD-13, then SD-14 (SD-14 since moved to an issue[^d-v2]).
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S10 · The context map: here, or with the coaching track
- **Today.** The context map is kept for later, with two triggers: the coaching track's first information-model
  change, or Stage D lifting the capture deferral[^pr9]. Stage D lifts it at SD-13. The map was tested on 963
  cross-context includes: 886 fit, 68 by a carve-out, 9 fit neither (the ruling's figures, not re-taken here).
- **Options.** (a) With the coaching track, named on its plan. (b) Here, as one more item (about 2.5 hours) after
  SD-13.
- **Recommendation: (a).** The map checks what the order cannot: that Coaching never re-derives and never reads raw
  streams. Until Coaching has code there is nothing for it to catch that lint A9 does not.
- **Class: Product Owner** (which of two triggers, already ruled as alternatives, is taken). Applying the map, when
  it is applied, is the Solution Architect's (C9, C11).
- **Waits:** nothing in Stage D under (a).
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

## The two levers, as proposals

### V1 · Q17: no QML global is renamed for the count
- **Today.** 18 QML context globals (`grep -n setContextProperty src/Gui/main.cpp src/Gui/app/*.cpp | wc -l`). Stage D's five are `cameraManager`, `imuManager`, `bufferController`,
  `launchMonitor` and `liveWrist`: 203 uses in 33 QML files (`grep -rnE '\b(cameraManager|imuManager|bufferController|launchMonitor|liveWrist)\.' src --include='*.qml' | wc -l`;
  81, 94, 1, 13 and 15). The item plan's 218 in 39 used a wider pattern. SD-12d moves them with a script, alone on
  the tree.
- **Proposal.** A global leaves only when its object gets a new owner, never to lower the count. In Stage D that is
  the board (S5) and nothing else, so SD-12d is dropped and the count stays near 18. The Accept row's "globals
  18 → 13" goes.
- **Reason.** A renamed global moves a name, not an owner, and wrapping them into new `devices` objects would
  itself be an information-model change (C1) with no gain[^q17]. It also avoids scripted edits to QML of the kind
  the domain refactor made without their tests.
- **Hours.** Stage D: **3** (SD-12d). The whole refactor: **9.5** (with Stage E's SE-5h, 3.5, and SE-9b, 3).
- **Class: Product Owner** (an Accept count and an item's scope; nothing in the model changes). One point stays
  with the Solution Architect and nothing waits on it: R-QML-1 allows "devices" as globals, and `liveWrist` is a
  presenter over a sensor, not a device.
- **Waits:** SD-12d.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### V2 · Q5: lint A6 back to P9's wording
- **Today.** P9 and the information model's A6 forbid an untyped map or list "in a presenter's or a domain type's
  public API". The lint was widened to every header, and that call still waits for the Solution Architect. Stage
  D's A6 lines: the five device headers, 10 findings (`buffer_controller.h` 1, `camera_instance.h` 2,
  `camera_manager.h` 2, `imu_instance.h` 2, `imu_manager.h` 3); the probe, 1; the board, 3 (`grep -E '\| A6 \|' src/Gui/tests/architecture_allowlist.txt`: 31 lines, 143 findings in all).
- **Proposal.** Narrow A6 to P9's wording. The device objects and the probe are neither presenters nor domain
  types, so their 11 findings leave the rule and SD-12c is dropped. **The board's model is a presenter, so its
  finding stays in scope and SD-10c stays.** The club art's 2 findings are on a QML singleton that hands out the
  drawing's anchor points; whether that is a presenter is for the ruling to say, and SD-10c halves if it is not.
- **Reason.** One ruling settles the lines instead of re-tagging them one at a time[^q5]. The same page also says
  the device shapes are "worth typing whatever the ruling", because they will carry rig facts[^q5]; after Stage C
  the Rig holds those facts in typed form, so the proposal is to re-count the device maps when Stage C closes and
  type only one that still carries a rig or capture fact.
- **Hours.** Stage D: **3** (SD-12c), up to **4.5** if the club art leaves the rule too, or **0** if the device
  shapes are typed regardless. The forecast gave Stage D 6 by counting all of SD-10c, which is too high by the
  board's model at least. The whole refactor: **6 to 7.5** (with Stage E's SE-9a, 3), and about **9 more that
  never arrive**: if A6 stays wide, Stage E inherits 42 findings in four headers that no plan holds.
- **Class: HARD: the Solution Architect.** A change to a lint rule's scope is an information-model change (C4,
  C9)[^imc].
- **Waits:** SD-12c; in Stage E, SE-9a and the three items it would otherwise add.
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

## What the design must also answer

**The three guards the four contexts set**[^guards]. They constrain S1, S4 and S8; none is an item.
- **The shot kind is not two-valued.** `addShot` already takes a kind (`Captured`, `DeviceOnly`). The interfaces of
  S1 pass the kind through and never test "captured or not".
- **A trigger is not always a ball impact.** S1's and S8's interfaces carry a trigger and its source (S4). The
  post-roll already differs by source; nothing new may assume a ball.
- **A play source is an Acquisition adapter.** It sits beside the launch-monitor connectors, in the Adapters
  directory S9 names. No row here builds one.

**E-09's capture half** ("`ShotProcessor` mixes capture, job building, serialisation, document write, carousel
rows, a replay player and session-folder ownership. Untested") is closed by SD-2a to SD-2c (untested) and SD-8a
to SD-8f (the mix). SD-15c re-takes it. Whether the split (SD-8a to SD-8c, SD-8f) is built is decided after the pins[^d-v4].

**No design-system change.** SD-10c, SD-12c and SD-12d re-point QML with no change of look, and V1 and V2 would
drop two of the three. If an item finds it needs a new component, variant or state, it stops and asks the Design
Lead[^hard].

**Already ruled, so not asked here.** Buffer's stream types move under `src/Vocabulary/`[^j109]. A device-only
recording records handedness (R-REC-8). #235's half (1) is Stage D's. `pp_rules_vision` was sent here from Part 2
of Stage B (since moved to an issue[^d-v2]). The 24 settings fallbacks are deleted in this stage, as the files they are in are rewritten.

**The walkthrough deck.** If S1, S2 or S5 is ruled as recommended, `docs/developer/information_model_walkthrough.pptx`
no longer shows the shot's allocation or the board's owner as built. The Product Owner regenerates it when the
items land.

## Ownership (IM §10), as proposed

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived | 5. Persisted where; the one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| The capture service (RING-APPLICATION) | application state | the application | the composition root | n/a | nothing of its own | Application; its device half Adapters (S1) | `app` |
| A shot being captured (LC-SHOT, DEC-142, EVT-01) | source | the `Shot`, allocated by `Session::addShot`; shot level | the freeze, in the session bound at the trigger (S2); ends at its first document; its shot id | n/a | the swing file: capture once (R-REC-1), then the `Shot` | Domain model (no new file) | the session's carousel |
| A late reading (R-REC-2, EVT-22) | source | the `Shot` | `Shot::completeCapture` | n/a | the swing file, through the repository | no new file | the session |
| The trigger's source (S4) | source (a field) | the `Shot` | capture | n/a | the swing file's existing `capture.shotSource` | Vocabulary | the card |
| The launch-monitor board (S5) | view state over derived rows | the `Session`; session level | the `Session` | `SessionSeries`; its events | none | Adapters (a presenter) | through the session |
| The deferred-source and live-detector interfaces (T2, S1, S8) | none: an interface holds no fact | n/a | n/a | n/a | none | Application (T2, S1); Adapters (S8: the interface with the camera code, its wrapper in the hub) | not reached by a view |

Information-model change? **Yes**, if ruled as recommended: C1 (S5), C8 (S2), C9 (T2, S1, S4, S6, S7, S8, S9, V2),
C4 (V2). C3 only under S4's option (b). §11 is updated by SD-15c.

## What waits on what

| Row | Class | Items that wait |
|---|---|---|
| T1 | Coordinator | SD-2a; 4 to 7 of the 11 pins in SD-2b and SD-2c |
| T2 | **HARD: the Solution Architect** (the interface); Coordinator (the camera back-end, the lifted values) | the other 4 to 7 pins; one new item; then SD-8a onward |
| S1 | **HARD: the Solution Architect** | SD-8a to SD-8f, SD-9a |
| S2 | **HARD: the Solution Architect** | SD-8d, SD-8e, SD-10a |
| S3 | Coordinator (option a) | SD-3, SD-4 |
| S4 | **HARD: the Solution Architect** | SD-9b, SD-9c |
| S5 | **HARD: the Solution Architect** | SD-10b, SD-10c |
| S6 | **HARD: the Solution Architect** | SD-6 |
| S7 | **HARD: the Solution Architect** | SD-7 |
| S8 | **HARD: the Solution Architect** | SD-11c |
| S9 | **HARD: the Solution Architect** | SD-13 (SD-14 moved to an issue[^d-v2]) |
| S10 | Product Owner | nothing, as recommended |
| V1 | Product Owner | SD-12d |
| V2 | **HARD: the Solution Architect** | SD-12c; Stage E's SE-9a |

Not waiting on any row: SD-1a to SD-1e (the inventories), SD-5 (the pixel-format mapping), SD-11a and SD-11b, SD-12a and SD-12b (SD-11a, SD-11b and SD-12a since dropped[^d-v1]), SD-15a to SD-15c. Each still waits for the items the plan
puts before it.

**Effect on the item plan, if every row is ruled as recommended.** One item added (T2, about 3 h) and SD-2a
1.5 h longer (T1); two items dropped (V1's SD-12d and V2's SD-12c, 6 h). About 98 h becomes about 96.5 h, for 37
items. The larger effect is on risk: the plan's range hung on whether the rig needs a seam, and the answer is yes,
for at least 4 of the 11 pins and for 7 if a camera-less analysis does not succeed.

[^hard]: `CLAUDE.md`: any information-model change needs the Solution Architect's approval, and any design-system change the Design Lead's, before it is built.
[^plan]: `docs/implementation/platform_refactor_assessment.md`, "Stage D · The capture seam": its bullets marked "(proposed, for the Solution Architect)" and "(proposed, for the Product Owner)", and its Closes, Test and Accept rows.
[^pland]: `build/platform/DEF/PLAN-D.md` (the items SD-0 to SD-15c, their order and hours) and `build/platform/DEF/FORECAST.md` (levers 1 and 2), both of 24 Nov 2026.
[^imc]: `governance/artefacts/information_model.md` §1.2 (C1–C11), §1.4 (the lint rules; its last line makes a change of a rule's scope an information-model change: DEC-120), P9, R-QML-1, R-CTX-1, RING-APPLICATION, RING-ADAPTERS, LC-SHOT, R-REC-1 and R-REC-2, read as excerpts.
[^inc]: The method's rule: check the include graph before proposing a dependency rule.
[^dec142]: The Solution Architect, 15 Nov 2026 (DEC-142): `Session::addShot` allocates, creates through `RecordingRepository::create` and returns the `Shot`; capture then writes.
[^j109]: The plan's "Rulings of 24 Nov": Buffer's stream types, the launch monitor's reading types and `frame_decode` move under `src/Vocabulary/`, Buffer's in Stage D (J-109; the Solution Architect). It stands: the lever pass's withdrawal of Buffer's part was reversed on 25 Nov 2026[^d-v3].
[^l11]: `docs/implementation/platform_b2_libraries.md`, L11: no library for the Application ring in Part 2 (the coordinator's call, 24 Nov 2026).
[^b1r]: `docs/implementation/platform_b1_placements.md`: the `Core/` rule becomes Platform, with `device_enumerator` the one named exception until Stage D (the Solution Architect, 24 Nov 2026).
[^rig]: `docs/design/rig_design.md`: "It does not move `Gui/cameras` or `Gui/imu`; Stage D does"; `docs/implementation/platform_b2_libraries.md`, L11 and P3.
[^pr9]: The plan's "Rulings of 24 Nov": the context map is kept for later (the Product Owner; `build/platform/JUDGEMENT-CALLS.md`, PR-9).
[^q17]: The plan's "Mandatory carry-ins" (iii) and §9 Q17: "Do not wrap for the count's sake" (proposed there for the Solution Architect).
[^q5]: The plan's Premise 4 and §9 Q5: narrow A6 to P9's wording; and, in the same premise, that the camera, sensor and buffer-controller shapes are worth typing whatever the ruling (both proposed there for the Solution Architect).
[^guards]: The plan's Stage D, "Guards from the four contexts" (ruled 24 Nov; `build/platform/JUDGEMENT-CALLS.md`, PR-4).

## The lever pass of 25 Nov

After every row above was ruled, a lever pass ranked Stage D's remaining items by what each buys per hour (`build/platform/LEVERS/LEVERS-U-C-D.md` §3). Its rulings[^d-lever]:

- **D-V1, taken: the camera and manager tidying is dropped.** SD-11a (`CameraInstance`'s pipeline to a collaborator), SD-11b (its ball-presence monitor to a collaborator) and SD-12a (the managers' shared selection mechanics) leave the plan: 8.5–9 h. They close no lint line and no issue. `CameraInstance` stays at about 2,186 lines and each manager keeps its own copy of the selection code. SD-11c re-bases to follow SD-1d, and SD-12b to follow SD-1e[^d-v1].
- **D-V2, taken: `pp_rules_vision` (SD-14) moves to an issue**, #292: about 3 h. It re-opens B2's P3, which sent it here[^d-v2].
- **D-V3, taken and then reversed: Buffer's seven stream-type headers move to `src/Vocabulary/Buffer/`, as J-109 ruled.** SD-3 (the window's handle as a plain value, S3) and SD-4 (the move) stay in Stage D: 4–4.5 h. The window types' two A1 lines and the seven per-file Vocabulary entries go, so the architecture lint ends with one Vocabulary rule, and SD-13 still ends with Buffer no longer spanning rings[^d-v3].
- **D-V4, taken as an order.** SD-8d (S2: bind the session at the trigger, allocate at the freeze) and the new teardown item are built on today's shot processor straight after the pins (SD-2b, SD-2c). The split (SD-8a, SD-8b, SD-8c, SD-8f) is kept in the plan, and whether it is built is decided with the pins' results; until then none of its 10–12.5 h leaves the plan[^d-v4].
- **Three items added (7.5–13 h, guesses):** the teardown that finishes or records a stopped shot (GAP-1's other half, 2–4 h; it rests on GU-3, an accepted shot is never lost without a trace, which is not yet ruled); one owner of the trigger policy (S11 and S13, 2.5–4 h); and landing #281's overlapping-capture patch, with a test of the shot queue on SD-2a's rig and its memory and copy time measured on the cabin (3–5 h)[^d-lever].

[^d-lever]: The Product Owner and the Solution Architect, 25 Nov 2026: the lever pass on #224-U, Stage C and Stage D, ruled as recommended, with D-V3 and D-V4 ruled by the Product Owner after the Solution Architect.
[^d-v1]: The Product Owner, 25 Nov 2026 (lever D-V1, as recommended): dropped, not deferred; no issue.
[^d-v2]: The Product Owner, 25 Nov 2026 (lever D-V2, as recommended): moved to an issue, #292.
[^d-v3]: The Product Owner and the Solution Architect, 25 Nov 2026: lever D-V3 was first taken (the Product Owner, after the Solution Architect re-opened and withdrew J-109's Buffer part), then reversed the same night. Buffer's stream types move to `src/Vocabulary/` in Stage D as J-109 ruled, keeping the Solution Architect's mandate of 23 Nov 2026 that the architecture lint ends with one Vocabulary rule. The information model says so again from v1.12.14.
[^d-v4]: The Product Owner, 25 Nov 2026 (lever D-V4, taken as an order); the Solution Architect agreed that S2 can be built on the unsplit class.

## Added after SD-1c (the inventory of `ShotController`, the arbiter and the source ordinals, 25 Nov 2026)

Evidence for each: `build/platform/DEF/DUTIES-SD-1c.md` and `NOTES-SD-1c.md`. **Only one reader acts on the persisted source number** (`recording_loader.cpp:789`, `== 4` with no `micTravel`), and the share holds only `4`, `"launchMonitor"` or no key, so renumbering the enum is safe if that one reader moves with it.

### S11 · Who owns trigger policy? (HARD: the Solution Architect)
- **Today.** It sits in `main.cpp` lambdas with no owning object (F1). The device-only shot bypasses the controller (F3), and the rescue overwrites the trigger source (F4).
- **Recommendation.** One owner, the capture service's trigger policy, in the Application ring: the device-only shot goes through it, and the rescue records its own source beside the trigger's, never over it (proposed, for the Solution Architect).
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov]. Built by a new item, one owner of the trigger policy (2.5–4 h)[^d-lever].

### S12 · Does R-INS-3 or R-INS-5 govern the impact instant? (HARD: the Solution Architect)
- **Today.** Both rules read on it; the code follows neither consistently (F5).
- **Recommendation.** R-INS-5 (rank the readings; the estimate is derived), the Product Owner's standing rule for estimates (proposed, for the Solution Architect).
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

### S13 · Pose has no producer for a capture-time instant, and does the local arbiter survive SD-9c? (HARD: the Solution Architect)
- **Today.** F2 and F13.
- **Recommendation.** No pose producer for capture time (pose comes after the shot); the local arbiter folds into the trigger policy of S11 (proposed, for the Solution Architect).
- **Ruled:** as recommended (the Solution Architect and the Product Owner, 25 Nov 2026)[^d-ruled-25nov].

[^d-ruled-25nov]: Ruled in the platform programme's coordination session, 25 Nov 2026: every open question of this pack as recommended. S2 (bind at the trigger, allocate at the freeze) is the structural fix for the lifecycle model's GAP-1; S10 is the context map with the coaching track (#277); S13's two-phone part is parked with Stage F (#272); Q17: a QML global leaves only when its owner changes.
