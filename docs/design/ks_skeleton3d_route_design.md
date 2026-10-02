# The kinematic sequence from the two-camera skeleton

**Status:** 2 October 2026. **K0 ran and changed the plan.** The skeleton's pelvis and thorax rates
coast through impact, because the down-the-line hips overlap just after the ball
(`docs/research/data/kinematic_sequence/skeleton_rate_k0_20261002.md` §8). The trunk rung is
therefore **not built**: the pelvis and thorax stay IMU → pair → face-on.

**What is built is the lead-arm rung** (§5.3, `faceOn+dtl3d`), plus the shared extractor (§5.1),
the stage reorder (§8) and the catalogue (§9).

**Later the same day:**

- **The coast was fixed at its source.** skeleton3d now gives pelvis yaw its own acceleration σ
  (v4, K0 §10).
- **The trunk rung was then built** (§5.2), behind `sequence.skel3d.trunk`, and measured (K0 §11).
  On 07-04 it placed nothing. Where it held it repeated the pair's answer with 3–6× the timing σ.
- **So it was moved BELOW the pair and turned ON**, at Mark's call: IMU → pair → skeleton → span.
  On 07-04 it then fires on none of the 15 swings, so nothing changes; it is there for the swings
  where the pair cannot produce. Sections 4, 5.2, 7 and 10 describe the trunk rung as
designed and are kept for the record. Two follow-ons were agreed with Mark on 2 October: a
time-boxed look at what holds the fitted pelvis back, and an honesty guard on the rotation route's
pelvis near impact.
**Parent documents:** `kinematic_sequence_design.md` (the sequence, its routes and its gates) and
`body_rotation_estimation.md` §7 (the two-camera rotation route this copies).

---

## Contents

1. Deliverables and definition of done
2. Why: what the sequence cannot see today
3. The approach being copied
4. The new route ladder
5. What the skeleton route measures, segment by segment
6. Uncertainty
7. Gates, placement and fallback
8. Pipeline order and plumbing
9. Catalogue, payload and chart
10. The metrics built on the sequence
11. Measure first: K0, then the gates
12. Build order and file list
13. What to expect on Mark's swings
14. Decisions needed from Mark

---

## 1. Deliverables and definition of done

**This document**, plus the **implementation** it describes. The implementation is done when:

1. The pelvis, thorax and lead-arm angular-speed series have a **two-camera skeleton rung**. It sits
   below the IMU and above today's camera routes. It only fills a segment no IMU measured.
2. When the skeleton fit is missing or refuses a segment, that segment **falls back** to today's
   route unchanged: the 2-D pair for the trunk, then face-on.
3. The skeleton rung's readings carry a σ, and that σ includes the assumed-camera term. The σ shrinks
   when a calibrated pair reaches skeleton3d, through the same flag the rotation route uses.
4. The catalogue, the chart's method glyph and the replay's "has DTL" check all recognise the new
   route. The `kinematicSequence` and `leadArmAngularSpeed` two-camera rungs go from PLANNED to live.
5. K0 and gates G1 to G4 (§11) have been run, and their numbers are written into
   `docs/research/data/kinematic_sequence/`.
6. 07-04 has been re-analysed in place, and Mark has judged the before/after **before** anything is
   committed.

The club and the `sequence_order` re-point are **not** part of done. They are §10 and §14.

---

## 2. Why: what the sequence cannot see today

The two-camera rotation route went live on 2 October (`BodyRotationTriangulatedStage`). It reads the
hip and shoulder lines' bearings off skeleton3d. The kinematic sequence never sees it, for three
reasons:

- **It has its own routes.** `segment_rates.cpp` resolves each segment in this order: IMU, then the
  face-on + DTL **separation pair**, then the face-on span.
