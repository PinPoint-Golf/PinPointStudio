# Posture — the metrics, loss of posture, and poor posture at address

*2026-10-21. Status: **design agreed by Mark (2026-10-21, all four §11 decisions as recommended). Built 2026-10-22 (stages 1–5, measurement V1–V5 on re-analysed copies; the live libraries re-analysed in place on the cabin PC 22 Oct, 217 swings). What the measurement changed — trunk rise held (V1 failed), the hip-joint pelvis lift out of the top composite, ρ, axis tilt at address — is in `docs/research/data/posture/posture_composites_2026-10-22.md`.** Follows
`dtl_posture_design.md` (the down-the-line producer, 21 Sept), the posture-cluster rebuild of
6 Sept (`swing_fault_ontology.md` §11) and the early-extension work of 8 Oct.*

## 0. Summary

Mark asked for posture metrics from both the face-on and the down-the-line pose, a **loss of
posture** characteristic that fires as the posture angles change, and a **poor posture**
characteristic that fires when the setup is poor.

Most of the single readings already exist. The down-the-line stage produces forward bend, pelvis
thrust, both knee angles, ball distance and a heel-to-toe balance proxy. The face-on stages produce
head lift, pelvis lift and axis tilt. The pack already grades eleven separate posture conditions,
from `posture_too_upright` at address to `early_extension` at impact. What it cannot say is the
thing a coach says first: **"you lost your posture"**, or **"your posture at address is off"**.
Both are statements about the posture *as a whole*, and the pack has no way to judge several
readings together. Every signal tests one measure against one corridor.

The design adds four things:

1. **Six new readings.** On face-on: `trunkRise`, the hip-to-shoulder line getting longer in the
   face-on image, which is the trunk standing up. That makes forward-bend *change* readable from
   the face-on camera alone (§3.2). On down-the-line: `armHang` and `headPitch`. On the pack side
   only, new measures read existing series at new instants (§3.4).
2. **A new kind of measure in the model: the composite.** A composite combines several graded
   measures into one z-score, using authored weights in 0…1 and a correlation measured from the
   library. It comes in two modes:
   - **coherent**: the parts moving together in one direction, which is loss of posture;
   - **distance**: the parts away from their ideal in any direction, which is poor posture.

   Both modes are calibrated so that a clean swing trips them as often as one reading trips its
   own fault line: 2.3 % under the Standard policy (§6.1, Appendix A). After that the composite is
   an ordinary measure. Corridors, two tails on one axis, P(fire), trends, the ledger and the
   Readings tab all work on it unchanged.
3. **Five new characteristics:**
   - `loss_of_posture` and `posture_sinks_impact`: the two tails of the posture held into impact;
   - `loss_of_posture_backswing` and `posture_sinks_backswing`: the same thing at the top
     (**changed 22 Oct 2026**: standing up going back now fires on the down-the-line head rise
     alone, and the trunk-bend change at the top is held; §4.5);
   - `poor_setup_posture`: one tail, how far the address shape is from a good one.

   Each is joined to the existing single-reading conditions by `causes` edges, so the explanation
   reads the way a coach talks: *"loss of posture, mostly early extension"* (§7).
4. **One small rule for the session layer.** An edge whose two ends are measured from the same
   readings is marked `sharedEvidence`, and its link grade caps at *Coherent*. Two numbers taken
   from one reading always move together, and that co-movement must not be scored as evidence
   that one causes the other (§6.5).

**The composite's number is never shown.** Session diagnostics principle 5 keeps composite scores
inside the engine. The golfer sees the finding and what changed, part by part, each in its own
unit: "forward bend −9°, hips 8 cm closer to the ball, head 3 cm higher" (§8).

## 1. Deliverables and definition of done

- **Document:** this file, plus a measurement write-up
  `docs/research/data/posture/posture_composites_<date>.md`. Every number in it must come from a
  named run root and a command written in the file.
- **Implementation:** listed file by file in §10. The work is done when all of these hold:
  - the composite engine passes its null-calibration and coverage tests;
  - the producers are on the library;
  - the content passes `core_pack_test` and `diagnostics_health`;
  - the detail page renders in Analyse, Replay and Print;
  - the library has been re-analysed on the cabin PC, with a firing table written for every new
    condition.

  A validation gate that fails and is written up honestly counts as done (§9). A gate that is
  skipped does not.

## 2. What posture is, and the boundary this design draws

**Posture is the shape of the body. Placement is where that body stands relative to the ball and
the target.** Posture is the trunk's hinge over the ball, the knee flex, how the arms hang, where
the weight sits over the feet, the curve of the back, the head, and the trunk's side tilt.
Placement is the ball's distance and position, stance width, and aim. The two interact: crowding
the ball produces a bent-over posture. That interaction is modelled as **causes edges from
placement into posture**, not by folding placement into the posture reading. A golfer told "your
posture is poor" must be able to fix it without moving their feet.

**Loss of posture is the shape changing during the swing in a way that changes the golfer's
height and distance to the ball.** Standing up straightens the hips and knees, raises the head and
pelvis, and moves the pelvis toward the ball. Sinking does the opposite. Side tilt is **not**
included. Axis tilt growing from about 6° at address to about 20° at impact is the swing working
correctly, and it already has five conditions of its own.

### 2.1 What exists today

| Reading | Camera | Pack measure (status) | Condition(s) |
|---|---|---|---|
| Forward bend at address | DTL | `m_spineBendAtAddress` (live) | `posture_too_upright` / `posture_too_bent` |
| Forward bend lost, P5–P7 | DTL | `m_spineBendLossDown` (live, floor) | `early_extension` (with thrust) |
| Forward bend gained, P5–P6 | DTL | `m_spineBendDive` (live) | `diving` |
| Pelvis toward the ball | DTL | `m_pelvisThrustDown`, `m_pelvisThrustBack` (live) | `early_extension`, `pelvis_thrust_backswing`, `backing_off_the_ball` |
| Knee flex at address | DTL | `m_kneeFlexAddress` (**held**; reads the occluded **lead** knee) | `excessive_knee_flex` / `insufficient_knee_flex` (draft) |
| Trail knee P1→P4 | DTL | `m_trailKneeFlex` (live, floor) | `trail_knee_straighten` (draft) |
| Heel-to-toe balance | DTL (proxy) | `m_balanceHeelToeAddress` (**held**: no norm) | `weight_in_heels_address` (draft) |
| Ball to toe line | DTL | `m_ballBodyGap` (live) | `ball_too_close` / `ball_too_far` |
| Upper/lower back curve | DTL contour | `m_thoracicCurve`, `m_lumbarCurve` (no producer) | `c_posture`, `s_posture`, `flat_*_spine` |
| Head height | face-on | `m_headLiftBack`, `m_headLiftDown` (live) | `head_rise_backswing`, `head_drop_backswing`, `coming_out_of_it` |
| Pelvis height | face-on | `m_pelvisLiftTop` (live) | `pelvis_rise_backswing`, `pelvis_sink_backswing` |
| Axis tilt | face-on | `m_axisTiltAtTop`, `m_axisTiltImpact` (live) | `reverse_spine_p4/p7`, `excessive_axis_tilt_*`, `coming_out_of_it` |

