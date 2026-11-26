# PinPoint Studio use cases

**Version 1.1, 26 Nov 2026; version 1.0 approved by the Product Owner on 26 Nov 2026**[^approved]. It organises the requirements ([`requirements.md`](requirements.md), v1.1) into epics and use cases; it adds no requirement and changes none. The steps it found uncovered are now requirements of their own, named where they occur[^gaps].

| Document control | Value |
|---|---|
| **Owner** | Product Owner[^po][^home] |
| **Home** | `governance/artefacts/use_cases.md`, beside the requirements, hand-edited like them[^home][^hand] |
| **Governs** | Nothing on its own. It groups the requirements by what a golfer, a coach or a studio owner is trying to do, so a gap or an overlap shows. The requirements stay the governing text: where a use case and a requirement differ, the requirement holds |
| **Traced by** | [`uc-map.csv`](uc-map.csv), beside this page: one row per requirement, with the use cases it is a step of, a path of, or a constraint on. The traceability matrix's generator reads it[^home] |
| **How it changes** | As the requirements do (`requirements.md`, "How a requirement changes"): proposed, approved by the Product Owner, and recorded in this page's change log. A use case that needs a step no requirement covers names the gap and its proposed requirement in the same change (proposed, for the Product Owner) |
| **Ids** | `EP-n` for an epic and `UC-n` for a use case, numbered once and never reused; a use case keeps its id if it moves to another epic. `UCG-n` numbers a step no requirement covers, fixed once and never reused, so a gap keeps its id when others are added or struck |
| **Status of this page** | v1.1, 26 Nov 2026: 9 epics, 45 use cases, 202 main-path steps, 69 alternative or failure paths (43 of them could lose a shot, a setting or other work, or misfile it). 278 of the 278 requirements map to a use case; 0 steps and paths have no requirement. Of the 19 gaps the use cases found, 18 are now requirements and 1 is struck |

## How to read a use case

- **Actor, goal, trigger**: who wants what, and what starts it. These organise the requirements and add no need.
- **Where it stands**: the status of the requirements it covers, counted from `requirements.md`. A use case is no further along than its requirements.
- **Main path**: the steps, each with the requirements that cover it. A step the use cases found with no requirement was numbered `UCG-n`; each accepted one is now a requirement, named with its gap id, and the list at the end says which.
- **Alternative and failure paths**: what happens when something differs or fails, and what is at stake when it does. A path that could lose a shot, a setting or other work, or file it under the wrong golfer, says so.
- **Also binds**: requirements that hold across the whole use case (a quality, not a step).

Each step's sentence traces to the requirements in its row. A requirement marked "(proposed)" in a row is not yet built or scheduled. One marked "(won't have)" is ruled out; its path is kept, so the gap shows[^wont]. The words follow the requirements' own; where a step is shorter, the requirement's wording holds.

## Actors

| Actor | Who | From |
|---|---|---|
| Golfer | The person swinging: a committed amateur coaching themselves, or a newcomer still forming a swing. | personas.md §3, §4 |
| Coach | A teaching professional giving lessons to several pupils a day, in a fixed studio. | personas.md §2 |
| Studio owner | Whoever installs the app, connects the devices and keeps the studio running; often the coach or the golfer themselves. The requirements call them "the user". | requirements.md ("the user"); personas.md §2 |
| Coaching Lead | The role `roles.md` defines ("The four roles"); in the app, the one who edits the diagnostics model. | roles.md, "The four roles"; REQ-DIAG-8 |
| Analyst | Someone checking the club and body tracking (the trackers) against hand-marked truth. | REQ-REV-30 |

## The epics