- **It runs too early.** `KinematicSequenceStage` runs *before* `Skeleton3DStage` in both profiles.
- **The weak link is the pair.** On 07-04 s8 the pair's pelvis rate was valid on only 43 of 271
  samples, and invalid from impact on. *(Corrected by K0, 2 October: those 43 samples ARE the
  sequence's domain. The pair only computes Transition/Top → Impact, and within it the pair is valid
  on 100% of samples on all 15 swings. Continuity inside the domain is not what the skeleton adds;
  `skeleton_rate_k0_20261002.md` §1.)* Across 21 two-camera swings it placed **no** trunk node. The
  shoulder leg relabels near the top, so the thorax ring is off (`kinematic_sequence_design.md`
  §13.6).

The two routes also disagree. Over 07-04 the rotation route's pelvis and the sequence's pelvis rate
correlate at a median Spearman of only **0.37** (`rotation_triangulated/checks_20261001.md`). The
result is that the 3-D view, the rotation cards and `hip_stall` read one pelvis, and the sequence
chart reads another.

The skeleton route measures **exactly** the quantity `segment_rates.h` defines: the hip line's
bearing in the horizontal plane. The pair only approximates that bearing. Its inter-view angle sits at
75–84° instead of 90°, which biases its level (§13.9 item 2). The skeleton route is also signed and
continuous through square, and it has a σ per frame.

---

## 3. The approach being copied

`BodyRotationTriangulatedStage` (`wrist_analyzer.cpp`, `body_rotation.cpp`) set the pattern. Every
rule below carries over:

| Rule in the rotation route | Here |
|---|---|
| Runs after `Skeleton3DStage`; needs `skeleton3d.valid && dtlUsed` | Same |
| **An IMU reading always wins**, per segment | Same: the IMU rung is resolved first, as today |
| A frame counts only when both of a line's joints are tier ≥ `Constrained`, never just inferred | Same |
| A line shorter than `kTriMinBaselineM` (8 cm) in plan has no bearing | Same |
| Lead-relative sign: `leadIsLeft ? +1 : −1` | Same. Opening is positive (`segment_rates.h` §3.1) |
| σ = the fit's `sigmaM` through the line-angle formula, plus a camera-scale term | Same terms, applied differently: see §6 |
| `camerasCalibrated` switches 10% to 3% | Same flag, same constants |
| Catalogue quality **Estimated**, method word Triangulated | Same |
| No skeleton fit means nothing changes | Same: the swing falls through to today's routes |

One thing is new. The rotation route has nothing below it, so it either fills a segment or the
segment stays empty. The sequence **does** have routes below. So the skeleton rung must use the
pair's existing rule: a rung that can neither **place** a node nor **bound** one withdraws, and the
next rung runs (`segment_rates.cpp`, "neither a placement nor a bound is worse than today").

---

## 4. The new route ladder

Per series, best first. New rungs are in bold.

| Series | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| `pelvisAngularSpeed` | `pelvisImu` | `faceOn+dtl` (pair) | **`faceOn+dtl3d` (skeleton)** — *below the pair as built, K0 §11* | `faceOn` (span) |
| `thoraxAngularSpeed` | `thoraxImu` | `faceOn+dtl` (pair) | **`faceOn+dtl3d` (skeleton)** — *below the pair as built* | `faceOn` (span) |
| `leadArmAngularSpeed` | `leadArmImus` | **`faceOn+dtl3d` (skeleton)** | `faceOn` (de-projected) | |
| `clubAngularSpeed` | `clubSensorFused` | `faceOn+dtl` (fused plane), unchanged | `faceOnClub` | |

**Why the club is left alone.** It already has a two-camera rung. That rung de-projects the tracker's
dense face-on shaft angle through the plane the two cameras measured (§14 of the parent document). It
is graded, and it halved the club node's spread on 07-04. skeleton3d's shaft is a fitted
reconstruction of the same evidence with the body attached, and it carries known faults: the
clubhead departs 12–14 cm, the head misses the ball, and there is the depth branch (`swing3d`
annotations, `skeleton3d_shaft_branch_design.md`). Swapping a measured rung for a reconstructed one
before calibration would be a step down. K0 still reports the skeleton's club rate next to the fused
one, so the question can be revisited after calibration with numbers.

