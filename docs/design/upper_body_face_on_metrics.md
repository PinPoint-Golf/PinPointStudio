# Upper-body metrics from a face-on camera

**Audience**: developers and content authors working on the chest, shoulder and arm half of the diagnostics model
**Code**: `src/Analysis/upper_body_metrics.{h,cpp}`, `UpperBodyMetricsStage` (`wrist_analyzer.cpp`), `UpperBodyMetricProvider`
**Content**: `src/Resources/diagnostics/core.json`, `norms.json`
**Status**: producer live; nine series shipped; the detection engine that turns them into findings is still dormant (`diagnostics_developer_guide.md` §8)
**Written**: 2026-08-02

The chest-and-arms counterpart to [`lower_body_face_on_metrics.md`](lower_body_face_on_metrics.md).
Read that first — the frontal-plane rule, the robust-address-reference shape and the
percentage-of-a-body-span unit argument are all stated there and are not repeated here.

---

## Contents

1. [What prompted this](#1-what-prompted-this)
2. [Built on the anatomy vocabulary, not on keypoint indices](#2-built-on-the-anatomy-vocabulary-not-on-keypoint-indices)
3. [What ships, and the geometry of each](#3-what-ships-and-the-geometry-of-each)
4. [Two channels that needed a decision](#4-two-channels-that-needed-a-decision)
5. [The duplicates that were retired instead of built](#5-the-duplicates-that-were-retired-instead-of-built)
6. [Signs, and how they are pinned](#6-signs-and-how-they-are-pinned)
7. [What is deliberately still planned](#7-what-is-deliberately-still-planned)

---

## 1. What prompted this

An audit of the metric catalogue against what a face-on camera actually resolves. Of 37 `.planned`
descriptors, a large majority needed no new sensing at all — the pose track that produces
`pelvisSway` and `headTilt` today carries every keypoint the upper-body channels need. Meanwhile
49 of 109 authored measures in the diagnostics pack were blocked on a missing producer, and those
blocked **70 of 145 conditions**: nearly half the library was written, cited, corridor-seated and
ungradeable.

The corridors were the surprise. They were **already authored** for almost every target measure —
so this was mostly a producer gap, not a content gap, and shipping the producers turned a status
flip into a working diagnosis rather than starting a new authoring round.

---

## 2. Built on the anatomy vocabulary, not on keypoint indices

`lower_body_metrics.cpp` predates `Diagnostics/anatomy_vocabulary.{h,cpp}` and spells its own
`kLHip = 11`. This module does not. Every point and segment resolves through `resolvePoint()` /
`resolveSegment()`, and there are three reasons, in order of importance:

**The content already names this geometry.** The composed measures in `core.json` carry a `series`
facet triple, authored before any producer existed:

| Measure | `what` | `quantity` | `reference` |
|---|---|---|---|
| `m_shoulderPlane` | `shoulderLine` | `angle` | `ground` |
| `m_thoraxDrift` | `thoraxCentre` | `distance` | `trailAnkle` |
| `m_trailElbowRise` | `trailElbow` | `height` | `shoulderLine` |
| `m_leadHandWidth` | `leadHand` | `distance` | `thoraxCentre` |
| `m_leadArmToTorso` | `leadUpperArm` | `angle` | `spine` |

**That table is the producer specification.** Building to the same vocabulary makes the join exact
rather than coincidental — `diagnostics_content_extension.md` §26 flagged precisely this risk
("confirm the facet-derived id will match the eventual producer key, or the join will silently
miss"). It is also why `m_thoraxDrift` is measured **from the trail ankle** and not from address:
the facet says so, and the measure over it is a `delta` that does the address referencing itself.

**Handedness and layout resolve once.** A role is lead or trail, never left or right, and a role
missing from a 17-keypoint track reports *why* rather than silently reading a neighbouring index.

**The overlay and the measurement agree by construction.** The vocabulary is explicitly intended to
become the single source of truth for the skeleton overlay too, so what is drawn is definitionally
what is measured.

The resolver works in whatever units the caller's points are in, so each frame is de-normalized to
pixels once into a scratch array. Both dimensions, always: keypoints arrive normalized by width and
height separately, and scaling by width alone makes every angle a statement about the aspect ratio.

---

## 3. What ships, and the geometry of each

| Metric | Geometry | Unit |
|---|---|---|
| `secondaryAxisTilt` | mid-hip→mid-shoulder line from vertical | ° |
| `spineSideBend` | shoulder-line tilt against hip-line tilt | ° |
| `thoraxLateralDrift` | chest centre along the stance line, from the trail ankle | % stance width |
| `shoulderPlaneAngle` | shoulder-line tilt from horizontal | ° |
| `elbowAlignment` | elbow-line tilt from horizontal | ° |
| `trailElbowHeight` | trail elbow's vertical height above the shoulder line | % shoulder width |
| `leadHandWidth` | lead hand to chest centre | % arm length |
| `leadUpperArmToChest` | chest centre to the lead upper-arm SEGMENT | % shoulder width |
| `leadArmToTorso` | lead upper arm against the torso axis | ° (unsigned) |
| `leadArmFlexion` (faceOn rung, 13 Nov 2026) | 180° − the angle at the lead elbow between the lead shoulder and the lead wrist | ° (unsigned, 0 = straight) |

Two more landed in `lower_body_metrics.cpp` rather than here, because they read the hips, knees and
ankles that module already resolves: `feetAlignment` (the ankle-line tilt) and `comOverLeadFoot`
(the pelvis centre's distance from the lead ankle along the stance line).

**Which denominator, and why it is not a free choice.** `% shoulder width` means the Euclidean
shoulder separation, because that is what it already means in the shipped `stanceWidth`.
`% stance width` means the absolute horizontal ankle span, because that is what it already means in
the shipped `pelvisSway`. A new producer does not get to redefine an existing unit string — a
corridor declares one unit and grading never looks at it again, so a silently different scale is a
wrong answer wearing a right answer's clothes.

---

## 4. Two channels that needed a decision

### `spineSideBend` is the shoulder line against the hip line

Side bend is lateral flexion of the **thorax relative to the pelvis**, and neither pose layout
carries a keypoint between the shoulders and the hips. The tempting reading — the neck-to-pelvis
line from vertical — is not side bend at all: it is a whole-body lean, and it is already shipped as
`secondaryAxisTilt`. Two metrics computing the same number under two names would have been the
defect this batch spent most of its effort avoiding elsewhere.

So side bend is `hipLineTilt − shoulderLineTilt`: two segments that both exist, and a difference
that **cancels** the whole-body tilt. Tilt both lines together and it reads zero, which is correct —
that is a lean, not a bend. Drop only the trail shoulder and it reads positive, which is the trail
shoulder working down and under the turn. The unit test asserts both cases directly.

**With trunk sensors worn (12 November 2026) this reading steps aside.** The chest and hip sensors'
frontal tilts (`thoraxSideBend − pelvisObliquity`, route `trunkImus`) are the ladder's top rung, and
where they read they are the only `spineSideBend` on the swing; this face-on series and its records
(`body.upperAddress`, `body.sideBendLines`) are taken off the key after the run
(`applyTrunkSensorPrecedence`, `body_rotation_estimation.md` §7.4).

### `trailElbowHeight` is a VERTICAL height, not a perpendicular distance

A perpendicular distance to the shoulder line needs a cross product, and the sign of a 2-D cross
product depends on whether the lead side is image `+x` — so it silently inverts on a mirrored
camera, which is the exact failure the absolute-denominator line form exists to avoid. The vertical
height above the line at the elbow's own `x` has no such dependency, and it is also what the
coaching reading means: the elbow rising out of the turn, not its distance from an inclined axis.

### `leadArmToTorso` is read only where both of its directions are in view (13 November 2026)

The reading is the unsigned angle between two image directions: the lead upper arm (lead shoulder →
lead elbow) and the torso (the shoulder midpoint → the hip midpoint). Each direction can stop being a
measurement, and a frame where either has is now **refused**:

- **The torso.** Its top end is the shoulder midpoint. Once the shoulders foreshorten toward one image
  column, which keypoint is which, and so where the midpoint sits, is guesswork. The gate is the
  shoulder line's own: `upperBody.minShoulderSpanRatio` (0.40 of the address `|dx|`), as for
  `trunkRise`.
- **The arm.** Its image length must lie between `upperBody.minUpperArmRatio` (0.45) and
  `upperBody.maxUpperArmRatio` (1.4) × its address length (`ref.leadUpperArmLenPx`, the median over
  the address reference frames). Below the floor the upper arm points along the view ray and its
  image direction is keypoint noise. Above the ceiling the elbow has left the arm.

Why: the arm-readings report (13 Nov). At Jess's late P8s the upper arm imaged 13–43 px against
about 110 px at address, the smoothed elbows still passed the 0.30 confidence gate, and the angle read
anything from 2° to 179°. At a correctly timed P8 the reading mostly measured chest turn: ρ −0.76 with
the shoulder-span ratio. A frame that passes both gates reads exactly what it read before.

What a refusal leaves:

- The refused instant goes on `gatedArmTorso`. The series' `valid` mask is 0 there and is never
  bridged, so the chart draws it as not measured.
- A **Masked** `body.armTorsoLines` record over the whole swing (the channel is not domain-masked,
  like `trailElbowHeight`).
- A **Refused** `body.armTorsoLines` record for each instant the grade reads, Impact (`phase` P7) and
  P8. One fires when fewer than `sampler.kMinValidSamples` ungated frames lie within
  ±`sampler.kWindowHalfUs` of that instant, the grader's own rule. A Masked record names no reason
  for a missing grade (`primaryRecord`), and this one does. Its evidence carries the shoulder and arm
  ratios at the refused frame nearest the instant, so it says which half refused.
- `m_leadArmToTorso` (the change P7 → P8, `chicken_wing`) is then not assessable, with that reason in
  golfer words (`gate_words.json`).

`leadArmToTorso` still carries no σ: it is unsigned, and the gate does not change that (§12f of the
test).

Since 13 Nov 2026 (plane stack D5) `chicken_wing` no longer reads it. It stays live and shown, with no
signal; the fault reads the lead elbow's fold after impact (next subsection).

### `leadArmFlexion` from the face-on camera: the lead elbow, gated on its two segments (13 November 2026)

Mark, 13 Nov: chicken wing "should be visible immediately between impact and 100 ms after (before
P8) … and crucially it should be easily visible from the face-on camera. There is no reason to need
3-D for this one." The coach's chicken wing is the lead elbow folding through and after impact with the
elbow jutting out, so the reading is the elbow's own angle, not the arm against the torso.

**The value.** At every frame where the lead shoulder S, the lead elbow E and the lead wrist W are
confident: `180° − θ`, θ the angle at E between E→S and E→W. 0° is a straight arm and + is more bend —
the sign and the unsigned magnitude of the `wristImus` rung (`metric_extractor.cpp`, acos of the
upper-arm and forearm axes), so one corridor means one thing on both rungs. The series is published
under the existing key with `routeId "faceOn"` (`routeDirect` false: Estimated). It is the whole swing,
not domain-masked, because the fold after impact is what it is read for.

**σ.** The three keypoints' smoother σ through the angle. θ is the difference of the two segments'
direction angles, and the elbow is in both, so its two terms are combined as vectors before squaring:

    σ_b = sqrt( σ_S²/|u|² + σ_W²/|v|² + σ_E²·(1/|u|² + 1/|v|² − 2·cos θ/(|u||v|)) ) · 180/π

with u = S − E, v = W − E. A straight arm puts the elbow's whole jitter on the bend,
(1/|u| + 1/|v|)·σ_E. The series σ is the median over the frames the mask keeps, as for every other
channel here. ⚠ The bend is unsigned (folded at 0°), so on a near-straight arm the published number is
biased away from 0 and σ is the spread of the unfolded angle. On the library the arm at impact images
9–22° bent (golfer medians) against a σ near 2.7°; 5 of 244 swings read below 2σ at impact.

**The gate.** A frame is refused when the lead UPPER ARM (S→E) images outside
`upperBody.minUpperArmRatio … maxUpperArmRatio` (0.45 … 1.4, FA's band) × its address length, or the
lead FOREARM (E→W) outside `upperBody.minForearmRatio … maxForearmRatio` (0.45 … 1.4) × its address
length (`ref.leadForearmLenPx`, the median over the address frames whose wrist was confident; 0 refuses
the channel for the swing). A segment pointing toward or away from the camera images short and hides
the bend; one imaging long is a keypoint off the arm. **There is no shoulder-line gate**: the reading
never uses the shoulder midpoint, so a chest turned edge-on does not refuse it (test §16d).

Library check (13 Nov 2026, read-only, stored face-on keypoints, 249 swings, 3711 frames in the 100 ms
after impact): the forearm ratio there has median 0.88 and 1st–99th percentile 0.40–1.29, the upper
arm 0.88 and 0.62–1.32. The band refuses 80 of those frames on 19 swings. The forearm's refused low tail
is the lead wrist keypoint sweeping across the elbow (Nick 2 Nov 2026, 0.29 at the elbow, σ_wrist
5–22 px). The forearm pair is its own so a sweep can move one segment without the other.

What a refusal leaves, as for `leadArmToTorso`:

- The instant goes on `gatedLeadElbow` (with both ratios); the series' `valid` mask is 0 there and is
  never bridged.
- A **Masked** `body.leadElbowLines` record over the swing.
- A **Refused** `body.leadElbowLines` record at Impact (`phase` P7), the instant the fold after impact
  is measured from, when fewer than `sampler.kMinValidSamples` ungated readable frames lie within
  ±`sampler.kWindowHalfUs`. It is the primary record for `leadArmFlexion`, so a missing fold names it.
  When impact reads, the span after it has samples too, so no second record is needed.
- Its own gate rather than an extension of `body.armTorsoLines`: that record's sentence and golfer
  words name the shoulders, which this reading never uses.

**The occlusion gate (13 Nov 2026, D5 round 3).** A frame is also refused when the lead arm is not
really seen, which the pose model does not say on its own. After impact a golfer who keeps the head down
hides the lead shoulder behind it, and both the smoother's predicted shoulder and the model's own detection
land on the chin or neck (Jess, frame sheet of 13 Nov). A frame is refused (`hiddenLeadElbow`, gate
`body.leadArmHidden`) when either:

- the lead shoulder, elbow or wrist lies within `upperBody.headRadiusRatio` (0.25) × the address
  shoulder width of the centre of the head keypoints scored ≥ `confMin` raw (nose, eyes, ears);
- **or** any of the three scores under `upperBody.minRawConf` (0.40) on the model's RAW confidence. The
  smoother's score is never used, because a predicted point is floored at 0.5 there.

This check comes before the length band, so a frame is on one list only. Both elbow gates leave a Masked
summary. When nothing is left at impact, a Refused record fires there; when too much of the 100 ms after
impact is unread, a Refused record fires for that span. Each comes from the gate that refused the most
frames in that stretch, with ties going to `body.leadArmHidden`. The span record (`phase` afterImpact, `gapMs`) fires when the frames left leave more than `reduce.msSpanMaxGapUs`
(30 ms) unread. That is the grader's own coverage rule for a `windowMs` span, under which the fold is then
not assessable rather than read from the frames before the hidden peak. The library audit is in
`plane_stack_design.md` §11, "D5 round 3".

**Precedence.** The catalogue puts the `faceOn` rung below `wristImus`, and
`applyLeadElbowSensorPrecedence` (run once after every stage) makes that true of the document: where
the wrist sensors wrote `leadArmFlexion`, the face-on series is erased, its `body.leadElbowLines`
records go and the key leaves the covers of this module's whole-reference refusals. The wrist
assessment (the sensors' DOF grid, its adapters and the session Wrist tab) never takes a `faceOn` series
(`isCameraWristDofSeries`).

**What reads it.** `m_leadElbowFoldAfterImpact` (extremum, max over P7 + 0…100 ms, from P7; the
reducer's `windowMs`, `diagnostics_developer_guide.md` §3), and through it `chicken_wing`. The eight
`m_leadElbowFlex_p1…p8` measures and `bent_lead_arm` / `locked_lead_arm` read the same key; they are
**held**, because their corridors came from the wrist sensors' grid and the face-on elbow at the top
reads a median 37° above address on the library (it would have fired `bent_lead_arm` on all 214
swings it could read). See `plane_stack_design.md` §11 D5.

---

## 5. The duplicates that were retired instead of built

`shoulderAlignment` and `hipAlignment` were in the design catalogue as separate metrics. Each was
geometrically **identical** to a line this product already carries — `shoulderPlaneAngle` and the
live `hipLineTilt` — differing only in which phase it was read at.

`metric_reducer.h` exists to express exactly that: *a series is what a producer produces; a reducer
is how it is sampled.* Its header says conflating the two "makes one producer look like four
separate roadmap items". Two descriptors for one curve is two names for one number, and here it was
worse than usual: the two carried opposite sign conventions, so a reader comparing them would have
concluded the golfer's hips and shoulders disagreed when they had simply been described twice.

Both descriptors were deleted (the catalogue went 72 → 70), and the measures `m_shoulderAlignment`
and `m_hipAlignment` now point at the surviving series with their labels and `highMeans` restated in
its convention.

`feetAlignment` was NOT retired against `toeLineAngle`, and the distinction is worth stating. The
content's own argument holds: the ankle joints are far less affected by foot flare than the toes,
and the impact read has no counterpart in an address-only scalar. It also fixes a sign defect —
`toeLineAngle` is a raw `atan2` of the lead→trail vector and inverts for a left-handed golfer.

---

## 6. Signs, and how they are pinned

Full table in [`pinpoint_sign_conventions.md`](pinpoint_sign_conventions.md). The rule here:

- **Body lines** — `shoulderPlaneAngle`, `elbowAlignment`, and `hipLineTilt` / `feetAlignment` next
  door — are positive when the **trail end sits above the lead end**, computed against the absolute
  horizontal separation so the answer does not flip for a mirrored camera.
- **`secondaryAxisTilt` is trail-positive**, the one lateral channel that deliberately breaks the
  lead-positive default. The quantity is named for the lean *away from the target*.
- **`leadArmToTorso` is unsigned.** A frontal projection cannot say which side of the torso the arm
  left on, and putting a sign on a quantity the camera did not resolve would be a fabrication.

A sign is the one thing a synthetic track can pin exactly and a corpus cannot, which is why
`upper_body_metrics_test.cpp` runs its whole sign suite a second time through a **mirrored camera
with a left-handed golfer** and asserts the numbers are unchanged. A convention that only holds for
a right-hander filmed from one side is not a convention. That test is also what makes a SwingLab
corpus pass unnecessary for this batch.

---

## 7. What is deliberately still planned

Restating the frontal-plane rule as a work queue, because "why not the rest of the upper body too"
is the obvious question:

- **`spineForwardBend`** — sagittal. The hinge from the hips is the plane a face-on camera
  foreshortens to almost nothing, and four characteristics over it would be graded off projection
  error. With the knee flexions, the strongest argument for a down-the-line pipeline.
- **`thoracicFlexion` / `lumbarExtension`** — no keypoint exists between the shoulders and the hips
  in EITHER layout, so these cannot come from the skeleton at all. They stay roadmap items rather
  than capture gaps because upper-back rounding and low-back arch are both plainly visible in the
  BACK CONTOUR of a down-the-line silhouette. That makes them a producer worth building, not a gap
  that can never close.
- **`ballBodyDistance`** — depth, across the stance line. The face-on camera's own axis.
- **Hip internal rotation** (`trailHipInternalRotation` / `leadHipInternalRotation` since 12 Nov 2026) —
  needs a pelvis IMU; the thigh's own turn comes from the two-camera fit, or is assumed fixed
  (`body_rotation_estimation.md` §7.4). No face-on route.

`pelvisRotation` and `thoraxRotation` used to head this list and no longer do. They are also not
frontal-plane quantities, but the image span of a body line collapses by the cosine of its turn, and
an honest estimate with a stated uncertainty beat leaving eleven characteristics dark. See
[`body_rotation_estimation.md`](body_rotation_estimation.md).
