# The down-the-line track on the 5 Oct cabin session: the arm at the top, the leg after impact, and a length that did not shorten

**Status:** results, 5 Oct 2026. Three `shaft.dtl.*` rules, each a switch with a bit-identical OFF,
on by default; `kDtlShaftStageVersion` 4 → 5. Not committed at the time of writing — Mark reviews first.

Every number is reproducible from `/mnt/swingdata/scratch/dtlfix-1005/` with
`build/run-me/dtl-fragility-grade.sh`. Frame sheets looked at by eye: `build/run-me/dtl-track-1005/`.

## 0. Summary

| | |
|---|---|
| Asked | Mark, on `2026-10-05_Mark-Liversedge_Wrist_01` (31 swings, gap wedge, DTL 576×988): "the DTL camera truncates the shaft at the top … the track veers off wildly on most swings at or near P4 and between P7 and P8" |
| Found | (1) the club is not seen from just after P3 to about P5 on any swing; (2) at the top the tracker publishes the LEAD ARM on 5 of 28 swings; (3) after impact the direction is right for ~40 ms and the drawn LENGTH is 1.6× the club's, then on 6 swings the line jumps to the trail leg; (4) P8 is 355–471 ms after impact on 8 of 29 swings |
| Built | `armChain`, `postImpactContinuity`, `postImpactRunLength` in `dtl_shaft_post.cpp`; tests C5–C7 in `dtl_shaft_post_test` |
| Gate, 21 DTL corpus swings, studio Release, pinned poses | OFF byte-identical 21/21; held-out truth 425 pairs, p50 0.25° / p90 0.65° unchanged, 0 frames worse; 43 frames no longer published, 34 of them the arm on four 07-04 swings (looked at); fused planes move ≤ 0.25°; face-on identical; no metric count change |
| 5 Oct session, 28 tracked swings | arm frames 41 → 0; leg frames 28 → 0; post-impact drawn length 268 → 168 px (median of swing medians); 2 frames changed outside the two windows; no metric count change |
| NOT fixed | (1) is the picture, not the tracker; (4) is the face-on track after impact; the 3-D club's kinks barely move (§5) |

## 1. What the frames show

- **Top.** Hands at P4 are 44–113 px below the top edge of the 988 px frame. With the gamma lifted
  there is no shaft in the picture around the hands at P4 ± 60 ms on s9, s13 or s25: it is end-on
  and unlit, not merely cut by the edge. P4 → P5 the tracker is unseen on 81–96 % of frames on
  every swing. 25 of 28 swings say UNSEEN / END_ON at the top, which is right.
- **The arm.** On the others the published line runs from the hands down the straight lead arm to
  the shoulder, θ 20–48°, at the club's full scheduled length, every frame a corridor escape. The
  same thing is in the corpus and always was: the "P4.0→P4.3" bands on 07-04 s8, s12, s13, s14
  (34 frames, θ 37–47°, all escapes) lie on the bare forearm.
- **After impact.** From down the line the club swings away toward the target: it holds its image
  direction (56–74° over 150 published frames, about 1° a frame) and shortens. The tracker's
  direction is on the shaft to about +40 ms. Its length is the schedule's — ρ̂_D from the ρ_F := 1
  bound on a face-on θ_F that is coasting — and stays near full: 317 px drawn against a 150 px run
  at +24 ms on s13; drawn / run 1.6 over 168 frames.
- **The leg.** From about +50 ms on 6 swings (4, 13, 14, 16, 19, 24) θ steps to 78–89° (210° on
  s24, after a 65 ms hole): the trouser edge, with the grip anchor on the golfer's back.
  `quarantine.postImpactUs` 80 → 30 ms removes two of s13's seven such frames; the row residual
  is under 40 px on the rest.

## 2. The rules

- **`armChain`** (`armChainLatPx` 100). A solved frame is refused when it is a corridor escape AND
  the lead elbow and then the lead shoulder lie ahead along the published ray, each within 100 px
  of it. Refused frames are never held. Both witnesses are needed: 104 escapes between P2 and P4
  on 5 Oct are mostly the true shaft (θ 220–250°, where the face-on corridor is weak), and at P5
  the true shaft runs within 100 px of the lead arm without being an escape.
  Two versions failed first. As a D2 cost on either arm (60 px) the solve stepped to the arm's
  other edge, 61–64 px off the shoulder, and published that. Widened to 90 px it moved 16–21 true
  P2–P3 frames a swing by 25–40° — the true shaft passes the TRAIL elbow and shoulder there.
- **`postImpactContinuity`** (`postImpactStepDeg` 10, `postImpactWindowUs` 150 000). Inside the
  window the first measured frame whose θ steps more than 10° per 20 ms of gap from the last
  accepted one ends the track: it and everything after it in the window is unpublished and unheld.
- **`postImpactRunLength`**. Inside the same window the drawn length is the median of the frame's
  measured run and its measured neighbours', where that is shorter than the schedule.

## 3. The corpus gate

`g_off` (this build, the three rules off) against `g_on3` (on); `g_off` against a second build's
`g_off2` with `--identical`: 21/21 OK.

| family | measured off → on |
|---|---|
| address | 2656 → 2656 |
| mid-backswing | 621 → 620 |
| delivery | 308 → 273 |
| impact | 270 → 267 |
| after | 47 → 43 |

Truth: paired 425 → 425, p50 0.25 → 0.25°, p90 0.65 → 0.65°, frames worse by > 0.5°: 0. The
grader files the "P4.0→P4.3" bands under delivery; 34 of its 35 lost frames are the arm (§1). The
held-out truth has no pair on any removed frame, so the gate says nothing was made worse, not that
every removed frame was wrong; the 34 were judged by eye, the other 9 were not looked at.

## 4. The 5 Oct session

`t_ctl` against `t_on3`, write-back on scratch copies, the stored poses reused (two runs with the
same settings are frame-identical; an EMPTY `--params` takes a different path and is not a control).

| | off | on |
|---|---|---|
| swings publishing the arm at the top | 5 | 0 |
| arm frames | 41 | 0 |
| swings publishing the leg after impact | 6 | 0 |
| leg frames | 28 | 0 |
| post-impact published frames | 192 | 157 |
| post-impact drawn length, px | 268 | 168 |
| frames changed outside the two windows | | 2 |
| swings with a metric count change | | 0 |

## 5. What this does not fix

- The club is not in the picture from P3 to P5. The camera stays where it is; what is owed is a
  tier that says "not in view" so the 3-D solve treats the stretch as blind.
- The 3-D club (skeleton3d) hardly changes: worst consecutive-frame kink after impact, median
  0.80 → 0.78 °/ms (max 2.55 → 1.87); at the top 0.31 → 0.29. Its kinks come from elsewhere.
- P8. On s13 the face-on track after impact reads θ 136, 70, 109, 132, 117, 73, then 64° for 80 ms
  and crawls to 0° at +359 ms; on s25 it reads 102, 67, 46, 23, −4 and P8 is at +71 ms. This is the
  face-on capture after impact already on file, on 8 of 29 swings here, and it also feeds the
  schedule and the corridor this tracker reads.
- The picture is spot-lit with dark upper corners and a projected range image behind the golfer.
  Not measured as a cause.

## 6. Reproduce

```
build/run-me/dtl-fragility-grade.sh
ctest --test-dir build/tests -R "dtl_shaft_post_test|dtl_shaft_decide_test|dtl_shaft_bands_test|shaft_fusion_test|camera_pose_sticks_test|dtl_shaft_synth3d_test|swing_doc_test"
```