The gaps are:

- nothing reads forward-bend change from face-on;
- nothing reads arm hang or head position at address;
- nothing reads axis tilt at address;
- two address measures are held for reasons that are partly stale (§7.4);
- there is no characteristic for the posture as a whole.

`coming_out_of_it` currently carries the alias "loss of posture". That alias is a leftover of the
September rename, when the old `loss_of_posture` was a single forward-bend reading that could not
fire. §7.2 moves the alias to the new family condition.

## 3. The posture readings

### 3.1 Down-the-line (sagittal), `src/Body/dtl_posture.h`

These are unchanged: `spineForwardBend`, `pelvisThrust`, `trailKneeFlexion`, `leadKneeFlexion`,
`ballBodyDistance` and `balanceHeelToe`. All the rules of `dtl_posture_design.md` §1 still apply:
ball side from the feet, the hip midpoint, the address median over ±150 ms, and no ruler means no
centimetres.

Two new channels. Both are **angles, so they need no ruler**:

- **`armHang`** (°). The line from the shoulder midpoint to the wrist midpoint at address, measured
  against vertical. Positive means the hands sit further toward the ball than the shoulders
  (reaching); negative means the hands are tucked in under the body. This is the coaching check
  "let the arms hang". It is read at Address only. In the DTL image the two shoulders and the two
  wrists sit nearly on top of each other at address, so each midpoint is stable even when one point
  of the pair is occluded. The frame is refused if neither wrist is confident.
- **`headPitch`** (°). The ear-to-nose line against the trunk line (hip midpoint to shoulder
  midpoint), measured at Address. Positive means the chin is down toward the chest. It uses body
  keypoints 0 and 3/4, **not** the 68 face landmarks; the hand landmarks have already shown that
  WholeBody's fine keypoints can score high and still be wrong
  ([[pose-is-133-wholebody-hands-unreliable]]). The 2026 rigs put the DTL camera behind the ball,
  so they see the golfer's front-trail quarter and the face is partly visible. This reading ships
  **held** until it passes the repeatability gate (§9 V5).

A third, added 22 Oct 2026 (§4.5). It **needs the ruler**, like `pelvisThrust`:

- **`headRise`** (cm). The ear midpoint's travel upward from its address height (the address median),
  + up. Both ears and nothing else: their midpoint sits near the axis the skull turns about, so it
  holds its height while the head rotates. The nose, the eyes or the near ear alone climb 5–10 cm at
  the top on a head that has not risen. On Nick Henn's 27 two-camera swings the ear midpoint reads
  +1.9 ± 1.6 cm at P4, against −0.8 cm face-on and +1.0 cm from the 3-D fit; the near ear alone
  reads +6.3 and the nose +4.9.

One change of input:

- **`m_kneeFlexAddress` reads `trailKneeFlexion`.** From this camera the lead leg is behind the
  trail leg, which is why the measure was held. At address the two knees are within a few degrees
  of each other (Mark 17° trail against 20° lead, 07-04), so reading the leg the camera can see
  costs almost nothing and makes the measure live.

### 3.2 Face-on (frontal), `src/Body/upper_body_metrics.cpp`: `trunkRise`

A face-on camera cannot measure the forward-bend *angle*, and `upper_body_metrics.h` is right to
refuse it. It can measure something that changes *because of* the angle. The face-on image
shortens the hip-to-shoulder line by the cosine of its lean toward the camera:

    ℓ(t) = L · cos β(t)        L = trunk chord (hip midpoint → shoulder midpoint), β = lean toward the ball

So `trunkRise(t) = ℓ(t) / ℓ(Address) − 1` in **% of address length**. Positive means the trunk
images longer, which means it is more upright.

- **Size of the effect.** Mark's 07-04 bend goes from 33.7° to 24.4° by impact. The trunk then
  images 10 % longer. That is about +1.2 % per degree of bend lost near 34°.
- **Side tilt cancels out.** Side tilt moves the line within the image plane, and only the line's
  depth component shortens it. Trunk rotation barely matters either. The midpoints sit close to
  the spine's own axis; with the shoulder midpoint a few cm in front of the spine and the
  shoulders 35° open at impact, the error is about 1 cm on a 50 cm chord.
- **What contaminates it, stated rather than hidden.** A pelvis thrust toward the ball is also a
  move toward the face-on camera. That enlarges everything by about 0.33 % per cm at 3 m, so 8 cm
  of thrust adds about 2.7 %. The bias points the same way as the loss it sits on, and thrust is
  itself part of losing posture. So on a face-on-only shot this reading partly includes the thrust
  it cannot see. On a two-camera shot it is never used, because DTL bend outranks it (§4.2).
- **Why it is a percentage and not degrees.** Turning the ratio into degrees needs the address
  bend, which face-on cannot see. Assuming 40° instead of 34° turns a true 9.8° loss into 7.5°.
  The unit stays honest.
- **Domain and gates.** It is read P1 to P7 only, and refused on any frame where the face-on
  shoulder span is below 40 % of its address span. That is the existing upper-body gate: at the top
  the two shoulders overlap and their midpoint becomes guesswork. So at P4 this reading is refused
  on most swings, and the backswing composite needs the DTL camera (§4.5).
- **Status.** Live only if it passes validation V1 against DTL bend on the two-camera swings (§9).
  If it fails, it ships held and the reason is recorded.

### 3.3 The instrument ladder

| Quantity | Best | Then | Then |
|---|---|---|---|
| Trunk inclination change | trunk IMUs (`thoraxForwardBend`, planned) | DTL `spineForwardBend` | face-on `trunkRise` |
| Pelvis depth | DTL `pelvisThrust` | — | — |
| Head height | face-on `headLift` (inter-ear ruler) | — | — |
| Pelvis height | face-on `pelvisLift` | — | — |

