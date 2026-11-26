# Platform refactor: the forecast, for a decision on scope and pace (24 Nov 2026, 22:30)

For the Product Owner. Sources: the six item plans (`docs/implementation/platform_refactor/plans/PLAN-B1.md`, `docs/implementation/platform_refactor/plans/PLAN-B2.md`, `docs/implementation/platform_refactor/plans/PLAN-C.md`, `docs/implementation/platform_refactor/plans/PLAN-D.md`, `docs/implementation/platform_refactor/plans/PLAN-E.md`, `PLAN-F.md`), `LEDGER.csv`, `ORDER.md`, and main at `cb9f376c`.

## The answer

**Yes: D, E and F grow the same way.** Planned item by item they come to **234 h, against 48–74 h rough: 3.8 times.**

| Stage | Rough | From its item plan | Items | Times | Why |
|---|---|---|---|---|---|
| D, the capture seam | 20–30 h | **98 h** (70–125) | 38 | 3.9 | no test builds the shot processor, a camera or a sensor manager today, so a test rig and pins come first; a design not yet written; about 20 h other plans sent here |
| E, host and settings | 16–24 h | **83 h** (58–117) | 34 | 4.2 | five jobs in one stage; no quit or settings pins exist; 550 QML uses of the settings object |
| F, the phone link | 12–20 h | **53 h** (37–69) | 23 | 3.3 | a 4,900-line class to split; six defects that need real phones |

**The whole refactor, from tonight: about 390 h of effort left (280–530 h).** Done so far: about 45 h, so it is about **10% complete**, not 16%. ORDER's "about 240 h left" becomes 390.

| Left | h | Range | State of the figure |
|---|---|---|---|
| Stage B Part 1 (44% done) | 29 | 21–36 | item-planned, ruled |
| Stage B Part 2 (6%) | 44 | 33–56 | item-planned, ruled; less B2.8's 3 h, which its P3 ruling sent to Stage D (counted there, as SD-14) |
| Stage C (12%) | 77 | 55–101 | item-planned, design approved |
| Stage D | 98 | 70–125 | item-planned, **design not written** |
| Stage E | 83 | 58–117 | item-planned, **six questions open** |
| Stage F | 53 | 37–69 | item-planned, **hangs on Q3** |
| #224-U | 8 | 8–24 | **still rough**: not item-planned, so "every stage" is not quite true; the high end assumes it grows as the others did. B1.20 (6 h, in Part 1's figure) is one line in its plan, for 511 files |
| **Total** | **392** | **282–528** | about 177 items; at the ledger's 342,000 tokens an item, about 60 million tokens |

## Pace, measured

- **Today:** 23 ledger items between 08:59 and 22:20: 13.3 hours of running at two lanes plus a docs lane. That is **1.7 items an hour**, and 7.9 million tokens.
- **An estimated hour is not a clock hour.** `LEDGER.csv` records tokens and builds, not hours. ORDER counts today's work as about 45 h of estimate, done in 13.3 h of running: **3.4 estimated hours for each hour of running.** The three earlier plans assumed 1.7 (190 h of effort as 112 h of running). Today was twice as fast as the plans assume.
- **Running hours a week at two lanes: 98–112** (14–16 h a day, seven days), or 70–80 on five days. That delivers **165–190 effort hours a week on the plans' conversion, and 330–380 on today's.**
- **Why not to bank today's figure:** it is one day, of small items (bug fixes and tools), none of which waited on a visit or ran alone as a tree-wide move; 7 of the 23 had a review blocker; and Stages D and E rebuild far more per item.

## Calendar

**About 2½ weeks: finished around 11 December. Range: 4 December to 24 December.**
- 392 h is 1.1 weeks of running at today's pace and 2.2 weeks at the plans' pace. The middle, 37 effort hours a running day, is about 11 days of running.
- Add about 6 days of waits: five visits, of which three hold a parked chain; three design sittings (D, E, F); and the moves that run alone.
- The early date needs the low figure, today's pace every day, and no wait longer than a day. The late date is the high figure at the plans' pace.

## What the range hangs on

1. **Three unruled designs.** D's ten questions, E's six, F's Q3. Each list is provisional until its pack is ruled; C's list doubled when its design was itemised.
2. **The capture test rig (SD-2a).** `ShotProcessor` takes seven concrete objects and has never been built in a test. If it needs a seam in the product first, D grows by an item and the whole shot chain waits.
3. **Rework.** 7 of 23 items needed a fix round. D's timing items and E's teardown are harder than anything done today.
4. **The visits.** Each parked chain waits for you at the studio PC. A day's delay on a visit is a day on that track.
5. **The conversion** from effort to running hours rests on one day.
6. **Both lanes staying full.** After Stage C, lane 2 holds Stage E (83 h) and D's device chain (35 h); lane 1 holds D's shot chain (46 h). The dates assume work is moved across when a lane runs out; `main.cpp`, the settings header and the two managers each allow one lane at a time, which limits that.

## The scope levers

Ranked by hours saved for each unit of value lost. **Value lost: 0** = the count is re-set by a ruling, and nothing a later item or a loss guard needs goes; **1** = an Accept count or a known defect stays open, on an issue, and nothing depends on it; **2** = a guard against loss, or a security gain, goes.