**Why a new route id, `faceOn+dtl3d`.** The node's `routeId` is what tells the strip, the replay and
the corpus reports which rung fired. If the skeleton and the pair shared `faceOn+dtl`, a
pair-vs-skeleton comparison could not be read off a document. The new id is chosen so that existing
matching keeps working:

- `sequenceMethodOf` (`chart_metrics.cpp`) matches on "dtl", so the new id gets the Triangulated glyph
  with no edit.
- The replay controller's `hasDtl` fallback compares **exactly** against `"faceOn+dtl"`
  (`shot_replay_controller.cpp` ~line 161). That has to become `startsWith("faceOn+dtl")`. It is the
  only GUI logic change.

---

## 5. What the skeleton route measures, segment by segment

### 5.1 One extractor, shared with the rotation route

Today the bearing extraction is a lambda inside `BodyRotationTriangulatedStage::run`. It moves into
a function both stages call:

```cpp
// body_rotation.h
struct TriangulatedLines {
    std::vector<int64_t> t_us;
    std::vector<double>  pelvisBearing, pelvisSigma;   // rad; NaN = unusable frame
    std::vector<double>  thoraxBearing, thoraxSigma;
    std::vector<QVector3D> leadArmDir;                  // unit shoulder→wrist; (0,0,0) = unusable
    std::vector<double>  leadArmSigma;                  // rad
    bool camerasCalibrated = false;
};
TriangulatedLines triangulatedLines(const skeleton3d::FitResult &fr, bool leadIsLeft);
```

`TriangulatedTurnInput` becomes a view of it. The rotation route's output **must not change**; G1
checks this byte for byte. One extractor guarantees that `pelvisRotationSigned` (and so `hip_stall`'s
P6→P7 rate) and `pelvisAngularSpeed` come from the same line in the same frames. Today they don't.

`segment_rates.cpp` stays pure and Qt-only. It gets a `const TriangulatedLines *` in
`SegmentRatesInputs` and never includes a skeleton3d header.

### 5.2 Pelvis and thorax

- **Observable:** the bearing ψ(t) of `LeftUpLeg → RightUpLeg` (pelvis) and `LeftArm → RightArm`
  (thorax) in the world XY plane. This is the vertical-axis rotation that §3.1 of the parent defines.
- **Rate:** unwrap ψ over usable frames, then `differentiate(at, windowUs, sign, magnitude=false)`.
  This is the one 25 ms derivative every route shares. `sign` is set so opening is positive. The
  sign is checked against `pelvisRotationSigned` on 07-04, which reads negative at the top and
  positive at the finish, so the downswing rate must be positive. G1 has a test for this.
- **No address reference.** A rate does not need one, because a constant offset differentiates to
  zero. This removes the rotation route's 100 ms address refusal and its reference σ from this route.
- **Gaps:** frames below `Constrained` are holes. They go through `validityBand(..., bandIsHole)` in
  the same way as the pair's guard holes.
- **Thorax observable, an open choice for K0.** The shoulder line runs through the upper-arm roots,
  so clavicle motion (free in the lean rig) can leak arm swing into it. The alternative is the
  **spine-chain yaw**: the root yaw plus the three spine twists, which is free of the clavicle. At
  the top the two agree (103° vs 105°), but a rate is more sensitive than a level. K0 computes both.
  The default is the shoulder line, to match `thoraxRotation`. It changes only if K0 shows the
  shoulder line is noisier or leads the spine through the downswing.

### 5.3 Lead arm

- **Observable:** the unit vector u(t) from `LeftArm` to `LeftHand` (from `RightArm` to `RightHand`
  for a left-hander). Both joints must be tier ≥ `Constrained`.
- **Its plane:** fit the plane of u over transition → impact by SVD (the normal is the smallest
  singular vector). The angle is α(t) = atan2(u·e₂, u·e₁) in that plane. This is the definition in
  `segment_rates.h`: the swing about the plane normal, with roll about the arm's own axis excluded by
  construction. The out-of-plane RMS is logged as a diagnostic.
- **Rate:** `differentiate(α, windowUs, 1.0, magnitude=true)`, the same as the face-on arm.
- **Why not the face-on arm through the fused plane?** The arm does not swing on the shaft's plane,
  and the parent document (§14) deliberately did not move it there. The skeleton gives the arm its
  own measured plane, which is what the face-on route only assumes.