skeleton3d is a **cross-check, not a rung**. On 8 Oct it read 15.7° of loss where DTL read 11.2°,
and there is no truth to say which of the two is right ([[arm-club-vision-needs-truth]]).

### 3.4 Pack-only readings of existing series

The composites read every part **at the same instant** (§4.4), so they need delta-at-instant
measures that the pack lacks today:

- `m_spineBendChangeImpact`: forward bend at P7 minus P1, °
- `m_pelvisThrustImpact`: pelvis thrust at P7, cm
- `m_headLiftImpact`: head lift at P7, cm
- `m_pelvisLiftImpact`: pelvis lift at P7 minus P1, % stance
- `m_trunkRiseImpact`: face-on trunk rise at P7, %
- `m_spineBendChangeTop`: forward bend at P4 minus P1, °
- `m_axisTiltAddress`: axis tilt at P1, °
- `m_armHangAddress`, `m_headPitchAddress`: the new address angles, °

All are `Target` shaped, so a reading on the good side lowers a coherent composite instead of
being clipped to zero.

## 4. Loss of posture

### 4.1 Why a family characteristic now

In September, `loss_of_posture` was retired because *"the old name described a whole family while
the detector behind it was one narrow reading"*. That diagnosis was correct. The repair this time
goes the other way: **the family name is kept, and the detector becomes the whole family**. The
specific faults (`early_extension`, `coming_out_of_it`, `diving`, `trail_knee_straighten`) stay as
they are. The family adds two things they cannot:

1. **It sums.** A golfer who loses 4° of bend, moves 4 cm toward the ball and rises 3 cm has lost
   their posture, even though no single reading crosses its fault line.
2. **It gives the hub a node.** It sits between the setup and capacity causes and the strike
   outcomes. That gives the explanation a sentence to say, "*loss of posture, mostly early
   extension*", and gives the work-on list a target for the golfer whose losses are spread thinly.

### 4.2 The parts, at impact

| Part | Measure (alternatives, best first) | Loss sign | Weight | Why this weight |
|---|---|---|---|---|
| Trunk inclination | `m_spineBendChangeImpact` → `m_trunkRiseImpact` | − / + | 0.9 | The posture angle itself |
| Pelvis depth | `m_pelvisThrustImpact` | + | 0.8 | The hips' half of standing up; DTL only |
| Head height | `m_headLiftImpact` | + | 0.6 | The swing centre's height, which is what the strike feels |
| Pelvis height | `m_pelvisLiftImpact` | + | 0.4 | Part of standing up, but a push-off raises it in good swings too |

**Alternatives are a ladder within one part.** The first measure that the swing can read stands
for that part, and the rest are ignored. DTL bend and face-on trunk rise measure one quantity, so
both together would count it twice. This is `DetectionMode::First` at the level of a part. It is
needed because a measure's `preferKeys` ladder must keep one unit, and these two do not share one.

**Weights stay strictly inside 0 and 1** ([[weights-normalised-0-to-1]]). The coherent statistic
does not change when all weights are scaled together, so the 0…1 range costs nothing. It also lets
the weights compose with the rest of the model.

### 4.3 Combining them: the coherent mode

Each part *i* reads a value *xᵢ*. Its own norm turns that into *zᵢ* = (*xᵢ* − μᵢ)/σᵢ, using the
σ of whichever side of μ the reading fell on. The loss sign is *sᵢ*. Then:

    Z = Σ wᵢ sᵢ zᵢ  /  √( Σᵢ Σⱼ wᵢ wⱼ ρᵢⱼ )

This is Stouffer's weighted combination, corrected for correlation (ρᵢᵢ = 1). If the parts were
read at their norms with correlation ρ, *Z* is exactly standard normal. So the composite's norm is
μ 0, σ 1, and it fires where every measure fires, past 2σ. In 200,000 simulated clean swings it
tripped 2.24 %, 2.23 % and 2.29 % of the time at ρ = 0, 0.3 and 0.6 (Appendix A).

**The correlation decides what the family means, and so it is measured, not authored.** These
worked readings are invented to show the arithmetic; the weights are those of §4.2:

| Readings (z: bend, thrust, head, pelvis) | ρ = 0 | ρ = 0.3 | ρ = 0.6 |
|---|---:|---:|---:|
| All four at 1.5, none firing alone | **2.89** | **2.14** | 1.78 |
| Bend alone at 3, so `early_extension` fires | 1.92 | 1.43 | 1.19 |
| 3, 3, 2, 1: a whole-body stand-up | **4.77** | **3.55** | **2.95** |
| Bend lost 2, pelvis sank 2: a compensation | 0.71 | 0.53 | 0.44 |

How to read the table:

- **If the parts are nearly independent,** spread-out small losses add up to a finding. That is
  the case the family exists for.
- **If they are strongly correlated,** they are one movement read four times. The family then
  fires only when that one movement is past its line, and four moderate readings are correctly
  *not* a fault.
- **One part alone does not make the family fire.** A golfer whose hips go 3σ toward the ball with
  the spine and head held has `early_extension`, which fires on its own. That is not a whole-body
  posture loss, and the family stays clean.
- **Compensations cancel.** Bend lost and knees sunk leave the golfer's height roughly where it
  was. The single readings still report each half.

ρ is the mean correlation between the parts' z-scores. It is measured per composite on the library
(§9 V2), floored at 0, and stored on the composite's norm row with its provenance. A single
average ρ is used, not a matrix, because three golfers cannot support a 4 × 4 estimate, and with
parts dropping in and out a full matrix would mostly be unread. The library is one golfer's
swing-to-swing variation, which is not the population's, and the norm row says so.

### 4.4 Read at one instant, not as extremes

`m_spineBendLossDown` and `m_pelvisThrustDown` are extremes over P5–P7. Each picks its own worst
frame. If those were fed into the composite, every part would bring the biased maximum of its own
noise, and the parts would come from different instants. The composite asks **what posture the
club is delivered from, compared with address**, so every part is read at P7 (§3.4). The existing
extreme-value measures stay where they are: they are the right reading for the single faults.

### 4.5 The two tails, and the backswing

The composite is a Target measure on the axis `posture_held_impact`:

- **High tail: `loss_of_posture`.** *"You stand up out of your posture through impact."*
- **Low tail: `posture_sinks_impact`.** *"You drop lower through impact."* This is different from
  `diving`, which is a dip at P5–P6 that may recover. Sinking is the posture still lower when the
  club arrives.

The backswing has its own composite, `m_postureHeldTop`, on axis `posture_held_top`, read at P4:

| Part | Measure | Loss sign | Weight |
|---|---|---|---|
| Trunk inclination | `m_spineBendChangeTop` (DTL only; trunk rise is refused at the top) | − | 0.9 |
| Trail knee | `m_trailKneeFlex` (P1→P4) | − | 0.8 |
| Pelvis height | `m_pelvisLiftTop` | + | 0.6 |
| Head height | `m_headLiftBack` | + | 0.6 |
| Pelvis depth | `m_pelvisThrustBack` | + | 0.5 |

Its tails were `loss_of_posture_backswing` (*"you stand up out of your posture going back"*) and
`posture_sinks_backswing` (*"you sink lower going back"*).

**Changed 22 October 2026: standing up going back is read on the head's height, from down the
line.** The composite fired `loss_of_posture_backswing` on every swing Nick Henn hit (10 of 10 on
20 Oct session 3; 27 of 28 over three sessions). Mark judges him rock solid going back, and the
heights agree: head +1.0 cm and pelvis −0.9 cm at P4 in the 3-D fit, head −0.8 cm face-on. The
firing came from the two angle parts, both scored against heuristic norms centred on 0:

- **Trunk, `m_spineBendChangeTop`: measurement error, now HELD.** The down-the-line line runs from
  the hip midpoint to the shoulder midpoint, and at the top both midpoints move with the turn. The
  shoulders go from end-on to spread across the image, and the trail hip rotates forward, so the
  line tips upright when the back has not changed. It read −8.2° ± 2.2 on Nick and −7.4° ± 1.8 on
  Mark, against −3.6° and −1.9° for the hip → ear line on the same frames. Mark reviewed the
  annotated P1/P4 frames and called it measurement error. It is held, shown but never graded, until
  the trunk IMUs can corroborate it (`trunk_imu_sequence_grf_design.md` §8.8). The composite reads
  without it: it is unread, never counted as z = 0 (§4.6).
- **Trail knee, `m_trailKneeFlex`.** The trail knee straightening while the lead knee flexes turns
  the pelvis; it does not lift it, and the pelvis height shows that. The 2-D reading also overstates
  it (Nick −19.7° against −10.9° in the 3-D fit). It keeps its own condition,
  `trail_knee_straighten`, and its `causes` edge into standing up.
- **Pelvis height, `m_pelvisSinkTop`, has no producer**, so the one part that measures height most
  directly was never read.

So **`loss_of_posture_backswing` is detected by `sig_standUpBackDtl`**: the new measure
`m_headRiseBackDtl` (the `headRise` channel, §3.1, P1 → P4, a ceiling at 0 ± 3 cm carried from the
face-on `m_headLiftBack`) above its corridor. It fires only on a measured rise. It sits on its own
axis, `head_height_top_dtl`, with one tail. `sig_lossOfPostureBack` is gone. The composite
`m_postureHeldTop` keeps its low tail, `posture_sinks_backswing`, through `sig_postureSinksBack`.

| Golfer, two-camera swings | `headRise` P1 → P4 | Mark's view |
|---|---|---|
| Nick Henn, 27 | +1.9 ± 1.6 cm | holds his posture, rock solid |
| Mark, 49 (7 Oct) | +2.7 ± 1.7 cm | — |
| Jess Ratcliffe, 7 with a ruler | −8.4 ± 1.2 cm | dips going back |

The backswing and the downswing are separate conditions, the same choice `swing_fault_ontology.md`
made for `diving` and `coming_out_of_it`. They are different events at different times, and a
golfer can do both.

### 4.6 Coverage: what each camera setup can say

A composite is read only if the parts it could read carry **at least half of its authored
weight**, and only after each alternative ladder has resolved. Otherwise the composite is
*unavailable*, under a new gate rule `rule.diag.compositeCoverage`. Missing parts are never counted
as z = 0. That would quietly turn "not seen" into "fine", which is the first principle of the
session design.

| Capture | At impact | At the top | Poor setup posture |
|---|---|---|---|
| Face-on + DTL | 2.7 of 2.7 | 3.4 of 3.4 | full |
| DTL only | 1.7 of 2.7 (63 %) | 2.2 of 3.4 (65 %) | full except axis tilt |
| Face-on only | 1.9 of 2.7 (70 %) **if V1 passes**; otherwise 1.0 (37 %), unavailable | 1.2 of 3.4 (35 %), unavailable | unavailable: trunk bend is required (§5.3) |

When a composite is read with a part missing, the trust fold says which parts were read
("read from 3 of 4 parts"), using the existing gate-words machinery.

## 5. Poor posture at address

### 5.1 The parts

| Part | Measure | Camera | Weight | Its own conditions |
|---|---|---|---|---|
| Trunk hinge | `m_spineBendAtAddress` | DTL | 0.9 (**required**) | `posture_too_upright` / `posture_too_bent` |
| Knee flex | `m_kneeFlexAddress` (now the trail knee) | DTL | 0.7 | `excessive_knee_flex` / `insufficient_knee_flex` |
| Balance over the feet | `m_balanceHeelToeAddress` | DTL | 0.6 | `weight_in_heels_address` / **`weight_on_toes_address`** (new) |
| Arm hang | `m_armHangAddress` | DTL | 0.6 | **`arms_reaching_at_address`** (new) / low tail composite-only |
| Upper-back curve | `m_thoracicCurve` | DTL contour | 0.5 | `c_posture` / `flat_thoracic_spine` |
| Low-back curve | `m_lumbarCurve` | DTL contour | 0.5 | `s_posture` / `flat_lumbar_spine` |
| Side tilt | `m_axisTiltAddress` | face-on | 0.4 | **`reverse_tilt_at_address`** (new) / high tail composite-only |
| Head | `m_headPitchAddress` | DTL | 0.3 | composite-only, held (§3.1) |

Ball distance, ball position, stance width and alignment stay out, as placement (§2). The two back
curves have no producer yet. They are part of the model, and they join the composite the day the
contour producer lands; until then they are simply parts that cannot be read.

### 5.2 Combining them: the distance mode

Setup faults go both ways: too upright and too bent are both poor. So this mode squares each part
instead of signing it:

    Q = Σ wᵢ zᵢ²

If the parts were read at their norms with correlation ρ, Q is a weighted sum of correlated χ²₁
terms, with mean Σwᵢ and variance 2 ΣᵢΣⱼ wᵢwⱼρᵢⱼ². Satterthwaite's approximation, *Q* ≈ *g*·χ²ₕ,
matches those two moments. It turns Q into a tail probability, and that probability into the
equivalent z:

    Z_eq = Φ⁻¹(1 − P(gχ²ₕ ≥ Q))