| # | Lever | Saves | Lost | What it gives up |
|---|---|---|---|---|
| 1 | **Q17: no global is renamed for the count.** A global leaves only when its owner changes (SD-12d, SE-5h, SE-9b) | 9.5 h | 0 | The count stays near 18. Also avoids about 1,000 scripted QML edits, the kind the domain refactor made without their tests |
| 2 | **Q5: lint A6 back to P9's wording** (SD-10c, SD-12c, SE-9a) | 9 h, and 9 h more that the plans do not hold | 0 | Untyped maps stay on the device, monitor and board boundaries, out of the rule's scope. If the answer is "keep A6 wide", Stage E gains three items for 42 findings in the settings, markup, profile and archive headers |
| 3 | **B1.20, the namespaces, to #224-H** (already recommended) | 6 h | 0 | 511 files keep today's namespaces; B1.20a's test still guards duplicates |
| 4 | **`pp_rules_vision` to an issue** (B2.8 or SD-14) | 3 h | 0 | 55 of 5,698 test listings (1%) |
| 5 | **#193, the app's own trash, to its issue** (SE-10) | 2.5 h | 0 | New behaviour; not refactor work |
| 6 | **Stage F parked** (Q3: phones are not a main path for six months) | 50 h | 1 | Seven A5 files, six A6 findings, #78, #79, #208 and #241 stay open, each named on an issue. Visit 5 goes. Stage E still gives PPCP's statics an owner |
| 6b | or **Stage F, files only** | 28 h | 1 | Every A5 line closes; the service is not split; the defects stay on their issues |
| 7 | **Settings: one class, its bodies out of the header, no groups** (SE-5d, SE-5e.1, SE-5e.2) | 9.5 h | 1 | `AppSettings` stays one object of 110 properties. No Accept count names groups |
| 8 | **Threads: name the eight owners as exceptions** (Q9's alternative; SE-6b–d) | 9.5 h | 1 | "Hand-written teardowns 8 → 0" is not met. One parked item fewer |
| 9 | **Camera and manager tidying left out** (SD-11a, SD-11b, SD-12a) | 9 h | 1 | `CameraInstance` stays 2,186 lines. It closes no lint line and no issue, and removes two of D's six parked timing items |
| 10 | **D-6, the board's owner, to #224-U** (SD-10b) | 3 h | 1 | Three `#224` lines stay |
| 11 | **Stage C: calibration stays in QML** (C-9, C-10, C-11 to C-15, C-17) | about 35 h | 1–2 | **Needs the Solution Architect**, who approved it tonight as part of the Rig design: 3,978 lines of QML keep their logic; A5 ×7 and A6 ×2 stay. Visit 2 halves |
| 12 | The secrets move (SE-7a–d) | 8 h | 2 | **Not recommended:** the API keys stay in a plain settings file |
| 13 | The pins and inventories in D, E and F | about 35 h | 2 | **Not recommended:** they are what stops a lost shot, a lost setting or a lost pairing |

**What they add up to.**

| Scope | Left | Finished about (37 effort hours a running day, plus 6 days of waits; 5 with a visit fewer) |
|---|---|---|
| As written | 392 h | 11 Dec |
| Levers 1–5 (rulings only; nothing lost) | 362 h | 10 Dec |
| and 7–10 (work that closes nothing) | 331 h | 9 Dec |
| and 6b, Stage F files only | 303 h | 8 Dec |
| or 6, Stage F parked | 281 h | 7 Dec |
| and 11, Stage C's calibration move | about 245 h | 6 Dec |

**Merges.** Within the plans, D's 38 items could be about 32 if the five inventories are two and SD-8e folds into SD-8d; that saves reviews (about 5 h), and makes each review larger. The plan's own rule is to split only where parts can be reviewed apart, so I have not counted it.

**Calendar levers (no hours saved).**
- **Five visits become four**: Stage E needs no visit of its own if its "before" quit run (10 min) is added to **visit 1** and its "after" run to visit 2 or 3.
- **Visit 3 mid-stage, not at Stage D's end** (see "Needs you", 4).
- **Four become three** if Stage F is parked or files-only: its pairing check rides on visit 3, which already needs two phones.
- **Write SD-0, SE-0 and SF-0 now**, in the docs lane, so the three sittings are ready before their stages. Each costs a day if it is asked for only when its stage starts.

## What I recommend

Take levers 1–5 now (five rulings, 30 h, nothing lost). Answer Q3: if phone capture is not a main path this winter, park Stage F or cut it to files only. Decide levers 7–9 when SE-0 and SD-0 are put to you, with their counts. Keep the pins, the inventories, the secrets move and the visits. That is **about 280–330 h left, finishing around 7–9 December** at a pace between the plans' and today's. Scope moves the date by days; the pace and the waits move it by weeks, so the visits and the three sittings matter more to the calendar than any cut.

## Needs you

1. **Q3:** how much does phone capture matter over the next six months?
2. **Visit 1:** add a quit at each start-up point and during the pose warm-up on the studio PC (10 min). Stage E's first items change teardown beside Stage C, and the Windows quit path has never been run. **There is no script for it yet:** it is a by-hand list of six steps, in `docs/implementation/platform_refactor/plans/PLAN-E.md` note N4, which the coordinator must add to `docs/implementation/platform_refactor/visits/VISIT-1.md` before the visit.
3. **Visit 3's date:** Stage D's parked patches sit mid-chain. Booked two-thirds through the stage, seven wait for you; left to its end, twelve do.
4. **The levers:** which of 1–11 to take.