- Hands are unreliable on this pose model, but the wrist joint is not a hand keypoint. It is also
  constrained by the grip term, so it is the best-observed distal joint the fit has.

### 5.4 The skeleton's own smoothing

The parent's rule (§6) is that every route is smoothed **once**, by the shared 25 ms derivative, so
that an IMU node and a camera node mean the same thing. skeleton3d breaks that rule:

- its unknowns are cubic B-splines, with knots every **10 ms** through top − 50 ms → impact + 60 ms;
- it has an acceleration prior there (`smoothAccRad` × `fastFactor`).

The knot spacing is finer than the derivative window, so the spline should not set the timing. But
the acceleration prior pulls toward constant velocity, and that could flatten or delay a peak. This
is not argued; K0 measures it (§11). If the prior moves the pelvis node by more than 10 ms on s8, the
fix is in skeleton3d, not here.

---

## 6. Uncertainty

The rotation route puts the camera-scale term into **every angle sample**, as `frac · |turn|`. That
is right for a level. It would be wrong for a rate, so here the same three terms are kept but placed
differently:

| Term | In the rotation route | Here |
|---|---|---|
| Fit σ: `hypot(σa, σb) / L` from `sigmaM` | per-sample angle σ | **per-sample angle σ.** It feeds the rate σ, the timing σ and placement, exactly as on every other route |
| Address reference σ | in quadrature | **dropped.** An offset has no rate |
| Camera scale `frac · |turn|` (10%, 3% calibrated) | per-sample angle σ | **a gain on the peak value only:** `peakSigmaDps = hypot(peakσ, frac · |peakDps|)` |

The reason for the last row: an assumed camera placement stretches every triangulated angle by about
the same factor. That is a **gain**, common to the whole swing. A gain scales the peak's size and does
not move its instant. If the gain went into the per-sample σ, it would inflate `tSigmaMs` on every
swing. More trunk nodes would read unresolved, but not because the timing was any less known.

One caveat, stated in the code: a wrong inter-view angle is a **skew**, not a pure gain. A skew
distorts ψ unevenly around the circle, and it can move a peak a little. The pair has the same
problem (γ 75–84°). K0 bounds the effect empirically: the pair and the skeleton see the same swing
through different geometry, so their node-time difference is an upper bound on how much geometry
moves timing on this rig. Calibration removes the skew.

`sigmaKind = Propagated` on every phase sample, as in the rotation route. The census row for these
four series (`uncertainty_census_design.md`) is updated to name these terms.

---

## 7. Gates, placement and fallback

New switches in `SegmentRatesConfig` and `pp_tuned_constants.h` (`sequence::`), each with its own
key, following the pair's pattern:

| Key | Default | Gates |
|---|---|---|
| `sequence.skel3d.enabled` | ON (pending G3 and Mark) | the whole rung |
| `sequence.skel3d.placement` | ON | the pelvis ring, which is the pair's §9 gate, kept separate |
| `sequence.skel3d.thoraxPlacement` | **OFF** | the thorax ring. The 103° top turn is suspect until calibration (§13 below) |
| `sequence.skel3d.leadArm` | ON (pending G2) | the arm rung |

These switches gate the **ring only**. Curves and bounds are always emitted, as on the pair.

What the rung reuses unchanged: `PlacementGate{ spikeGuard, endEdgeBound, bandIsHole }`, the
rigid-body rate limit (`pairMaxTurnDps`, 2000 °/s) as a sanity guard, and `maxPlaceSigmaMs`.

**Fallback, per segment:**

1. An IMU bound for the segment: the IMU, as today.
2. Else, a skeleton fit with `dtlUsed` and enough usable frames in the domain: the skeleton rung. If
   it gives **neither a placed node nor a bound**, it withdraws (channel and node), and its refusal
   reason is logged.
3. Else, the pair, exactly as today (its `!produced()` checks already make it a fallback).
4. Else, face-on.