The composite is **Ceiling** shaped: a low value means close to a good setup, which is not a
fault. Its norm is μ 0, σ 1, like the coherent mode's. Simulated null trip rates are 2.35 %,
2.43 % and 2.47 % at ρ = 0, 0.3 and 0.6. The approximation is slightly generous as ρ rises, by up
to 0.2 points of rate. That is small enough to accept, and it is written into the norm row.

Worked readings, again invented, using the four parts that can be read today (weights 0.9, 0.7,
0.6, 0.6):

| Readings (z) | ρ = 0 | ρ = 0.3 | ρ = 0.6 |
|---|---:|---:|---:|
| Bend alone at 3 | **2.01** | 1.84 | 1.57 |
| Bend alone at 3.5 | **2.67** | **2.43** | **2.03** |
| Bend and knees both at 2.5 | **2.46** | **2.24** | 1.88 |
| All four at 1.8 | **2.25** | **2.05** | 1.73 |

So, unlike the coherent mode, **one badly-off part makes the setup poor**. That is right for a
static shape: a golfer who is far too upright has a poor posture whatever their knees do. The
member condition fires as well, and the explanation names it (§7.3).

### 5.3 What it needs

The trunk hinge is **required**: without it there is no posture to judge. So face-on alone cannot
assess setup posture. The face-on side-tilt part only adds to a DTL reading. There is no
face-on-only skeleton3d either, since v15 refuses to fit from one camera. This goes on the coverage
line in the same words as §4.6.

## 6. The model updates

### 6.1 A composite measure (`measure_vocabulary.h`, `pack_io.cpp`)

`MeasureKind` gains `Composite`. A composite names no series and no metric key. It names its parts:

```json
{
  "id": "m_postureHeldImpact",
  "kind": "composite",
  "status": "live",
  "label": "Posture held into impact",
  "unit": "z",
  "highMeans": "standing up out of the address posture",
  "composite": {
    "combine": "coherent",
    "minWeight": 0.5,
    "parts": [
      { "name": "trunk",       "measures": ["m_spineBendChangeImpact", "m_trunkRiseImpact"],
        "signs": [-1, 1], "weight": 0.9 },
      { "name": "pelvisDepth", "measures": ["m_pelvisThrustImpact"], "signs": [1], "weight": 0.8 },
      { "name": "head",        "measures": ["m_headLiftImpact"],     "signs": [1], "weight": 0.6 },
      { "name": "pelvisHeight","measures": ["m_pelvisLiftImpact"],   "signs": [1], "weight": 0.4 }
    ]
  }
}
```

- A **sign** is given per alternative, because two instruments for one quantity can run opposite
  ways (bend falls as trunk rise grows).
- **Distance-mode parts carry no signs.** They may name `"required": true`.
- `minWeight` is the coverage rule of §4.6.

### 6.2 Its norm (`norms.json`, `norm.h`)

A composite's norm row is always `mu 0, sigmaLo 1, sigmaHi 1, unit "z"`. It adds `rho`, plus `n`,
`source: "measured"` and a provenance note naming the run root. ρ lives on the norm rather than
the measure because it is a number measured from data, and norms are where this pack keeps those.
A future row keyed to a club or cohort can then carry its own ρ.

### 6.3 The engine (`norm_measure_source.h`, new `composite_reading.h`)

The composite reading is built in `NormMeasureSource::read`. That is the one place where values
meet norms, so each part is graded under the shot's own context. It also inherits every existing
refusal: implausible, withheld, no corridor. A part refused for any of those reasons is simply a
part not read.

The steps:

1. **Read the parts.** For each part, walk its alternatives and keep the first that reads with a
   corridor. Compute its z from its own norm (side σ).
2. **Check coverage.** If the parts read carry less than `minWeight` of the total authored weight,
   or a `required` part is missing, the reading is unavailable. A new
   `MissingKind::CompositeCoverage` carries the refusal into `SignalRefusal`, and from there to the
   gate rule `rule.diag.compositeCoverage` in `gate_catalogue_rules.h`. Its words are "Too few
   parts read" and "too few parts".
3. **Combine.** Z (coherent) or Z_eq (distance), over the parts read, with the denominator or the
   moments computed from those parts only. A reading taken from three parts is calibrated as a
   three-part reading.
4. **Propagate σ.**
   - Coherent: σ_Z = √Σ(wᵢsᵢσᵢᵐ/σᵢⁿ)² / denominator, where σᵐ is the measurement σ and σⁿ the
     norm σ.
   - Distance: the delta method through Q, then dZ_eq/dQ found numerically.
   - If any part read has no σ, the composite is unquantified and the verdict stays hard.
5. **Record the contributions.** `MeasureReading` gains
   `std::vector<CompositePart> parts`, each with `{name, measureId, value, unit, z, weight,
   share}`:
   - coherent: share = wᵢsᵢzᵢ / Σ wⱼsⱼzⱼ;
   - distance: share = wᵢzᵢ² / Q.

   This is the evidence the detail page draws (§8), and what the explanation reads to name the
   dominant part.

`characteristic_engine.cpp` needs **no change to its tests**. The composite's signal is an ordinary
`outsideCorridor` on a measure whose corridor is μ 0 σ 1.

### 6.4 Validator rules (`diagnostics_health.cpp`, `core_pack_test`)

New load errors:

- `compositeNested`: a part names a composite.
- `compositeWeight`: a weight not strictly inside 0 and 1.
- `compositeSign`: a coherent sign that is not ±1, or a sign on a distance part.
- `compositeUnknownPart`: a part names a measure that does not exist.
- `compositeScope`: a part with Session scope.
- `compositeNorm`: the composite's norm is not μ 0 σ 1 in "z", or is missing ρ.
- `compositeShape`: distance mode that is not `Ceiling`.

One existing lint changes:

- `ungradedTail` treats membership of a composite as watching *both* tails, because a coherent sum
  consumes both. That is what lets `m_trunkRiseImpact` and the other parts exist without each
  having a condition of its own.

### 6.5 Shared evidence (`characteristic_pack.cpp`, `diagnostic_ledger.h`)

At load, an edge is marked `sharedEvidence` when the two ends' signals read a common measure,
directly or through a composite's parts. `early_extension → loss_of_posture` is one: both read the
forward bend. Two effects follow:

- **Session link grade.** The 2×2 dependence test is not run on such a link. Its grade caps at
  *Coherent*, and it shows the words "measured from the same readings, so the link is not tested".
  This is a sibling of the existing range-restriction cap.
