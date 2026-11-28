# NOTES-281: a swing hit while the previous one analyses is never detected, and the next one's clip is short (#281 part A)

**REJECTED, 28 Nov 2026, kept for the record.** The owner: "the next shot should only be captured once the current shot has been analysed and saved"; "there is no queue". One shot at a time is the design; the trigger arms once every device's buffer is active again (#394, step 85). Nothing below is to be built.

**State (before the ruling): built and green on the Mac; seam-tested only; unverified at the studio PC; uncommitted.**
- **This is not a minimal fix, and it could not be.** Re-arming the detector alone would detect a shot that has no
  frames: the paused buffer records nothing, and the resume clears the rings.
- The brief's "detected and fully captured while an earlier shot analyses" is the "overlapping shot processing" that
  `shot_controller.cpp` l.557 deliberately left undone. It needs three changes:
  1. a copied window;
  2. a queue of captured shots;
  3. one pairing slot per swing.
- That is about 800 changed lines in 15 files, 4 of them tests. §5 says what Stage D must keep.

Worktree `build/wt-5b` at `6def679e` (origin/main; `merge --ff-only`: already up to date). Lane 2: shot detection and
the capture window. Lane 1 (`build/wt-37c`, NOTES-280) holds End and quit (#280, #281 part B) and says it edits none of
`shot_processor.*`, `shot_controller.*`, `camera_manager.*` and `imu_manager.*`. Unattended; no commits.

## 1. The cause, with evidence (written before any fix)

**One mechanism causes both symptoms.** From the freeze of one shot until that shot has finished its analysis, export
and document write, the whole event buffer stays **Paused**. Then `resume()` **clears every ring**.

The code path (`src/Gui/shot/shot_processor.cpp`, `src/Buffer/event_buffer.cpp`):
- `beginGather()` (l.345) calls `pauseBuffer()` at post-roll expiry (impact + 1.25 s for Acoustic, plus the arbiter's
  200 ms hold). `makeRingPayloadSource()` (l.376) raises the resume guard (`swing_window_live_`).
- The `SwingWindow` reads the frozen ring zero-copy: "Both workers read the same frozen window — const, zero-copy reads
  over stable memory (producers stopped while Paused)" (l.689).
- The guard holds `CameraManager::resumeBuffer()` off until `finishShot()` (l.2091) destroys the window. That happens
  after the analysis, the export, the document write and any in-window replay.
- `EventBuffer::resume()` with `resume_clear_rings = true` (event_buffer_config.h l.40) resets every ring and the
  index (event_buffer.cpp l.736–746).
- Meanwhile `ShotController::armed()` = capturing **and not processor-busy** and not review (shot_controller.cpp
  l.101). main.cpp l.1271 feeds it `ShotProcessor::busy()` (any state but Idle). While paused, `acquireWriteSlot`
  refuses every frame (event_buffer.h l.92), so **nothing at all is recorded**.
- A refused candidate is logged at `ppDebug` only (shot_controller.cpp l.248, l.595), which a release build compiles
  away. **That is why the Message Log has no line for swing 2.** The code knew this was a limit: "The real fix is
  overlapping shot processing … Deliberately not attempted here" (l.557).

**Swing 2 (≈12:28:50).** It came during swing 1's processing (window captured 12:28:42, joined 12:28:57): the buffer
was paused and the detector disarmed. No ShotController line appears, only Premier's reading `8a790d49`, "arrived with
no shot waiting; discarded" (step3 log l.416). The swing was never recorded, not just undetected: with the buffer
paused, not one frame of it reached a ring.

**Swing 3 (12:28:58), 278 frames against 595.**
- Swing 1's join tail ran on the GUI thread at 12:28:57 (l.393, "total 1430 ms"), and `finishShot()` resumed the
  buffer with cleared rings.
- Swing 3 committed at 12:28:58 (l.379–380). Its window was frozen 1.25 s later and holds 557 entries = 2 × 278 + the
  shot marker (l.378).
- 278 frames / 150.714 fps = **1.845 s**: exactly the time from the resume to the freeze. The window asks for 4 s, so
  its first **2.155 s are missing: the pre-roll is cut, not the post-roll.** At most about 0.4 s before impact was all that
  had been recorded since the resume (impact 16.06 s after swing 1's; l.379 vs l.451).
- The pose pass found no swing span in it (l.375) and the shaft track was invalid (l.368), as #53's defect swing did.

**Swing 3's launch-monitor reading was lost as well, by a second, smaller mechanism.** Its reading `82f81b3a` arrived at
12:28:58 *before* the shot commit (l.384–382 come before l.380–379). The GUI thread was busy with swing 1's join (UiStall
2310 ms at 12:28:57, l.389), so the arbiter's 200 ms hold timer fired late. `ShotPairing` saw nothing armed and
discarded the reading. The reading's own contact time would have matched the swing.

**#53's late clip is very likely the same mechanism (an inference: that visit's log is not in hand).** A golfer's
swing_0010 came 9 s after swing_0009: "232 frames covering 1.5 s, … both cameras' videos begin about a quarter of a
second after impact". A clip starting *after* impact needs a candidate whose impact came before the resume but which
was handled after the re-arm. That is what the join tail's GUI-thread stall does (1.4–2.9 s, l.393/l.389):
1. The acoustic detector's candidate is queued from the audio thread onto the GUI thread.
2. It waits behind `finishShot()`, which resumes the buffer (rings cleared) and then re-arms.
3. It is handled after that, still carrying its back-dated impact.

Swing 3 is the same case with the resume ≈0.4 s before impact rather than ≈0.26 s after.

**Why the warning wasn't seen.** The capture check already flags this. `captureIntegrityOf` sets `lateStart` when a
lane starts more than 50 ms after the window's start (capture_integrity_check.h l.207), and the gate `shot.captureShort`
shows it on the card. Swing 3 would have carried the warning, but End dropped it before its document was written
(lane 1, #281 B).

## 2. Reproduction (on the Mac, no devices)

No test builds a `ShotProcessor` (PLAN-D premise 1), and a headless probe lists no camera (camera_manager.cpp l.48), so
the app cannot make a shot here. The reproduction is at the seams the cause lives in, on the real `EventBuffer` with
a fake 150.714 fps two-camera feed (G1's fake): `src/Buffer/tests/capture_overlap_test.cpp`.

- **`TodaysProtocol_SwingTwoIsNeverRecordedAndSwingThreeStartsAtTheResume`** drives the buffer as `ShotProcessor` did
  until now, on visit 1's timeline (swing 2 at +8.5 s, swing 3 at +16.06 s, freeze = impact + 1.45 s):
  1. Pause at swing 1's freeze and hold the ring source; the window is whole (603 frames a lane).
  2. Swing 2's 4.2 s of frames: **0 accepted, 1,266 refused**.
  3. Resume 278 frames before swing 3's freeze (as the log puts it); the rings are cleared.
  4. Swing 3's window: **278 frames a lane**, `lateStart`, `missingStartMs` ≈ 2155, not `earlyEnd`, `ok`, 59 frames
     before impact, `edgesShort()` (the ⚠).

  It passes on the old code: it is the reproduction. Run 1: `build/platform/A/281-ctest1-red.log`.
- **The answers to the brief's three questions.**
  - Is the detector disarmed while analysing? **Yes:** `armed()` includes `!m_processorBusy`, fed by `busy()`.
  - Is the ring shared with, or held by, the previous shot's analysis? **Held:** the zero-copy window keeps the whole
    buffer Paused, and the resume clears it.
  - Is the post-roll cut by the next arm? **No:** the cut is at the start.
- **Swing 3's lost reading** is reproduced in `gcquad_pairing_test`,
  `ShotPairingInFlight.AReadingThatArrivesBeforeItsShotIsCommittedIsAttachedByItsContactTime`.

## 3. The fix

**What changed**

1. **The window is copied out at the freeze, and capture resumes at once.**
   - `EventBuffer::copyPayloads` / `copySwingWindow` (`src/Buffer/event_buffer.{h,cpp}`) copy every payload the window
     names, and every source's format, into an `OwnedPayloadSource`: one block per source, no resume guard.
   - `ShotProcessor::beginGather` pauses, snapshots, copies, then calls `applyCaptureIntent()` (the resume, unless the
     user pressed Stop). It logs `window frozen — N entries, M MiB copied in T ms`.
   - The deferred (HackMotion) stitch reads the copy, so the gather no longer holds the buffer either.
2. **Capture and processing are two stages, with a queue between them** (`shot_processor.{h,cpp}`).
   - `m_captureStage` (PostRoll / Gathering) is separate from `m_state` (Processing / Replaying).
   - A captured shot is a `ShotCapture`: every per-shot value the processing half reads 12–37 s later (source,
     impact, session type, label, window bounds, wall-clock anchor, HM provenance and stitch counts, the window, the
     replay tracks and the Recording). It waits in `m_captured`, in order; `launchNext()` moves it into the members
     the processing half has always read, so the analysis, export, join and replay code is unchanged.
   - The Recording (`snapshotAtFreeze`) is still taken at the end of the capture, so a queued shot records its own
     moment's club, not one a later shot's reading switched to.
   - `m_swingWindow` is now a `unique_ptr`, so the Recording's pointer survives the move out of the queue.
   - At most `kMaxQueuedShots = 2` wait behind the one processing.
3. **Arming.**
   - `ShotProcessor::captureBusy()` (capture stage active, or queue full) now disarms `ShotController`; main.cpp's one
     wiring line changed from `busy()`. `ShotController` itself is untouched, so its decline reasons and
     `shot_controller_decline_test` still hold.
   - `busy()` keeps its meaning (anything in flight, queue included). The ANALYSING badge, the LM's held club data and
     the re-analysis hold read it unchanged.
   - When the queue fills, the log says so at ppWarn.
4. **Each launch-monitor reading attaches to its own shot** (`src/LaunchMonitor/shot_pairing.h`).
   - **One slot per swing in flight.** Readings go to the newest armed swing, as before. Folders go to the oldest
     swing without one, as the pipeline finishes them in order. A new swing stops the previous one waiting but keeps
     what it parked. `replaceParked` (Premier's late club data) searches every swing.
   - **A timed reading that beat its commit is held** (`holdUnclaimed`, from the LM controller's discard path; not one
     from before our impact). The next committed shot takes it if the contact Δ ≤ 1500 ms (swing 3's case), and the
     LM logs it.
   - **The LM arms from `ShotProcessor::shotAccepted`** (main.cpp), emitted inside the same `shotDetected` delivery.
     So the pairing waits only on shots the pipeline will finish or drop.
   - **New `ShotProcessor::shotsDropped`** (from `finishNowBlocking` when anything was in flight, and from the
     capture-stage abort). The LM (`onShotsDropped`) and the clip filer clear what they held, so a dropped shot's
     reading can never reach the next shot's folder.
5. **Phone clips attach to their own shot** (`ppcp_clip_filer.{h,cpp}`). `awaiting()` is the oldest shot waiting for a
   folder, not the newest: with the newest, swing 1's folder would have taken swing 2's clip.
   `onShotsDropped()` abandons every waiting shot, each as `onSwingFailed()` (declined `discarded`).
6. **A really cut window still carries the warning (#53's rule).** The capture check (`lateStart` / `earlyEnd`,
   `shot.captureShort`) was already right, and the reproduction pins it on visit 1's exact window. A clip can still be
   cut:
   - by the copy's own pause, tens to hundreds of ms;
   - by Stop pressed in the post-roll (`earlyEnd`);
   - by a swing hit before capture started.

   All of these keep the ⚠. Nothing new was needed in the check.

**Tests (red first, then green)**
- Run 1 (`281-ctest1-red.log`, against stubs with the new names and today's behaviour):
  - `capture_overlap_test`: the copy-out cases fail, 1,206 of swing 1's frames overwritten under its analysis, and
    the guard still up. The reproduction passes.
  - `gcquad_pairing_test`: 5 fail (the two rewritten old cases and three in-flight cases).
  - `ppcp_clip_filer_test`: 2 fail (A's clip filed into B's folder).
- **`ppcp_clip_filer_test`'s "dropped" case and `AHeldReadingIsNeverGivenToAShotItDoesNotMatch` passed in run 1.** They
  guard hazards the fix itself introduces (oldest-first filing; the held reading), so they could not fail first.
- **Two old pairing tests are rewritten, not weakened:** `ANewShotDisplacesASwingStillWaiting` and
  `AParkedReadingDoesNotLeakIntoTheNextSwing`.
  - Both encoded "a second shot means the first produced no folder", which `shot_pairing.h` itself said would have to
    go when the invariant went ("this class needs a map and this comment needs deleting").
  - The first now also asserts that A's folder gets nothing. The second asserts that the reading stays with its own
    swing.
  - The leak hazard stays covered by `DropsAParkedReadingWhenTheShotFails` and the new
    `ShotsDroppedInFlightTakeTheirReadingsWithThem` / `AFailedShotTakesOnlyItsOwnReading`.

## 4. Ownership (IM §10)

Ids: LC-SHOT, R-REC-1, EVT-01, RING-ADAPTERS (Stage D's own list in PLAN-D). Read: §1.2, §9.1, §10, LC-SHOT.

| New or changed thing (ids) | 1. Kind | 2. Owner, level | 3. Created / ended by; identity | 4. If derived | 5. Persisted; one writer | 6. Ring of each new file | 7. How a view reaches it |
|---|---|---|---|---|---|---|---|
| A captured shot waiting for the pipeline (`ShotCapture`, `m_captured`) | application state (a shot's capture before its `Shot` exists) | `ShotProcessor`, application level, as the one in-flight capture always was | created at the freeze (`finishCapture`); ended when the pipeline takes it (`launchNext`) or on `finishNowBlocking`; no identity, it is ordered | — | not persisted; the `Shot` is still allocated by `Session::addShot` at the export and written at the join (LC-SHOT, R-REC-1, EVT-01 unchanged) | no new file | not reached by a view (`busy()` covers it) |
| The window's owned copy (`OwnedPayloadSource`) | application state | the `SwingWindow` that owns it (in the `ShotCapture`, then `ShotProcessor`) | `EventBuffer::copyPayloads` at the freeze; ends with the window | — | not persisted | no new file (inside event_buffer.cpp, Buffer's ring) | — |
| Per-swing pairing slots, the held reading (`ShotPairing`) | application state | `LaunchMonitorController`, application level, as before | a slot per accepted shot; ended by its folder, its failure or `shotsDropped` | — | not persisted; the reading's write is unchanged (`Shot::completeCapture` / the doc) | no new file | — |

**Information-model change? No.**
- No domain object, owner of facts, kind, file, field, identity, event or ring is added or moved (C1–C11).
- The `Shot`'s lifecycle (LC-SHOT: allocated by `Session::addShot`, EVT-01 at the document write) is untouched, and
  shots are still allocated in capture order.
- What changed is how many captures the capture pipeline holds at once, and that its window is a copy.
- **§11 still matches the code this touches.** §11 says nothing about the capture holding the buffer paused.
- The IM walkthrough deck is unaffected.

## 5. What Stage D must keep (PLAN-D: SD-2, SD-8, SD-9)

1. **The window is the shot's own copy, and capture resumes at the freeze.** A capture seam that holds the ring for
   the shot's life brings #281 back.
   - SD-8c ("the export job and the frozen window out") must take `copyPayloads`, not the ring.
   - SD-3's window handle type must not assume a ring backing.
2. **The capture stage and the pipeline are separate, with an ordered queue between them.** Everything a shot needs
   travels in its `ShotCapture`.
   - SD-8a/8b/8f will move these halves to collaborators. They must not reintroduce a member that a later detection
     rewrites.
   - SD-8d ("the shot is allocated by `Session::addShot`, bound at the trigger") must allocate per accepted shot,
     in order.
3. **`ShotController` is disarmed by "cannot accept", not by "busy".** SD-9a's narrow interface should carry that
   predicate.
4. **The pairing and the clip filer finish shots oldest first.**
   - They hear `shotAccepted` / `shotsDropped` from the pipeline, not the controller's `shotDetected`.
   - SD-8e (late readings through `Shot::completeCapture`) should key a reading to its shot when the `Shot` exists
     at the trigger, which would make the order rule unnecessary.
5. **The queue bound and the copy cost are measured numbers to re-take** (the log line `window frozen — … MiB copied
   in … ms`). SD-2a's rig should pin a back-to-back case with the real `ShotProcessor`: there is still no such rig,
   so this fix is seam-tested only.

## 6. For lane 1 (#281 part B; NOTES-280)

- **End can now find more than one shot in flight:**
  - one processing;
  - up to two captured and queued;
  - one in its post-roll.

  `busy()` is true until all of them are done. **End's wait must be for `busy()` false, or it drops the queued ones.**
- `finishNowBlocking()` (reached from End through `cameraManager.disconnectAll()`) now also drops the queued and
  capturing shots. It emits `shotsDropped`, and logs at ppWarn how many captured shots were dropped before analysis.
- No lane-1 file was edited: not `app_state.*`, `session.*`, `Main.qml`, `PpSessionToolbar.qml` or the quit path.
  - main.cpp was edited only in the capture wiring (≈ lines 1220–1280 and 1880): the `setProcessorBusy` source, the
    LM arming signal and two `shotsDropped` connects.
  - `launch_monitor_controller.{h,cpp}` was edited only in `onShotDetected`, the discard path and a new
    `onShotsDropped`; the carry sweep was not touched.
- **A queued shot launched after End** reaches `captureSession()` with no session begun and would get a stray
  on-demand session (#188's path). Lane 1's End fix must drain the queue before `endSessionFolder()`, not only the
  shot processing.

## 7. Builds, tests, docs

- **Builds (3 of 3)**, all through `one_at_a_time.sh`, `-j4`:
  1. `281-build1.log`: the red tests.
  2. `281-build2.log`: the green tests and the app.
  3. `281-build3.log`: the affected suites and the app after the review fixes, with the ctest run.
- **Tests:**
  - New: `capture_overlap_test` (Buffer; the new target in `src/Buffer/tests/CMakeLists.txt`).
  - Added to: `gcquad_pairing_test` (+5 cases, 2 rewritten) and `ppcp_clip_filer_test` (+2 cases).
  - Re-run because they reach the changed code: `swing_window_test`, `event_buffer_test`,
    `shot_controller_decline_test`, `architecture_lint_test`, `namespace_collision_test`, `store_isolation_lint_test`.
  - Results: `281-ctest1-red.log` (red), `281-ctest2.log` (green), `281-build3.log` (the final run).
- **Docs:**
  - `docs/developer/shot_detector_developer_guide.md`: the pipeline step, the armed gate and the refractory, now that
    the gate is the capture's and not the pipeline's.
  - `docs/design/event_buffer_design.md` decision 18: the copied window.
  - Both carry a footnote marking the design **proposed, for the Solution Architect** (AG-14).
  - Not updated, as they describe the old zero-copy pause historically: `docs/design/deferred_sources_design.md` §3.5
    (the resume guard across the gather, now unnecessary) and `docs/design/shot_analyzer_design.md` l.1200/1688/1711.
    A commit should either note them or amend them.
- **G2 (1,500-line gate):** `shot_processor.cpp` was already 2,156 lines and is now 2,256. Its split is Stage D's
  SD-8a–f.
- **No QML, no design-system or Theme change.** The ANALYSING badge still takes the DETECT cluster's place while a shot
  analyses, so the golfer isn't told that detection is live then. That is a question for the Design Lead; nothing was
  built for it.

## 8. What is not done, unverified, or parked (edge, not blocking)

- **Untested:** `ShotProcessor`'s two-stage state machine, its queue and main.cpp's rewiring have no test. No rig
  builds a `ShotProcessor` (SD-2a), so a break there turns nothing red. **They are seam-tested only:** the buffer
  copy-out, the pairing and the filer.
- **Unverified:**
  - Not built with MSVC; not run at the studio PC.
  - The copy's cost is estimated, not measured: about 1.2 GB per shot on the studio's two cameras (595 frames of about
    1 MB), and a GUI-thread memcpy of a few hundred ms during which the buffer is paused. The new log line measures it.
  - Peak memory rises by one copy per shot in flight, up to three; the studio PC's private set was 13–15 GB on visit 1.
    Measure it at the next visit.
- **Residual cut:**
  - `resume()` still clears the rings, so the copy's pause empties them each time. Two triggers 1.5–4 s apart (a #282
    noise, then a real swing) cut the second's pre-roll to the time since the first's freeze. It carries the ⚠.
  - A queue overflow (more than two waiting) refuses detections until one starts, said at ppWarn only. A steady
    cadence faster than the analysis will reach it. Spilling to disk, or the UI's words for it, is a decision for
    Stage D or the Product Owner.
- **Deferred gather (HackMotion):** the capture stage stays busy for up to 6 s past the post-roll. Today's limit
  stands there.
- **Manual shots:** a manual SHOT processed while a phone shot is queued behind it would take that shot's phone clip
  (the filer files oldest first). The DETECT cluster, which is the manual trigger, is hidden while ANALYSING shows, so
  this is not reachable from the UI today.
- **Stale wording:** `ShotController`'s PPCP drop text "still working on the previous one" now fires only when a
  capture is in its post-roll or the queue is full. The text is unchanged (ShotController untouched).
- **Late club data:** Premier's late club data for an earlier swing is held until `busy()` is false. With steady
  hitting that can be the end of the session (the End sweep writes it).
- **Ball detection during in-window replay:** in-window replay (the fallback when nothing reviewable is on disk) stops
  the camera's ball detector (`CameraInstance::setReplaying`). A shot captured during it has no ball samples.
- **#282 (false shots from noise):** not touched. The same detector now stays armed through analysis, so a noise
  during analysis becomes a queued shot rather than nothing. #282's gate is still needed.
