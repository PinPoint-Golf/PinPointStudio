# Down-the-line posture, the swing plane, and where the camera is pointing

*2026-09-21. Status: **built and graded** on 24 two-camera swings of one golfer; not
committed when this was written. Follows `shaft_fusion_design.md` (same day).*

## 0. Summary

A down-the-line camera resolves the **sagittal** plane: image x is toward-and-away from the
ball, image y is up. That is exactly the set `lower_body_metrics.h` refuses to produce from
face-on. With the DTL pose persisted (`analysis.poseDtl`), the face-on P ladder on the same
clock, the DTL ball and the fused shaft plane, seven catalogue keys that were planned are
now produced:

| key | what | 07-04 (7-iron, 15) | 06-11 (wedge, 9) |
|---|---|---|---|
| `pelvisThrust` | hip-MIDPOINT travel toward the ball from address, cm | P4 4.0 ± 1.1, **P7 8.6 ± 1.7** | refused — no ruler (§2) |
| `spineForwardBend` | hip→shoulder line against vertical, ° | P1 33.7 ± 1.2, P4 27.3, **P7 24.4 ± 1.1** | P1 36.2, P4 30.7, P7 28.1 ± 1.2 |
| `trailKneeFlexion` | 180° − (hip, knee, ankle), ° | 17.3 → **1.4 at the top** → 31.2 | 15.5 → 1.2 → 29.8 |
| `leadKneeFlexion` | same, lead side, ° | 19.8 → 36.5 → 18.3 → P8 6.2 | 19.1 → 39.9 → 19.8 → 4.7 |
| `ballBodyDistance` | ball to the toe line, % shoulder width | 182 ± 5 | 159 ± 4 |
| `balanceHeelToe` | mass-weighted point over heel→toe, % foot — a PROXY | 53 ± 6 | 64 ± 4 |
| `swingPlane` | downswing shaft plane − address shaft plane, ° | **+6.0 ± 1.2** | +3.2 ± 1.8 |

Two sessions a month apart, different clubs, tell the same golfer's story: ~9° of forward
bend lost between address and impact, the pelvis 8–9 cm nearer the ball at impact, a trail
knee that straightens completely at the top, the shaft delivered a few degrees above the
plane it was set up on. That agreement is the strongest evidence available, because
**there is no truth for any of these numbers**.

Parity against a `dtlPosture.enabled=false` control: nothing but the six posture series
differs, 24/24. Single-camera swings cannot reach the stage.

## 1. How each is read

- **Which way is the ball** comes from the golfer's own feet (toes are ball-side of heels),
  never from handedness or "image-right". The DTL ball must agree; if it does not,
  everything is refused — a posture read with the sign backwards is early extension reported
  as its opposite.
- **Thrust is the hip MIDPOINT.** A hip turn carries one hip toward the ball and the other
  away by the same amount, so the midpoint holds still under rotation (tested: 25 px of
  depth swap, 0.0 cm of thrust).
- **Address reference**: median over ±150 ms about the ladder's Address, ≥ 5 confident frames.
- **Values from the smoothed pose, confidence from the raw one.** Thrust and bend are masked
  past impact (the golfer stands up and walks through; that is not early extension). The
  knees are read into the follow-through.
- **`swingPlane`** is one fitted plane, so the series is one number held over top → impact.
  The address plane is read from the DTL view ALONE — the plane through that camera's view
  ray and the address shaft — because face-on coasts at address and would leave a dozen
  frames where the DTL tracker has ninety. The P2→P4 backswing reading of the same key finds
  no sample and stays absent: the backswing is not one plane and DTL is end-on at both ends.

## 2. The ruler, and the one that was refused

Centimetres need a ruler at the golfer's distance. The DTL ball is one (42.67 mm, measured
radius) **when it was found by its bright cue**; on 07-04 it reads 0.27 cm/px against
0.29 cm/px from the club's known length — two independent rulers within 7 %.

The fallback — face-on's ball ruler carried across by the ratio of the body's vertical
extent in the two views — **failed on 06-11**: it implied 52–55 cm of shoulders and 31 cm
of foot on a golfer the DTL ball measures at 35 cm and 24 cm. So a ruler now has to measure
the golfer too: implied shoulder width outside 30–50 cm drops the ruler, `pelvisThrust` goes
with it, and the angles are untouched. The unit never switches at runtime.

## 3. What went live in the diagnostics, and what was held back

Catalogue: the seven keys' `dtl` rungs lost `PLANNED` (planned descriptors 20 → 13).

`core.json`: **three** measures went live — `m_pelvisThrustDown`, `m_spineBendDive`,
`m_shaftPlaneDelivery`. **Seven were held at `planned`** although their series now exist,
because the pack's own integrity test refuses them: `m_balanceHeelToeAddress` has no norm,
and `posture_too_upright`, `ball_too_close`, `ball_too_far`, `excessive_knee_flex`,
`insufficient_knee_flex`, `trail_knee_straighten` and `pelvis_thrust_backswing` could fire
with no authored cause. That is content, not code: author the causes and the norm, flip the
status. `m_leadKneeFlex` is held deliberately as well — the lead leg is behind the trail leg
from this camera.

With the heuristic norms as they stand, this golfer's 8.6 cm of thrust is ~2σ
(`early_extension`) and +6° of delivery plane ~1.5σ.

## 3a. The hand-path loop — over the top from the hand trace (2026-09-23)

