# Stage E: five rulings for the morning (overnight 25–26 Nov 2026)

Last night's rulings on Stage E (every question as recommended; levers 7 and 8 taken, lever 8 on condition) are now in Stage E's plan, `docs/implementation/platform_refactor/plans/PLAN-E.md`:
- ten items are out of scope, each with its reason;
- the condition on lever 8 is attached to the quit-tests item (SE-1b);
- the plan's estimate is about 54 h for 24 items. The programme's order page says about 52 h. The 2 h difference is the studio profile's two small jobs, which last night's rulings keep in Stage E (typing its interface, about 1.5 h, and moving its screen name under `app`, about 0.5 h: both the pack's guesses). The order page is not changed.

No ruling settles the five questions below, so the plan leaves them open. Each says what happens if it is not ruled: only the architecture-check part of the composition root's item (§3) and the wording at Stage E's close (§2, §4, §5) wait; everything else runs as planned. None of their hours are in the 54 h.

Two terms used below: a **headless** test runs the app with no window and no devices, so it can run unattended; a thread with an **event loop** sits waiting for messages (a camera frame, a sensor packet), unlike a pool job that runs once and ends.

## 1. The quit tests found a fault. What fixes it, and where? (Product Owner; Solution Architect for the shape of the fix)

**What it is.** Lever 8 was taken on a condition: quit tests early in Stage E, and if one finds a fault, the shared thread helper comes back as its fix, for the owner at fault first. The quit tests (SE-1b, which the other lane is running now) found one, filed as #294. If the coach quits while a swing is being re-analysed, the window closes but the app keeps running for about 3 s, or 5–6 s when the pose model is still loading. Then it throws the new result away. Nothing already saved is lost: the swing keeps its old analysis. The two tests that show this are kept as known failures, naming #294.

**Why the condition doesn't settle it.** The helper that lever 8 dropped is for long-lived threads with an event loop (the camera, sensors, speech). The re-analysis is not one of those. It is a job on Qt's shared worker pool. So the helper would not fix it. What fixes it is a rule for that job at quit: stop it, or let it finish and save before the app exits. No item and no hours hold that.

**The choice.**
- (a) **A small item in Stage E for #294:** at quit, the re-analysis job is stopped, or allowed to finish and save, with the app's exit waiting no longer than the quit tests allow. Its two known-failure tests become ordinary passing tests. About 2 h, a guess. It goes after SE-1b, and before SE-2b (the teardown change), so SE-2b is built on a clean quit.
- (b) **Leave #294 on its issue until after the refactor.** It is filed as low priority, and nothing saved is lost. The two tests stay as known failures.
- (c) **Bring back the thread helper (SE-6b),** as the condition's words say. It would not fix this fault.

**If nothing is ruled:** (b). #294 stays on its issue and the two tests stay as known failures.

**Recommendation: (a).** You attached the condition so that a fault found early gets fixed early, and this is exactly the kind of quit it was aimed at: the app waiting with no window. (c) keeps the condition's words but not its purpose. The Solution Architect should agree the shape (stop, or finish and save), because it sets what a quit promises the coach.

## 2. Most long-lived threads can't be made busy in a headless test. Does a hand-run quit at the studio count? (Product Owner)

**Superseded (26 Nov 2026).** Ruled at 06:55: each long-lived thread gets an automatic quit test, not a hand-run quit at the studio (E1, morning batch 4; its sizing is in `docs/implementation/platform_refactor/plans/SIZING-E1-E4.md`, not yet accepted). At midday the Product Owner ruled that a visit holds only steps that need clubs and a ball hit, so option (a)'s quits at visit 3, and (b)'s at visit 4, no longer go to any visit (`docs/implementation/platform_refactor/visits/RECUT-26nov.md`). The text below is kept as it was put.

**What it is.** The condition asks for quit tests "while each long-lived thread is busy". The re-set target for the stage's close is "each thread listed in the model, each covered by a quit test". Headless on the Mac, the app starts no devices. So only two threads can be made busy in an automatic test, both while the pose model loads at start-up: the pose thread and the short-lived thread that starts the load. The rest need real devices running: the camera's four threads, the microphone and speech threads, speech out (it makes a sound, so never unattended), the sensor scan, the sensors' data thread and the phone link. They can only be tested by quitting by hand at the studio, with the devices running. Visit 1's hand-run quits (25 Nov) already quit with a session open and once while a shot was analysing; every timed close took 0.7–1.7 s (`docs/implementation/lifecycle_audit_25nov.md`). But they were not aimed at one busy device each: nobody quit mid-sentence, mid-scan or with a phone connected. Stage E's plan puts the scripted "after" quit run after the teardown change (SE-2b), at visit 2 or 3; the order page puts the stage's quit tests at visit 4, near the end of Stage E. Either way, the decision not to build the helper is acted on before most of that evidence exists.

**The choice.**
- (a) **Count a hand-run quit at the studio as the test for those threads, and do it early:** at the next visit that has every device running (visit 3, the live capture run), add about 10–15 minutes of quits, each with one device busy. Visit 2 already quits while a shot analyses with the sensors on, so the sensors' part could ride there if its time allows. The microphone-permission quit and speech out are at the desk on the Mac, with you there. Later, when Stage D's synthetic camera lands (SD-2t), the camera's part can become an automatic test.
- (b) **Count it, but leave it at visit 4,** as the plan has it now. The condition is met late.
- (c) **Don't count a hand-run quit:** each thread needs an automatic test. That needs a way to make each device busy headless, which no item holds and which is not sized.

**If nothing is ruled:** (b). Only the close's wording (SE-11b) waits.

**Recommendation: (a).** It meets the condition's purpose (evidence early, before the helper is ruled out for good) at the cost of a few minutes on a visit already planned. (c) would cost more than the helper it was meant to judge.

## 3. Which item teaches the architecture check what the "composition root" is? (Product Owner, for where it goes; the Solution Architect has already ruled what it says)

**What it is.** The Solution Architect ruled that the new object which builds the app at start-up (the composition root, SE-2a) goes in its own file. The automatic architecture check is told that this file is allowed to build the stores. That is the root's whole job, so it should not be listed as an allowed exception. But no item edits the check to say so, and SE-2a's 3.5 h does not include it. This is the same gap as the one already open for Stage C's rule on loose data bundles (`docs/implementation/platform_refactor/rulings/MORNING-RULINGS-C.md` §1): a ruling that changes the check, with no item to make the change.

**The choice.**
- (a) **One small item of its own, just before SE-2a:** the check names the composition root's file, and its file-access findings stay as findings. About 1 h, a guess. It never runs beside another item that edits the check or its exception list.
- (b) **Inside SE-2a,** which creates the file. About 1 h more on that item.
- (c) **No edit to the check:** the new file gets an allowed-exception line, as `main.cpp` has today. That is the option the Solution Architect did not choose.

**If nothing is ruled:** SE-2a is built without the check's part, and the new file's findings stay on the exception list until the check is edited. Nothing else waits. *Needs (added by the precheck, 26 Nov; proposed): `main.cpp`'s exception line is per file, and a new file does not inherit it (H1), so this default adds a new exception line for the new file. That is option (c) for a while, it needs the **Solution Architect**'s approval, and an unattended run stops before adding one (AG-3).*

**Recommendation: (a).** A change to what the check allows is a change to the architecture rules, and it is easier to review on its own than inside the biggest edit of `main.cpp`.

## 4. Thread pools that no item holds (Solution Architect, for the threading rule's wording; Product Owner, for the deferral)

**What it is.** The threading rule says the app has one worker pool. Last night's ruling makes the rule name the long-lived threads as they are. It says nothing about pools. Stage E's first questions (SE-0) found nine places that make a pool of their own or put work on Qt's shared pool, and asked for each to be placed before Stage E starts. The thread inventory (SE-6a) then found 22 more uses of Qt's shared pool in 13 files. None of them is in any item, so under the planning rule they are "unscheduled", not deferred. Qt waits for all of them at quit. That wait is how #294 happens.

**The choice.**
- (a) **Describe them, and defer the move:** when the threading rule is amended at Stage E's close (SE-11b), it also says plainly that these pools and jobs exist and are not yet on the one pool. Their move is deferred, with your agreement, on one issue that lists them. A quit test that fails on one of them brings its fix back, as #294 does.
- (b) **An inventory now, and an item for each:** not sized. It touches many files across the app for no fault seen except #294.
- (c) **Leave them unscheduled.** That breaks the planning rule.

**If nothing is ruled:** they stay unscheduled; only the close's wording (SE-11b) waits.

**Recommendation: (a).** It is the same principle last night's ruling applied to the threads: the rule describes the code, and a fault found by a quit test brings the fix. Saying the gap out loud is better than a rule that is quietly untrue.

## 5. Five screen-facing objects stay global, and the rule says only four kinds may (Solution Architect; nothing waits on it)

**What it is.** The rule for what the screens can reach directly says: only devices, settings, logging and `app`. Last night's ruling (a global leaves only when its owner changes) keeps five that are none of those by name: the speech-out and transcription controllers, the profiler, the resource monitor and the graphics-card runtime. The question pack left this one point with you, with nothing waiting on it.

**The choice.**
- (a) **Amend the rule to name these five as allowed, as they stand,** at Stage E's close (SE-11b). No code changes.
- (b) **Read "devices" and "logging" widely enough to cover them.** That stretches the words.
- (c) **Move them under `app`.** That renames screen names only to meet the rule, which last night's ruling rejected.

**If nothing is ruled:** the rule and the code keep disagreeing, as today; only the close's wording (SE-11b) waits.

**Recommendation: (a).** The rule should describe what is ruled, as with the threading rule.
