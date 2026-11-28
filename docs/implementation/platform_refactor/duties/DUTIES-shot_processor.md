# The shot processor's duties, each with the pin that holds it (#394, steps 78 and 79)

Taken at `47431e47`. One list for the post-roll, the gather, the freeze, the export, the bookkeeping, the analysis
start and the live replay of `src/Gui/shot/shot_processor.{h,cpp}`. Each row is a ⚠ note or a side effect, and the
pin that holds it, or "not pinned" with the reason. The pins are built in #394's steps 80 (the rig), 81 (the seam),
83 (timing) and 84 (writes). Rig cases are in `capture_rig_test` (the Debug app tree, `PP_CAPTURE_RIG`); a camera-less swing writes no document (its analysis has nothing to read), so the saved swing is the stand-in camera's; the lifted constants are in
`shot_source_map_test` (test tree). **XF** marks a case that is expected to fail on today's code, with its issue.

What the rig cannot reach, and so leaves to the visit or to reading: a real camera back-end (the stand-in camera stands
in for it), the HackMotion's Bluetooth pull below `HmInstance` (a stand-in deferred source stands in), the pose pass,
the x264 encode of a real clip, and QML.

## Post-roll, gather, freeze (step 78)

| # | Duty (where) | Pin |
|---|---|---|
| 1 | Post-roll per source: Manual 500 ms, the four detectors 1250 ms (`:79-83`, `postRollMsFor`) | `shot_source_map_test` (the values); rig T1 (the freeze lands that long after the trigger) |
| 2 | ⚠ PPCP takes Manual's value, on purpose (`:142`) | `shot_source_map_test` |
| 3 | ⚠ The two deadlines are ordered: the library's (7 s past impact) sits inside ours (6 s past the pause) (`:114`) | `shot_source_map_test` (the values and the order for every detector source); rig T4 (the wait ends at our deadline) |
| 4 | ⚠ Reserve at detection, collect at the gather (`:305`, `:439`) | rig T3 (reserve called at the trigger with impact and impact + 7 s; collect after the pause) |
| 5 | A trigger while busy, or with the buffer not capturing, is ignored with a log line (`:290`, `:295`) | rig ONE1 (one shot at a time, by ruling: a swing while a shot is analysed, saved or replayed is refused, and nothing of it is taken; #281 closed as by design) |
| 6 | The post-roll timer racing a teardown is a no-op (`:322`) | not pinned: a teardown pin is SD-8t's (#396) |
| 7 | The pause: `pauseBuffer`, abort to idle if the buffer is not paused (`:349`) | rig R1 (the buffer is paused while the window is processed); the abort: not pinned, needs a buffer torn down mid-post-roll (SD-8t) |
| 8 | ⚠ The window's bounds are frozen at the pause, not after the gather (`:358`, `.h:408`) | rig T2 (with a deferred source holding the gather open, the window still ends at the pause) |
| 9 | The wall-clock anchor read with the window end, in the same breath (`:371`) | rig T2 (`clock.wallclock` within a few ms of the pause, not of the save) |
| 10 | ⚠ The resume guard goes up at the pause and is held until the window dies (`:374`, `.h:389`) | rig T2 (a resume asked for during the gather is refused) |
| 11 | ⚠ No deferred source: the window is built at once, exactly as before deferred sources (`:389`) | rig R1 (no gather state is entered) |
| 12 | ⚠ The gather deadline is not an error and not a cancel (`:415`) | rig T4 (the shot goes on and is saved) |
| 13 | The stitch: a deferred lane replaces its ring entries, served wholly from RAM (`:439-499`) | not pinned in the rig: the stitch maths is `deferred_stitch_test`'s; the rig's stand-in source returns no block |
| 14 | ⚠ Element-wise copy, not `=` (LP64 Linux) (`:460`) | not pinned: a compile trap, caught by the Linux build |
| 15 | One replay track per live camera with frames; the impact camera loops (`:513-550`) | rig with the stand-in camera (step 81): one track per camera; the loop: not pinned (no impact camera stand-in) |
| 16 | The ball accumulator frozen at the freeze; a launch outside the window refused (`:526-548`) | not pinned: the stand-in camera runs no ball detector |
| 17 | Per-shot state cleared at the freeze (`:552-561`) | rig R2 (two runs equal: nothing leaks from the first) |
| 18 | The Recording read once at the freeze: athlete, session, settings, the Rig's learned prior (`:568-595`) | rig W3 (the document's club is the one at the freeze, though changed before the write) |

## Export, bookkeeping, analysis start, live replay (step 79)

