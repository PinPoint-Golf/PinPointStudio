# Kinematic sequence — one measure, three routes, and a chart preset

**Audience**: developers working on the sequence producer, the metric catalogue and the session chart
**Code (to build)**: `src/Analysis/angular_rate.h`, `src/Analysis/segment_rates.{h,cpp}`, `KinematicSequenceStage` (`wrist_analyzer.cpp`), `KinematicSequenceProvider`, four new manifest descriptors, `PpSequenceStrip.qml`
**Status**: BUILT 2026-09-17 (uncommitted), §1–§10 as written plus two gates the corpus pass forced — see §12. The four series, the Sequence, the chart preset and the strip are live; the face-on pelvis / thorax NODES are emitted but never placed until the §9 truth capture (`sequence.faceOnTrunkPlacement`, default off); the pair route stays planned.
**Supersedes**: `metric_catalogue_developer_guide.md` Appendix B.6 ("kinematicSequence off the face-on rotation series — not pursued") — see §2 for what changed and what did not.

---

## Contents

1. [Deliverables and definition of done](#1-deliverables-and-definition-of-done)
2. [What this plan has to answer for](#2-what-this-plan-has-to-answer-for)
3. [The common measure](#3-the-common-measure)
4. [The route ladder](#4-the-route-ladder)
5. [The producers, route by route](#5-the-producers-route-by-route)
6. [One peak finder, one uncertainty](#6-one-peak-finder-one-uncertainty)
7. [Catalogue, payload and schema](#7-catalogue-payload-and-schema)
8. [The chart preset and the sequence strip](#8-the-chart-preset-and-the-sequence-strip)
9. [The gate: measure before promoting](#9-the-gate-measure-before-promoting)
10. [Build order and file list](#10-build-order-and-file-list)
11. [Open questions](#11-open-questions)
12. [What the corpus reported, and the two gates it forced](#12-what-the-corpus-reported-and-the-two-gates-it-forced)

---

## 1. Deliverables and definition of done

**Document**: this file, extended with a §12 "What the corpus and the truth capture reported" whose
every figure is reproducible from a named `swinglab_run` invocation against a named run root.

**Implementation**: the code in §10, listed new-or-modified, producing four angular-speed series and
one Sequence metric on every swing that carries the inputs, with a "Kinematic sequence" preset in
the session chart's METRICS combo.

**Done means**:

- The four series and the Sequence resolve on the 61-swing pose2 corpus from the face-on route
  alone, with propagated sigma, and the Sequence says `orderResolved: false` wherever the gaps
  are inside the combined timing uncertainty. A metric that charts and is silently wrong is not done.
- The IMU route is written against `SegmentStream::gyroDps` and unit-tested on a synthetic swing,
  even though no recorded swing carries a pelvis or thorax IMU today. Same for the pair route
  against synthetic two-view spans. The point of the common measure is that these drop in without
  touching the Sequence or the chart.
- §9's truth capture has been run and written up, **including if it fails**. A face-on pelvis or
  thorax node that the capture shows to be unresolvable ships as `unresolved`, not as a number. A
  skipped capture is incomplete.
- The KS preset appears in the chart on a swing that produced at least two of the four series, and
  disappears on one that did not, pinned by `tst_chart_presets.qml`.

---

## 2. What this plan has to answer for

Two documents already say not to do part of this, and the plan owes them an answer.

**Appendix B.6** rejected differentiating the face-on `pelvisRotation` / `thoraxRotation` series
because a cosine is flattest where the sequence needs resolution, and "the metric would resolve,
chart, and be wrong in a way no reader could see".

**The 2026-09-09 removal** (`body_rotation.cpp` §"THE CAMERA TIER IS GONE") then deleted the
foreshortening tier altogether: 2.1 % span jitter over 59 still address frames propagated to ±3.5°
at 20° of turn and ±13.9° at 5°; the address frame itself read 19° of turn; and the unsigned
magnitude convention folded the curve at square, which destroyed every derivative across impact
(`hip_stall` fired on 7 of 7 shots for that reason alone).

Both are right about the level series, and this plan does not resurrect it. What it does differently:

1. **The quantity is a rate, and it is signed.** The fold at square was a convention choice, not a
   property of the camera. §5.3 unfolds the sign across the downswing's span maximum, so the
   derivative is continuous through square. The level series stays deleted.
2. **The claim is timing, not magnitude.** A sequence node is *when* a segment peaked. The 19° address
   bias moves the operating point of the acos, which changes the curve's shape less than its value.
   Whether "less" is enough is a measurement, not an argument — §9.
3. **The uncertainty is propagated to the node and gates the verdict.** Every node carries a timing
   sigma. The Sequence declares its order only when adjacent gaps exceed the combined sigma. A
   pelvis node with ±30 ms cannot be ordered against a thorax node 19 ms away (Cheetham's pro gap),
   and the metric says so rather than picking one.
4. **The arm and club nodes do not have this problem at all.** They rotate in the swing plane, which
   the face-on camera sees as an ellipse whose axis ratio the shaft-plane conic already fits. Their
   in-plane rate is a de-projection, not a foreshortening inversion, and its sensitivity is bounded
   (§5.3). Half the sequence is sound from one camera today.
5. **"At least we try" is a route with a stated quality, not a lowered bar.** The face-on rung is
   `Estimated`, the Sequence resolves `Bridged` on it, and the reason names the method. The user
   who adds a pelvis IMU or a second camera gets a better node for the same metric on the same chart.

What has NOT changed: a single horizontal camera has zero sensitivity to axial turn rate at the
instant the segment is square to it. That is geometry (`ẇ = −w₀ sin θ · θ̇`, zero at θ = 0). The
thorax squares up close to where it peaks. §9 exists because the thorax node from face-on alone may
well be unresolvable, and the plan should find that out with an IMU on a chest rather than assert it
either way.

---

## 3. The common measure

Every route produces the same four signed angular-speed series and the same Sequence object. The
definitions are fixed here so that an IMU, a calibrated pair and a single camera are measuring the
same physical quantity and can be compared node for node.

### 3.1 Four series

| Key | Segment vector | Axis | Sign (+) | Unit |
|---|---|---|---|---|
| `pelvisAngularSpeed` | pelvis medio-lateral axis (hip line) | world vertical | turning toward the target (opening) | °/s |
| `thoraxAngularSpeed` | thorax medio-lateral axis (shoulder line) | world vertical | turning toward the target (opening) | °/s |
| `leadArmAngularSpeed` | lead shoulder → lead wrist | swing-plane normal | moving in the downswing sense | °/s |
| `clubAngularSpeed` | grip → clubhead (shaft) | swing-plane normal | moving in the downswing sense | °/s |

**Pelvis and thorax** are the *bearing rate* of the segment's medio-lateral axis projected into the
horizontal plane — the same construction `body_rotation.cpp`'s IMU tier already uses for the level,
differentiated, with the sign kept. This is what the Cheetham (2008) benchmark measured and what
`golf_swing_normative_reference.md` §2 seats: pelvis 477 ± 53, thorax 727 ± 61 °/s for professionals.

**Arm and club** are the angular speed of the segment's long axis about the swing-plane normal —
`plane_coupled_kinematic_sequence_proposal.md` §3.3's ω∥, "the honest version of the chart we
already show". Roll about the segment's own long axis (forearm pronation, shaft roll) is excluded by
construction on every route: an IMU takes `ω − (ω·â)â`, a camera never saw the roll to begin with.
Benchmark: arm 980 ± 68, club 2254 ± 68 °/s.

**Handedness** resolves once, through the anatomy vocabulary (lead/trail, never left/right), and the
sign suite in the unit test runs a second time mirrored with a left-handed golfer, as
`upper_body_metrics_test` does.

**Phase domain** is Transition → Impact for the pelvis, thorax and arm, and Transition → the P7 knot
of the shaft track for the club (the same `maskAfter` boundary `clubheadSpeed` uses). Samples
outside the domain are emitted with `valid = 0`, so the chart draws them dashed and no reducer reads
them. The downswing is where the sequence is defined; a backswing peak is not a node.

**Time base** is the shared timeline; every node is also reported as milliseconds before Impact
(P7 from the ladder), which is the benchmark's coordinate (pelvis 87 ± 19, thorax 68 ± 14,
arm 65 ± 8 ms before impact for professionals).

### 3.2 The Sequence object

```
KinematicSequence {
  nodes[]: {
    segment:      "pelvis" | "thorax" | "leadArm" | "club"
    tPeakUs:      int64      // absolute, shared timeline
    beforeImpactMs: double   // P7 − tPeak
    peakDps:      double     // signed, in the segment's convention above
    tSigmaMs:     double     // 1σ on the peak instant (§6)
    peakSigmaDps: double     // 1σ on the peak value
    routeId:      "segmentImu" | "clubSensor" | "faceOn+dtl" | "faceOn"
    quality:      "direct" | "estimated"
    state:        "placed" | "unresolved"   // unresolved: produced, but sigma too wide to place
  }
  order:          [segment…]  // placed nodes, ascending tPeak
  gapsMs:         [double…]   // between adjacent placed nodes
  gainsDps:       [double…]   // peak(n+1) − peak(n), the benchmark's "rotational speed gain"
  orderResolved:  bool        // every adjacent gap > k · sqrt(σₙ² + σₙ₊₁²), k = 1
  verdict:        "proximalToDistal" | "armBeforeThorax" | "other" | "unresolved"
  pelvisDecelerates: bool?    // pelvis rate falling at P7 (Cheetham's present/absent finding); null if unplaced
  routeSummary:   "direct" | "mixed" | "estimated"
}
```

`verdict` is the one categorical statement the normative reference says is robust
("professionals peak pelvis → thorax → arm → club; amateurs pelvis → arm → thorax") and it is
withheld whenever `orderResolved` is false. NR-03 applies to everything else: the strip reports
direction of difference, never "47 °/s below tour average".

A node from a route that produced a curve but could not place a peak within tolerance is emitted
as `unresolved` with its sigma, not dropped. The chart still draws the curve; the strip greys the chip.
Dropping it would hide the one thing the reader needs to know — that this route cannot see this segment.

---

## 4. The route ladder

Per **series**, best first. Each series is its own descriptor with its own ladder, because the pelvis
may be measured while the thorax is estimated, and one ladder per metric could not say so.

| Series | Rung 1 (Direct) | Rung 2 (Direct) | Rung 3 (Estimated) |
|---|---|---|---|
| `pelvisAngularSpeed` | `pelvisImu` — Inertial | `faceOn+dtl` — Triangulated, PLANNED until calibration lands | `faceOn` — Projected, **gated by §9** |
| `thoraxAngularSpeed` | `thoraxImu` — Inertial | `faceOn+dtl` — Triangulated, PLANNED | `faceOn` — Projected, **gated by §9** |
| `leadArmAngularSpeed` | `leadArmImus` — Inertial (LeadForearm; LeadUpperArm when bound) | `faceOn+dtl` — Triangulated, PLANNED | `faceOn` — Projected |
| `clubAngularSpeed` | `clubSensorFused` — Fused (Club role + track) | `faceOn+dtl` — Triangulated, PLANNED | `faceOnClub` — Projected |

`kinematicSequence` itself keeps three rungs that mirror the members (`segmentImus`, `faceOn+dtl`,
`faceOnClub`) so the directory can say what it needs, and gets the one `availability()` override the
providers header says must be justified: **the Sequence is composite, and its state is the weakest
placed node's** — `Measured` only when every placed node came from a Direct rung, `Bridged` when any
came from an Estimated one, `Unavailable` with fewer than two placed nodes. `routeSummary` carries
the same fact into the payload so the strip can say "mixed: pelvis measured, chest estimated".

Nothing reads `sessionType`. The gates are the stages' own `canRun()` facts: a bound role, a valid
shaft track, a face-on pose track, a conic fit.

---

## 5. The producers, route by route

All routes end in the same call: `angularRateSeries(t, angleRad, sigmaRad, cfg)` from §6, then
`placeNode(series, domain)`. What differs is how each route gets a signed angle (or a rate) with a
per-sample sigma.

### 5.1 IMU (`segment_rates.cpp: fromImu`)

`SegmentStream` already carries `gyroDps` in the anatomical frame on the 200 Hz grid and `qAnat`
into world. No differentiation is needed:

- **pelvis / thorax**: `ω_world = qAnat · gyro · qAnat⁻¹`; the series is `ω_world · ẑ`, signed by
  handedness so opening is positive. Sigma from the gyro noise density (a constant per device,
  `tuned::imu::gyroNoiseDps`), not propagated through a derivative.
- **lead arm**: with LeadForearm only, `ω_swing = ω − (ω·â)â` where `â` is the forearm long axis in
  world; `|ω_swing|` signed by the downswing sense (dot with the arm's own velocity direction from the
  Top). With LeadUpperArm bound, the upper arm is preferred: it is the segment Cheetham instrumented,
  and the forearm adds the wrist hinge.
- **club**: the Club role's gyro, perpendicular part, as above.

This is the reference route. It is written and unit-tested now against synthetic streams so the
§9 capture analyses on day one, even though `segmentRoleForSlot()` maps no placement slot to Pelvis or
Thorax and the session wizard is not touched (§9 uses a `swinglab_run --bind` override instead).

### 5.2 Calibrated pair (`fromPair`, PLANNED)

Both cameras are horizontal, so both see the horizontal plane edge-on — but at 90° to each other:
`w_faceOn = w₀ cos θ`, `w_dtl = w₀ sin θ`, and `θ = atan2(w_dtl/w₀ᵈ, w_faceOn/w₀ᶠ)` has uniform
sensitivity through square. This is the rung that fixes exactly the defect §2 names, without a full
3-D reconstruction. Arm and club come from the triangulated 3-D vectors. Stays PLANNED until
`camera_calibration_design.md`'s geometry stage lands; the function signature and its synthetic test
are written now so the rung is a drop-in.

### 5.3 Face-on (`fromFaceOn`)

**Arm and club — de-projected in-plane rate.** The shaft-plane conic (`shaft_plane.h`, already
fitted per swing over top→impact) gives the downswing ellipse's axis ratio `k = ratioMinorMajor` and
node bearing `ν = nodeDeg`. A vector at in-plane angle α images at angle ψ with
`tan(ψ − ν) = k · tan α`, so

```
α̇ = ψ̇ · k / ( cos²(ψ − ν) + k² sin²(ψ − ν) )
```

The correction factor lies in `[k, 1/k]` — for a typical iron plane seen face-on `k ≈ 0.85–0.9`, so
the magnitude is never off by more than ~15 % even before calibration, and the *timing* of the peak
is barely moved. ⚠ Uses `k` and `ν` directly; never the absolute inclination ι, which
`transition_plane_producer_brief.md` §9 bounds at a 64° uncalibrated bias. If the downswing conic
was rejected, fall back to `α̇ = ψ̇` with the factor's full range folded into sigma (±15 %) rather
than emit nothing.

- club: `ψ̇` is `ShaftSample2D::thetaDotRadS` — the tracker's RTS-smoothed angular velocity, already
  computed; sigma from `conf` via the posterior. The synth 240 Hz tier is used when it is the
  headline channel, exactly as `transitionPlaneDelta` selects it.
- lead arm: `ψ` is the image angle of lead shoulder → lead wrist from the smoothed pose, unwrapped;
  per-sample sigma from the two keypoints' posterior σ through the atan2 Jacobian. The arm's plane
  is taken as the club's; the bias this introduces is stated in §11.

**Pelvis and thorax — unfolded bearing from the span.** With `w₀` the robust address span
(`body_rotation`'s median over the address window, retained code) and `r(t) = clamp(w/w₀, 0, 1)`:

```
θ̃(t) = acos r(t)                         // unsigned, as before
t_sq  = argmax_{t ∈ [Transition, Finish]} w(t)   // the square-up instant: span maximum in the downswing
θ(t)  = +θ̃(t) for t < t_sq,  −θ̃(t) for t ≥ t_sq  // closed positive before square, open after
rate  = −dθ/dt                            // opening positive
σ_θ   = σ_w / (w₀ · max(sin θ̃, sin θ_floor))
```

`t_sq` is a phase-ladder inference, and §2 of `body_rotation_estimation.md` was right that a sign
inferred from *when* is not a measurement — which is why this rung is `Estimated`, why the level is
not re-published, and why the sigma floor is not a fudge: with `θ_floor = 5°` the reported σ_θ near
square is ~11× the span noise in radians (±13.9° at 2.1 %), and §6 turns that into a timing sigma the
verdict respects. `w₀` must be the square span; the 19° address bias §2 quotes goes straight into the
operating point, and §9's capture measures what that does to the peak *time*.

Two refinements that cost little and are worth carrying from the start:

- **Rate from the span rate, not from the angle.** `ẇ = −w₀ sin θ · θ̇` ⇒ `θ̇ = −ẇ / (w₀ sin θ)`.
  Differentiating the smooth span and dividing once is better conditioned than differentiating an
  acos, and the division's singularity is the same `sin θ_floor` guard.
- **The thorax has more points than the pelvis.** Shoulder span plus the neck-to-thorax-centre offset
  and the elbow separation are all foreshortened by the same turn; a joint estimate over three spans
  with independent jitter roughly halves σ_w. Cheap, and the thorax is the node most at risk.

---

## 6. One peak finder, one uncertainty

`angular_rate.h` — header-only, pure, no Qt-GUI, in the `series_reduce.h` mould — is the single code
path every route ends in, so a node's timing and sigma mean the same thing whichever sensor
produced the curve.

- **Differentiation** is a Savitzky–Golay first derivative with the window fixed in **time**
  (`tuned::sequence::kDerivWindowMs`, start at 25 ms), not in samples, so a 120 fps camera, the
  240 Hz synth tier and the 200 Hz IMU grid are smoothed alike. Routes that already hold a rate (IMU
  gyro, `thetaDotRadS`) are smoothed with the same window's zeroth-order kernel so the peaks are
  comparable.
- **Peak placement** is `reduceExtremum` over the phase domain — the same reducer the chart's PEAK
  tile and the diagnostics engine use, so the strip and the chart agree to display precision.
- **Timing sigma** from the curvature at the peak: with `σ_r` the rate noise after smoothing and
  `r̈` the second derivative at the peak, `σ_t = sqrt(2 σ_r / |r̈|)` (the half-width over which the
  curve is within one σ_r of its maximum). A broad pelvis peak on a noisy route gets a wide σ_t; a
  sharp club peak a narrow one. `σ_r` is propagated per route from the per-sample sigma through the
  SG kernel's variance factor — it is not a constant.
- **Unresolved** when `σ_t > tuned::sequence::kMaxPlaceSigmaMs` (start at 40 ms, twice the pro
  pelvis SD) or when the peak sits at a domain edge (`partial`).

Unit-tested on synthetic curves with known peaks and injected noise: the reported σ_t must cover the
realised timing error at the 68 % level across 1000 draws. A sigma that does not cover is a lie with
decimals, and the test exists so the number on the strip has been checked against something.

---

## 7. Catalogue, payload and schema

**Manifest**: four new `TimeSeries` descriptors in group "Tempo & sequence", each with the §4 ladder,
`.presets = { "Kinematic sequence" }`, coach-facing `description` / `howToRead` written to NR-03
(direction of difference, order and consistency; no absolute-gap language), and sign entries added to
`pinpoint_sign_conventions.md`. `kinematicSequence` loses the single PLANNED rung and gains the three
mirrored ones, none planned. Its `howToRead` currently quotes "pelvis ~480, thorax ~605 and lead-arm
~1310 °/s", which is not what `golf_swing_normative_reference.md` §2 seats (727 and 980); it is
rewritten to the reference's figures and to NR-03's direction-of-difference language. `clubheadPeakLead` stays as it is: it is a linear-speed timing and a
useful cross-check on the club node (§9).

**Provider**: `KinematicSequenceProvider` claims the four series keys (no override) and the Sequence
key (with the §4 override). `metric_providers.h`'s comment gets the justification.

**Stage**: `KinematicSequenceStage` after `ShaftPlaneStage` (needs `k`, `ν`) and after
`BodyRotationStage` (shares the address-span reference and the fused streams). `canRun()` is
data-gated; `skipReason()` names which input is missing.

**Payload**: the four series go into `analysis.metrics[]` unchanged in shape — `sigma`, `valid`,
`phaseSamples` all exist. The Sequence is a new additive object `analysis.kinematicSequence` (§3.2),
documented in `swing_json_schema.md` with a dated history row, no `/3` bump. `QmlPayload` converts
it once with the rest of the detail (the deep-copy fix stands) and exposes
`analysisDetail.kinematicSequence`.

**swinglab**: the ledger gains the node columns and `orderResolved`; `regrade_ledger` reads them.
A `--bind <slot>=<Role>` override on `swinglab_run` maps a recorded IMU slot to Pelvis / Thorax /
Club at analysis time, for §9 and for any future capture that predates a placement UX.

---

## 8. The chart preset and the sequence strip

**The preset is free.** `ChartMetrics::seriesGroups()` already builds cross-cutting presets from
`MetricDescriptor::presets` and offers one when at least two members are plottable on the swing —
the Plumb Bob mechanism. Declaring `"Kinematic sequence"` on the four descriptors puts it in the
METRICS combo, in manifest order, on any swing with two or more of the curves, and removes it
honestly on a swing with fewer. The four share a unit, so overlay mode draws them on one °/s axis,
which is how the benchmark chart is drawn (the club dwarfs the pelvis by design; split mode remains
one click away). `showSigmaBand` is the existing ±σ ribbon and is what makes the face-on pelvis curve
readable for what it is.

**The strip is the one new surface.** `PpSequenceStrip.qml`, shown under the plot only while the
preset is "Kinematic sequence": four chips in `order`, each labelled with the segment, its
`beforeImpactMs`, and its route glyph (the directory's method glyphs — inertial, triangulated,
projected), with the gap in ms drawn between adjacent chips. An `unresolved` chip is greyed and
sits at the end with "not placed from this view". The verdict line reads one of:
"pelvis → chest → arm → club", "arm peaks before chest", or "order not resolved at this fidelity —
add a pelvis IMU or a second camera" — the upgrade string the availability already carries. No
JavaScript logic: `ChartMetrics` gets one `sequenceRows(kinematicSequence)` invokable that returns
the display rows, in C++, unit-tested.

Nothing in the strip is a grade. The measure and corridor for `sequence_order` already exist over
`m_pelvisRotPeak` / `m_thoraxRotPeak`; re-pointing them at the new nodes and adding the gain and
consistency measures is §11's item, after the gate.

**Probe, not screenshot.** The preset and strip are verified with `--probe-qml` offscreen on a
fixture swing that carries the four series, and `tst_chart_presets.qml` gains the case "four
sequence curves ⇒ the preset is offered; one ⇒ it is not".

---

## 9. The gate: measure before promoting

The face-on pelvis and thorax rungs ship `Estimated` only if a measurement says their timing sigma
is honest and useful. The IMU route is the truth, and this product owns the sensors.

**Stage 0 — synthetic.** A 3-D swing with known segment peaks, rendered to (a) IMU gyro streams,
(b) face-on 2-D keypoints and a shaft track with pose-like jitter, (c) two-view spans. All three routes
through the one peak finder; assert the club and arm nodes agree within 5 ms across routes and the
pelvis and thorax nodes' σ_t covers their error. This is the test that pins the common measure.

**Stage 1 — corpus.** The 61 pose2 swings via `--pose` (~6 min per configuration, judged by metric
COUNT first). Report: how many swings place each node; the distribution of `beforeImpactMs` per
node against Cheetham's; the club node against `clubheadPeakLead` (linear vs angular peak should sit
within ~10 ms); `orderResolved` rate. No truth here — this is yield and plausibility, and a
pelvis-node distribution centred on 87 ms with a 40 ms spread is a very different finding from one
centred on 20 ms.

**Stage 2 — truth capture.** Two Witmotion WT901 units, one on the belt at the sacrum, one on the
sternum, ~20 swings on the face-on rig, no HackMotion worn. Analysed twice through `swinglab_run`:
once with `--bind` mapping the two slots to Pelvis and Thorax (the IMU route places the nodes), once
without (face-on only). Per swing, per segment: `Δt = t_faceOn − t_imu`, and whether `|Δt| ≤ σ_t`.

**Decision rule**, per segment, stated before the data is in:

| Result | The face-on rung |
|---|---|
| median \|Δt\| ≤ 20 ms **and** σ_t covers Δt at ≥ 68 % | ships `Estimated`; nodes `placed` |
| σ_t covers Δt but the median is wide | ships `Estimated`; the verdict will mostly read "unresolved", which is the truthful output |
| σ_t does **not** cover Δt | the rung stays PLANNED with the numbers attached, and the node is emitted `unresolved` with the *measured* σ substituted — the sigma model is the defect, and it is fixed before anything ships |

The thorax is expected to land in the third row. If it does, that is a result, written up, and the
sequence still ships with a measured thorax the day a chest IMU is bound.

**Not a gate, but measured alongside**: the address-squareness bias. Stage 2's IMU gives the true
address bearing, so the 19° reading has a truth to be compared with for the first time — the single
highest-value open question in `body_rotation_estimation.md` §6.

---

## 10. Build order and file list

In the order the dependencies run; each step is buildable and testable on its own.

1. **`src/Analysis/angular_rate.h`** (new) — SG derivative in time, peak placement over a domain via
   `reduceExtremum`, timing sigma, `unresolved` rule. `angular_rate_test.cpp` (new): the 1000-draw
   coverage test.
2. **`src/Analysis/kinematic_sequence.h`** (modify) — `SeqNode` gains `tSigmaMs`, `peakSigmaDps`,
   `routeId`, `quality`, `state`; `kinematicSequenceNodes` gains `orderResolved`, gaps, gains,
   `verdict`, `pelvisDecelerates`. `kinematic_sequence_test.cpp` extended: unresolved node ordering,
   verdict withheld inside sigma, mirrored handedness.
3. **`src/Analysis/segment_rates.{h,cpp}`** (new) — `fromImu`, `fromFaceOn`, `fromPair` (signature +
   synthetic test only), all returning `MetricSeries` with sigma and valid. `segment_rates_test.cpp`
   (new): Stage 0 of §9. `tuned::sequence::*` constants in `pp_tuned_constants.h`, SwingLab-sweepable
   as `sequence.*`.
4. **`src/Analysis/wrist_analyzer.cpp`** — `KinematicSequenceStage`, appended after `ShaftPlaneStage`.
5. **`src/Metrics/metric_catalogue_manifest.cpp`** — four descriptors with presets and ladders;
   `kinematicSequence` rungs rewritten; header comment counts updated (PRODUCED +5, PLANNED −1).
   `metric_catalogue_test.cpp` extended. **`metric_providers.{h,cpp}`** — `KinematicSequenceProvider`.
6. **Payload**: swing exporter + `QmlPayload` for `analysis.kinematicSequence`;
   `docs/reference/swing_json_schema.md` history row; `swing_export_developer_guide.md` note.
7. **`src/Gui/review/chart_metrics.{h,cpp}`** — `sequenceRows()`; `chart_metrics_test.cpp`.
   **`PpSequenceStrip.qml`** (new), mounted in `PpMetricChart.qml` behind the preset name.
   **`tst_chart_presets.qml`** — the two-vs-one case.
8. **`tools/swinglab/src/swinglab_run.cpp`** — `--bind`; ledger columns; `regrade_ledger`.
9. **Docs**: `pinpoint_sign_conventions.md` (four rows), developer guide B.6 rewritten to point here
   and Appendix A's `kinematicSequence` row updated, `body_rotation_estimation.md` §6.4 closed with a
   pointer, this file's §12 written from the §9 runs.

Builds: one app build after step 7, one test build at the end (`ctest`, the affected targets only).
Nothing is committed until Mark has seen the §9 results.

---

## 11. Open questions

1. **The arm's plane is not the club's.** The lead arm swings on a plane a few degrees steeper than
   the shaft's, and §5.3 de-projects it with the shaft's `k`, `ν`. The error is bounded by the same
   `[k, 1/k]` argument and mostly hits magnitude; if Stage 1 shows the arm node drifting against the
   IMU forearm on wrist-motion swings, fit a second conic to the wrist path.
2. **Which arm segment is "the arm".** Cheetham instrumented the upper arm; the face-on route reads
   shoulder → wrist, which includes elbow flexion. Stage 2 with a LeadUpperArm binding answers how
   much that moves the node. Start with shoulder → wrist because it is the more robust keypoint pair.
3. **Corridors.** `m_pelvisRotPeak` / `m_thoraxRotPeak` carry no corridor and `sequence_order` is
   the only measure over them. After the gate: re-point `sequence_order` at the node order, and author
   the gain measures and the cross-shot timing-consistency measure (the discriminator the benchmark
   found — amateur SDs at least double the professionals'), which needs Appendix B.8's `Range`
   reducer.
4. **Per-tier corridors.** A camera-estimated node and an IMU-measured one against one corridor is
   the compromise `body_rotation_estimation.md` §6.2 already names. Not before a single shot carries
   a trunk IMU; Stage 2 will be that shot.
5. **The pair route's `w₀` per camera.** Two address spans, two biases. The calibration thread's
   extrinsics may let the bearing be read from triangulated hip points directly and skip the spans;
   decide when the geometry stage exists.

---

## 12. What the corpus reported, and the two gates it forced

**Run**: the 61 pose2 swings, pinned pose, production defaults, one configuration each:

```
build/tools-parity-ninja/swinglab_run <swings>/<session>/<swing_NNNN> --out <root>/<session>__<swing_NNNN> \
    --pose /mnt/swingdata/corpus/pose2/<session>__<swing_NNNN>.json
python3 tools/swinglab/sequence_report.py --root <root> --tag "<label>"
```

Two passes, ~6 min each. The per-swing rows are in the repo:
`docs/research/data/kinematic_sequence/corpus_20260917_ungated.csv` and `…_gated.csv`. The run
trees were deleted once the CSVs were in.

### 12.1 First pass — everything placed by σ alone

| Segment | produced | placed | peak before impact, ms (median [IQR]) | σt ms (median) | peak °/s (median) | Cheetham 2008 pros |
|---|---|---|---|---|---|---|
| Pelvis | 61 | 47 | 34 [0, 87] | 32 | 476 | 87 ± 19 · 477 ± 53 |
| Thorax | 61 | 58 | 141 [80, 173] | 14 | 819 | 68 ± 14 · 727 ± 61 |
| Lead arm | 61 | 59 | 94 [67, 132] | 19 | 697 | 65 ± 8 · 980 ± 68 |
| Club | 61 | 59 | 63 [46, 68] | 10 | 1869 | — · 2254 ± 68 |

Order resolved on 16 of 61; the commonest placed order was thorax → arm → club → pelvis (13).

**The thorax number is a spike, not a peak.** Read against one swing's curves: the shoulder span
sits ABOVE its address width for most of the downswing (the golfer is not square at address, and
the 2026-09-09 removal's "impact span at 95 % of address" is the same effect), so the ratio clamps
to 1, the unfolded angle sits at 0, and where the span dips below the address width the acos has
infinite slope — the rate swings −1900 … +3500 °/s inside 30 ms. A spike has enormous curvature,
and the timing σ formula (§6) reads high curvature as a confidently located peak: ±7 ms on garbage.
The pelvis is the same mechanism with a wider σ (its span is shorter). This is the failure §2 was
worried about, arriving through the σ model rather than around it.

**The club number on a broken track is the synth tier's straight line.** Swing 0003 of the 06-11
session carries a clubhead speed of 23 mph at impact — a broken track — and its synth shaft angle
is a constant −560 °/s between anchors; the node landed at the domain start with ±12 ms.

### 12.2 Second pass — the two gates

1. **Near square is invalid, not zero** (`spanTurnTrack`): a span sample at or above
   `cos(asin(sinFloor))` of the address width is dropped from the angle track rather than clamped,
   so the curve shows a gap where the estimator has no sensitivity and no spike is manufactured at
   its edge.
2. **Face-on trunk nodes are not placed** (`sequence.faceOnTrunkPlacement`, default false): the
   pelvis and thorax series are produced and charted, their nodes are emitted `unresolved` with the
   σ attached, and nothing orders them. This IS the §9 decision rule applied before the capture
   exists: without a truth the rung cannot claim a node. Flip the knob per the §9 table.
3. **A club node needs a credible track** (`sequence.minCredibleClubMph`, 40): the same track's
   clubhead speed at impact below the floor leaves the club node unplaced.

| Segment | produced | placed | peak before impact, ms (median [IQR]) | σt ms (median) | peak °/s (median) | Cheetham 2008 pros |
|---|---|---|---|---|---|---|
| Pelvis | 61 | 0 | — | 51 | — | 87 ± 19 · 477 ± 53 |
| Thorax | 61 | 0 | — | 19 | — | 68 ± 14 · 727 ± 61 |
| Lead arm | 61 | 59 | 94 [67, 132] | 19 | 697 | 65 ± 8 · 980 ± 68 |
| Club | 61 | 57 | 63 [45, 68] | 10 | 1887 | — · 2254 ± 68 |

Order resolved on 41 of 61 (67 %); verdicts: partial 38, unresolved 20, other 3. Placed orders:
arm → club 51, club → arm 4, single node 6. Route summary: estimated on all 61.

**Club node against the linear peak** (`clubheadPeakLead`, 57 swings with both): the angular peak
leads the linear one by 21 ms [8, 58]. The two read the same track two ways — the shaft's angular
rate about the plane and the head's linear speed — and on this golfer both peak before the ball;
the median club node at 63 ms before impact is consistent with the `deceleration` characteristic
this corpus already carries. Cheetham's professionals peak the club AT impact; the corpus is one
amateur's swings and the direction of difference is the right reading (NR-03).

**The arm** lands at 94 ms [67, 132] before impact at 697 °/s — earlier and slower than the
benchmark's 65 ms / 980, in the amateur direction on both counts. Whether that is the golfer or the
face-on de-projection (§11 item 1, the arm's plane taken as the club's) is what the §9 capture with
a LeadUpperArm binding answers.

**The trunk σ's** — pelvis 51 ms, thorax 19 ms median — are the honest cost of a 120 fps single view
through a 25 ms window. On the noiseless synthetic the same model gives ~85 ms for the pelvis
(segment_rates_test §2), so the threshold of 40 ms is not what is holding these back; the σ model
is, and it is the thing the truth capture calibrates.

### 12.3 What this changes in the plan

- §9's decision table stands, with the default state being the third row for both trunk segments.
- §6's σ_t needs a guard the design did not have: curvature from a non-linearity is not curvature
  from a peak. Gate 1 removes the specific source; a general spike detector (rate excursions beyond
  a physical ceiling, or a residual-roughness σ) is a §11 item for the truth capture to size.
- §5.3's optional multi-span thorax and span-rate formulation are untried; neither addresses the
  address-width bias that is the real problem, and the pair route (§5.2) does.
