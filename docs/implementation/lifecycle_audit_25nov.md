# Lifecycle audit, 25 Nov 2026: the lifecycle model's starting audit

| Record | Value |
|---|---|
| **What it is** | A dated audit of the code against the lifecycle model (`governance/artefacts/lifecycle_model.md`): where today's behaviour falls short of an obvious guarantee, where the code differs from a ruled lifecycle, and what was not traced. It is a record, not a specification: it is never edited to follow the code. A later audit is a new, dated file |
| **Pinned to** | `main` at `15d41578`. Every file:line was re-checked at `10473584`, where only build files had changed |
| **Evidence** | The code (file:line); the studio visit of 25 Nov 2026 (cited as "visit 1 §n" and "the step-n log"); seam tests built in worktrees, not yet on `main` |
| **Labels** | *Observed* (visit 1, §n) · *seam, in a worktree* · *reading* (read in the code, not run) |
| **Bugs and findings** | Under AG-24, only an observed gap cites an issue. A gap known by reading is a finding for the role that rules on it, never a bug to file. Nothing was filed by this audit |
| **Ids** | `GAP-n`. The model's tables cite them; the guarantees' "Today" column names them as "gap n" |
| **Review** | Two independent review rounds, 25 Nov 2026, each blocking; their fixes are applied |

## 1. The gaps

Each gap is a place where today's behaviour falls short of a guarantee a golfer would assume. Each has its evidence, its label and the guarantee it breaks (the lifecycle model's §6). Under AG-24, only an **observed** gap cites an issue. A gap known **by reading** is a finding for the role that rules on it, never a bug to file. Nothing was filed here.

**The session type.** Several rows describe code that still steers by the session type: SES-T3's "same type", SES-T6, SHOT-T3, End's carry sweep and GAP-16. That steering is what #273 retires (DEC-202); no guarantee leans on it.

### The shot

**GAP-1 · A live shot exists only after its analysis.**
- A live, analysed swing has no `Shot`, no number, no folder and no file from the impact until `Session::addShot`, seconds to 40 s later (`src/Gui/shot/shot_processor.cpp:1229-1234`; NOTES-280 §1). The exceptions are under SHOT-T6.
- **What this explains:**
  - why a lost shot leaves no trace: nothing on disk says it was accepted;
  - why a session whose only shot is in flight counts as empty, and is discarded (GAP-12, GAP-13, and the toolbar End under SES-T7).