"Enough usable frames" means at least 80% of the domain's frames usable. Below that, the gaps would
be wider than the derivative window, and a bound read across them would be an artefact.

**Past impact.** The parent decided not to search past impact (§13.4, decision: Mark). That stays.
But the skeleton fit runs to impact + 60 ms, so this route **could** say "peaked 35 ms after impact"
instead of "did not peak before impact". That is decision 14a below. The design does not assume it.

---

## 8. Pipeline order and plumbing

**Move `KinematicSequenceStage` to after `BodyRotationTriangulatedStage`** in `wristProfile()` and
`cameraKinematicsProfile()`. This is safe:

- `Skeleton3DStage` reads only `pose2d` and `poseDtl`. It reads nothing the sequence writes.
- `BodyRotationTriangulatedStage` reads only the skeleton and the rotation series.
- Nothing else in `wrist_analyzer.cpp` reads `kinematicSequence` or the four rate series.
  `AssessmentStage` and the stages after it still run after the sequence.

**One consequence:** the four rate series move **later** in `detail->series`. A byte diff of a
document will show them moved even when nothing changed. Every parity gate here compares by **key
and value**, not bytes. G1 states this.

**Plumbing in the stage:** each of the two stages calls `triangulatedLines(ctx.detail->skeleton3d,
…)` when `valid && dtlUsed`. The extraction is a single pass over the frames, so no cache is needed.
The sequence stage passes the result to `buildSegmentRates` in `SegmentRatesInputs::skel`.

**Re-analysis:** the sequence has no stage version, and skeleton3d recomputes on every re-analysis
(`rotation-triangulated` precedent). So the reuse path picks this up with no version bump. Confirm
this on s8 before the batch: the sequence must re-run on the reuse path, and `--full-window` must NOT
be passed.

---

## 9. Catalogue, payload and chart

`metric_catalogue_manifest.cpp`:

- `pelvisAngularSpeed`, `thoraxAngularSpeed`: insert `via("faceOn+dtl3d", RM::Triangulated,
  Estimated, { faceOnCamera, dtlCamera }, …)` **above** the pair rung. The summary reads: "the hip
  line's bearing from the two-camera skeleton fit, differentiated — the second camera's placement is
  assumed until it is calibrated".
- `leadArmAngularSpeed`: the PLANNED `faceOn+dtl` Direct rung becomes live `faceOn+dtl3d`
  **Estimated**. It is not Direct, for the same reason as the rotation rungs.
- `kinematicSequence`: the PLANNED `faceOn+dtl` rung becomes live, Estimated, with its summary
  rewritten to say what is built (trunk and arm from the skeleton, club from the fused plane).
- `clubAngularSpeed`: unchanged.
- `metric_catalogue_test.cpp`: the rung lists and PLANNED counts.

The payload is unchanged in shape: the nodes already carry `routeId` and `direct`. `routeSummary`
reads "estimated" when the trunk is skeleton-routed and nothing is IMU-routed, as today.

Chart and strip: the glyph comes free (§4). The node tooltip's route wording gets one case for
`faceOn+dtl3d` ("two-camera skeleton"), next to the pair's. `chart_metrics_test` and
`tst_chart_presets.qml` each get a skeleton-routed node fixture.

---

## 10. The metrics built on the sequence

**What changes automatically:** the four series, the `kinematicSequence` object and its strip
verdict. The diagnostics read none of the sequence nodes today (parent §11 item 3), so **no ledger
regrade** is needed for the core of this work.

**`sequence_order` is an open problem.** `sig_sequenceOrder` tests `order` over `m_pelvisRotPeak`
and `m_thoraxRotPeak`. Those are the **largest turn angles** between P4 and P7, not the peak rates.
Since the rotation route went live, those measures have a producer on every two-camera swing, so
the characteristic can now fire there. But it is grading the wrong thing: the largest
|turn| in P4→P7 is at the top, for both segments.