| # | Duty (where) | Pin |
|---|---|---|
| 19 | Skip-analysis corpus capture: export only, and join itself if nothing else will (`:603-624`, ⚠ `:612`) | rig R5 (a skip-analysis capture with the stand-in camera: exported, written, processed); with no camera: not pinned |
| 20 | ⚠ The pre-stage does not trim the export (`:633`) | not pinned: true by construction (`SwingExportJob` has no span) |
| 21 | The one job builder on the Recording, origin "live", monotonic progress (`:639-663`, `:271`) | rig R1 (progress ends at 1.0) |
| 22 | The pre-stage runs on a worker and gates the analysis (`:664-698`) | rig R1 (implicitly: the analysis runs) |
| 23 | The analysis and its exception barrier (`:700-728`) | rig R1; the barrier: not pinned (needs a throwing stage) |
| 24 | The club-length prior learned after the analysis, through the Rig (`:1599-1621`) | not pinned: needs a fixed face-on camera and a shaft track; `club_length_prior_test` holds the entry's maths |
| 25 | The export after the analysis, never beside it (`:1623-1627`) | rig W2 (the allocation happens after the analysis has finished) |
| 26 | No swing folder: `swingSaveBlocked`, in the lock's own words when locked (`:743-754`) | rig W4 (a session folder that cannot be written: blocked once, nothing written, a card) |
| 27 | No camera: an analysis-only swing (`:755-759`) | rig R1 (the camera-less swing is analysis-only) |
| 28 | ⚠ Every recording input comes from the freeze's snapshot (`:807`) | rig W3 (as 18) |
| 29 | The impact camera's keep band, gain provenance, strobe and CRF (`:891-941`) | not pinned: no impact camera stand-in |
| 30 | The IMU device objects: aliases, rates, calibration, ⚠ placementSlot not written, ⚠ HackMotion units get their own entries and no binding fields (`:999-1222`) | not pinned in the rig: no sensor instance under the headless switch; the HackMotion half needs a real `HmInstance` |
| 31 | The allocation: `Session::addShot` at the export, not at the trigger (`:1230-1249`) | rig W1, W2 (after the analysis; the first shot of the session begun); `--xfail 188`, `--xfail 188b` (#188: ended while it is analysed, or in its post-roll, the running session is discarded as empty and the shot lands in a new one started on demand) |
| 32 | The session lifecycle: begin, extend, end, on demand, ⚠ the folder fixed in a session, ⚠ D7's library change, ⚠ the start's outcome (`:1252-1576`) | rig W1 (the swing lands in the session begun); `--xfail 188` (End); D7 and extend: not pinned here (`session_test` and the athlete tests hold the Session's side) |
| 33 | The device-only shot through the same allocator (`:1401-1412`) | not pinned: the launch monitor's own path (SD-10a) |
| 34 | The join waits for both workers; the document is written off the GUI thread; nothing is announced before it lands (`:1650-1735`) | rig R3 (the degraded path: one document, the media's, announced as failed) and R5 (one document, announced as processed after it) |
| 35 | EVT-01 once per shot, through the shot's Session (`:1781-1794`) | rig R3, R5 (`shotDocumentAnnounced` once) |
| 36 | The card always lands, with or without a document (`:1812-1849`) | rig R1, W4 (a card with no document, #235 (1)'s case; its edits at End: not pinned, as no right behaviour is ruled) |
| 37 | ⚠ One terminal statement per shot; faulted, not "did not succeed" (`:1868-1891`) | rig R1, R3, R5, W4 (`shotOutcome` once, with the right two flags) |
| 38 | The replay: span clamped to the analysed finish, the view-boundary offset, ¼ speed, ESC (`:1931-2085`, ⚠ `.h:98`) | rig R3 (a shot not reviewable on disk gets the in-window replay, and it ends); the clamp to the analysed finish: not pinned, needs a successful analysis, and the stand-in's frames hold no golfer |
| 39 | ⚠ The replay detail is a `QJSValue`, converted once (`.h:110`) | not pinned: a QML cost, measured by the `[ShotProcessor] join tail` log line |
| 40 | Finish: the window destroyed, then the capture intent applied, then idle (`:2091-2103`) | rig R1 (the buffer capturing again after the shot) |
| 41 | ⚠ Abort releases the resume guard (`:2109`) | not pinned: SD-8t's (#396) |
| 42 | ⚠ Teardown joins every worker, the document writer too (`:2117-2156`) | not pinned: SD-8t's (#396) |
| 43 | ⚠ `swingSaveBlocked` is a condition, not an event (`.h:274`) | rig W4 |
| 44 | The UI-thread stall watchdog (`:215-230`) | not pinned: a diagnostic log line |

## The ready rule (step 85; the owner, 28 Nov 2026)

One shot at a time: the next shot is captured only once the current one is analysed and saved, and there is no queue.
The trigger (`ShotController::armed`) then arms only once every device's buffer is active again: each device source
that has written this session has written since the resume (rig READY1, red before it was built). Only the devices
still connected and delivering are waited for: a camera unplugged, or a sensor whose link drops, never holds it (rig
DROP1, red before the fix: the trigger stayed disarmed with the camera gone). After a shot the
header's badge holds "not ready" until the trigger arms (`ShotController::notReadyAfterShot`; rig ONE1).

## The known bugs, and what the rig does not reach

Each known bug is written to its right behaviour and run as its own test, expected to fail on today's code
(`capture_rig_xfail_<issue>`): #188 (End while a shot is analysed, and in its post-roll) and #53's capture half as it
stands under the ruling: a shot whose impact lies before ready (a detector's or a phone's back-dated impact) is taken,
and saved with no frame before its impact. (#281 is by design: rig ONE1. A swing just after ready with a short pre-roll
is not designed for: the golfer takes longer than that to get ready to hit.) Not reached: a late launch-monitor reading (#192), which needs
a stand-in launch monitor (under the headless switch no connector starts), and the edits of a card with no document at
End (#235 (1)), which has no ruled right behaviour.

## What changes when #379 lands

C-8b adds one read of the Rig at the freeze (row 18) and the `rig` block in the header (row 34). No pin asserts the
whole document, so neither edits a pin; C-8b adds its own cases to the rig.