The down-the-line hand trace (the `traceHands` view preset) shows over the top the way a coach
reads it: the lead wrist goes up one path, shifts toward the ball at the top, and comes down
**outside** the path it went up on. Nothing measured that, and `over_the_top` was signalled
from the face-on `transitionPlaneDelta` alone — whose corridor is a deliberately unreachable
placeholder, so the condition could never fire.

**`handPathLoop`** (% of the hand rise, + = outside / toward the ball). The lead wrist's
backswing path (Address → the top of the hands) and downswing path (top → Impact) are each
crossed at 40, 50, 60 and 70 % of the rise; at each height the backswing's last crossing is
subtracted from the downswing's first, signed toward the ball by the feet (§1), and the mean
is scaled by the rise. Both paths are the same wrist in the same image, so no ruler and no
calibration: the rise cancels the camera's distance. Above 70 % every swing loops over at the
top; below 40 % every swing converges on the ball. Emitted at Top. Needs ≥ 3 of the 4 heights.

**On the 34 corpus DTL swings** (`docs/research/data/dtl_posture/hand_path_loop_20260923.csv`):
positive on all 33 that resolve — 06-11 +4 to +24, 07-03 +6 to +32, 07-04 +18 to +32 (±4).
The same swings' `transitionPlaneDelta` runs −20° to +9° and changes sign between swings of a
golfer who comes over the top on every one of them; the launch monitor on the swing that
prompted this read the club path 3.7° out-to-in. The C++ matches the Python prototype to 0.1 on
every swing. 07-03 s2 resolves in neither (lead wrist lost near the top).

**Diagnostics.** `sig_overTheTop` now reads `m_handPathLoop` (a CEILING — dropping inside is
shallowing, which good players do on purpose); `over_the_top` moved to its own axis
`hand_path`. The norm is a coaching fault line, not a fitted spread: Action from +10 % of the
rise (σ = 10/3), about 10 cm outside on a one-metre rise; the detector surfaces from ~6.7 %.
`m_transitionPlaneDelta` keeps the shallowing tail only; its steepening tail is declared
unwatched. A face-on-only capture no longer assesses `over_the_top` at all.

**Not claimed.** One golfer, and a camera behind the ball rather than on the hands line and
turned 4–9° toward the golfer (§4): some target-line movement leaks into image x. The ratio
is invariant to distance, not to yaw.

## 4. Where the DTL camera is pointing (the alignment stick)

The fused plane's HEADING — swing direction, and so club path — moves one for one with the
DTL camera's yaw. `tools/shaftlab/dtl_yaw_probe.py` finds the alignment stick on the mat in
each swing's address frames and extends it to the horizon:

- 07-04 s4–15: the stick images 1.3–2.0° from vertical, so its vanishing column is
  **x = 365–379 of 512 whatever the horizon row**, 12/12 swings. The camera is turned
  **toward the golfer** (positive yaw), which is what "behind the ball, aimed at the hands"
  predicts.
- The angle needs the focal length, which this footage does not record. From the scene's
  own scale, f = D / 0.27 cm·px⁻¹: **yaw 8.7° at D = 2 m, 7.0° at 2.5 m, 5.8° at 3 m, 4.4°
  at 4 m.** The pair route independently put the two cameras 6–15° off square.
- Applied to the fusion, yaw 7° moves the downswing plane's heading from −89.7° to −82.7°
  and its inclination by 0.05°.
- **The camera was moved between swing 3 and swing 4.** On s1–2 the stick images 12–13°
  off vertical and the golfer is framed differently. Those are the three swings whose DTL
  backswing band reads mirrored and whose downswing plane reads 63–65° instead of 60°.

Not claimed: the principal point is assumed at the frame centre (an ROI offset moves it
pixel for pixel), the lens is visibly wide, and one stick is one line. **`clubPath` stays
planned.** What would close it is in §6.

## 5. Found on the way

- **`deprojectGain` had its sine and cosine terms swapped** (`segment_rates.cpp`): inside
  [k, 1/k] at every angle, which is all its test asked, and wrong at all but one of them.
  It only ever scaled σ. Fixed, with a test against the numerical derivative; on 24 swings
  no node's placement, peak time or verdict moved — only the σ values.
- **3-D clubhead speed was NOT built.** Through the fused plane the in-plane rate at impact
  is 0.89 × the face-on image rate (0.87–0.94), so the honest correction LOWERS the
  rotational component by ~11 % — on a speed that already reads 0.959 of the launch monitor.
  Either the face-on club length is short by about as much, or the composed formula is
  compensating somewhere; both are guesses, and no swing in the library has BOTH a launch
  monitor and a DTL camera to settle it. (Mark, same day: the GC Quad is known to OVER-read
  clubhead speed by about 2 mph at 70–100 mph. Against the true speed the face-on reading is
  therefore nearer 0.98 than 0.959 — which makes the puzzle sharper, not softer: the
  uncorrected number is almost right, and the geometrically honest correction would take it
  to ~0.87.)

## 6. Owed

1. **One session with both cameras AND the launch monitor.** It grades club path, 3-D
   clubhead speed and the plane in one go.
2. Capture protocol (step 3): camera-to-golfer distance written down; two sticks (target
   line through the ball, toe line) so the vanishing point is an intersection and not an
   extension; the camera not moved mid-session; the ROI offset recorded.
3. Content: the seven causes and one norm of §3.
4. The mirrored DTL band (s1–3) — now known to coincide with a different camera placement.
5. `ballBodyDistance` reads 159–182 % against a heuristic norm of 130 ± 20 %: the toe
   keypoint is the big-toe joint, not the shoe tip, and the norm was never measured.