- **`explain()` does not change.** A fired member covering its fired family is exactly the sentence
  we want. An assessed member keeps its own P(present), under the 1 Oct rule.

The flag is **computed, never authored**. An author cannot forget it, and a later edit to a measure
cannot leave it stale.

### 6.6 Ranking and the work-on list

- **Strength.** The composite's z *is* its distance from its line, so `firingExcess` reads it
  directly. A distance-mode row must carry its corridor shape as one-sided, which depends on
  diagnostics defect 3 (#16) being fixed first. Otherwise a clean setup would read past the fault
  line.
- **Work-ons** need no rule change. When `early_extension` and `loss_of_posture` are both
  patterns, "roots before symptoms" lists early extension and drops the family as its symptom.
  That is the behaviour a coach wants: work on the specific move. When no member is a pattern, the
  family is listed, and its content (goal, cue, check, picture, drill) is the generic posture
  lesson, which is right for losses spread thinly.

## 7. Content (`core.json`, `norms.json`)

### 7.1 New measures, signals and norms

- **Measures:** the ten of §3.4, plus the composites `m_postureHeldImpact`, `m_postureHeldTop` and
  `m_setupPosture`.
- **Signals:** one `outsideCorridor` per tail. That is `sig_lossOfPosture`, `sig_postureSinksImpact`,
  `sig_lossOfPostureBack`, `sig_postureSinksBack`, `sig_poorSetupPosture`, `sig_weightOnToes`,
  `sig_armsReaching` and `sig_reverseTiltAddress`.
- **Norms:** every new part measure gets a row seated in V3 (§9), labelled with how many golfers it
  rests on. No new norm is copied from another measure without saying so in its citation.

### 7.2 New conditions

| id | kind / group | label | prominence | state |
|---|---|---|---|---|
| `loss_of_posture` | fault / posture | Loss of posture | common | draft → active after V4 |
| `posture_sinks_impact` | fault / posture | Sinking through impact | occasional | draft |
| `loss_of_posture_backswing` | fault / posture | Standing up going back | common | draft (on the DTL head rise from 22 Oct, §4.5) |
| `posture_sinks_backswing` | fault / posture | Sinking going back | uncommon | draft |
| `poor_setup_posture` | setup / setup | Poor posture at address | common | draft |
| `weight_on_toes_address` | setup / setup | Weight on the toes at address | occasional | draft |
| `arms_reaching_at_address` | setup / setup | Reaching with the arms at address | occasional | draft |
| `reverse_tilt_at_address` | setup / setup | Tilted toward the target at address | occasional | draft |

- **Aliases.** "loss of posture" and "losing your posture" move from `coming_out_of_it` to
  `loss_of_posture`. `coming_out_of_it` keeps "standing up through it" and the rest, which name
  the specific move.
- **Words.** Every row needs `golfer`, `golferWell`, `golferWhy`, `goal`, `cue`, `check`,
  `picture` and a drill. The report gate requires all of them on listable rows. Drafts are in
  Appendix B for Mark to edit; the wording lint has to pass them.
- **Provenance.** All edges are at practice tier, with `searchedOn` set when the search is done,
  as part of the content stage.

### 7.3 New edges

All edges below are `causes`. An edge marked † gets `sharedEvidence` at load (§6.5).

**Into `loss_of_posture`:**

| From | Strength |
|---|---|
| `early_extension` † | strong |
| `loss_of_posture_backswing` (no † since 22 Oct 2026, §4.5) | moderate |
| `posture_sinks_backswing` † | moderate (dropping going back, then rising to make room) |
| `poor_setup_posture` | moderate |
| `poor_core_stability` | moderate |
| `limited_thoracic_rotation` | moderate |
| `limited_lead_hip_ir` | moderate |

**Out of `loss_of_posture`:**

| To | Strength |
|---|---|
| `thin` | strong |
| `top` | moderate |
| `flipping` | moderate (the hands reach for a ground that has moved away) |
| `strike_toe` | weak |

**Into `posture_sinks_impact`:**

| From | Strength |
|---|---|
| `diving` † | strong |
| `trying_to_hit_down` | moderate |
| `poor_core_stability` | weak |

**Out of `posture_sinks_impact`:**

| To | Strength |
|---|---|
| `chunk` | strong |
| `attack_too_steep` | moderate |
| `strike_heel` | weak |

**Into `loss_of_posture_backswing`:** (no † since 22 Oct 2026: the condition reads the
down-the-line head rise, which none of these reads; §4.5)

| From | Strength |
|---|---|
| `trail_knee_straighten` | strong |
| `pelvis_rise_backswing` | strong |
| `head_rise_backswing` | moderate |
| `limited_trail_hip_ir` | moderate |
| `limited_thoracic_rotation` | moderate |
| `poor_setup_posture` | moderate |

**Out of `loss_of_posture_backswing`:**

| To | Strength |
|---|---|
| `flat_shoulder_plane` | moderate |

**Into `posture_sinks_backswing`:**

| From | Strength |
|---|---|
| `pelvis_sink_backswing` † | strong |
| `head_drop_backswing` † | moderate |

**Into `poor_setup_posture`:**

| From | Strength | Why |
|---|---|---|
| `posture_too_upright` † | strong | |
| `posture_too_bent` † | strong | |
| `excessive_knee_flex` † | strong | |
| `c_posture` † | strong | |
| `s_posture` † | strong | |
| `insufficient_knee_flex` † | moderate | |
| `weight_in_heels_address` † | moderate | |
| `weight_on_toes_address` † | moderate | |
| `arms_reaching_at_address` † | moderate | |
| `reverse_tilt_at_address` † | moderate | |
| `ball_too_close` | moderate | placement causes posture (§2) |
| `ball_too_far` | moderate | placement causes posture (§2) |
| `poor_core_stability` | moderate | capacity |
| `thoracic_kyphosis` | moderate | capacity |
| `limited_hamstring_length` | moderate | capacity |

**New setup conditions:**

| Edge | Strength |
|---|---|
| `weight_on_toes_address` → `early_extension` | weak |
| `weight_on_toes_address` → `strike_heel` | moderate |
| `arms_reaching_at_address` → `flat_backswing_plane` | moderate |
| `hands_set_high` → `arms_reaching_at_address` | moderate |
| `reverse_tilt_at_address` → `reverse_spine_p4` | strong |
| `reverse_tilt_at_address` → `attack_too_steep` | moderate |

**Not linked on purpose.** `loss_of_posture` and `coming_out_of_it` are siblings. They share the
head reading and the same commonest cause, early extension. A causal edge between them would claim
an order the evidence cannot test.

**Checks on the graph.** No cycles; the graph's only route back to setup is placement, and setup is
never caused by a swing fault. The triangles, such as `excessive_knee_flex → early_extension →
loss_of_posture` beside `excessive_knee_flex → poor_setup_posture → loss_of_posture`, are each an
independent claim under the rule in `swing_fault_ontology.md` §12.1.

### 7.4 Two stale holds, corrected

- **`m_kneeFlexAddress`.** Its `gapReason` says `insufficient_knee_flex` "has no authored cause
  yet". It has had two since August: the two ankle-dorsiflexion capacities, both weak. With the
  switch to the trail knee (§3.1) the measure goes live, and both knee conditions move to active
  after V4.
- **`m_balanceHeelToeAddress`.** It is held for want of a norm. V3 seats one, and the measure goes
  live with `weight_in_heels_address` and the new `weight_on_toes_address`. Mark's readings were
  53 ± 6 % (7-iron) and 64 ± 4 % (wedge), the proxy's weights are geometric, and its absolute
  value has never been checked against a pressure plate. The norm says so.

## 8. What the golfer sees

- **Session diagnostics detail page,** in Analyse, Replay **and** Print ([[feedback_surface_everywhere]]).
  A composite condition opens on **"What changed"**: one row per part, each with
  - its name in the golfer's words (`golferName`);
  - address and impact (or top) values in the part's own unit;
  - the change;
  - the part's own band colour;
  - an ordinal share word: *most of it*, *some*, *a little*, *against it* (a negative share).

  A part that was not read shows "-", and the trust fold carries the reason. Coverage is said once,
  in the trust fold, following the Charts page pattern. **No composite number appears anywhere.**
  The header card quotes the dominant part's change. That fixes, for these rows, the mismatch of
  diagnostics defect 1 by construction: the card and the page quote the same reading.
- **Poor setup posture** uses the same table without a "change" column: address value against the
  ideal band, and the share.
- **Charts.** A **Posture** preset (`.presets`) plots `spineForwardBend`, `pelvisThrust`,
  `headLift`, `pelvisLift`, `trailKneeFlexion` and `trunkRise` together, from address. These are
  the angles changing, which is what Mark asked to see. It follows the page pattern: headline, then
  plot, then trust, then folds. It is in both Analyse and Replay.
- **Readings tab.** The part measures appear like any other measure. The composites do not, since
  they have no golfer-facing unit.
- **Report and home.** Through the work-on rule of §6.6, with the Appendix B words. The report
  picture is `change / dtl / P7 / spine` for the impact pair, `change / dtl / P4 / spine` for the
  backswing pair, and `typical / dtl / P1 / spine` for setup.

## 9. Validation and measurement

Every multi-swing run goes on the **cabin PC**, in Release, with run trees under
`/mnt/swingdata/scratch/posture-<date>/`. Pinned inputs go into `corpus/`. Result trees are deleted
once their CSVs are in the repo.

**Populations:**

- Mark's library: about 113 DTL swings, of which 24 are two-camera (07-04 ×15, 06-11 ×9).
- The 20 Oct corpus: Jess Ratcliffe (23 swings) and Nick Henn (26), two cameras each, no truth.

| Gate | What | Pass | If it fails |
|---|---|---|---|
| **V1** | Face-on `trunkRise(P7)` against DTL −Δbend(P7) on every two-camera swing. Also the impact composite's verdict read face-on-only against both-camera. | Spearman ≥ 0.7 on each golfer; verdict agreement ≥ 85 % | `trunkRise` held; face-on-only impact composite unavailable (§4.6); written up |
| **V2** | ρ per composite: the mean pairwise correlation of the parts' z-scores, per golfer and pooled | Reported with n; floored at 0 | n/a. It is a measurement |
| **V3** | Norms for the ten new part measures and the balance proxy, from the three golfers plus whatever coaching figures exist | Every row cites its golfers and n | Row stays heuristic and says so |
| **V4** | Firing table on library and corpus, for every new condition: fires / assessable, family-without-member, member-without-family, P(fire) spread | Written | n/a |
| **V5** | Repeatability: the library pose against the pinned corpus pose on the same swings, with a control, because pose runs are not deterministic | Verdict flips ≤ 1 in 20; `headPitch` spread ≤ 3° | The unstable part is held |
| **V6** | Engine unit tests: null trip rate within ±0.5 points of 2.28 % over 10⁵ draws per mode at ρ ∈ {0, 0.3, 0.6}; coverage and required-part refusals; alternative ladders; σ propagation against Monte Carlo; `sharedEvidence` on the shipped graph | All pass | Not done |

**Expected before measuring, so the outcome can be checked against it.** Mark's swings lose 9–11°
of bend and 5–10 cm toward the ball, and `early_extension` fires on 29/31 and 49/50. So
`loss_of_posture` should fire on nearly every Mark swing, with early extension as the driver. The
informative swings are Jess's and Nick's. That is also where family-without-member, the case the
family exists for, either turns up or does not. If it never does, the write-up says the family
is acting only as a hub. Its weights and ρ are still correct; the population is simply too small
to show the summing.

## 10. Build plan

Stages run in order, unattended, with commits only at the end and Mark judging the results before
anything is committed.

1. **Engine**
   - `src/Diagnostics/measures/measure_vocabulary.h`: `Composite` kind and part struct
   - `pack_io.cpp`: parse and write
   - `src/Diagnostics/norms/norm.h`, `norm_pack.cpp`: `rho`
   - new `src/Diagnostics/norms/composite_reading.h`
   - `norm_measure_source.h`: compose
   - `characteristic_engine.h`: `MeasureReading::parts`
   - `live_measure_source.h`: `CompositeCoverage`
   - `gate_catalogue_rules.h`: the new rule
   - `diagnostics_health.cpp`: §6.4 rules
   - `characteristic_pack.cpp`: `sharedEvidence`
   - `src/Analysis/diagnostic_ledger.h`: the link cap
   - tests for V6 in `src/Diagnostics/tests/`
2. **Producers**
   - `src/Body/upper_body_metrics.{h,cpp}`: `trunkRise`
   - `src/Body/dtl_posture.h`: `armHang`, `headPitch`
   - `src/Metrics/metric_catalogue_manifest.cpp`: three descriptors and the Posture preset
   - `docs/design/pinpoint_sign_conventions.md`: three signs
   - unit tests in `src/Analysis/tests/`
3. **Measurement:** V1–V3 → `docs/research/data/posture/`, ρ and norms into `norms.json`.
4. **Content:** §7, all of it, in `core.json` and `norms.json`. `core_pack_test` and
   `diagnostics_health` clean.
5. **Presentation**
   - the "What changed" fold in `PpDiagAnalyse`, `PpDiagReplay` and `PpDiagPrint`
   - the Posture preset on Charts
   - probe each register with `--probe-qml` before calling it done, then a windowed run
6. **Library:** re-analyse on the cabin PC, so the new producer series land. This reuses the
   recorded pose; no pose re-run is needed. Then run `regrade_ledger`, V4 and V5, and the
   write-up.

## 11. Decisions (agreed by Mark, 2026-10-21)

Mark agreed all four as recommended on 2026-10-21. The build follows them.

1. **Four posture-change conditions, not one.** Up and down, at impact and at the top.
   *Agreed:* yes, for the same reason `diving` and `coming_out_of_it` are separate.
2. **The family does not fire on a single part alone** (the coherent mode, §4.3). *Agreed:*
   yes. The single faults already cover a one-part move, and the family means the whole posture.
   The alternative, a maximum or the distance mode, would make it fire on exactly the swings that
   `early_extension` already names.
3. **The alias "loss of posture" moves** from `coming_out_of_it` to `loss_of_posture`.
   *Agreed:* yes.
4. **Three new setup member conditions** (`weight_on_toes_address`, `arms_reaching_at_address`,
   `reverse_tilt_at_address`). The other tails (arms tucked, too much tilt at address, chin) are
   parts of the composite only, for now. *Agreed:* yes. These three have a coaching fix of
   their own; the others do not yet.

## 12. Considered and not done

- **The family as `Any` over its members' signals.** This adds a name and no information, and it
  double-counts every member in the ranking.
- **A per-frame "posture drift" curve.** The part curves on the Posture preset already show the
  angles changing. A z-unit curve would put a composite number on screen, which principle 5 rules
  out.
- **Putting the conjunction in a producer.** A C++ composite would bypass context norms, the
  alternative ladders and `preferKeys`. That is the reason `DetectionMode::All` was built instead
  of producer-side conjunctions.
- **A full correlation matrix.** Three golfers cannot support one (§4.3). It can be revisited once
  the corpus has more golfers.
- **Including side tilt in loss of posture.** Its normal growth into impact would read as a loss on
  every good swing (§2).
- **Re-opening `early_extension`'s forward-bend signal** (8 Oct, Mark's decision). It stays. The
  shared reading is handled by §6.5 instead.