The fix is the one the parent planned (§11 item 3): re-point it at the sequence nodes. Add two
measures, `m_pelvisPeakBeforeImpactMs` and `m_thoraxPeakBeforeImpactMs`, read from the
`kinematicSequence` nodes with σ = `tSigmaMs`. An unplaced node counts as unmeasured, and a bound
counts as unmeasured too. Then point `sig_sequenceOrder` at them. On Mark's swings the thorax ring
is off and the trunk is still rising at impact, so this would make `sequence_order` **not
assessable** on 07-04. That is truthful, but visible.

This is **stage S7, Mark's call** (14d). It touches `core.json`, needs a ledger regrade, and
changes what the panel says. Before S7, check what `sequence_order` currently reports on 07-04, and
put that line in the K0 note.

---

## 11. Measure first: K0, then the gates

The work is tested on one swing first: **07-04 s8**, the one where the pair's pelvis was valid on
43 of 271 samples. Only after it holds does it go to all 15.

### K0: offline, before any C++

The data source is the persisted `analysis.skeleton3d` in the 07-04 `.ppsw` documents. These hold
the joints in mm and the per-joint tier, but **not** `sigmaM`. So K0 measures shape and timing, and
σ is first seen in G3. The script is `tools/swinglab/skeleton_rate_offline.py` (system python3,
numpy and scipy), modelled on `span_pair_offline.py`. Output goes to
`docs/research/data/kinematic_sequence/skeleton_rate_k0_<date>.md` plus a CSV.

For each of the 15 swings, K0 reports:

1. **Continuity:** the usable fraction over transition → impact for the pelvis and thorax, against
   the pair's fraction.
2. **The pelvis and thorax rates** (the same 25 ms local-quadratic derivative), against the stored
   pair curves: correlation, peak instant, peak value against Cheetham (477 / 727 °/s), and whether
   each still rises at impact.
3. **Thorax, two observables:** the shoulder line against the spine-chain yaw (§5.2).
4. **Lead arm:** the skeleton's in-plane arm node time against the face-on arm node in the document.
5. **Spline-prior sensitivity, s8 only:** re-fit with `skeleton3d.splineBasis=false` (per-frame
   unknowns) and compare the node times. This is the only K0 item that needs `swinglab_run`, and it
   runs on one swing.
6. **Club, for the record:** the skeleton's shaft rate node against the fused-plane club node.
7. **`sequence_order` today:** what it reports on each 07-04 swing.

**K0 decision rules, written before the data:**

| Check | Go | Stop |
|---|---|---|
| Pelvis usable fraction | ≥ 0.9 on ≥ 12/15 | below that, the skeleton is no more continuous than the pair, so stop and report |
| Arm node vs the face-on arm node | median \|Δt\| ≤ 15 ms | > 25 ms: the two well-observed arm routes disagree, which is a defect in one of them. Find it first |
| Spline prior (s8) | node shift ≤ 10 ms | > 10 ms: a skeleton3d issue. Raise it, and do not ship the trunk ring |
| Pelvis/thorax vs the pair | **reported, not gated.** There is no truth. Disagreement is expected, because the pair's geometry is biased | |

### The gates after building

- **G1, unit tests** (`segment_rates_test`, `body_rotation_test`): a synthetic skeleton line with a
  known peak gives a node within 2 ms; left-handed mirror; the rate sign agrees with
  `pelvisRotationSigned`; withdraw-and-fall-through to the pair; the camera-scale term changes
  `peakSigmaDps` and leaves `tSigmaMs` alone; the refactored extractor leaves the rotation route
  byte-identical.
- **G2, parity:** with `sequence.skel3d.enabled=false`, the payload is identical **by key and value**
  to today's, on 07-04 and on one face-on-only session. With it on, only the four rate series,
  `kinematicSequence` and timings differ.
- **G3, 07-04 re-analysed** (reuse path, Mac): `sequence_report.py` before and after; nodes placed and
  bounded per segment; the verdict histogram; per-node route ids; σ_t distributions. The run tree goes
  on the share (`/mnt/swingdata/scratch/`) and is kept until the note citing it is written.
- **G4, Mark judges** the chart on s8 and two other swings in the app. This happens **before**
  commit.

---

## 12. Build order and file list

