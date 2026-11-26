# Cabin visit 2: End and quit while a shot is analysing (folded into visit 3, 26 Nov)

> **Folded into visit 3 (26 Nov).** There is no visit 2 trip. Its two bay steps, V5 and V6, run at visit 3, Stage D's visit, in its latest-main pass; V5a is merged with visit 3's row 3.4 (End with a shot in flight), so it is not run twice[^fold]. Stage C's close (C-16b) waits for that visit. Visit 3's installer of the latest main is the one installer: there is no separate installer A. The desk checks D12 and D13 (§3) stay at the Mac, any time before visit 3, and run from this page. C-16a, this visit's pack, is retired: its work is part of visit 3's pack (SD-15a). Where each step went: `docs/implementation/platform_refactor/visits/FOLD-2-into-3.md`.
>
> This page is kept because it is the only written text of V5, V6, D12 and D13 so far: SD-15a copies V5 and V6 into `VISIT-3.md`, and §3's before-visit runs into visit 3's own list. Read §2 and §3 below as that source, not as a visit of their own.

A visit holds only the steps that need a real shot: golf clubs and a ball being hit. Every other step is automated, or done as a short check at the desk on the Mac, beforehand, and the visit gets only its result[^rule][^recut].

**Re-cut 26 Nov.** The visit is now **End and quit while a shot is analysing** (#280, #281), four sessions with the cameras and the launch monitor only, on **one installer, A: a build of main from `a878282f` (25 Nov) or later, installed before the visit**. That commit brought the fix these runs check. Everything else moved out:
- the three trunk ceremonies, and the HackMotion routine twice in a row (#270), go to **a desk check on the Mac** (§3, D12 and D13; not yet run there);
- #105's check is its two tests in the suite, and the real connect's log lines at visit 3, which wears the sensors: no step;
- the sensors' `rig` blocks on real shots (the old step 2 and V11.6) come from visit 3's shots;
- V1's start folds into V5a.

The placements are proposed, for the Product Owner; each step's place and reason is in `docs/implementation/platform_refactor/visits/RECUT-26nov.md`. Step numbers are kept (V1 to V4 and V7 to V11 no longer run here), so the notes and the Claude session's file still match. Installer B and V7 to V10 were dropped on 25 Nov[^c-v1].

The steps don't depend on each other. If a step fails, note it and carry on with the next.

A Claude Code session on the studio PC runs the visit with you, from `the share (the share's `scratch/visit-2`). It times the steps, reads the session folders, and writes the summary. You wear, hit and click. You copy nothing and write nothing down: after each session it asks you what only you saw.

~~**It needs only the latest main**, which you install at every visit anyway, so it can ride on your next practice at the bay (proposed). The other choice is to fold it into visit 3.~~ **Folded into visit 3** (26 Nov)[^fold].

## 1 · Every step, sorted

One row per step of the procedure as it stood on 25 Nov. On 26 Nov the test became: does the step need a ball hit? A shot hit only so that End keeps the session doesn't count. "Where it runs now" gives each step's place after the re-cut.

| # | Step (as it was) | Needs a ball hit? | Where it runs now | Who |
|---|---|---|---|---|
| 0 | Photos of the camera and sensor settings before the install | **No** | Before the visit: the settings file is copied, read-only, over ssh (D4) | coordinator |
| 1a | Install A | **No**, but it needs the Product Owner's Windows account | Before the visit (D6) | Product Owner |
| 1b | Time the first start-up | **No** | Before the visit: from `install-a.txt` and the app log (D7) | coordinator |
| 1c | About box: the commit matches A | **No** | Before the visit: size and build time over ssh (D7) | coordinator |
| 2 | The cut-over check: nothing lost | **No** | Before the visit: D5 (headless, on the Mac) and D7 | coordinator |
| 3a | Put on the trunk sensors and switch them on | **No** | The desk (D12); at visit 3 as its live run's set-up | |
| 3b | Turn on "Speak calibration instructions" | **No** | Before the visit (D6) | Product Owner |
| 3c | #105: watch both trunk sensors connect | **No** | No step: `wt901_config_check_test` and `wt901_config_retry_test` in the suite (D2), and the settings read-back lines in visit 3's Message Log. Never a desk test of a sensor (AD-11) | coordinator reads the log |
| 4 | Trunk ceremony 1, then a shot | **No**: the shot only kept the session | The desk, on the Mac (D12) | Product Owner |
| 4n | Note each ceremony's session name and start time | **No** | The desk (D12), from the session folders | coordinator |
| 4w | How the sensors were worn; what the voice said | **No** | The desk (D12) | Product Owner |
| 5 | Trunk ceremony 2 | **No** | The desk (D12) | Product Owner |
| 6a | Trunk ceremony 3 | **No** | The desk (D12) | Product Owner |
| 6b | Check the three `trunk_ceremony.json` files | **No** | The desk (D12): copied out as each session starts | coordinator |
| + | End while a shot analyses: wait, then End anyway (#280, #281) | **Yes** | Visit, V5 | |
| + | Quit while a shot analyses: wait, then Close anyway (#280) | **Yes** | Visit, V6 | |
| 7a–10 | Installer B and its procedures | — | **Dropped** (25 Nov)[^c-v1] | |
| 11 | The HackMotion arm procedure, with a short raise on purpose | **No** | The desk, on the Mac (D13) | Product Owner |
| + | The HackMotion calibration twice in a row (#270) | **No** | The desk, on the Mac (D13) | Product Owner |
| 11.6 | A shot with the HackMotion on, for the arm's `rig` block | **Yes**, but the sensors' set-up needs none | Visit 3's shots, which wear the sensors anyway (proposed; the other choice, wearing them at V5a, adds about 8 min here) | |
| 12 | Re-install A | — | **Dropped**: A stays installed | |
| N | Your notes table | **No** | During the visit | operator session |

**Only V5 and V6 stay in the visit.** Everything else runs before the visit, moves to the desk or to visit 3, or was dropped.

**Time.** About 60 minutes on 25 Nov; **about 20 minutes** now. About 25 minutes move to the desk on the Mac (D12, D13).

## 2 · The visit

| | Step | Time |
|---|---|---|
| V5a | Start the launch monitor and PinPoint Studio; End while a shot analyses, waiting | 6 min |
| V5b | End while a shot analyses, End anyway | 4 min |
| V6 | Quit while a shot analyses, twice (#280) | 8 min |
| | Message Log exports, and closing the app and Premier | 3 min |
| | **Total** | **about 20 min** |

Export the Message Log after each session (Resource Monitor → Message Log → Export, about 30 s): the log keeps only its last 500 lines, so the Claude session asks for it after every session.

### Before you go

The coordinator posts on the dashboard when everything in §3 is done, and writes the results to `scratch\visit-2\BEFORE.md`. The Claude session reads that file first. **Nothing in the visit waits on the coordinator**, who can't be reached from the bay.

**Bring:** nothing but the clubs and balls. No sensor is worn at this visit.

**Keys and shots.** Typing on the keyboard, a glove rustle or a sneeze can be detected as a shot. Don't type while a session is open; tell the Claude session things after you press End. Use the mouse for End.

### The installer

**A, the latest main, from `a878282f` or later.** The one staged on the share today, `the share (main 15d41578, 25 Nov 16:27), is **too old**: it predates the fix V5 and V6 check. The coordinator builds a new one (D1) and writes its path here and in BEFORE.md. **You install it before the visit** (§3, D6).

---

## V1 to V4: moved out (26 Nov)

V1's start is now the start of V5a. #105's first look is read from visit 3's Message Log, since that visit wears the sensors. The three trunk ceremonies (V2 to V4) are recorded at the desk on the Mac before the visit (§3, D12).

## V5 · End while a shot is analysing, twice (10 min)

Each session starts with one normal shot, so the session is never left empty and "nothing else lost" can be seen.

**V5a, waiting:**
1. Start Foresight Premier, as for a normal session, and PinPoint Studio.
2. Start a new session with both cameras and the launch monitor. Check the capture view, not Replay.
3. Hit shot 1. Wait until its card has appeared.
4. Hit shot 2, then put the club down and **click End straight away**, while shot 2's card is still analysing. (A shot takes about 10 seconds to analyse.)
5. Expect a **"Saving the last shot…"** dialog. **Leave it.**

- **Pass:** the session ends by itself once shot 2 is saved, and both shots are kept.

**V5b, End anyway:**
1. The same, in a new session: shot 1, wait for its card, shot 2, then End straight away.
2. In the dialog, click **End anyway** at once.

- **Pass:** shot 2 is dropped; shot 1 and the session are kept.

**If not** (no dialog, or a shot or the session missing): say what you saw, and carry on. The Claude session looks for the session in the trash.

## V6 · Quit while a shot is analysing, twice (8 min)

The Claude session can't close PinPoint Studio's window, and a keystroke during a session can be taken for a shot. So the close is sent by `quit.ps1`, which you start **before** the session, as at visit 1. It counts down with beeps; the long beep means hit now; it closes the app 1 s later.

**V6a, waiting:**
1. With no session open, in the PowerShell window the Claude session tells you to use, run the line it gives you (`quit.ps1 -Label 6a -HitIn 150 -After 1`). From here on, don't type.
2. Start a new session with both cameras and the launch monitor. Hit shot 1; wait for its card.
3. Address the ball and hit shot 2 **on the long beep**. The script closes the app a second later.
4. A dialog appears. **Leave it.**

- **Pass:** the app closes by itself once shot 2 is saved. Relaunch it and open the session: both shots are there, with video.

**V6b, Close anyway:**
1. The same, with `-Label 6b`, in a new session.
2. In the dialog, click **Close anyway** at once.

- **Pass:** relaunch and open the session: shot 1 and the session are there; shot 2 is not.

**If not:** say what you saw.

**Then close PinPoint Studio and Premier,** and tell the Claude session the visit is over. (The coordinator's studio PC runs can't start while either is open.) **That's the visit.** Installer A stays on.

## V11: moved out (26 Nov)

The HackMotion routine, its short raise on purpose and #270's second attempt are run at the desk on the Mac before the visit (§3, D13). The arm's `rig` block on a real shot comes from visit 3.

---

## 3 · Before the visit (the desk runs)

**Since the fold (26 Nov):** D1, D2 and D4 to D11 become visit 3's before-visit runs, for its latest-main installer, written into SD-15a's pack; D3 is always yes for that build, so it goes. **D12 and D13 stay here**, at the Mac, any time before visit 3. By reading the code on 26 Nov: set-up offers "Start anyway" when not everything is confirmed (`ScreenSessionSetup.qml`), the ball step has Skip, and the trunk step appears only when a present sensor has the pelvis or thorax role (`SetupSteps.qml`, `groups.trunk.inSession`). Whether the Mac's settings give a sensor that role is seen at the desk: if no trunk step appears, D12 falls back to visit 3's sessions as it says[^fold].

The coordinator runs these, and writes each result to `the share The visit reads only that file's results. D6, D12 and D13 need the Product Owner's hands: D6 because only his Windows account can install the app, D12 and D13 because they need a person wearing the sensors.

| | Desk run | Where | Owner (the item) |
|---|---|---|---|
| D1 | Build installer A (the latest main) with the one studio script, named by its commit; write its path here and in BEFORE.md, and whether it carries C-14 (the ceremony file written by the session). There is no installer B[^c-v1] | the studio PC, over ssh | C-16a |
| D2 | The full suite, both passes, for A's commit, through `tools/method/cabin_run.sh`. It holds what stands behind the visit: #105's retry tests (`wt901_config_check_test`, `wt901_config_retry_test`) and the three golden traces of today's routines (C-9a, C-9b, C-9c) | the studio PC, unattended | each landed item in A |
| D3 | Does A's commit carry #280's and #281's fix (the "Saving the last shot…" dialog, End anyway, Close anyway)? **Yes for any commit from `a878282f` (25 Nov) on.** On no, V5 and V6 are dropped and there is no visit | the Mac, from the code | #280, #281 |
| D4 | Copy the Product Owner's account's `%APPDATA%\PinPointStudio\PinPointStudio.ini` read-only to `scratch\visit-2\settings-before\`, **before A is installed**. This replaces step 0's photos: it holds every camera's name, view, mirrored, fixed in place, hitting-area box and enabled, and every sensor's name, role and enabled | the studio PC, over ssh | the cut-over items in A: C-1b, C-1c; C-5b and C-6b.1 once landed |
| D5 | Rehearse the cut-over: run A's commit headless on the Mac with `tools/probe/run_headless.sh`, against a scratch copy of D4's file and a scratch library, then compare its camera and sensor keys (and `rig/rig.json`, if A has the Rig) with D4's copy | the Mac, headless | C-5a.2, C-5b, C-6b.1 (when in A) |
| D6 | **The Product Owner installs A** from his own account, through `install_a.ps1` (staged by the coordinator: runs installer A, waits, launches the app, and logs the install, the launch and the first window to `scratch\visit-2\install-a.txt`). He turns on **Settings → General → "Speak calibration instructions (experimental)"** and closes the app normally. The evening before over Remote Desktop into his account, if that works; otherwise at the studio PC before the visit begins | the studio PC, the Product Owner's account | C-16a |
| D7 | After D6: check the installed program's size and build time against A's build; take the first start-up time from `install-a.txt` and the app log; copy the settings file (and `rig.json`, if any) again and compare with D4: **the cut-over check**. Write pass, or which camera or sensor setting changed. Nobody fixes it in the app: it stays as the install left it | the studio PC, over ssh | the cut-over items (as D4), C-16a |
| D8 | Look in the Product Owner's account's Recycle Bin, read-only, for visit 1's lost session of 25 Nov 13:01. It says where a thrown-away session goes, which V5b and V6b may show | the studio PC, over ssh | #280 |
| D9 | Stage `scratch\visit-2\`: `VISIT-2-FOR-CLAUDE.md`, `quit.ps1` (visit 1's, unchanged; its PinPoint Studio path checked against the install), `install_a.ps1`, `BEFORE.md` | the share | C-16a |
| D10 | Keep the studio PC free: no `cabin_run.sh` and no studio build from 30 minutes before the visit until the Product Owner says it's over; no `scratch\studio PC.lock` folder left behind | the Mac | the coordinator |
| D11 | Confirm whether the sensors' harness has arrived: it decides whether D12 runs. The visit itself doesn't need it | the Product Owner tells the coordinator | C-16a |
| D12 | **The three trunk ceremonies, at the desk on the Mac** (about 15 min, once the harness has arrived; proposed). Three set-ups on the Mac's latest main build, each with the cameras step skipped, the ball step skipped, and the hip and chest sensors worn as the design's placement table says (the hip sensor in the belt, in the middle of the lower back below the waistband; the chest sensor in the vest's back pocket, between the shoulder blades; both **USB end down, label out**; never a thigh sensor), through the trunk step (stand tall; the backswing turn on "Start your backswing turn"; the follow-through turn; "Turn back to address") and on to a started session. Each session's `trunk_ceremony.json` is written when the session starts: the coordinator copies it out then, so a session ended with no shot losing it doesn't matter. **Don't press End until the coordinator has said the copy is made**: End on a session with no shot throws the session, and its file, away. After each, the Product Owner says how the sensors were worn and whether the voice said anything besides the three cue lines. **Not yet run:** by reading `src/Gui/setup/SetupSteps.qml`, the cameras step has no gate and the ball step can be skipped; if a session won't start this way, the ceremonies come from visit 3's sessions with the trunk sensors, each of which writes one at its start. These are the app's own set-up routines, not a test of a sensor's axes or configuration (AD-11; proposed, for the Product Owner to confirm) | the Mac, the Product Owner | for #291 (C-17 is out of scope); C-14's check if A carries it |
| D13 | **The HackMotion routine at the desk on the Mac** (about 10 min; proposed). A set-up on the Mac with the cameras and ball skipped and the HackMotion connected (it was verified on the Mac's hardware), to the arm step. Then: a short raise on purpose (**pass:** it stops and says the forearm only moved a few degrees and needs about 30°); Recalibrate and do it properly (**pass:** done); **#270: Recalibrate once more straight away, and say whether it holds you still while the sensor checks itself, or jumps straight to done.** Jumping straight to done is #270's fault seen for real; its fix goes with #291[^arm-hf]. **Not yet run:** if the Mac can't reach the arm step, #270's real sighting waits for #291 (the fake-device harness already shows it) | the Mac, the Product Owner | #270 |

## 4 · After the visit

You copy nothing by hand. The Claude session's `SUMMARY.md` and the copied files are on the share. From them the coordinator:
- reads the app log exports;
- reads the shots' `rig` blocks for the cameras; the hip's, chest's and arm's (method `deviceHeld` for the HackMotion) come from visit 3's shots (C-8b, C-6b.2);
- keeps D12's three ceremony files on the share for the calibration project (#291), removing any golfer's name; if A carries C-14, reads them as the check of its write;
- posts the results on the dashboard.

What happens next: nothing of Stage C waits on the visit to commit; there is no parked chain[^c-v1]. Stage C's close (C-16b) reads V5's and V6's results, and the sensors' `rig` blocks, from visit 3, which now carries both[^fold]. You won't be asked to re-run anything: the coordinator decides from what there is.

The calls behind the 25 Nov procedure are in `NOTES-VISIT-2.md`; the 26 Nov re-cut is `docs/implementation/platform_refactor/visits/RECUT-26nov.md`.

[^rule]: The Product Owner, 25 Nov 2026, after visit 1: "many of the steps did not require a golf shot and could have been done at a desk".
[^recut]: The Product Owner, 26 Nov 2026: "please do make sure those visits need golf clubs and a ball being hit. I don't want to waste time there doing checks we can automate or do at the desk." Governance BT-43, tightened the same day.
[^c-v1]: The Product Owner and the Solution Architect, 25 Nov 2026, lever C-V1, as recommended: the calibration routines stay in today's screens until one calibration project after the platform programme (#291), so visit 2 loses installer B's part (V7 to V10, about 30 of its 90 minutes) and keeps #105's check, the three ceremonies and #270's two attempts, all on installer A (`docs/implementation/platform_refactor/rulings/LEVERS-U-C-D.md` §2.2; `docs/implementation/platform_refactor/plans/PLAN-C.md`, "Cabin").
[^arm-hf]: The Product Owner and the Solution Architect, 25 Nov 2026 (21:56): the arm routines' timed start, and Recalibrate after a failure, are accepted until #291's track (`docs/implementation/platform_refactor/plans/PLAN-C.md` `[^arm-hf]`).
[^fold]: The Product Owner, 26 Nov 2026: visits 2 and 3 combined, to save time (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Visits 2 and 3 combined"). How the steps were placed: `docs/implementation/platform_refactor/visits/FOLD-2-into-3.md`.
