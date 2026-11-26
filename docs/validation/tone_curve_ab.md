# #183: does the tone curve help the analysis? The A/B result

25 Nov 2026, run overnight on the studio PC. The test plan was approved with its criteria P1 to P4 and S1[^approvals], and the verdict below is accepted[^accepted]. The plan and the run log are working files kept outside the repository; a "plan §" reference is to the plan's sections. The scripts are in `tools/swinglab/` (`tone_ab*.py`, `tone_ab.sh`, `tone_ab_studio.cmd`, `ppsw_peek.py`), and the SwingLab developer guide (§5) says what each does.

## Verdict: neutral. The curve does not help pose, so it does not go into the analysis input

By the decision rule registered before the run (plan §1), with P1's reading of "helps" (pose better, and the shaft and ball trackers no worse):

| Stage | Verdict | In one line |
|---|---|---|
| **Pose** (decides) | **neutral** | Nothing passed the registered test, but everything that moved, moved the wrong way. Face-on jitter rose 3.4 % (interval +1.9 % to +4.9 %, under the 10 % margin). Dropped joints at the top rose from 68 to 83 joint-frames (+22 %). That passes both size margins, and only its interval, which holds zero, keeps it from "worse". |
| Shaft tracker | neutral (leaning better) | Tracked frames in the span rose 0.91 points (interval +0.45 to +1.41), just under the 1-point margin. No give-up changed. The band-lock and hand-mark checks hold. |
| Ball tracker | no worse | Launches found: net +4 passes the flip rule. But only 1 of the 5 gained launches lands within 20 ms of impact; the others are 47 ms to 2.6 s away from it, so they are not real launches. Detection moved on 39 swings (median 0). The still-ball spread can't move at all (0 on every swing). |
| Clubhead speed against the launch monitor | holds by the rule (§6) | With pose free, the limits of agreement widened from 24.8 to 46.4 mph. The interval of the change holds zero, and one swing causes almost all of it. |
| **Overall** | **neutral** | Pose doesn't improve, so not "helps". Nothing worsens beyond the registered test, so not "hurts". |

**What it leads to** (plan §1.3):
- The curve goes to the **display only** (the display-curve plan), and recordings stay linear.
- The curve would go into recordings only if the Product Owner wanted it there for consistency, through a Solution Architect proposal. He did not[^accepted].

**Why "neutral" isn't "harmless".** On pose, every figure that moved, moved against the curve:
- face-on jitter, on every golfer (A +2.6 %, B +7.6 %, C +3.8 %);
- dropped joints at the top, face-on and down the line;
- the motivating swing itself (§3.3);
- the 3.5 mph median shift in clubhead speed it causes through pose.

The trackers lean the other way, but only slightly. Together this argues against curving the analysis input, even for consistency.

The likely reason is in the strata: these recordings are very dark. The face-on scene's median luma at address is 5 to 12 codes out of 255 on every swing. A curve applied after encoding can't bring back shadow codes that x264 has already merged (plan §7.1), so it mainly raises noise. The recommendation is to fix the light at the camera, the exposure and gain target already filed under #183, not with a decode curve. That is a recommendation, not a finding of this test.

## 1. What ran, and the checks that make it a valid run

- **Machine.** The studio PC's Release `swinglab_run`, built from main at 66a4d535 (34ad0388, the switch, plus later docs commits). It was built by `studio-build.cmd tool nopull` after `git pull --ff-only`, under the studio PC's build lock (lane i183-ab), with the studio PC idle.
- **Population.** 148 swings in 23 sessions:
  - the corpus's 139 swings with video, plus the 25 Nov library's 9 shots (P2);
  - 92 two-camera and 56 face-on only;
  - 3 golfers: **A** (99 swings), **B** (23) and **C** (26), lettered in the order of each golfer's first session.
  - The 25 Nov library session was an app-testing session, not a real practice session[^apptest]. Its 9 shots are 6 % of the population.
- **Inputs.** Copies: `in/` has no `.raw`; `in_nobase/` has no `.raw` and no `.ballbase.f32`.
- **The arms**, each one `swinglab_run` per swing into a fresh folder, with a 900 s watchdog:

| Arm | Runs | Failed | What it is |
|---|---|---|---|
| B1, B2 | 148 each | 0 | the baseline twice, pose free (the floor) |
| C1, C2 | 148 each | 0 | curved (γ 0.80), pose free, twice (C2 ran because one B2 differed) |
| PB, PC | 148 each | 0 | baseline and curved, pose pinned, with `--trace`, `--dtl` (two-camera), `--doc-blocks` |
| PBs, PCs | 80 each | 0 | the ball's no-baseline pair |
| PB2 | not run | | the one-swing PB repeat was identical |

- **The checks** held on every run:
  - every camera read `frames: mp4`;
  - only the curved arms were curved;
  - pose ran in every free arm;
  - the pinned arms list their injected tracks in `tone.injected`;
  - **histogram check:** every curved run's first-frame histogram equals its baseline's mapped through the table, and differs from it: 240 of 240 cameras for C1, 240 of 240 for PC and 138 of 138 for PCs. So the curve was applied, and applied once.
- **The one-swing step** (25 Nov Wrist_03 shot 1, on a copy):
  - both write-back refusals exit 1 and leave the copy's document unchanged;
  - PB with and without `--trace`/`--dtl` is identical;
  - PB with B1's own pose reproduces B1's ball, club, phases and metrics.
- **Read only.** The 932 corpus and library files read were re-checked after the run: unchanged.
- **Time.** 1,048 runs from 20:19 to 23:28.

**About the pinned arms (PB/PC).** Their pose was injected from uncurved frames, which `tone.injected` marks. So PB/PC measure **only the shaft and ball trackers, given the same pose**. Pose is judged only on C1 against B1, where pose ran on the curved frames themselves.

## 2. The noise floor

| Pair | Identical outside wall times | Different |
|---|---|---|
| B1 against B2 | 147 | 1: 07-03 W1 shot 2, one down-the-line pose frame |
| C1 against C2 | 148 | 0 |
| PB against PB2 | one-swing step: identical | not run |

So the floor is effectively zero, and the margins alone decide.

## 3. Pose (C1 against B1)

| Measure | Face-on (147 swings) | Down the line (91) |
|---|---|---|
| **P-1 dropped joints at the top** (5–12 below 0.3, Top ± 50 ms, pooled) | 68 → 83 of 17,544 (0.39 → 0.47 %); +0.09 points, interval −0.06 to +0.31. The size passes both margins (+22 %, +15); the interval holds zero, so **not beyond** | 154 → 179 of 10,840 (1.42 → 1.65 %); interval −0.02 to +0.98. Not beyond |
| **P-2 jitter** (7-frame quadratic fit, dense span to Top + 100 ms) | B1 median 0.51 px. **+3.4 %**, interval **+1.9 to +4.9 %**; margin 10 %. Not beyond, worse in direction | −0.8 %, interval −1.7 to +0.5 % |
| Jitter, joints ≥ 0.3 only (reported) | +3.2 % | −0.8 % |
| Truth: P1, P4, P7 against the hand ladders | 49, 33 and 33 swings: median change 0 ms. **Holds** | n/a |

**By stratum and by golfer.** The strata come from B1 alone and were written before any curved arm ran.

| | Face-on drops at the top | Face-on jitter | DTL drops at the top |
|---|---|---|---|
| Dark top (torso luma 4–11; A 10, B 23, C 16) | 68 → 82 | +3.7 % | 64 → 90 (one swing) |
| Middle (12–15) | 0 → 1 | +4.8 % | 0 → 0 |
| Bright (16–189, all A) | 0 → 0 | +0.1 % | 90 → 89 |
| Scene dim / middle / bright | | +6.2 / +3.6 / +0.4 % | |
| Golfer A / B / C | 37→43 / 31→40 / 0→0 | +2.6 / +7.6 / +3.8 % | 154→179 / 0 / 0 |

The darker the scene, the more jitter the curve adds. That fits amplified shadow noise, not recovered detail.

Reported only (P-3):
- face-on: confidence of joints 5–12 at the top +0.002, and dense-span drops 0.48 → 0.44 %;
- hands (never deciding): +0.002;
- down the line: +0.000, 2.48 → 2.49 % and +0.006 respectively.