## 13. Not claimed

- There is no truth for any posture number. The DTL numbers were graded on repeatability and on
  two sessions agreeing; `trunkRise` will be graded against DTL, which is not truth either.
- The norms rest on three golfers at most, ρ on within-golfer variation, and the weights on
  editorial judgement. Every one is labelled.
- The composite's calibration assumes the parts are roughly normal about their norms. A one-golfer
  norm seated on a golfer with early extension is not centred on a good swing, and the write-up has
  to say how far off centre each norm is.

---

## Appendix A — the null calibration and worked tables

Run with `/opt/homebrew/bin/python3` (numpy, scipy). It reproduces §4.3 and §5.2.

```python
import numpy as np
from scipy.stats import norm, chi2
w  = np.array([0.9, 0.8, 0.6, 0.4])          # coherent parts, §4.2
ws = np.array([0.9, 0.7, 0.6, 0.6])          # distance parts readable today, §5.1
R  = lambda r, k=4: np.full((k, k), r) + (1 - r) * np.eye(k)
Z  = lambda z, r: (w @ z) / np.sqrt(w @ R(r) @ w)
def Zeq(z, r):
    Q = (ws * z**2).sum(); E = ws.sum(); V = 2 * (np.outer(ws, ws) * R(r)**2).sum()
    g, h = V / (2 * E), 2 * E * E / V
    return norm.isf(chi2.sf(Q / g, h))
rng = np.random.default_rng(1)
for r in (0, .3, .6):
    x = rng.multivariate_normal(np.zeros(4), R(r), 200000)
    zc = (x @ w) / np.sqrt(w @ R(r) @ w)
    Q = (ws * x**2).sum(1); E = ws.sum(); V = 2 * (np.outer(ws, ws) * R(r)**2).sum()
    zd = norm.isf(chi2.sf(Q / (V / (2 * E)), 2 * E * E / V))
    print(r, (zc > 2).mean(), (zd > 2).mean())   # 0.0224/0.0235, 0.0223/0.0243, 0.0229/0.0247
```