- **What it does not explain: the drops.** Those are `finishNowBlocking`'s (`src/Gui/shot/shot_processor.cpp:2117-2155`; GAP-6, GAP-7, GAP-18, GAP-19). It stops the post-roll timer, joins the workers, then clears the in-flight flags, so no later step runs. GAP-19 happens after the allocation, so it does not come from this gap.
- **So allocating at the trigger alone (Stage D's SD-8d) would not save a shot.** It would turn each GAP-6, GAP-7 and GAP-18 drop into a GAP-19 folder with no document: still lost, now with a trace. Saving every accepted shot needs both changes: the early allocation, and a teardown that finishes or records the shot.
- LC-SHOT does not say when `addShot` runs relative to the trigger; the code runs it after the analysis (§2 below).
- *Reading*; its effects were *observed* (visit 1 §4, §6), and those are issues #280 and #188.
- Breaks GU-3.

**GAP-2 · A swing hit while the previous shot analyses is not recorded, and nothing says so.**
- The trigger is disarmed while the processor is busy (`src/Gui/shot/shot_controller.cpp:99-102`). The buffer is paused for the whole shot (`src/Gui/shot/shot_processor.cpp:337-392`, `:2091-2103`).
- The refusal logs at debug level only, which the default log level leaves out of every build (`src/Gui/shot/shot_controller.cpp:247-250`). The code calls the real fix "deliberately not attempted" (`:553-557`).
- *Observed* (visit 1 §4, swing 2); *seam, in a worktree* (`capture_overlap_test`, NOTES-281 §2; not on `main`).
- Issue #281 (part A).
- Still pending: the stopgap that tells the golfer (NOTES-281-stopgap) is built and held for a Design Lead ruling; it is not on `main`. Overlapping capture is parked for Stage D.
- Breaks GU-1.

**GAP-3 · The next swing's clip can lose its start.**
- When a shot finishes, the buffer resumes with its rings cleared (`src/Buffer/event_buffer.cpp:723-746`). A swing hit soon after gets only what was recorded since the resume.
- At visit 1 that was 278 frames against 595, with 2.2 s of pre-roll missing.
- The "starts late" warning exists, but only in the document, so it is lost when the shot is dropped.
- *Observed* (visit 1 §4, swing 3); *seam, in a worktree* (NOTES-281 §2; not on `main`).
- Issue #281 (and #53).
- Breaks GU-1.

**GAP-4 · A launch-monitor reading that arrives before its own shot is committed is discarded.**
- The pairing has nothing armed yet, so the reading "arrived with no shot waiting" (`src/Gui/launchmonitor/launch_monitor_controller.cpp:422`).
- *Observed* (visit 1 §4, swing 3's reading; NOTES-281 §1).
- Issue #281. The fix is parked for Stage D.
- Breaks GU-8.

**GAP-5 · Noise becomes a saved shot.**
- Keyboard typing, glove noise and a sneeze were each detected and saved. Only the golfer's delete removes them.
- *Observed* (visit 1 §8).
- Issue #282.
- Breaks GU-21.

**GAP-6 · End during capture or analysis drops the shot.**
- The toolbar's End releases the devices. That reaches `finishNowBlocking`, which clears the handlers' flags (`src/Gui/session/PpSessionToolbar.qml:419-431`; `src/App/app_state.cpp:220-239`; `src/Gui/shot/shot_processor.cpp:2117-2155`).
- *Observed* (visit 1 §4, swing 3); *seam, in a worktree* (`app_state_test` S10, NOTES-280 §10; S10 is not in `main`'s test).
- Issues #281 (part B) and #188.
- **Closed, 25 Nov 2026, by a878282f** (NOTES-280 §9, §11; product control AD-50): End waits for a shot being saved, or "End anyway" drops it by the golfer's choice.
- Breaks GU-2, GU-5.

**GAP-7 · Changing the camera selection, or deselecting a sensor, drops a shot in flight.**
- `CameraManager::setSelected` calls `finishNowBlocking` on any change of selection, selecting a camera included (`src/Gui/cameras/camera_manager.cpp:370`). `ImuManager::setSelected` calls it only when deselecting (`src/Gui/imu/imu_manager.cpp:467-468`). Both act whatever is in flight. Nothing shows that a link loss reaches either.
- The End and quit fix (a878282f, 25 Nov 2026) covers End and the close prompt only.
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-2.

**GAP-8 · Carry was not shown live, and late launch-monitor data waits in memory.**
- *Observed* (visit 1 §3): no card showed carry during the session for any of the three shots. End's sweep filled one carry, and all three files hold one afterwards, so two were on file before End. Why the cards did not show them was not traced. The device itself sends Premier's club data only with the next shot (visit 1 §3).
- *Reading*: a fuller reading for a saved swing is held in memory while any shot is in the pipeline, and written when it goes idle (`src/Gui/launchmonitor/launch_monitor_controller.cpp:690-750`). A carry the per-reading lookup missed is written only by End's sweep (`src/Gui/launchmonitor/launch_monitor_controller.cpp:78-88`). So a crash or a Cmd+Q (GAP-17) before either point loses it.
- The observed half (carry not shown live) is the one observed gap with no issue: the one new candidate bug, not filed here. The by-reading half is a finding (AG-24).
- Breaks GU-8.

**GAP-9 · A rating or a note sends no event.**
- So a second view of the same session misses it (`src/DomainModel/shot.cpp:191-196`).
- **Ruled, not a defect:** DEC-70 decided that a rating and a note send no event. GU-15 asks the Solution Architect to revisit it.
- *Reading*. A finding (AG-24).
- Breaks GU-15.

**GAP-10 · "Recoverable" lasts only until the OS empties its trash, and the swing number's record is best effort.**
- A trash goes to the OS trash first (`src/Export/swing_paths.cpp:185-201`). Windows can empty a full Recycle Bin by itself (DEC-175 keeps this for now).
- The high-water write before a shot's move is best effort, so a failed write could let a trashed swing's number be issued again (`src/Export/session_store.cpp:1004-1010`; DEC-91).
- *Reading*.
- The OS trash is ruled (DEC-175, kept for now); the best-effort number is a finding (AG-24).
- Breaks GU-4, GU-14.

**GAP-11 · The re-analysis queue lives in memory.**
- A quit drops the queued swings without a word. Only the swing in hand is waited for (`src/Gui/shot/reanalysis_controller.cpp:38-68`; NOTES-SE-6a H2).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-16.

### The session

**GAP-12 · The empty-session discard counts only swing folders, and is silent.**
- `Session::end` trashes a session this run started that has no swing folder (`src/DomainModel/session.cpp:215-233`, through `src/Export/session_store.cpp:391-402`).
- It does so even when the session holds a trunk ceremony (FILE-SESS-11), a declared focus (FILE-SESS-02), or a shot still analysing (GAP-13).
- The golfer is told nothing; the app log has one info line (`src/DomainModel/session.cpp:232`). The folder goes to the OS trash, which the app does not show.
- DEC-93 rules the discard itself.
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-6.

**GAP-13 · "End session & close" while the only shot analyses loses the shot and the whole session.**
- The close ends the session at once (`src/Gui/shell/Main.qml:190-196`). The session holds no swing folder yet (GAP-1), so it is discarded as empty (GAP-12). Then the teardown joins the analysis and drops the shot, and nothing of it is written (SHOT-T16, the analysing case).
- *Observed* (visit 1 §6, attempt 3: no folder found).
- The cause is *by reading* and **unconfirmed**. The app log cannot confirm it: it is kept in memory and went with that instance. Two ways remain. The Product Owner can look in the trash of the account that ran the app for a session folder from about 13:01 (NOTES-280 §1). Or a seam test can end a session whose only shot is analysing and check for the discard (NOTES-280 §9's S10 shows it, in a worktree).
- Issue #280. The Product Owner ruled the loss the golfer's choice.
- **Closed, 25 Nov 2026, by a878282f** (NOTES-280 §9; product control AD-50): the close prompt waits for a shot being saved, or "Close anyway" drops it by the golfer's choice.
- Breaks GU-6, GU-7.

**GAP-14 · A session left in progress is stuck until its athlete's next Start.**
- It cannot be trashed, archived or moved (`src/Export/session_store.cpp:744-763`; `src/DomainModel/athlete.cpp:433-434`). Its athlete cannot be deleted (`src/DomainModel/athlete.cpp:245-251`). It cannot be continued the next day (DEC-134).
- No command ends it short of starting a new session. A search of `src/Gui`, `src/App` and `src/DomainModel` finds only two ways to end a session: the capture service's own open session (`src/Gui/shot/shot_processor.cpp:1338-1349`) and the sessions running here (`src/DomainModel/athlete.cpp:321-327`). Neither reaches a session left by a crash.
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-9.

**GAP-15 · Starting a session leaves the app in Replay if it was there.**
- `AppState::startSession` sets no mode (`src/App/app_state.cpp:189-205`).
- It never did. Before refactor step 4-a, Start could not be reached while a saved session was on screen, so the missing switch was hidden. The set-up screen and the one start command made Start reachable from Replay (the coordinator's account, 25 Nov 2026). This guarantee held only because a path was unreachable (the lifecycle model's §6, the rule above the table).
- *Observed* (visit 1 §8).
- Issue #283. **Closed, 25 Nov 2026, by 8f33e190** (#283 closed): every start path lands in Capture.
- Breaks GU-12.

**GAP-16 · Capture and the guards ask different questions.**
- The trigger arms on "the buffer is capturing" with no session term (`src/Gui/shot/shot_controller.cpp:99-102`). A swing outside a session starts an on-demand one (`src/Gui/shot/shot_processor.cpp:1371-1393`).
- The close prompt and the updaters ask whether the session clock runs (`src/Gui/shell/Main.qml:172`; `src/Update/win_sparkle_update.cpp:75-78`).
- A third "live" fact: the session type, `activeSessionType`, drives End's carry sweep (`src/Gui/launchmonitor/launch_monitor_controller.cpp:81-87`) and the work-ons pause (`src/Gui/main.cpp:701-703`). Under DEC-202 the type should steer nothing.
- When capture can run with no session begun was not traced: capture intent is off at launch (`src/Gui/cameras/camera_manager.h:242`).
- *Reading*.
- A finding (AG-24).
- Breaks GU-13.

### The application

**GAP-17 · Cmd+Q and the app menu's Quit on the Mac may bypass the close prompt.**
- A code comment says so (`src/Gui/shell/Main.qml:167-169`); it is unverified on Qt 6.11, whose macOS termination may already send close events to the windows. Check it on the Mac before it is treated as a gap. If it holds, the session is not ended, so it is left in progress (GAP-14), and End's carry sweep does not run (GAP-8).
- *Reading* (a code comment; not run).
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-7.

**GAP-18 · A quit during a shot's post-roll forfeits the shot.**
- The chime has played, and nothing is written (`src/Gui/shot/shot_processor.cpp:2119`: "a pending shot is forfeited — acceptable on teardown"; NOTES-SE-6a L1).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-2, GU-7.

**GAP-19 · Stopping a shot during its save leaves media with no document, or a document that is never announced.**
- This applies to all three routes into `finishNowBlocking`: End (SHOT-T15), a quit (SHOT-T16) and a deselect (SHOT-T17).
- Once the shot is allocated (SHOT-S4), the folder and number exist. `finishNowBlocking` waits for the media save, then clears the flags. The document write is started only from the save's handler, so it never starts (`src/Gui/shot/shot_processor.cpp:2117-2155`; NOTES-SE-6a L2).
- A document write already in flight finishes and is written. But its handler is a no-op, so EVT-01 is never announced (`src/Gui/shot/shot_processor.cpp:1739-1746`).
- Either way, the session now has a swing folder, so End keeps it.
- At the next load, a folder with no document is held as a shot whose findings are "unread": the firing reader finds no document file (`src/Export/firing_reader_store.cpp:94-99`). What the carousel shows for it was not traced.
- *Reading*. Visit 1's launch log names three shots in older sessions read this way ("no swing document yet"), so such folders exist in the studio's library (*observed*). Their cause is unknown.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-2, GU-7.

**GAP-20 · A quit can wait with no window, with no time limit.**
- It waits for a shot waiting on the pose warm-up (H1), a re-analysis (H2), an export or a Linux update's copy (H3), or the Settings benchmark (H4).
- Each `QThread`'s teardown is bounded only by the call in progress (H5) (NOTES-SE-6a §3; `src/Gui/main.cpp:159-163`).
- *Reading*. Not reproduced at visit 1: every timed close took 0.7–1.7 s (§5). The close that joined an analysis (§6, attempt 3) has no click time; the process was gone 12.7 s after the close was sent.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-10.

**GAP-21 · A quit can touch destroyed objects or kill a thread.**
- A Mac permission answer after its controller has gone (D1); the sensor scan `terminate()`d after 3 s (D2); a report still rendering into static destruction (D3) (NOTES-SE-6a §6).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-10.

**GAP-22 · On the Mac, a held update can fire inside End.**
- `AppState::endSession` stops the clock before it ends the session (`src/App/app_state.cpp:226-228`). The clock's `runningChanged` (`src/Gui/session/session_controller.cpp:126`) fires the held install at once (`src/Update/mac_sparkle_update.mm:85-93`, `:181-185`).
- Whether Sparkle's install quits before `Session::end` writes the session file was not traced.
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-11.

**GAP-23 · The updaters guard only "a session is running".**
- A re-analysis, an export, the catch-up or a report being made is not a guard (`src/Update/win_sparkle_update.cpp:75-78`; `src/Update/update_controller.cpp:156-157`). So an update's quit meets GAP-20.
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-11.

**GAP-24 · The screen freezes at the steps of the lifecycle.**
- 3.96 s at the first launch (during the catch-up), about 3 s at each session start, and 1.3–2.9 s after each save and at End.
- *Observed* (visit 1 §2, §8).
- Issue #284.
- Breaks GU-19.

### Devices

**GAP-25 · Sensor calibration does not follow the link or the role.**
- A Witmotion's mid-stream drop starts no retry and keeps its calibration on the object (`src/Gui/imu/imu_instance.cpp:1111-1117`).
- A role moved to another body segment keeps the old segment's calibration (DUTIES-C-6i CL8, F34).
- A HackMotion's link loss invalidates its calibration, but the golfer is told only through the state (CL5).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-17.

**GAP-26 · A trunk ceremony can be lost before it is written.**
- It is lost on a quit while it is held (the destructor only stops the taps: `src/Gui/imu/trunk_ceremony_recorder.cpp:55-58`), on a session started with no folder (`:392-411`), or on a failed write, since it is cleared from memory first (`:416-451`).
- *Reading* (DUTIES-trunk_ceremony_recorder §2, F10).
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-18.

### The athlete

**GAP-27 · Renaming a settings-only athlete moves where its sessions are looked for.**
- For an athlete whose record is still a settings group (ATH-S1a), a rename re-derives the athlete's folder from the new name (`src/Export/athlete_store.cpp:441-448`: "a settings athlete's folder follows its name until M1"). The sessions in the old folder are then no longer the athlete's folder.
- A library athlete is safe: its folder never moves (`src/Export/athlete_store.cpp:449-455`).
- Whether any library still holds a settings-only athlete was not traced.
- *Reading* (the code calls it E-02's last case).
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-23.

**GAP-28 · Deleting an athlete is one large move into the OS trash.**
- The whole athlete folder goes, every session and video in it (`src/Export/athlete_store.cpp:490-596`), to the OS trash first (`src/Export/swing_paths.cpp:185-201`; DEC-175).
- Whether the OS keeps a folder that large, or empties it at once, was not traced. Neither was whether the golfer is told when it does.
- Deleting is also blocked while any of the athlete's sessions is InProgress, a crash's included, until a new session is started for them (`src/DomainModel/athlete.cpp:245-251`; GAP-14).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-24.

### The Rig (continued)

**GAP-29 · "Zero orientation" says the calibration is cleared, and leaves the alignment applied.**
- `ImuInstance::zeroOrientation` re-seeds the device and logs "Calibration cleared — recalibration required". But it calls `clearCalibration`, which clears only the legacy half (`src/Gui/imu/imu_instance.cpp:570-585`, `:755-763`). The functional alignment (A, M) stays applied, and "fully calibrated" is unchanged (DUTIES-C-6i CL1, CL3, F35).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-27.

**GAP-30 · A HackMotion can stop being calibrated without the "invalidated" signal.**
- When the library reports ABSENT or INDETERMINATE, only the state changes (`src/Gui/imu/hm_instance.cpp:400-409`). The `calibrationInvalidated` signal comes only from a link loss or a stream restart (`src/Gui/imu/hm_instance.cpp:1024-1080`). So a listener that hears only the invalidation misses it (DUTIES-C-6i CL6, F36; the Rig design does not place it).
- *Reading*.
- A finding (AG-24), for the role that rules on it; not a bug to file.
- Breaks GU-27.


## 2. Where the code differs from the ruled lifecycles

| LC row | The ruled row says | The code does | Gap |
|---|---|---|---|
| LC-SHOT | `Session::addShot` allocates, creates and returns the `Shot`, which capture then writes into: allocate → create → capture writes → late parts | The code follows that order (`src/DomainModel/session.cpp:157-161`). The row does not say when `addShot` runs relative to the trigger; the code runs it after the analysis. The row does not name the outcomes "dropped", "media with no document" or "not recorded". | GAP-1, GAP-19 |
| LC-SESSION | A session left InProgress by a crash stays so "until `end` or the athlete's next `startSession`" | No golfer command ends it: only the next Start does (the search under GAP-14) | GAP-14 |
| LC-SESSION | The empty discard is a rule of the lifecycle (DEC-93) | "Empty" means no swing folder, not "nothing recorded" | GAP-12 |
| LC-SESSION | `end` | Committed, 25 Nov 2026 (a878282f): the views wait for `shotSaving` before calling End (a new line in the information model's §6, ruled not an information-model change) | GAP-6 (closed) |
| LC-ATHLETE | "active → trashed → purged". Its "today" note says `deleteAthlete` removes only the settings group, leaving the folder and its sessions behind | That note is stale: delete now moves the whole folder (`src/Export/athlete_store.cpp:490-596`). The code also has states the row does not name: settings-only, read-only, recording and the Unassigned owner (§4.1) | GAP-27, GAP-28 |
| LC-RIG | "no states"; created at launch; commands change it; EVT-15 | Not built on `main`: no class, no producer (Stage C). The lifecycles that run today are a device's and a calibration record's (the model's §5) | GAP-29, GAP-30 |
| none | — | The application has no lifecycle row. §1 is offered as one: **LC-APP (proposed, for the Solution Architect)** | — |

## 3. Not traced

- **Camera link loss.** What happens when a camera stops sending mid-session. No handler was found by search.
- **The launch monitor's and the microphone's link loss.**
- **The carousel and a doc-less swing folder.** How the carousel shows one (GAP-19), and whether the next launch tidies it.
- **WinSparkle's retry.** Whether WinSparkle asks again after a refused shutdown.
- **Sparkle's quit.** Whether Sparkle's install quits through `aboutToQuit` (GAP-22).
- **The athlete switch.** Whether its refusal while capture runs (R-APP-1) holds on every route.
- **The archive's checks.** The byte-for-byte check and the three-way merge on restore were taken from the information model, not re-read.
- **The OS trash.** A session put back from the OS trash, and how it is listed again.
- **The athlete's delete.** What the OS trash does with a whole athlete folder (GAP-28). Whether a refused switch or delete during a session tells the golfer: the controller logs it (`src/Gui/athlete/athlete_controller.cpp:947-950`, `:979-982`), and the QML was not read.
- **Who sees the trash.** Visit 1's missing folder (GAP-13) would be in the trash of the account that ran the app. The visit's search ran as another account, so it could not have seen it.