| Stage | What | Files |
|---|---|---|
| K0 | Offline measurement; stop/go | `tools/swinglab/skeleton_rate_offline.py`, the K0 note |
| S1 | Shared extractor; the rotation stage uses it; byte-identical | `body_rotation.h/.cpp`, `wrist_analyzer.cpp`, `body_rotation_test.cpp` |
| S2 | Trunk skeleton rung, switches, σ placement, fallback | `segment_rates.h/.cpp`, `pp_tuned_constants.h`, `segment_rates_test.cpp` |
| S3 | Lead-arm skeleton rung | same |
| S4 | Stage reorder; inputs plumbed | `wrist_analyzer.cpp` (both profiles) |
| S5 | Catalogue, glyph wording, replay `hasDtl` | `metric_catalogue_manifest.cpp`, `metric_catalogue_test.cpp`, `chart_metrics.cpp` + test, `shot_replay_controller.cpp`, `tst_chart_presets.qml` |
| S6 | Docs | parent §4 table and a new §15; `body_rotation_estimation.md` §7.2 last bullet; feature-switches guide; census rows |
| S7 | *Mark's call:* re-point `sequence_order` | `core.json`, diagnostics tests, ledger regrade |

**Build economy:** one app build plus the affected tests, run once at the end through `ctest`:
`segment_rates_test`, `body_rotation_test`, `kinematic_sequence_test`, `metric_catalogue_test`,
`chart_metrics_test`, and the QML `tst_chart_presets`. **No commits** inside the plan. Everything is
committed at the end, after Mark has judged G3 and G4.

---

## 13. What to expect on Mark's swings

This is stated now, so the result is not read as a failure later.

- **The trunk will probably still read "did not peak before impact".** The pair found, and an
  offline extension past impact confirmed, that Mark's pelvis peaks about +39 ms and his thorax
  about +29 ms **after** impact (parent §13.4). A better instrument will not move a real peak. What
  the skeleton route adds is a **continuous** curve through impact, with a σ, from the same line as
  the rotation cards and `hip_stall`, instead of 43 usable samples out of 271. *(K0, 2 October: the skeleton's trunk rates dip at impact, and a pelvis ring would
  place a node 67 ms before impact on 13/15 swings. Just after impact the DTL hips overlap; the fit
  discounts them and coasts, and the coast reaches back before impact. The pair's pre-impact data
  is clean, so its "still rising" stands. The skeleton trunk rung is not built;
  `skeleton_rate_k0_20261002.md` §8.)*
- **The thorax magnitude will look high.** It inherits the 103° top turn, which is either real or
  the assumed DTL geometry. Calibration decides which (`body_rotation_estimation.md` §7.1). That is
  why its ring stays off.
- **The arm is the real gain.** A measured plane in place of the face-on ellipse, and a cross-check
  between two independent arm routes, which the sequence has never had.
- **No truth exists yet.** Only the Witmotion sacrum/sternum capture (parent §9 stage 2) can grade
  placement. This route makes that capture more useful, but it does not replace it.

---

## 14. Decisions needed from Mark

a. **Look past impact on the skeleton rung?** The fit covers impact + 60 ms, so "peaked 35 ms after
   impact" is measurable. That reverses the 20 September decision, for this rung only.
   *Recommendation:* not in this build. Report it in K0 and decide with the numbers.
b. **Leave the club on the fused-plane rung?** *Recommendation:* yes, until calibration (§4).
c. **Default ON after G3?** *Recommendation:* curves, bounds and the pelvis ring ON; thorax ring
   OFF; the arm ON if K0's arm check passes. The rung only replaces the pair where the skeleton fit
   exists, and it withdraws where it cannot do better.
d. **S7, re-point `sequence_order` at the nodes?** It is currently grading peak *angles* from the
   rotation route. The honest fix makes it unassessable on 07-04. *Recommendation:* yes, as its own
   step after this one, with a regrade.
e. **Order against calibration.** The 2 October note put this after the calibration-status session.
   This design does not need calibration: it consumes `camerasCalibrated` and works with it false.
   Doing it first means calibration later tightens two routes at once.