## Appendix B — draft words (for Mark to edit)

| id | golfer | golferWell | golferWhy |
|---|---|---|---|
| `loss_of_posture` | you stand up out of your posture through impact | you keep your posture through impact | Standing up lifts the bottom of your swing, so contact gets thin and harder to repeat. |
| `posture_sinks_impact` | you drop lower through impact | you keep your height through impact | Dropping lower moves the bottom of your swing down, so you catch the ground before the ball. |
| `loss_of_posture_backswing` | you stand up out of your posture going back | you keep your posture going back | Standing up going back changes your height over the ball, so you have to find it again coming down. |
| `posture_sinks_backswing` | you sink lower going back | you keep your height going back | Sinking going back changes your height over the ball, so you have to find it again coming down. |
| `poor_setup_posture` | your posture at address is hard to swing from | you set up in a balanced, athletic posture | A posture that is hard to hold changes during the swing, so the strike gets harder to repeat. |
| `weight_on_toes_address` | your weight is forward on your toes at address | you set your weight over the middle of your feet | Weight on the toes tips you toward the ball as you swing, so contact drifts toward the heel. |
| `arms_reaching_at_address` | you reach out for the ball with your arms at address | your arms hang naturally from your shoulders | Reaching puts the club out of its natural path, so the swing gets flatter and harder to repeat. |
| `reverse_tilt_at_address` | your upper body leans toward the target at address | your upper body tilts slightly away from the target | Leaning toward the target at address tends to carry into the top, so the club comes down steep. |

| id | goal | cue | check |
|---|---|---|---|
| `loss_of_posture` | Keep your address posture through the ball | Feel your chest stay over the ball and your seat stay back until the ball has gone. | Film from down the line. At impact your back and hips should still sit on the lines you drew at address. |
| `poor_setup_posture` | Set up in a balanced, athletic posture | Hinge from your hips until your arms hang straight down, then soften your knees just enough to feel ready. | Film from down the line at address: a line down from your shoulders should pass over your knees and the balls of your feet. |

The remaining rows' goal, cue and check, and every drill reference, are written in content stage 4
against the same lint.
