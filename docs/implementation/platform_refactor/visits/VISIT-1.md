# Cabin visit 1: the live check, then #53 and #183 (about 50 minutes)

## Install first (built 25 Nov 02:51 from main @ 978a9d24)

`the share (810 MB, core). Run it from the account that runs the official build: it upgrades in place with no admin prompt.
It replaces every earlier installer from the 24th and 25th. Since 677c573d, main gained P-3b (each session summary gains a `gates` block), the priorities' renames, #186 (**the score pill is gone from the shot cards, the action bar, the filter, the picker's readout and the sessions drawer: that's intended**), #268 (**Settings → Cameras keeps a saved hitting-area box when you open the ball panel or click on the view, and the Mirrored chip now changes**), C-1a and C-1b (camera keys built by a type: same bytes), and several file moves with no behaviour change. Use this one.

## Added tonight: a 10-minute quit run, by hand (Stage E's "before" record)

Stage E changes how the app shuts down, so it needs today's behaviour on record first. On the studio PC, with the installer above, start PinPoint Studio and **quit it** at each of these six moments, one run each:
1. at the splash screen;
2. as soon as the first window appears;
3. during the pose warm-up (the first minute after launch);
4. after the warm-up has finished;
5. with a session open;
6. after End, with the session closed.

7. **The important one (added 04:30):** with a session open, hit a shot, then quit within about 5 seconds, while the shot is still being analysed. Relaunch, open the session: **is that shot there, with its video and numbers?** Then once more, quitting the instant after impact, before the shot card appears.

Reading the code suggests a shot that's still analysing or saving at quit never gets its file, and one still in its post-roll is dropped (`build/platform/DEF/NOTES-SE-6a.md`). This step tells us whether that's real. Quitting during the pose warm-up (step 3) also checks #20's hang: note how long the process lingers after the window closes.

For each, note: did it close cleanly, show a crash dialog, or leave a hung process (check Task Manager for `PinPointStudio.exe` a few seconds later)? A line each is plenty, e.g. "3: hung, killed after 30 s". That's the whole step; no script exists for it yet (`docs/implementation/platform_refactor/plans/PLAN-E.md` note N4).

**When:** the next time you're at the studio PC, ideally on 25 Nov. It's the platform refactor's entry check (Q20), which we assumed so the work could start.

**Before you go** (the coordinator does this the evening before, and posts it on the dashboard when it's ready): the latest main is built into a studio PC installer, and its path is given here. The installer of `94b3b9a1`, the domain refactor's last build, stays on the studio PC as the baseline: if something is wrong, comparing the two shows whether the domain refactor or the platform work caused it.

## 1 · Install and first launch (10 min)
1. Close PinPoint Studio, Foresight Premier and anything else using the cameras.
2. Run the installer from the account that runs PinPoint Studio. It's an in-place upgrade, and needs no admin.
3. Launch it and **time the start-up** until the main window responds. The first launch re-analyses the library once (version gates), with no pose re-run. Note the time.
4. Expect, once:
   - legacy sessions' report files split;
   - earlier PDFs reading "out of date";
   - new direction words on some LM cards;
   - 63 shots showing the attack-angle low point.

   Note anything else that looks different.

## 2 · The 3-shot live check (15 min)
1. Start a session with **both cameras and the launch monitor**. No IMUs: the harness hasn't arrived.
2. Hit **3 shots**, at a normal pace.
3. For each shot, check that **the launch monitor's reading lands on that shot's card** (carry and ball speed matching the LM screen). This is the check that matters most: the Windows folder-spelling fix has never run on Windows.
4. On one card, change the rating and the note, then press End. Reopen the session and check both were kept.
5. If anything is wrong, keep the session as it is and note the shot number. Don't retry.

## 3 · #105: deferred
The sensors' harness hasn't arrived (the Product Owner, 24 Nov). #105's check moves to visit 2 (Stage C), which uses the real sensors anyway.

## 4 · #53: a clip that starts late (10 min)
1. In a live session, hit **two swings a few seconds apart**. Then hit one more and **press End straight after it**.
2. Each swing should save a full-length clip on both cameras. If one doesn't, it should show the capture warning (it starts late, or ends early), not pass silently.
3. Note which swings, if any, show the warning. The coordinator checks the saved swings.

## 5 · #183: the camera brightness comparison (15 min)
1. **Turn on all the bay lights,** as for a normal session.
2. Close PinPoint Studio. Tell the coordinator, who then reads both cameras' settings (exposure compensation, gain, black level and gamma), read-only, over ssh.
3. Open PinPoint Studio and record **3 swings**, then close it. The coordinator measures the image levels against the 2 Nov and 20 Oct sessions.
4. Optional, if it's quick: 3 swings in Swing Catalyst on the same cameras, for the same comparison.
5. That gives #183 its comparison, so its fix can be designed from evidence (Q4).

## After
Nothing to copy by hand: the coordinator reads the app log and the saved sessions from the studio PC over ssh, and posts the results on the dashboard.

## Run by a Claude Code session on the studio PC (Product Owner, 25 Nov)
The PO runs this visit with Claude Code on the studio PC, from `the shareisit-1\VISIT-1-FOR-CLAUDE.md` (the share's scratch/visit-1). It writes SUMMARY.md and the logs there. Step 5.2: the PO tells the coordinator, who reads the cameras read-only.