**The motivating swing** (Wrist_03 shot 1):
- **Face-on:** the lead elbow and wrist drop 30 of 120 joint-frames in **both** arms. Confidence goes 0.38 → 0.43, and jitter 1.6 → 2.2 px.
- **Down the line:** drops 64 → 90, and confidence 0.30 → 0.22.
- This swing's DTL pose is far off in both arms (jitter 54 and 37 px against a median of 0.5). It came from an app-testing session, not a real one, so no issue is filed[^apptest].

The contact sheet is `torso_sheet.png` in the kept run tree (§8).

## 4. Shaft tracker (PC against PB, the same pose)

Both arms are counted over PB's span. The span did not move in PC on any swing.

| Measure | Face-on (148 swings) | DTL (92; Address to Finish) |
|---|---|---|
| **S-1 tracked fraction** (B1 median 87 %; DTL 47 %) | **+0.91 points**, interval +0.45 to +1.41; 100 up, 33 down; margin 1. Not beyond | +0.40 points, interval 0.00 to +1.00. Not beyond |
| **S-2 give-ups** | 2 and 2; net 0 | n/a |
| **S-3 longest predicted run** (PB median 15 frames) | median 0; 50 shorter, 29 longer | median 0 |
| S-4 tiers in the span, PB → PC | pred 7,622 → 7,008; ray 15,952 → 16,381; seg 13,115 → 13,465; band 687 → 765; wedge 1,547 → 1,326; recon 128 → 106. The rung changed on 7 swings | |
| Truth, taped clubs (11 swings) | PC against PB's band lock: band 0.21° / segment 0.11°, within 0.3°: **holds**. 11 swings in 2 sessions, so the interval means nothing | |
| Truth, bare clubs (46 swings, seen P1–P6) | median change 0.00°: **holds** | |

Plan §2.2 expected this: a curve scales signal and noise together, so trackers with relative thresholds barely move.

## 5. Ball tracker

The no-baseline pair decides: PBs/PCs for the 80 swings with a stored baseline, and PB/PC for the 68 without one.

| Measure | No-baseline (147 swings) | With the linear baseline (80, reported) |
|---|---|---|
| B-1 detection, address to impact | median 0.0 points; moved on 39 swings (28 up, 11 down, −75 to +34) | 0.0 |
| B-2 still-ball spread | 0 px on all 135 baseline runs: **it can't detect a change** | 0.0 |
| B-3 launch found | 5 newly found, 1 lost; net +4 passes the flip rule. But against impact, only one gain is within 20 ms (−13 ms). The others are −2,646, −87, −47 and +1,225 ms from impact, and the loss was −49 ms | net 0 |
| Launch time against truth | **not testable**: no launch truth labels | |

The ball is read as **no worse**. The flip-rule "helps" doesn't hold up against impact timing (plan §1.1: confidence is not accuracy). Plan §2.1's predicted loss didn't happen.

## 6. Metrics, against their uncertainty (`metrics.csv`)

| | C1 against B1 | PC against PB |
|---|---|---|
| Key × phase samples compared | 24,500 (93 keys) | 24,109 (94 keys) |
| Moved beyond max(σ, floor) | 4,396 | 1,117 |
| Appeared or disappeared ("-" ↔ value) | 989 | 254 |
| Moved with no σ (uncertainty not characterised; 28 keys have none) | 5,342 | 1,116 |
| Most often flagged | ballReachExcess 65/80, clubAngularSpeed 324/419, shaftAngleVsHorizontal 384/580 | clubAngularSpeed 261/408, shaftAngleVsHorizontal 260/576 |
| Clubhead speed at impact | 88 of 435 flagged; median shift 3.5 mph | 36 of 432 |
| Attack angle (reported only) | 69 of 145 | 54 of 144 |

**Clubhead speed against the launch monitor.** 47 swings (C1/B1) and 49 (PC/PB). 2 mph is taken off GC Quad readings (AD-33); Premier readings are used as read.

| | LoA width | Change (interval) | Bias |
|---|---|---|---|
| C1 against B1 | 24.8 → 46.4 mph | +87 % (−15 to +287 %) | +0.9 → +2.8 mph |
| PC against PB | 24.4 → 23.9 mph | −2 % (−21 to +28 %) | +0.9 → +1.8 mph |

- **The check holds by the rule:** the interval includes zero.
- **One swing causes it:** golfer B's 20 Oct W03 swing reads 6.3 mph curved, against 64.8 in the baseline and 70.8 from the launch monitor. Without it, the width goes from 24.7 to 26.7 mph (+8 %).
- **The change comes through pose:** with pose held, the width doesn't move.