| Epic | What the golfer or coach gets | Use cases | Requirements |
|---|---|---|---|
| [EP-1 Getting the app and keeping it current](#ep-1-getting-the-app-and-keeping-it-current) | The studio owner installs the app on the studio's computers and keeps it up to date, without ever losing a session or a golfer's history to an update. | UC-1, UC-2, UC-3 | 24 |
| [EP-2 Setting up the studio and its devices](#ep-2-setting-up-the-studio-and-its-devices) | The studio owner connects the cameras, phones, sensors and launch monitor, calibrates them once for the bay, and sets the app up the way the studio works, so each session can start in minutes. | UC-4, UC-5, UC-6, UC-7, UC-8, UC-9, UC-10 | 51 |
| [EP-3 Capturing a session of shots](#ep-3-capturing-a-session-of-shots) | The golfer hits balls at their own pace, hands on the club, and every swing is kept as one shot with everything every device saw, ready for the next swing at once. | UC-11, UC-12, UC-13, UC-14, UC-15, UC-16, UC-17, UC-18 | 68 |
| [EP-4 Reviewing a shot](#ep-4-reviewing-a-shot) | The golfer or coach looks back at a shot: sees it from every camera, reads what the app measured and how far to trust it, and asks what happened on it and how it compares. | UC-19, UC-20, UC-21, UC-22, UC-23, UC-39 | 78 |
| [EP-5 Seeing a session's diagnosis and what to work on](#ep-5-seeing-a-sessions-diagnosis-and-what-to-work-on) | After or during a session the golfer and coach see what the session showed: the faults and their likely causes, the evidence, what went well, and the few things to work on. | UC-24, UC-25, UC-26 | 35 |
| [EP-6 Lessons, practice and progress over time](#ep-6-lessons-practice-and-progress-over-time) | The golfer and coach follow what is being worked on from one session to the next: lessons with a purpose, practice against the one thing prescribed, play, and progress over weeks and months. | UC-27, UC-28, UC-29, UC-30, UC-31 | 25 |
| [EP-7 Reports and sharing](#ep-7-reports-and-sharing) | The golfer leaves with a report, and the golfer's data can leave the app when the golfer chooses: to a coach for review, or out as files, and nothing more than the golfer chose. | UC-32, UC-33, UC-34, UC-35 | 22 |
| [EP-8 Keeping the library safe](#ep-8-keeping-the-library-safe) | The golfer's and coach's history is never lost, misfiled or removed without their say: through crashes, upgrades and a shared folder, and back again from the trash or an archive. | UC-36, UC-37, UC-38, UC-40, UC-41, UC-42, UC-43 | 29 |
| [EP-9 Tuning and checking the analysis](#ep-9-tuning-and-checking-the-analysis) | The faults and norms a golfer is graded against are kept right by the coach who owns them, inside the app, and the club and body tracking is checked against marked truth, so a gap is found before a golfer meets it. | UC-44, UC-45 | 6 |

## EP-1 Getting the app and keeping it current

The studio owner installs the app on the studio's computers and keeps it up to date, without ever losing a session or a golfer's history to an update.

Where it stands: 24 requirements: 7 built, not verified; 8 partial; 6 planned; 3 proposed.

### UC-1 Install the app on a studio computer

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | A working app on a new machine, ready for a first session. | A new or rebuilt studio computer. |

Where it stands: 9 requirements: 4 built, not verified; 4 partial; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner downloads the one signed installer for the computer's platform and runs it. | REQ-INST-1, REQ-INST-2 |
| 2 | The installer leaves out the graphics card's extra software (its runtime), and the app fetches it only if the machine has a supported card. | REQ-INST-8 |
| 3 | On first start the app asks for each system permission it needs (camera, microphone, Bluetooth, local network), saying why. | REQ-INST-10 |
| 4 | Phones on the local network can reach the app without the studio owner touching the firewall. | REQ-INST-11 |
| 5 | With no golfers and no devices yet, the app starts cleanly and leads the studio owner to a first session. | REQ-ACC-7 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The machine has no supported graphics card | Body tracking (the pose engine) runs on the card it has, or on the processor. | REQ-INST-9 | – |
| The studio owner refuses a permission | The app says what will not work without it and how to grant it later. | REQ-INST-14 (proposed) (the use cases' gap UCG-1) | could stop a device working |

**Also binds:** REQ-PERF-7

### UC-2 Update the app

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | The latest build, installed safely, with the golfer's history intact. | An update is published on the chosen channel, or the studio owner asks for a check. |

Where it stands: 14 requirements: 2 built, not verified; 5 partial; 5 planned; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner picks the stable or the beta channel. | REQ-INST-6 |
| 2 | The app says an update is ready. | REQ-INST-3, REQ-INST-13 |
| 3 | The app downloads only what changed, where the platform allows it. | REQ-INST-7 |
| 4 | The app checks the update's signature before installing it. | REQ-INST-4 |
| 5 | The studio owner says when to install; the app quits through its one quit prompt, installs, and relaunches. | REQ-INST-3, REQ-REL-1, REQ-REL-2 |
| 6 | After the upgrade the system permissions are still granted, and every past swing file still opens with the set-up it was captured with. | REQ-INST-10, REQ-LIB-23 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A session is live, a shot is being saved, or a re-analysis, an export or the background refresh of old results (the catch-up) is running | The app waits to install until nothing is in progress. | REQ-INST-5 | could lose a shot |
| The signature does not check | The app refuses the update. | REQ-INST-4 | – |
| The installed update fails or will not start | The previous version can be put back, with the library untouched, including reading any file the newer version wrote. | REQ-INST-15 (the use cases' gap UCG-2) | could lose other work |
| The new version changes what a stored field means | The change is caught. | REQ-HON-14 | could lose other work |
| The app is uninstalled or reinstalled | The library is left untouched. | REQ-INST-16 (proposed) (the use cases' gap UCG-3) | could lose a shot |
| Any update installs | The studio's settings, the bay's calibration and set-up, the paired phones and the Coaching Lead's edits to the model are all kept. | REQ-INST-17 (proposed) (the use cases' gap UCG-15) | could lose a setting |

### UC-3 Find out which version is running, and get help with a problem

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Enough information for support to solve a problem in the studio. | Something goes wrong, or the studio owner wants to know the version. |

Where it stands: 3 requirements: 1 built, not verified; 1 partial; 1 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner sees the app's version and licences, and asks it to check for an update. | REQ-INST-13 |
| 2 | The studio owner opens the app's log, filters it, and hands it to support. | REQ-REL-11 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The same problem recurs (for example a network folder that keeps dropping out) | The app tells the studio owner once, not again and again. | REQ-REL-8 | – |

## EP-2 Setting up the studio and its devices

The studio owner connects the cameras, phones, sensors and launch monitor, calibrates them once for the bay, and sets the app up the way the studio works, so each session can start in minutes.

Where it stands: 51 requirements: 18 built, not verified; 17 partial; 12 planned; 4 proposed.

### UC-4 Connect and frame the cameras

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Every camera giving a sharp, well-lit, well-framed picture in its role. | A new camera, a moved camera, or a first set-up. |

Where it stands: 8 requirements: 4 built, not verified; 2 partial; 2 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner connects machine-vision cameras, the computer's own cameras or phones, and gives each a face-on, down-the-line or impact role, or the high down-the-line third view. | REQ-DEV-1, REQ-CAP-32 |
| 2 | The app offers for the swing roles only cameras fast enough, and lets the studio owner choose a local camera's frame rate. | REQ-DEV-3 |
| 3 | Each camera gives a usable picture in the studio's light. | REQ-DEV-2 |
| 4 | Live pictures show the framing. | REQ-CAP-11 |
| 5 | The app says which readings this framing will lose, and does not block. | REQ-DEV-23, REQ-HON-12 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A camera's focus lock fails | The app shows it and restores the camera's focus. | REQ-DEV-18 | – |
| A camera's picture is too dark for the club | The app makes the picture usable. | REQ-DEV-2 | – |

### UC-5 Pair the phones as cameras

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | One or two phones filming as cameras, reconnecting by themselves on later visits. | A phone is to be used as a camera. |

Where it stands: 6 requirements: 1 built, not verified; 3 partial; 1 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner pairs a phone by scanning a code. | REQ-CAP-27 |
| 2 | The phone connects over Wi-Fi or a cable, and is never connected twice. | REQ-CAP-28, REQ-INST-11 |
| 3 | The app checks that the phone app and the studio app can work together. | REQ-CAP-34 |
| 4 | A second phone is paired for the other view. | REQ-CAP-24 |
| 5 | On a later visit an already-paired phone reconnects by itself. | REQ-CAP-27 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The two apps' versions cannot work together | The app says which one to update. | REQ-CAP-34 | – |
| The studio no longer uses a phone | The studio owner unpairs it, and it stops reconnecting. | REQ-DEV-29 (proposed) (the use cases' gap UCG-4) | – |

### UC-6 Connect the sensors, the launch monitor and the microphone

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Every sensor and the launch monitor connected, set up the same way each time, and worn where this session needs it. | A session needs sensors or ball data. |

Where it stands: 7 requirements: 3 built, not verified; 4 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner connects the launch monitor: a GC Quad directly, a device bridged through GSPro Connect, a Foresight device under Premier, or an Open Launch Nova. | REQ-DEV-5, REQ-DEV-6 |
| 2 | The studio owner connects the wrist sensors and the chest and hip sensors; the app sets each up the same way at every connect. | REQ-DEV-7 |
| 3 | The studio owner says where each motion sensor is worn for this session. | REQ-DEV-8 |
| 4 | Every device control the app shows does what it says. | REQ-DEV-9 |
| 5 | The studio owner chooses the microphone. | REQ-DEV-28 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A chest or hip sensor is worn facing the wrong way | The app says so. | REQ-DEV-13 | – |

### UC-7 Calibrate the sensors

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | A calibration the golfer can trust, done without touching the screen. | A sensor is connected, moved, or its calibration is no longer valid. |

Where it stands: 7 requirements: 6 partial; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app guides the golfer with a face-on figure and golf-swing words, speaking only when the golfer cannot see the screen. | REQ-DEV-11, REQ-ACC-8, REQ-ACC-5 |
| 2 | The golfer takes each position; the routine runs without the golfer touching the screen. | REQ-DEV-10 |
| 3 | The app gives one plain answer to "is the calibration good enough?", with the detail one click away. | REQ-DEV-14 |
| 4 | The calibration's evidence is saved with the session. | REQ-REL-5 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The calibration stops being valid (a lost link, a re-link, a move to another body segment)[^c5] | The app clears it and tells the golfer. | REQ-DEV-12 (proposed) | could lose a setting |
| The evidence cannot be saved | The golfer is told. | REQ-REL-5 | could lose other work |

### UC-8 Calibrate the cameras for the bay

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Cameras calibrated once per bay, so 3-D readings are measured rather than estimated. | A new bay, or a camera moved. |

Where it stands: 7 requirements: 1 built, not verified; 1 partial; 4 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner calibrates every camera in one routine with an alignment stick, a sheet of paper and a ball. | REQ-DEV-15 |
| 2 | The app keeps the bay's calibration and set-up, and loads it for the next session. | REQ-DEV-17 |
| 3 | Each session, the app checks quickly that the calibration still holds. | REQ-DEV-16 |
| 4 | Every change to the bay's set-up is saved. | REQ-DEV-20 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A camera was knocked overnight | The session check says the calibration no longer holds. | REQ-DEV-16 | – |
| Saving a set-up change fails | The studio owner's change still takes effect, and is not refused. | REQ-DEV-20 | could lose a setting |
| Saving a set-up change fails | The studio owner is told it was not saved, so it is not lost unnoticed at the next start. | REQ-DEV-30 (proposed) (the use cases' gap UCG-16) | could lose a setting |
| The cameras are not calibrated | Readings that need calibration say they are estimated. | REQ-HON-6, REQ-ANA-16 | – |

### UC-9 Set the app up the way the studio works

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Screens, words, units, storage and privacy set once for the studio. | A first set-up, or a change of preference. |

Where it stands: 15 requirements: 8 built, not verified; 5 partial; 1 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner chooses light or dark, the screen density, the text size, reduced motion and the strength of the overlays; every screen follows. | REQ-ACC-3, REQ-ACC-9 |
| 2 | The studio owner chooses the language and the units. | REQ-ACC-10, REQ-ACC-11 |
| 3 | The studio owner chooses the screen the app opens on and the display the views are cast to. | REQ-DEV-28 |
| 4 | The studio owner chooses where the library lives, including a shared folder. | REQ-LIB-15 |
| 5 | The studio owner chooses what is kept with each shot and at what video quality, and the rules for archiving and emptying the trash. | REQ-LIB-27, REQ-LIB-26 |
| 6 | The studio owner enters the studio's and the coach's details for reports. | REQ-REV-32 |
| 7 | The studio owner turns outside services (speech, voice, a language model, email) and crash reports on or off; nothing leaves the machine unless turned on. | REQ-PRIV-2 (proposed), REQ-PRIV-6, REQ-PRIV-1 |
| 8 | Passwords and keys entered go to the system's secure store. | REQ-PRIV-3 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The app runs in a smaller window | The screens stay usable. | REQ-ACC-4 | – |

**Also binds:** REQ-ACC-2

### UC-10 Check the studio's devices and what they can measure

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Know before hitting that the devices are healthy and what the rig can measure. | Before a session, or when something seems wrong. |

Where it stands: 2 requirements: 2 built, not verified.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner sees what the connected rig can measure. | REQ-DEV-26 |
| 2 | The studio owner opens the device-health screen (cameras, sensors, phones, and the recording memory), which a first-time golfer never meets unless they go to it. | REQ-DEV-25 |

## EP-3 Capturing a session of shots

The golfer hits balls at their own pace, hands on the club, and every swing is kept as one shot with everything every device saw, ready for the next swing at once.

Where it stands: 68 requirements: 15 built, not verified; 32 partial; 11 planned; 9 proposed; 1 won't have.

### UC-11 Start a session

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Capturing, for the right golfer, with the right club, in under two minutes. | The golfer, or the coach for a pupil, starts a practice or a lesson. |

Where it stands: 17 requirements: 5 built, not verified; 7 partial; 1 planned; 4 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach picks the golfer, the most recent ones one click away. | REQ-LIB-4 |
| 2 | Session set-up shows only the steps the connected hardware needs, explains each plainly with recommended defaults, and ends in a plain "Ready". | REQ-DEV-21, REQ-ACC-6 (proposed) |
| 3 | A studio whose devices have not changed goes straight to Ready after a quick check. | REQ-DEV-22 (proposed), REQ-DEV-16 |
| 4 | The app shows what the rig can measure. | REQ-DEV-26 |
| 5 | The golfer says what they are working on and their usual miss. | REQ-CAP-20 |
| 6 | The golfer sets the club, or chooses to take it from the simulator, and may say what kind of session it is. | REQ-CAP-17, REQ-CAP-33, REQ-CAP-31 (proposed) |
| 7 | The session opens in Capture. | REQ-CAP-21 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The golfer already has a session today | The golfer continues it instead of starting a new one. | REQ-CAP-23 | – |
| No golfer is chosen | Shots are kept for a golfer to be assigned later. | REQ-LIB-1 | could file shots under the wrong golfer |
| The golfer's session is live on another computer | The app does not record into it. | REQ-LIB-16 | could lose a shot |
| The session starts from another path (continuing today's, or from Home) | It still opens in Capture. | REQ-CAP-21 | – |
| The disk is nearly full | The app warns before the session starts. | REQ-CAP-35 (proposed) (the use cases' gap UCG-6) | could lose a shot |

**Also binds:** REQ-PERF-2, REQ-PERF-7

### UC-12 Hit a shot and have it kept

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Every real swing kept as one complete shot, with nothing for the golfer to touch. | The golfer swings while the app shows it is capturing. |

Where it stands: 23 requirements: 5 built, not verified; 12 partial; 5 planned; 1 won't have.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app finds the ball at address by itself, with nothing for the golfer to calibrate. | REQ-DEV-24 |
| 2 | The app detects the swing by itself, from every detector it has. | REQ-CAP-3 |
| 3 | The app records the swing from every camera, sensor and launch monitor in use as one shot, with every device's readings on one clock and impact timed to within a frame. | REQ-CAP-1, REQ-CAP-15, REQ-CAP-16 |
| 4 | A short sound tells the golfer the swing was accepted. | REQ-CAP-6 |
| 5 | The app pairs the launch-monitor reading with the swing by time, and adds each device's later data to the shot within seconds. | REQ-CAP-8, REQ-CAP-9 |
| 6 | The app keeps the video trimmed to the swing plus a margin, and an impact clip trimmed around impact. | REQ-CAP-12, REQ-CAP-13 |
| 7 | The shot keeps each camera's lens, the part of the picture it uses (region of interest), exposure and gain. | REQ-DEV-4 |
| 8 | The shot is saved, or the golfer is told it was not and why. | REQ-CAP-7 |
| 9 | The app replays the shot slowed down; the golfer may skip it. | REQ-CAP-10 |
| 10 | The app analyses and diagnoses the shot by itself, shows it is still analysing, and shows the results within the set time. | REQ-ANA-1, REQ-DIAG-14, REQ-HON-15, REQ-PERF-1 |
| 11 | The golfer can hit the next swing straight away, without waiting for the analysis or the replay. | REQ-CAP-2 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| Detection misses a swing | The golfer or coach records the shot by hand. | REQ-CAP-4 | could lose a shot |
| The golfer swings while the previous shot is analysing or replaying | The swing is still detected and kept. | REQ-CAP-1, REQ-CAP-2, REQ-CAP-3 | could lose a shot |
| A sound or movement that is not a swing (typing, a sneeze) | It is not taken as a shot; one the app is unsure of is saved and marked, never hidden. | REQ-CAP-5 | could lose a shot |
| A launch-monitor reading arrives with no swing to match | It is kept as a shot of its own. | REQ-CAP-8 | could lose a shot |
| A launch-monitor reading was paired with the wrong shot | The golfer moves it to the right shot. | REQ-CAP-36 (won't have)[^wont] (the use cases' gap UCG-7) | could put a reading on the wrong shot |
| A swing is refused | A different sound tells the golfer. | REQ-CAP-6 | – |
| The shot cannot be saved (for example the disk is full) | The golfer is told it was not saved and why. | REQ-CAP-7 | could lose a shot |

**Also binds:** REQ-HON-11, REQ-PERF-2

### UC-13 Capture with phones

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | One shot per ball with both phones' clips, and a clear view of which clips have arrived. | A session with one or two phones as cameras. |

Where it stands: 5 requirements: 1 built, not verified; 2 partial; 2 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | Two phones, face-on and down-the-line, capture one ball as one shot with both clips. | REQ-CAP-24 |
| 2 | A phone's shot is accepted only when the computer's own detectors agree, where it has any. | REQ-CAP-26 |
| 3 | A clip that arrives late is attached to its shot and the shot is re-analysed. | REQ-CAP-25 |
| 4 | The app shows for each clip whether it is requested, arriving, absent or timed out. | REQ-CAP-25 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A phone's shot is refused | A refuse sound tells the golfer. | REQ-CAP-6 | – |
| A clip never arrives | The shot shows the clip timed out, and keeps what it has. | REQ-CAP-25, REQ-CAP-14 | could lose a shot |

### UC-14 Change the club, and correct a wrong one

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Every shot carries the club it was hit with. | The golfer changes club, or notices a shot logged with the wrong club. |

Where it stands: 3 requirements: 1 built, not verified; 1 partial; 1 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer changes the club between shots, or the app takes it from the simulator. | REQ-CAP-17, REQ-CAP-33 |
| 2 | A shot with no declared club records "not declared". | REQ-CAP-17 |
| 3 | The golfer corrects a saved shot's club, and the correction flows into every per-club number. | REQ-CAP-18 |

### UC-15 Keep capturing when something fails mid-session

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | One failure costs at most what failed, never the session. | A device drops out, the shared library goes out of reach, or a problem recurs. |

Where it stands: 5 requirements: 1 built, not verified; 1 partial; 1 planned; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app keeps capturing, tells the golfer which device was lost, and keeps each shot with what remains. | REQ-CAP-14 |
| 2 | A sensor that re-links has its calibration cleared, and the golfer is told. | REQ-DEV-12 (proposed) |
| 3 | If the shared library goes out of reach, the app keeps working, says so, and catches up when it returns. | REQ-REL-9 (proposed) |
| 4 | Each problem is reported once. | REQ-REL-8 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The golfer wants to stop capturing for a while (to talk, to type a note) | The golfer stops capture and starts it again within the session. | REQ-CAP-37 (the use cases' gap UCG-8) | – |

### UC-16 End the session or quit the app

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | A session closed with every shot and its diagnosis on disk. | The golfer presses End, closes the window, quits from the menu, or an update asks to quit. |

Where it stands: 15 requirements: 1 built, not verified; 12 partial; 1 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | Every way of ending or quitting goes through one prompt. | REQ-REL-1 |
| 2 | The app ends only after any shot still being saved is saved, unless the golfer chooses to end anyway. | REQ-CAP-22, REQ-REL-1 |
| 3 | Device data that has arrived is on its shots, and the session's diagnosis is kept with the session. | REQ-CAP-9, REQ-DIAG-14 |
| 4 | A session with nothing recorded is discarded, and the golfer is told. | REQ-LIB-18 |
| 5 | Once the golfer has answered, the app is gone within a few seconds, with no hidden process left. | REQ-REL-2 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A shot is still being saved | End, quit and update agree that it is, and wait or ask. | REQ-REL-1, REQ-CAP-22, REQ-INST-5 | could lose a shot |
| The golfer ends before a device's late data has come (Premier sends a shot's club data only with the next shot, so the last shot never gets it; a phone clip may still be on its way) | The shot keeps what has come; a missing reading shows as missing, with its reason, and a clip that never comes shows as timed out. | REQ-CAP-25, REQ-HON-2, REQ-DIAG-15; see also GU-8 | could lose a shot |
| A phone clip is still on its way at End | The app says so and offers to wait for it. | REQ-CAP-39 (proposed) (the use cases' gap UCG-17) | could lose a shot |
| The app crashes, or the power is cut | Every shot saved before it is in the library, and the session can be ended. | REQ-REL-3, REQ-REL-6, REQ-LIB-17 | could lose a shot |
| The session holds no shot but a calibration or a note | It is kept, not discarded as empty. | REQ-LIB-18 | could lose other work |

**Also binds:** REQ-PERF-2

### UC-17 Import a session recorded away from the studio

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Swings recorded on a phone elsewhere join the golfer's history. | The golfer comes back with a phone session and launch-monitor files. |

Where it stands: 4 requirements: 2 built, not verified; 1 partial; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer imports the phone's recorded session and launch-monitor files into the library. | REQ-CAP-29 |
| 2 | The imported shots are analysed like any other. | REQ-ANA-1, REQ-ANA-2 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The import stops part-way (a cut power, a full disk) | The golfer is told which shots came in, and can run it again. | REQ-CAP-38 (proposed) (the use cases' gap UCG-9) | could lose a shot |

### UC-18 Run a studio day with several pupils

| Actor | Goal | Trigger |
|---|---|---|
| Coach | Pupil after pupil, all day, without the app slowing down or misfiling a shot. | A teaching day. |

Where it stands: 7 requirements: 1 built, not verified; 4 partial; 1 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach switches pupil in seconds from the recent golfers. | REQ-LIB-4 |
| 2 | The golfer on screen changes only when no session is being recorded. | REQ-LIB-6 |
| 3 | The bay's calibration and set-up load for each pupil without recalibrating. | REQ-DEV-17 |
| 4 | The pupil watches a dashboard on a second screen while the coach uses the main one. | REQ-REV-26 |
| 5 | The app shares the studio computer's graphics card with the simulator software all day. | REQ-PERF-6 |
| 6 | The app runs the whole day without slowing down or running out of memory. | REQ-REL-10 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The coach tries to switch golfer mid-capture | The golfer on screen does not change while a session is being recorded. | REQ-LIB-6 | could file shots under the wrong golfer |
| The coach forgets End, and the next pupil's shots go into the previous pupil's session | The coach moves those shots to the right golfer. | REQ-LIB-30 (proposed) (the use cases' gap UCG-18) | could file shots under the wrong golfer |

## EP-4 Reviewing a shot

The golfer or coach looks back at a shot: sees it from every camera, reads what the app measured and how far to trust it, and asks what happened on it and how it compares.

Where it stands: 78 requirements: 30 built, not verified; 30 partial; 14 planned; 4 proposed.

### UC-19 Replay a shot

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | See the swing from every side, in step, with what the app tracked drawn over it. | A shot is captured, or the golfer picks one from the session. |

Where it stands: 9 requirements: 4 built, not verified; 5 partial.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer finds the shot by its card: outcome, club, key numbers and a picture. | REQ-REV-18 |
| 2 | The app replays every camera together, in step, frame by frame and slowed down; opening the shot does not freeze the screen. | REQ-REV-1, REQ-PERF-2 |
| 3 | The golfer turns the tracked body and club overlays on or off. | REQ-REV-2 |
| 4 | The app draws the coach's lines at address by itself. | REQ-REV-3 |
| 5 | The coach draws lines, circles and angles, and the marks stay with the swing. | REQ-REV-4 |
| 6 | The golfer turns the swing in 3-D, with the club and ball, and the session's usual swing. | REQ-REV-24 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The shot is still being analysed | The app shows that it is. | REQ-HON-15 | – |
| The golfer moves to another swing | The coach's marks stay with the swing they were drawn on. | REQ-REV-4 | could lose other work |

**Also binds:** REQ-ACC-1

### UC-20 Read what the app measured on a shot

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Every reading the shot supports, named and graded in golf words. | The golfer opens a shot's readings. |

Where it stands: 37 requirements: 13 built, not verified; 13 partial; 9 planned; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app shows the swing positions P1 to P10, named in plain words or explained on hover. | REQ-ANA-3, REQ-ANA-4 (proposed) |
| 2 | The app shows the body readings: posture, turn, lower and upper body, wrists, tempo and head movement, and the kinematic sequence. | REQ-ANA-5, REQ-ANA-13, REQ-ANA-14, REQ-ANA-15, REQ-ANA-16, REQ-ANA-17, REQ-ANA-21, REQ-ANA-22, REQ-ANA-36, REQ-ANA-24 |
| 3 | The app shows the club readings: shaft and clubhead from both views, combined in 3-D, the plane and delivery, clubhead speed, low point and the face from the wrist sensor. | REQ-ANA-6, REQ-ANA-7, REQ-ANA-8, REQ-ANA-9, REQ-ANA-11, REQ-ANA-18, REQ-ANA-35 (proposed) |
| 4 | The app shows set-up aim, ball position and reach, lead-side load and ground force, and the impact camera's numbers. | REQ-ANA-19, REQ-ANA-20, REQ-ANA-25, REQ-ANA-26 |
| 5 | Where a launch monitor measured a quantity, its reading comes first, with the app's own estimate beside it. | REQ-ANA-12 |
| 6 | The app reads a left-handed golfer the right way round. | REQ-ANA-33 |
| 7 | Each reading is graded against a norm for players like the golfer and by club, with what it measures and what high or low means. | REQ-DIAG-1, REQ-DIAG-17, REQ-DIAG-2, REQ-DIAG-18 |
| 8 | A body figure shows each joint by how its readings graded. | REQ-REV-33 |
| 9 | The golfer opens a table of every reading and exports it as a spreadsheet file. | REQ-REV-19 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A chipping or putting stroke | The app measures tempo, acceleration through impact and length. | REQ-ANA-28 | – |
| The cameras and the sensors disagree | The app names the device that is wrong. | REQ-ANA-27 | – |

**Also binds:** REQ-HON-1, REQ-ACC-5, REQ-ACC-11, REQ-ANA-23

### UC-21 Check how far to trust a reading

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Know how sure each number is, and why one is missing. | A reading looks surprising, or is missing. |

Where it stands: 11 requirements: 4 built, not verified; 4 partial; 3 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | Each number shows how sure it is; each estimated reading says how it was made. | REQ-HON-3, REQ-ANA-10 |
| 2 | A reading from uncalibrated cameras says it is estimated, and a stand-in says it is a stand-in. | REQ-HON-6, REQ-HON-12 |
| 3 | New readings are graded at once with their warnings, and faults resting on readings not yet validated are marked. | REQ-DIAG-7 |
| 4 | Every reading's sources of error, and what its uncertainty covers, are stated. | REQ-HON-4 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A reading is missing | The golfer can tell it from a zero, and the app gives the real reason it is unavailable or refused. | REQ-HON-2, REQ-HON-5, REQ-DIAG-15 | – |
| A reading cannot see what it would grade | The app does not grade it. | REQ-HON-13 | – |

**Also binds:** REQ-HON-7

### UC-22 Ask what happened on that shot

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | A plain answer to "what happened there?" after a bad shot. | A shot the golfer did not like. |

Where it stands: 5 requirements: 2 built, not verified; 2 partial; 1 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app gives the shot's three outcome factors: strike, direction and flight. | REQ-ANA-29 |
| 2 | The app names the direction miss with one consistent set of thresholds. | REQ-DIAG-19 |
| 3 | The app draws the ball's flight from the launch-monitor reading. | REQ-ANA-30 |
| 4 | The app judges carry, ball speed and club speed against the golfer's own usual for that club. | REQ-DIAG-16 |
| 5 | The app shows what stood out on the shot against the golfer's usual swing. | REQ-DIAG-5 |

### UC-23 Chart a shot and compare it

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | See how a reading moved through the swing and what changed against another swing. | The golfer asks "what did I change?". |

Where it stands: 13 requirements: 6 built, not verified; 4 partial; 1 planned; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app charts each reading through the swing, says how far to trust each chart, and draws a gap as a gap. | REQ-REV-5, REQ-HON-8 |
| 2 | The golfer compares the shot against the session's median, its typical or least typical shot, the previous or next, similar shots, or a reference swing, lined up by swing position. | REQ-REV-6 |
| 3 | The golfer marks a reference swing and compares others against it, from any session, or against the golfer's best swings chosen by outcome. | REQ-REV-9, REQ-REV-8 (proposed) |
| 4 | The golfer sees two swings' videos side by side or overlaid, kept in step. | REQ-REV-7 (proposed) |
| 5 | The golfer plots any reading against any other in the session. | REQ-REV-10 |
| 6 | The golfer opens the wrist, posture, club-delivery and kinematic-sequence views, and a short read-out per area. | REQ-REV-20, REQ-REV-21, REQ-REV-22, REQ-REV-23, REQ-REV-31 |

**Also binds:** REQ-HON-9

### UC-39 Rate and note a shot

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | The golfer's own feel about a shot kept with the shot. | The golfer reviews a shot. |

Where it stands: 3 requirements: 1 built, not verified; 2 partial.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer rates a shot and adds a note; both stay with the shot. | REQ-REV-27 |
| 2 | The edit is on disk before the app shows it as made, and every view of the shot then shows it. | REQ-REL-4 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The app crashes straight after the edit | The edit is not lost. | REQ-REL-4, REQ-REL-6 | could lose other work |

## EP-5 Seeing a session's diagnosis and what to work on

After or during a session the golfer and coach see what the session showed: the faults and their likely causes, the evidence, what went well, and the few things to work on.

Where it stands: 35 requirements: 9 built, not verified; 18 partial; 3 planned; 5 proposed.

### UC-24 See the session's diagnosis

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Know what the session showed, and why, with the evidence to check it. | During a session, or after it, on Home or the session's diagnosis. |

Where it stands: 19 requirements: 7 built, not verified; 12 partial.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app shows the diagnosis while the session runs, in depth for analysis, and in print, with the same content in each. | REQ-REV-12, REQ-DIAG-14 |
| 2 | The app lists the faults and habits the session shows, with their likely causes where the session's shots support the link. | REQ-DIAG-3, REQ-DIAG-22 |
| 3 | For each finding the app shows the shots it fired on, the readings that fired it, how often, and whether it is still happening. | REQ-DIAG-23, REQ-DIAG-4 |
| 4 | The app groups findings by swing area on Home, and judges the backswing plane in layers. | REQ-COACH-9, REQ-DIAG-20 |
| 5 | The app explains the outcomes: where the yards went, what is usual for the golfer, which faults come together. | REQ-DIAG-24 |
| 6 | The app shows what the golfer does well, or says why nothing qualifies yet. | REQ-DIAG-21 |
| 7 | The app shows where the swing repeats and where it gets loose, and the launch-monitor numbers with dispersion and per-club figures. | REQ-DIAG-6, REQ-REV-11 |
| 8 | The app shows the kinematic sequence among the first things a coach sees. | REQ-REV-23 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A habit cannot be judged yet | It is shown as not yet judged, never as passed. | REQ-DIAG-10 | – |
| A finding rests on a provisional norm, or on coaching practice rather than published evidence | The app says so. | REQ-DIAG-12, REQ-DIAG-11 | – |

**Also binds:** REQ-HON-10, REQ-HON-11, REQ-PERF-4

### UC-25 Choose what to work on

| Actor | Goal | Trigger |
|---|---|---|
| Coach | One or two things to work on, causes before what they cause, that the golfer can take home. | The end of a session or a lesson, or a look at the work-ons. |

Where it stands: 12 requirements: 1 built, not verified; 5 partial; 3 planned; 3 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app picks from all the findings the few the golfer should work on, causes first. | REQ-COACH-1 |
| 2 | The golfer can see the whole coaching picture, not only the first five. | REQ-COACH-2 |
| 3 | The golfer dismisses a work-on, makes one the session's focus, and watches it live. | REQ-COACH-4 |
| 4 | The coach overrides any coaching decision, and the app records that the coach did. | REQ-COACH-13 (proposed) |
| 5 | The coach records a physical screen (mobility, strength) for causes the camera cannot see. | REQ-DIAG-13 |
| 6 | The app gives one or two things to work on, how each should feel, a drill from a coach-written library, and what to ignore. | REQ-COACH-21, REQ-COACH-22 |
| 7 | The app narrates its decisions in words suited to the golfer's level, and the golfer can ask about the session by voice or typing. | REQ-COACH-16, REQ-COACH-23 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The coach is teaching a pupil | The app's coaching reasoning stays off the pupil's screen, one step away on the coach's. | REQ-COACH-17 (proposed) | – |
| The golfer turns on the end-of-session observation | The app gives an observation at the end of the session; nothing goes to an outside service the golfer has not turned on. | REQ-COACH-23, REQ-PRIV-2 (proposed) | – |

**Also binds:** REQ-ACC-5

### UC-26 Find what separates my good shots from my bad ones

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | An answer to "what do I do on my pushes that I don't on my good shots?". | A self-coaching golfer looks for a pattern. |

Where it stands: 4 requirements: 1 built, not verified; 1 partial; 2 proposed. **Half or more of its requirements are proposed: much of this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer sees their tendencies across sessions by strike, direction and flight, filtered by club, and how they lead into one another. | REQ-COACH-5 |
| 2 | The golfer picks shots by outcome, or by a reading's value, and sees how often each fault fires on them and how their readings differ. | REQ-COACH-6, REQ-COACH-7 (proposed) |
| 3 | The app ranks which body readings move with each ball-flight number across the library, worded as "moves with". | REQ-COACH-8 (proposed) |

## EP-6 Lessons, practice and progress over time

The golfer and coach follow what is being worked on from one session to the next: lessons with a purpose, practice against the one thing prescribed, play, and progress over weeks and months.

Where it stands: 25 requirements: 3 built, not verified; 6 partial; 3 planned; 13 proposed. **Half or more of its requirements are proposed: much of this epic is not yet built or scheduled.**

### UC-27 Track progress over weeks and months

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | See whether last week's work stuck, and how the numbers moved over months. | The golfer or coach looks back over the golfer's history. |

Where it stands: 8 requirements: 3 built, not verified; 2 partial; 1 planned; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach opens a golfer's page and sees that golfer's sessions, whoever is chosen on Home, with date, shots, clubs, devices and work-on. | REQ-LIB-11, REQ-LIB-10 |
| 2 | The app follows each work-on across sessions: still happening, improving or resolved, with a trend. | REQ-COACH-3 |
| 3 | The app shows a dated trend for each fault, work-on, launch-monitor number or chosen reading. | REQ-REV-25 |
| 4 | The app shows the golfer's distances and sideways spread by club. | REQ-LIB-25 |
| 5 | The app shows where the swing repeats and gets loose across sessions. | REQ-DIAG-6 |
| 6 | Progress is judged on what holds at the next visit and in varied practice. | REQ-COACH-12 (proposed) |
| 7 | The golfer compares this session with an earlier one, side by side. | REQ-REV-35 (proposed) (the use cases' gap UCG-10) |

### UC-28 Give a lesson

| Actor | Goal | Trigger |
|---|---|---|
| Coach | A lesson with a purpose that concludes with a takeaway and leads into the next. | A pupil arrives for a lesson. |

Where it stands: 7 requirements: 2 partial; 1 planned; 4 proposed. **Half or more of its requirements are proposed: much of this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach sets the lesson's purpose and its sets: a baseline, a drill, a re-test. | REQ-COACH-10 |
| 2 | The pupil watches a second screen while the coach's reasoning stays on the coach's. | REQ-REV-26, REQ-COACH-17 (proposed) |
| 3 | The coach overrides the app where the coach disagrees, and the override is recorded. | REQ-COACH-13 (proposed) |
| 4 | The coach records a spoken commentary over a replay, and keeps a note for the session. | REQ-REV-34 (proposed), REQ-REV-28 (proposed) |
| 5 | The lesson ends with a takeaway and leads into the next one. | REQ-COACH-10, REQ-COACH-21 |

### UC-29 Practise between lessons

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Practice that measures the one thing the lesson prescribed, honestly. | The golfer practises alone between lessons. |

Where it stands: 5 requirements: 1 partial; 1 planned; 3 proposed. **Half or more of its requirements are proposed: much of this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer sets the session's focus. | REQ-CAP-20, REQ-COACH-4 |
| 2 | The app runs the practice against the one reading the lesson prescribed: the rate in its zone, the trend against the lesson and the spread, never a made-up score. | REQ-COACH-11 (proposed) |
| 3 | The app gives feedback by a designed policy, for example a tone for in or out of the chosen band, fading over shots. | REQ-COACH-14 (proposed) |
| 4 | When the golfer asks, the app gives a live cue from the wrist sensor at the top of the swing. | REQ-COACH-15 (proposed) |

### UC-30 Play a simulator round

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | See how the swing holds up in play, without the round polluting practice. | The golfer plays a round on the simulator. |

Where it stands: 4 requirements: 1 partial; 3 proposed. **Half or more of its requirements are proposed: much of this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app captures every shot of the round. | REQ-CAP-30 (proposed) |
| 2 | The golfer may say the session is play. | REQ-CAP-31 (proposed) |
| 3 | During the round the app gives at most one cue the golfer opted into, and debriefs the round afterwards. | REQ-COACH-20 (proposed) |
| 4 | The golfer views the round's shots apart from practice, each still counted in full. | REQ-CAP-30 (proposed), REQ-HON-11 |

### UC-31 Get a quick fix before an event

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | A way round a broken swing on the morning of an event, without rebuilding the plan. | The golfer's game breaks just before a round that matters. |

Where it stands: 1 requirement: 1 proposed. **Every requirement here is proposed: this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The app offers a short-term fix that expires and never changes the golfer's plan. | REQ-COACH-18 (proposed) |

## EP-7 Reports and sharing

The golfer leaves with a report, and the golfer's data can leave the app when the golfer chooses: to a coach for review, or out as files, and nothing more than the golfer chose.

Where it stands: 22 requirements: 3 built, not verified; 7 partial; 4 planned; 8 proposed.

### UC-32 Make and send a session report

| Actor | Goal | Trigger |
|---|---|---|
| Coach | The golfer takes home what happened, what to work on and why, under the studio's name. | The end of a session or lesson. |

Where it stands: 12 requirements: 1 built, not verified; 6 partial; 3 planned; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach selects one or more sessions for a report. | REQ-LIB-10 |
| 2 | The app makes the report in the background: what happened, what to work on and why, with pictures, a launch-monitor appendix and the coach's note. | REQ-REV-13, REQ-COACH-21, REQ-PERF-5 |
| 3 | The report carries the studio's and the coach's details. | REQ-REV-32 |
| 4 | A golfer's photo goes in only with their recorded consent. | REQ-PRIV-4 |
| 5 | The coach reads the report inside the app. | REQ-REV-15 |
| 6 | The report is small enough to email, and the app sends it directly or hands it to the mail app, on every platform; the mail password stays in the secure store. | REQ-REV-17, REQ-REV-14, REQ-PRIV-3 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The email cannot be sent | The app says so and keeps the report to send again. | REQ-REV-36 (proposed) (the use cases' gap UCG-11) | could lose other work |
| The golfer has not consented to photos | The report goes without them. | REQ-PRIV-4 | – |

**Also binds:** REQ-PRIV-2

### UC-33 Print a panel

| Actor | Goal | Trigger |
|---|---|---|
| Coach | Paper to hand a pupil. | The coach wants a printed panel. |

Where it stands: 1 requirement: 1 planned. **Nothing here is built yet.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach prints, or saves as a PDF, the Diagnostics, Launch monitor or Charts panel. | REQ-REV-16 |

### UC-34 Send a session to a coach for review

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Coaching between visits, with no server the project must run. | The golfer wants a coach's view of a session between visits. |

Where it stands: 5 requirements: 1 built, not verified; 4 proposed. **Half or more of its requirements are proposed: much of this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer chooses what to send: the session, and only what the golfer picks. | REQ-PRIV-5 (proposed), REQ-COACH-19 (proposed) |
| 2 | The golfer exports a clip with the coach's marks and lines drawn in. | REQ-REV-29 (proposed) |
| 3 | The coach reviews it, with a spoken commentary over the replay, and sends the review back. | REQ-COACH-19 (proposed), REQ-REV-34 (proposed) |

**Also binds:** REQ-PRIV-1

### UC-35 Take the data out of the app

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | The golfer's data in other tools: spreadsheets, motion software, a reader of the open format. | A researcher, a spreadsheet coach or the golfer wants the data. |

Where it stands: 4 requirements: 1 built, not verified; 1 partial; 2 proposed. **Half or more of its requirements are proposed: much of this use case is not yet built or scheduled.**

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer exports shots or a whole session as videos and swing data. | REQ-LIB-19 |
| 2 | The golfer exports one row per shot with every reading, its uncertainty and the launch-monitor numbers, for a session or the library. | REQ-LIB-20 (proposed) |
| 3 | The golfer exports the 3-D skeleton in a standard motion format, which says when the cameras were not calibrated. | REQ-LIB-22 (proposed) |
| 4 | The swing files are in an open, documented format with a published reader. | REQ-LIB-21 |

## EP-8 Keeping the library safe

The golfer's and coach's history is never lost, misfiled or removed without their say: through crashes, upgrades and a shared folder, and back again from the trash or an archive.

Where it stands: 29 requirements: 8 built, not verified; 13 partial; 3 planned; 5 proposed.

### UC-36 Look after the golfers

| Actor | Goal | Trigger |
|---|---|---|
| Coach | Each golfer's details and bag right, so every norm and per-club number fits them. | A new pupil, a change of details, or a pupil list to bring in. |

Where it stands: 5 requirements: 1 built, not verified; 3 partial; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach adds a golfer with name, handedness, height, handicap and the bag, or imports golfers from a roster file. | REQ-LIB-2, REQ-LIB-5 (proposed) |
| 2 | The coach keeps the golfer's bag (loft, length, shaft, markings), and the analysis uses it. | REQ-LIB-3 |
| 3 | The coach renames a golfer; every session stays with them. | REQ-LIB-7 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The coach moves a golfer to the trash | All their sessions go to a trash the app can restore from, the coach is told what went where, and it is refused while one of their sessions is being recorded. | REQ-LIB-8 | could lose a shot |

### UC-37 Put a session under the right golfer

| Actor | Goal | Trigger |
|---|---|---|
| Coach | A session recorded under the wrong golfer, or none, ends up with the right one, complete. | The coach notices shots filed under the wrong golfer, or under no golfer. |

Where it stands: 2 requirements: 2 built, not verified.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The coach assigns shots recorded with no golfer chosen to a golfer. | REQ-LIB-1 |
| 2 | The coach moves a session to another golfer, with everything in it. | REQ-LIB-9 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The session is still in progress | The move is refused. | REQ-LIB-9 | could file shots under the wrong golfer |

### UC-38 Remove a shot, and bring it back

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | A shot removed only by choice, and recoverable until the app's own clean-up purges it. | A false shot, or a shot the golfer does not want. |

Where it stands: 3 requirements: 1 built, not verified; 2 partial.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The golfer moves a shot to the trash. | REQ-LIB-12 |
| 2 | The golfer undoes it until the app's own clean-up purges it, even where the system would empty its own trash first. | REQ-LIB-12 |
| 3 | The trashed shot's number is never given to another shot. | REQ-LIB-13 |
| 4 | The app empties the trash by itself on the studio's rules. | REQ-LIB-26 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The system would empty its own trash first (a full Recycle Bin) | The shot can still be brought back until the app's own clean-up purges it. | REQ-LIB-12 | could lose a shot |
| The golfer wants a whole session gone | The session goes to the trash and can be brought back, as a shot can. | REQ-LIB-12 (the use cases' gap UCG-12) | could lose a shot |

### UC-40 Archive old sessions to save space

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | A library that fits the disk, with nothing lost that was not chosen. | The disk is filling, or the archiving rules fall due. |

Where it stands: 5 requirements: 3 built, not verified; 1 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner archives a session. | REQ-LIB-14 |
| 2 | The app archives old sessions by itself, on the rules the studio set. | REQ-LIB-26 |
| 3 | The studio owner restores an archived session with every edit kept. | REQ-LIB-14 |
| 4 | Two-camera shots stay within the storage budget. | REQ-CAP-12, REQ-LIB-27 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The studio owner asks to drop the raw frames | The app warns first, and drops them only when asked. | REQ-LIB-14 | could lose other work |
| The disk holding the library fails | A second copy of the library brings every golfer's history back. | REQ-LIB-28 (proposed) (the use cases' gap UCG-13) | could lose a shot |

### UC-41 Share one library between computers

| Actor | Goal | Trigger |
|---|---|---|
| Studio owner | Two computers in the studio see the same golfers, without tearing a session in two. | A second computer uses the studio's library. |

Where it stands: 5 requirements: 2 built, not verified; 1 partial; 2 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The studio owner points each computer at the same shared folder. | REQ-LIB-15 |
| 2 | The library stays on the studio's own machines, with no cloud service. | REQ-PRIV-1 |
| 3 | Two computers never record into one session, and neither ends a session live on the other. | REQ-LIB-16 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| The shared folder goes out of reach | The app keeps working, says so, and catches up when it returns. | REQ-REL-9 (proposed) | could lose a shot |
| The library moves to a new disk or computer | Everything in it moves too, and the app finds it. | REQ-LIB-29 (proposed) (the use cases' gap UCG-14) | could lose a shot |

### UC-42 Recover after a crash or a power cut

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | Nothing saved before the failure is lost, and nothing is left stuck open. | The app crashed, or the power went. |

Where it stands: 3 requirements: 3 partial.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | Every shot saved before the crash is in the library. | REQ-REL-3 |
| 2 | No saved shot or edit is lost to the power cut. | REQ-REL-6 |
| 3 | The golfer ends the session the crash left running, without starting a new one. | REQ-LIB-17, REQ-REL-3 |

### UC-43 Bring old shots up to date

| Actor | Goal | Trigger |
|---|---|---|
| Golfer | A golfer's history improves with the app, and says which version made each result. | A new version improves an analysis, or the golfer asks for a re-analysis. |

Where it stands: 7 requirements: 4 partial; 2 planned; 1 proposed.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | Old shots get the app's improvements, and every result says which version made it. | REQ-ANA-31 |
| 2 | The golfer re-analyses a session or the library; the app finishes or says that it did not. | REQ-ANA-32 |
| 3 | Each shot is analysed by the data it holds. | REQ-ANA-2 |
| 4 | No upgrade alters a recording or a golfer's input, and every swing file the app has written still opens. | REQ-LIB-23, REQ-HON-14 |

**Alternative and failure paths**

| When | What happens | Covered by | At stake |
|---|---|---|---|
| A re-analysis stops part-way | The app says it did not finish. | REQ-ANA-32 | could lose other work |
| The golfer asks for a re-analysis | Every input of the golfer's and the coach's (ratings, notes, club corrections, the coach's marks, the reference swing) is kept as it was. | REQ-LIB-31 (proposed) (the use cases' gap UCG-19) | could lose the golfer's own input |
| A swing file written by a much older version | It still opens, with the set-up it was captured with. | REQ-LIB-23 | could lose a shot |
| An update arrives during the re-analysis | The update waits. | REQ-INST-5 | could lose other work |

## EP-9 Tuning and checking the analysis

The faults and norms a golfer is graded against are kept right by the coach who owns them, inside the app, and the club and body tracking is checked against marked truth, so a gap is found before a golfer meets it.

Where it stands: 6 requirements: 3 built, not verified; 2 partial; 1 planned.

### UC-44 Tune the diagnostics model

| Actor | Goal | Trigger |
|---|---|---|
| Coaching Lead | What counts as good is owned and changed by the coach, not a developer. | A norm, a fault or a cause needs changing. |

Where it stands: 4 requirements: 2 built, not verified; 2 partial.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The Coaching Lead changes the range counted as normal (a norm's corridor), which side fires, a measure, a characteristic and its causes, and how strict grading is; the edits are kept. | REQ-DIAG-8 |
| 2 | The Coaching Lead sees the model's health: measures nothing yet calculates, habits that can never fire, broken links. | REQ-DIAG-9 |
| 3 | Each cause link records whether it rests on published evidence or on coaching practice. | REQ-DIAG-11 |
| 4 | A norm stays provisional, and is shown so, until the Coaching Lead rules it proven on evidence. | REQ-DIAG-12 |

### UC-45 Check the club and body tracking against marked truth

| Actor | Goal | Trigger |
|---|---|---|
| Analyst | A measured accuracy for the club and body tracking. | The tracking changes, or its accuracy is in question. |

Where it stands: 2 requirements: 1 built, not verified; 1 planned.

**Main path**

| # | Step | Covered by |
|---|---|---|
| 1 | The analyst marks the club and body on video frames as reference truth. | REQ-REV-30 |
| 2 | The app cross-checks the cameras against the sensors and names the device that is wrong when they disagree. | REQ-ANA-27 |

## Traceability

### Requirements in no use case

None: every one of the 278 requirements is a step, a path or a constraint of at least one use case (`uc-map.csv`).

### The gaps the use cases found

Steps and paths that no requirement covered when the use cases were first drawn. The Product Owner accepted each as a requirement, except one, which was struck[^gaps]. The gap ids are fixed and never reused.

| Gap | Use case | Kind | Step | Now |
|---|---|---|---|---|
| UCG-1 | UC-1 | an alternative path | The studio owner refuses a permission: the app says what will not work without it and how to grant it later. | covered by REQ-INST-14 |
| UCG-2 | UC-2 | an alternative path | The installed update fails or will not start: the previous version can be put back, with the library untouched, including reading any file the newer version wrote. | covered by REQ-INST-15 |
| UCG-3 | UC-2 | an alternative path | The app is uninstalled or reinstalled: the library is left untouched. | covered by REQ-INST-16 |
| UCG-4 | UC-5 | an alternative path | The studio no longer uses a phone: the studio owner unpairs it, and it stops reconnecting. | covered by REQ-DEV-29 |
| UCG-5 | UC-6 | an alternative path | A sensor's battery is low: the app warns before the session starts. | struck for now: REQ-CAP-14 already keeps the session going when a sensor drops |
| UCG-6 | UC-11 | an alternative path | The disk is nearly full: the app warns before the session starts. | covered by REQ-CAP-35 |
| UCG-7 | UC-12 | an alternative path | A launch-monitor reading was paired with the wrong shot: the golfer moves it to the right shot. | covered by REQ-CAP-36, won't have[^wont] |
| UCG-8 | UC-15 | an alternative path | The golfer wants to stop capturing for a while (to talk, to type a note): the golfer stops capture and starts it again within the session. | covered by REQ-CAP-37 |
| UCG-9 | UC-17 | an alternative path | The import stops part-way (a cut power, a full disk): the golfer is told which shots came in, and can run it again. | covered by REQ-CAP-38 |
| UCG-10 | UC-27 | a step | The golfer compares this session with an earlier one, side by side. | covered by REQ-REV-35 |
| UCG-11 | UC-32 | an alternative path | The email cannot be sent: the app says so and keeps the report to send again. | covered by REQ-REV-36 |
| UCG-12 | UC-38 | an alternative path | The golfer wants a whole session gone: the session goes to the trash and can be brought back, as a shot can. | covered by REQ-LIB-12 |
| UCG-13 | UC-40 | an alternative path | The disk holding the library fails: a second copy of the library brings every golfer's history back. | covered by REQ-LIB-28 |
| UCG-14 | UC-41 | an alternative path | The library moves to a new disk or computer: everything in it moves too, and the app finds it. | covered by REQ-LIB-29 |
| UCG-15 | UC-2 | an alternative path | Any update installs: the studio's settings, the bay's calibration and set-up, the paired phones and the Coaching Lead's edits to the model are all kept. | covered by REQ-INST-17 |
| UCG-16 | UC-8 | an alternative path | Saving a set-up change fails: the studio owner is told it was not saved, so it is not lost unnoticed at the next start. | covered by REQ-DEV-30 |
| UCG-17 | UC-16 | an alternative path | A phone clip is still on its way at End: the app says so and offers to wait for it. | covered by REQ-CAP-39 |
| UCG-18 | UC-18 | an alternative path | The coach forgets End, and the next pupil's shots go into the previous pupil's session: the coach moves those shots to the right golfer. | covered by REQ-LIB-30 |
| UCG-19 | UC-43 | an alternative path | The golfer asks for a re-analysis: every input of the golfer's and the coach's (ratings, notes, club corrections, the coach's marks, the reference swing) is kept as it was. | covered by REQ-LIB-31 |

### Requirements that bind across use cases

These hold everywhere they apply; the use cases above name them only where they bite hardest.

| Requirement | Constraint on |
|---|---|
| REQ-ANA-23 | UC-20 |
| REQ-PERF-2 | UC-11, UC-12, UC-16 |
| REQ-PERF-4 | UC-24 |
| REQ-PERF-7 | UC-1, UC-11 |
| REQ-PRIV-1 | UC-34 |
| REQ-PRIV-2 | UC-32 |
| REQ-ACC-1 | UC-19 |
| REQ-ACC-2 | UC-9 |
| REQ-ACC-11 | UC-20 |
| REQ-ACC-5 | UC-20, UC-25 |
| REQ-HON-1 | UC-20 |
| REQ-HON-7 | UC-21 |
| REQ-HON-9 | UC-23 |
| REQ-HON-10 | UC-24 |
| REQ-HON-11 | UC-12, UC-24 |

## Change log

| Version | Date | Change | Approved by |
|---|---|---|---|
| v0.1 | 26 Nov 2026 | First draft: epics and use cases over the requirements' draft v0.2. Review round 1 applied: a last shot whose late data never comes, a re-analysis the golfer asked for, and an update keeping the studio's settings, given their paths; UCG-15 to UCG-19 added | (a draft) |
| v1.0 | 26 Nov 2026 | Approved and moved into `governance/artefacts/` with the requirements. Nine epics. The accepted gaps are now requirements, each named where its step is; the low sensor battery (UCG-5) is struck and its path removed. Two gaps no longer cite a source that did not say them (UCG-16, UCG-17) | Product Owner[^approved] |
| v1.1 | 26 Nov 2026 | With the requirements' v1.1: REQ-CAP-36 is won't have, and UC-12's path for a reading paired with the wrong shot says so; REQ-INST-15 is planned, and nothing waits on the Solution Architect; the four counts that named them follow. UC-7's lost-calibration path no longer names a zero: the trunk calibration sets "straight" | the Product Owner[^wont]; the Solution Architect (REQ-INST-15)[^sa-afternoon]; the Product Owner and the Design Lead (the zero)[^c5] |

[^approved]: Product Owner, 26 Nov 2026 (the use-case decisions: "agree with your recommendations"): Q1, nine epics; Q2, the gaps; Q4, this page's home.
[^po]: Product Owner, 26 Nov 2026: "we need a layer above the PRD that organises the requirements into use-cases and epics."
[^home]: Product Owner, 26 Nov 2026 (the use-case decisions, Q4): the use cases have their own page, `governance/artefacts/use_cases.md`, owned by the Product Owner, and the traceability matrix's generator reads `uc-map.csv`.
[^hand]: Product Owner, 25 Nov 2026: the requirements are hand-edited at their governed home; applied to the use cases over them by the coordinator's call of 26 Nov 2026, for the Product Owner's review.
[^gaps]: Product Owner, 26 Nov 2026 (the use-case decisions, Q2): 18 gaps accepted as new requirements, and UCG-5 (the low sensor battery) struck; UCG-2, UCG-7, UCG-12 and UCG-19 also go to the Solution Architect. UCG-12 is met by widening REQ-LIB-12 to a whole session (Q3).
[^wont]: Product Owner, 26 Nov 2026 (the Solution Architect's afternoon batch, item 6): REQ-CAP-36 is won't have (`requirements.md`, `[^wont]`).
[^sa-afternoon]: Solution Architect, 26 Nov 2026 (the Solution Architect's afternoon batch, item 7): REQ-INST-15 is approved as a requirement, marked planned.
[^c5]: Product Owner and Design Lead, 26 Nov 2026 (C5, and the battery colours): the Zero button is removed; the trunk calibration sets "straight".