## 7. What this test can't show

1. **A table after decoding is not a curve before encoding.** Shadow codes that x264 merged stay merged. Curving before encoding is untested.
2. **Live analysis reads linear frames in RAM.**
3. **The stored ball baselines are linear.** A baseline learned on curved frames wasn't tested.
4. **One exponent, three golfers, one studio.** Golfer A is two-thirds of the swings.
5. **Thin truth.**
   - Event truth covers only 33 to 49 of golfer A's corpus swings. There is none for golfer B, golfer C or the 25 Nov library's 9.
   - So most dark-top swings have no timing truth.
   - There are no launch labels, and only 11 taped swings in 2 sessions.
6. **Confidence and coverage are not accuracy.**
7. **The tracker floor is one swing's repeat.** PB2 was not run.
8. **Analysing curved frames is a different question from curving the files.** The "player curve, analysis undoes it" route is untested.

## 8. Reproducing every figure

Kept on the share, in `<share>/scratch/i183-ab/`:
- `digests/<arm>/<id>.json`;
- `runs/<arm>/<id>/{done.json,runmeta.json,runner.log}`;
- `manifest.json`, `strata.json`, `torso_sheet.png`, `truth/`, `pins/`, `one/one.json`;
- `one.console.log` and `corpus.console.log`.

```
python3 -I tools/swinglab/tone_ab.py --root <share>/scratch/i183-ab --out <folder>
python3 -I tools/swinglab/tone_ab.py --self-test        # 14 cases hold
```

Every interval and per-golfer and per-stratum figure is in `verdict.json`. The rows are in `pose.csv`, `shaft.csv`, `ball.csv`, `metrics.csv`, `lm.csv` and `strata.csv`.

The run itself. Nothing private is a default in the scripts: the share, the machine and the studio PC's paths come from the environment (the header of `tone_ab.sh` lists them), and the library sessions and the one-swing step's swing are arguments.

```
export TONE_AB_HOST=<user@studio-pc> TONE_AB_SHARE=<share> TONE_AB_WSHARE=<the share on the studio PC> \
       TONE_AB_WREPO=<the studio PC's checkout> TONE_AB_WPY=<the swinglab venv's python.exe> TONE_AB_WQT=<Qt's bin folder>
python3 -I tools/swinglab/tone_ab_inputs.py --library <25 Nov session> ...     # the day's six sessions, 9 swings
tools/swinglab/tone_ab.sh lock && tools/swinglab/tone_ab.sh preflight && tools/swinglab/tone_ab.sh tools
TONE_AB_ONE_ID=<the 25 Nov Wrist_03 swing_0001 id> tools/swinglab/tone_ab.sh launch one && tools/swinglab/tone_ab.sh wait one
tools/swinglab/tone_ab.sh launch corpus && tools/swinglab/tone_ab.sh wait corpus
python3 -I tools/swinglab/tone_ab_inputs.py --verify-sources            # the corpus and library files unchanged
tools/swinglab/tone_ab.sh unlock
```

**Correction after first reading.**
- The shaft's S-1 and S-3 were first counted over the whole window, not the span. The first figures were +0.40 points, interval +0.17 to +0.60.
- They were corrected to the plan's span definition, with no margin moved. The run log has both sets.

**Clean-up.**
- Deleted: the input copies, and every `result.json`, `trace*.jsonl`, `pose_dtl.json` and `club_dtl.json`. 17.5 GiB freed.
- Kept: 482 MiB.
- The studio PC was left clean on main, with the lock released.

[^approvals]: Product Owner and Solution Architect, 25 Nov 2026, `build/platform/JUDGEMENT-CALLS.md`.
[^accepted]: Product Owner, 26 Nov 2026 (morning batch 3, `build/platform/JUDGEMENT-CALLS.md`): the verdict accepted, so the curve stays on the display only and the analysis keeps reading linear video; the report and scripts committed; the down-the-line pose defect ignored, as an app-testing session.
[^apptest]: Product Owner, 26 Nov 2026 (morning batch 3, `build/platform/JUDGEMENT-CALLS.md`): the 25 Nov library session was app testing, not a real practice session, so Wrist_03 shot 1's down-the-line pose is ignored and no issue is filed.
