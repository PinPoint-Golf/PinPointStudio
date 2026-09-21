# Kinematic sequence — one measure, three routes, and a chart preset

**Audience**: developers working on the sequence producer, the metric catalogue and the session chart
**Code (to build)**: `src/Analysis/angular_rate.h`, `src/Analysis/segment_rates.{h,cpp}`, `KinematicSequenceStage` (`wrist_analyzer.cpp`), `KinematicSequenceProvider`, four new manifest descriptors, `PpSequenceStrip.qml`
**Status**: BUILT 2026-09-17, §1–§10 as written plus the gates the corpus pass forced — see §12. The four series, the Sequence, the chart preset and the strip are live. Since 2026-09-18 (§12.4) the face-on pelvis / thorax nodes are placed only where the camera can see the rate — the SIGHTED BAND, |turn| ≥ 20° — and emitted as a BOUND ("peaked no earlier than N ms before impact") where it cannot; the span reference is the swing's own square-up width, not the address span. The pair route stays planned; the §9 truth capture still calibrates the sighted nodes' σ.
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
    — 12.4 [the reference was the fault, and the sighted band](#124-the-reference-was-the-fault-and-what-a-single-camera-can-still-claim)
13. [The down-the-line pair, uncalibrated — built, and what it found (2026-09-20)](#13-the-down-the-line-pair-uncalibrated--built-and-what-it-found-2026-09-20)

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
| `pelvisAngularSpeed` | `pelvisImu` — Inertial | `faceOn+dtl` — Triangulated, **LIVE 2026-09-20, Estimated, uncalibrated** | `faceOn` — Projected, **gated by §9** |
| `thoraxAngularSpeed` | `thoraxImu` — Inertial | `faceOn+dtl` — Triangulated, **LIVE 2026-09-20, Estimated, uncalibrated**; ring OFF, bound only | `faceOn` — Projected, **gated by §9** |
| `leadArmAngularSpeed` | `leadArmImus` — Inertial (LeadForearm; LeadUpperArm when bound) | `faceOn+dtl` — Triangulated, PLANNED | `faceOn` — Projected |
| `clubAngularSpeed` | `clubSensorFused` — Fused (Club role + track) | `faceOn+dtl` — the face-on angle de-projected through the FUSED two-camera downswing plane, BUILT 2026-09-21 (§14) | `faceOnClub` — Projected |

> **Amended 2026-09-20.** The two trunk rungs are live. They are **Estimated, not Direct**, because
> the pair is uncalibrated — the pixel-scale ratio is measured off the body's vertical extent, not
> from a board — and `Triangulated` is kept as the method word only because it is the one enum value
> that says "two views"; `metric_catalogue_manifest.cpp` corrects it in the rung's own summary
> ("atan2 of the pair, uncalibrated"). The rung sits **below the IMU and above the single-view
> estimate**, which is the measured order (§13.3). The thorax rung produces its curve and its bound
> but may not place a node — `sequence.pairTrunk.thoraxPlacement` is OFF, for the reason in §13.6.
> The arm and club rows are untouched: the pair route is pelvis and thorax only.

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

> **Corrected in place, 2026-09-20.** The last sentence was **false when it was written and stayed
> false for three days**: there was no `fromPair`, no stub, no signature and no synthetic test in
> `segment_rates.{h,cpp}` or `segment_rates_test.cpp` until today. Nothing had been written. It is
> recorded rather than deleted because the class of error matters — a plan that says a thing is
> "written now so the rung is a drop-in" is a claim about the repository, and a reader has no way
> to tell it from the claims around it that were true.
>
> **What exists today is the UNCALIBRATED pair, and its observable is not the one above.** A 2-D
> span distance `w` absorbs the segment's out-of-horizontal tilt, which for the shoulders in the
> downswing is large. The route reads the **signed horizontal separation** in each view instead —
> `d_fo = s_F·W·cos τ·cos ψ`, `d_dtl = s_D·W·cos τ·sin ψ` — so `W` and the tilt `τ` cancel in the
> ratio and `ψ = atan2(d_dtl / r, d_fo)` needs no reference width, no square-up inference and no
> sign unfold. `r = s_D/s_F` is measured, not calibrated, from the body's vertical extent at
> address, which both level cameras see undistorted by the turn. §13 is what that route was built
> from and what it found; the calibrated form above is still PLANNED and is the stronger version of
> the same idea.

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

**Pelvis and thorax — unfolded bearing from the span.** With `w₀` the reference span and
`r(t) = clamp(w/w₀, 0, 1)`. ⚠ As written on 2026-09-17 `w₀` was the address-window median; §12.4
replaced it with the span at the square-up instant `t_sq` below (floored by the address median),
because the address span sat below the downswing maximum on every corpus swing and the acos
clamped through the whole downswing. The formulae are otherwise unchanged:

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

> **2026-09-18 — the sequence is drawn ON the plot.** The chip row this section describes was
> replaced: it restated the four curves' peaks in a row of boxes at a different scale under the
> plot, and read oddly beside them. `ChartMetrics.sequenceOverlay` turns the Sequence into plot
> geometry and `PpChartPlot.sequence` draws it in the series' own colours — a hollow ring at
> EVERY node's peak wherever the route found it, full weight when placed and dimmed when not,
> with its timing σ as a whisker and "Lead arm −87 ms" above (below when the peak is at the top
> of the range), and the lead between consecutive placed peaks as a bracket along the top of the
> combined view. A split facet draws only its own segment. Nothing is written about a bound or
> the blind band on the chart: the curve's own style already says how far to trust it, and a
> reader of this chart needs no second telling (Mark, same day — a first cut drew the §12.4
> bounds as labelled spans, and it was noise). The bound stays in the node, the JSON and the
> report. `PpSequenceStrip` is now one line: the chain with its leads ("Lead arm −87 ms → Club
> −4 ms (+83 ms)") and the verdict. Probe: `tools/probes/ks_overlay_chart.qml`.


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
   decide when the geometry stage exists. *(§12.4: the biases are now read off each view's own
   square-up maximum, and the uncalibrated ellipse pairing needs no `w₀` at all.)*
6. ~~**The DTL span, uncalibrated**~~ (§12.4 item 6): run pose on the corpus's Down-the-Line clips,
   pair the spans on the shared clock, and re-read the trunk nodes where face-on is blind. Before
   the truth capture if the DTL far-hip confidence allows it; the capture then grades both.
   **DONE 2026-09-20 — §13.** Offline measurement first
   (`docs/research/data/kinematic_sequence/pair_span_turn_20260920.md`), then the C++ route. Three
   corrections the item itself got wrong: the observable is the signed **horizontal separation**,
   not the span; the far hip is *not* the weak keypoint (the face-on **shoulders** are); and the
   pair does **not** re-read the trunk nodes where face-on is blind, because on this golfer the
   trunk does not peak inside the domain at all. What it produces instead is a much tighter bound.

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
  address-width bias that is the real problem, and the pair route (§5.2) does. *(§12.4, the next
  day: the address-width bias was addressed on its own, and the real limit turned out to be a
  different one.)*

### 12.4 The reference was the fault, and what a single camera can still claim

**Written 2026-09-18.** The 17 Sept pass read the trunk spikes as the failure §2 had feared —
the cosine's flatness near square — and closed the trunk nodes pending the truth capture. An
offline pass the next day over the same 61 swings, from the pose2 keypoints alone
(`tools/swinglab/span_turn_offline.py`, rows in
`docs/research/data/kinematic_sequence/offline_span_turn_20260918.csv`), separated two things
that pass had run together.

**1. The address span is not the square span, on every swing.** The widest a span images is the
closest the line came to square to the camera, and that was the downswing, not address:

| Segment | downswing max span / address span | implied turn at address | turn at the top, max reference | turn at impact, max reference |
|---|---|---|---|---|
| Pelvis | 1.036 [1.023, 1.057] | 15° [12, 19] | 41° [40, 47] | 12° [7, 18] |
| Thorax | 1.056 [1.044, 1.067] | 19° [17, 20] | 63° [59, 69] | 3° [2, 6] |

That is the 2026-09-09 "address reads 19°" finding on all six sessions (thorax ~19° in each,
pelvis 10–22° by session — the golfer set open at the shoulders plus some setup, not one camera
yaw). With the address reference the ratio clamps at 1 for most of the downswing and the acos
spikes where the span dips below it — §12.1's "spike, not a peak" was this, not the flat cosine.
With the square-up span as reference the magnitudes read as a golfer: pelvis 41° closed at the
top, thorax 63°. **Built**: `spanTurnTrack` now reads `w₀` off a local median at the square-up
instant it already located for the sign unfold, floored by the address median.

**2. The flat cosine is still the limit, and it is geometry, not pose accuracy.** Pose jitter on a
still golfer is 3.5 px on an 83 px hip span (4.1 %) and 2.7 px on a 130 px shoulder span (2.1 %) —
±4° at 30° of turn, which is fine. But a span's rate of change with turn goes as sin θ, and at
square it is zero whatever measures the span; markers on the hips would show the same. A
monotone model fit (`w = w₀ cos θ(t)`, rate ≥ 0, sign from timing — the §5.3 idea done properly)
put the pelvis "peak" on the pelvis's own square-up instant on 47 of 60 swings (median offset
0 ms), and the thorax profile rose monotonically into impact. Both are the estimator: near square
the fitted rate comes only from the curvature of the span maximum, and that maximum is sharpened
by the hips moving TOWARD the camera — the hip centre rises 19 px over the last 100 ms into
impact (early extension), and 3–5 cm closer at 1.5–2 m widens the span by 2–4 %, the size of the
whole address-to-max signal. One view cannot separate that from squaring up. The fit's bootstrap
σ_t (11–13 ms) is fit stability, not truth, and would have passed §9's first row: the "confident
on garbage" trap of §12.1 by another route. It is not used.

**3. What a single camera CAN claim: the peak, when it happens in sight.** Cheetham's
professionals peak the pelvis 87 ms and the thorax 68 ms before impact, still 20–30° closed,
where sin θ ≥ 0.34 and the slope carries the rate. So a pro-shaped sequence is visible from
face-on; the amateur pattern that squares up at impact is not, and the honest output for it is a
bound, not a node. **Built**: the peak is searched over the SIGHTED samples only (|turn| ≥
`sequence.sightedTurnDeg`, 20°); a peak that sits within one derivative window of the instant
the segment entered the blind band was still rising when the view went blind, and the node is
emitted unplaced with `peakNoEarlierThanMs` = the edge of sight (the symmetric `peakNoLaterThanMs`
covers a peak at the band's exit). The strip prints the bound ("peaked after −N ms, out of this
camera's sight"). `sequence.faceOnTrunkPlacement` is now ON, meaning "may place in the sighted
band"; OFF still reads every trunk node unresolved. On the synthetic (segment_rates_test §8) the
45°-start pelvis peaks 15° closed and is bounded at 92 ms with the 87 ms truth inside; an 80°-start
pelvis with 84° of turn peaks 38° closed and places 4 ms off the truth; an address set 15° open
moves neither. A 75°-start pelvis that never comes back to square has NO observable reference, and
the test says so: the square-up span is only a reference when the line squares up somewhere.

**4. A hands-over-hips mask was tested and NOT built.** The pelvis rate profile dips at 80–100 ms
before impact (median 59 °/s against 181 at −120 and 432 at −20). The nearest wrist is 1.6 hip
spans from either hip at that instant and the hip confidence holds at 0.84, so the hands are not
on the hips there; a wrist-proximity mask touches 2 % of the downswing samples and leaves the dip.
Whether the dip is a slide-then-turn pelvis or a keypoint semantic is for the truth capture.

**5. The corpus re-run with the change** (the §12 command, 61 pose2 swings, production defaults;
rows in `docs/research/data/kinematic_sequence/corpus_20260918_sighted.csv`):

The first re-run, with the reference fix and the sighted band alone, placed 12 pelvis and 32
thorax nodes — and reading their series showed both to be false: every placed pelvis had its rate
rising again above the "peak" in the last two windows before the band (338 → 469 → 520 °/s into
the gap on one swing), and most placed thorax nodes were transition spikes, ±1000 °/s inside
30 ms of the rate's sign change as the arms cross the chest at the top. Two rules followed, both
inside `finishChannel` and both only for a route with a blind band: **rising edge** — an interior
sighted peak is claimed only if the mean rate over the last window before the band is below the
window before it, else the node is bounded; **reversal spike** — a sighted peak within
`sequence.minAfterReversalMs` (60 ms) of the rate's last non-positive sample is neither placed nor
bounded. With those:

| Segment | produced | placed | bounded | placed peak before impact, ms | bound: no earlier than, ms (median [IQR]) | σt ms (placed) | peak °/s | Cheetham 2008 pros |
|---|---|---|---|---|---|---|---|---|
| Pelvis | 61 | 0 | 45 | — | 87 [54, 114] (min 17, max 141) | — | — | 87 ± 19 · 477 ± 53 |
| Thorax | 61 | 2 | 42 | 117 [109, 126] | 47 [40, 87] | 27 | 507 | 68 ± 14 · 727 ± 61 |
| Lead arm | 61 | 59 | 0 | 94 [67, 132] | — | 19 | 697 | 65 ± 8 · 980 ± 68 |
| Club | 61 | 57 | 0 | 63 [45, 68] | — | 10 | 1887 | — · 2254 ± 68 |

Order resolved on 41 of 61 (67 %), verdicts partial 38 / unresolved 20 / other 3 — identical to
the 17 Sept gated pass, as it must be: the arm and club nodes are unchanged on every swing. Of
the 45 pelvis bounds, 5 are `peakNoLaterThanMs` (the rate still falling when sight returned on
the open side); the 11 pelvis and 8 thorax nodes with neither a placement nor a bound are the
reversal spikes (4 and 7) and the σ threshold (7 and 1). **The reading**: on this golfer the
trunk peaks are out of the face-on camera's sight on essentially every swing, and the producer
now says exactly that — "the pelvis peaked no earlier than 87 ms before impact" — instead of a
spike or a silence. The two thorax placements at 117 ms are the only in-sight trunk peaks in the
corpus and are not a finding at n = 2. Whether the out-of-sight peaks are the amateur pattern
(trunk squaring up at impact) or the perspective confound of item 2 is what the DTL span (item 6)
and the §9 truth capture decide.

⚠ **Observed in passing, not caused here**: `clubheadPeakLead` (the LINEAR clubhead-speed
peak's lead before impact, a live metric) reads 145 ms median on this run against 10 ms on the
17 Sept pass over the same 61 swings, while the angular club node is unchanged on 60 of 61. The
sequence producer does not touch it; the tracker commits between the two runs (4b84fe9d,
df044ff2) are the candidates. Not chased in this session.

**6. The cheap unblock is the down-the-line camera, uncalibrated.** Its hip and shoulder spans go
as |sin θ|: maximal sensitivity at square, zero at 90° — the complement of face-on — and the move
toward the face-on camera is a lateral move in that view, so the pair also separates the
perspective confound. Two spans on one clock lie on an axis-aligned ellipse (`(w_fo/W)² +
(w_dtl/W')² = 1`), which fixes both pixel scales without a board and gives θ everywhere; §5.2's
calibrated pair is the stronger form of the same idea. The corpus holds the DTL videos but no DTL
pose cache, so this is one pose run away. The far hip is occluded at address in that view; the
confidence gate says per frame how bad that is. The §9 truth capture remains the arbiter for
every node the camera places.

---

## 13. The down-the-line pair, uncalibrated — built, and what it found (2026-09-20)

**Written 2026-09-20.** §11 item 6 and §12.4 item 6, done. The deliverable was two things and both
exist: the offline measurement (**K0**, `tools/swinglab/span_pair_offline.py`, written up in
[`docs/research/data/kinematic_sequence/pair_span_turn_20260920.md`](../research/data/kinematic_sequence/pair_span_turn_20260920.md)
with per-swing rows in `pair_span_turn_20260920.csv`), and the C++ route it decided the shape of.
K0 came first deliberately: §12.1 and §12.4 are two records of an estimator that was confident on
garbage, and building the third one in C++ before measuring it would have been the same mistake a
third time.

This section is what was built, what was tried and rejected on the way, and what the route says
about the golfer in the corpus — which is not what §12.4 item 6 expected it to say.

### 13.1 Why a second view at all

§2 and §12.4 name the defect precisely. A single face-on camera reads a body line's turn from how
wide it images, `w = w₀ cos ψ`, and `dw/dψ = −w₀ sin ψ` is **zero at square**. The segment's rate
is exactly what the camera stops being able to see at the moment the golfer squares up, which on
an amateur is the moment the peak is. On the 61-swing corpus that reads as **0 pelvis nodes placed
and 45 bounded** (§12.4 item 5's table): the producer's honest output on essentially every swing is "the
pelvis peaked no earlier than 87 ms before impact", which is a bound, not a measurement.

The down-the-line view is the complement: its separation goes as `sin ψ`, maximal where face-on is
blind. Pairing them removes the blind band rather than working around it.

### 13.2 The observable is a signed horizontal separation, not a span

**This is the single most important thing K0 changed, and it is not what §5.2 said.** For a body
line of length `W`, tilted `τ` out of horizontal and turned `ψ` about the vertical:

```
d_fo  = s_F · W · cos τ · cos ψ          (face-on, signed, pixels)
d_dtl = s_D · W · cos τ · sin ψ          (down-the-line, signed, pixels)
ψ     = atan2(d_dtl / r, d_fo),   r = s_D / s_F
```

`W` and the tilt `τ` **cancel in the ratio**. That matters because a 2-D span distance
`hypot(Δx·W_frame, Δy·H_frame)` absorbs the tilt, and the shoulder line's tilt is large through
the downswing. The paired form therefore needs **no reference width, no square-up inference and no
sign unfold**, and its σ has no singularity — the atan2 Jacobian is bounded everywhere, which is
the whole reason the rung exists.

**`r` is measured, not calibrated.** Both cameras are level, so both see *vertical* undistorted by
the turn: the body's vertical extent at address — ankle midpoint to shoulder midpoint — is one
physical length imaged twice, and its ratio is `r`. Measured **1.12–1.15 on the 07-04 rig and
1.16–1.25 on 06-11**. That is why the rung is **Estimated** and not Direct.

**The two orientation signs are MEASURED, and they have to be.** Which image side a right-hander's
lead hip lands on, and which side of the target line the down-the-line camera stands, are
properties of the *rig*. `leadIsLeft` picks which two keypoints to subtract; it cannot know either
of those. So the route reads each sign as the median of that view's own signed separation over a
window where the quantity is large and unambiguous — face-on over the address hold, down-the-line
over the 200 ms ending just after the Top, where the body is most closed. Nothing depends on
locating an extremum in time, which is exactly the inference §12.4 condemned.

⚠ **`segment_rates.h:201` says "right-hander seen face-on: lead is image-left". That comment is
false on this corpus**, which is why the bit is measured rather than assumed. The comment is left
where it is and contradicted here rather than quietly edited, because it is a claim about rigs and
one line of documentation is not evidence about rigs either way.

**γ, the angle between the two views, is not 90°.** K0 fits it at **75–84°** on the two rigs: the
down-the-line camera sits behind the **ball**, not behind the hands. A γ ≠ 90 biases ψ's **level** —
at impact the pelvis reads about 20° closed when it is not — and therefore the published °/s. It
does **not** move the peak's time, which is what a sequence node is. Fitting γ per swing is
possible (K0 does it) and is deliberately **not** done in the producer: it would buy magnitude this
rung is not allowed to publish anyway, at the cost of a fit that can fail.

### 13.3 What K0 established before a line of C++ was written

All from `pair_span_turn_20260920.md`, 21 two-camera swings (12 × 2026-07-04, 9 × 2026-06-11):

| question | answer | where |
|---|---|---|
| Are the two views looking at one line turning? | R² **0.90–0.96**, rms **9–13 px** on spans of 85–145 px | §2.1 |
| What is the inter-view angle? | **75–84°**, per rig, measured for free | §2.2 |
| Is there a blind band? | **No.** σ_θ 2.3–2.5° for the pelvis at address, at the top and at impact | §3.5 |
| How good is the square-up instant? | DTL zero crossing, IQR **3–5 ms**, against face-on's **5–53 ms** (87 ms of scatter on one rig's pelvis) | §2.3 |
| Does the peak time survive an uncalibrated scale? | ±10 % moves it by **at most one 4 ms grid step**; ±3 % moves the magnitude by **5.8 %** | §3.6 |
| Are the DTL keypoints real? | downswing p10 confidence **0.75 hips / 0.72 shoulders**; **the DTL shoulders beat face-on's 0.52** | §3.4 |

Two of those overturn what §12.4 item 6 assumed. The far hip was the keypoint the design worried
about; it holds up, and the **face-on shoulders** are the weak leg. And the closure residual —
`|(w/W)² + (w'/W')² − 1|`, which item 6 proposed as the gate — **is not usable as one**: it sees
the *sum* of the two relative scale errors while the angle sees their *difference*, so a cloud
sitting perfectly on the circle can have both scales wrong in the same direction with ψ still
right. Making it a gate would need γ fitted, and even on a perfect pairing the expected residual at
γ = 82° is ≈ 0.15 — i.e. the threshold would be the geometry, not a fault. Reported, not gated.

For the same reason `r_ell` (the ellipse-fit scale ratio) is carried in the diagnostics as a
**lower bound contaminated by dwell**, not as a competing estimate of `r`.

**K0's own trap, and it is the third of its kind.** Taken literally, §5.2 says `w_dtl = w₀ sin θ`
and §5.3's sign unfold flips at the span extremum. An **unsigned** DTL span unfolded at its minimum
inserts a step of `2·w'_min` into ψ at one sample — 13° in 4 ms on one swing — and the 25 ms
derivative reads a step as an enormous sharply-curved rate, which `σ_t = √(2σ_r/|r̈|)` then reports
as ±12 ms. It manufactured a pelvis "peak" of ~806 °/s **exactly one grid step after the DTL span
minimum, on 21 of 21 swings**. That is §12.1's "confident on garbage" arriving by a third route,
and it is why `segment_rates_test` §9d exists as a named gate: the unsigned observable must never
yield a placed node at square.

### 13.4 The finding: on this golfer both trunk rates are still rising at impact

Running the same peak finder on the sequence domain as designed (transition → impact) and then on a
domain extended 150 ms past impact:

| segment | peak, sequence domain | peak on the extended domain | Cheetham 2008 pros |
|---|---|---|---|
| Pelvis | **none** — still climbing at impact | **+39 ms [28, 44] AFTER impact** | 87 ms **before** · 477 ± 53 °/s |
| Thorax | **none** — still climbing at impact | **+29 ms [24, 36] AFTER impact** | 68 ms **before** · 727 ± 61 °/s |
| Lead arm | −100…−115 ms before impact | — | 65 ± 8 · 980 ± 68 |
| Club | −50…−55 ms before impact | — | — · 2254 ± 68 |

None of the extended-domain peaks sits at that domain's edge and none is a reversal spike, so this
is not "the estimator ran out of window": the peaks are interior, and they are on the wrong side of
the ball. By session the pelvis peak lands +29 ms [27, 31] on 07-04 and +43 ms [40, 46] on 06-11,
and the two rigs agree on its size to 3 %.

**"Rising at impact" is solid. "+39 ms" is not.** The DTL hip confidence dips by 0.12 in the
100 ms after impact — which is exactly where the extended search puts the peak — and the pelvis
magnitude there (639–841 °/s depending on the reference) is 3σ or more above a 477 ± 53 benchmark.
Whether the pelvis genuinely whips through square after the ball on this golfer, or the DTL hip
keypoints are being dragged by the trunk and arms through the follow-through, this pass cannot
separate.

**Design decision taken, and it is a decision rather than a fix: the producer does NOT search past
impact.** The sequence is defined on [transition, impact]; a route that quietly extended its own
domain to find a peak would be answering a different question from the one the chart asks, and it
would do it on the strength of the least confident 100 ms of keypoints in the swing. So the route
places **no trunk node inside the domain** and says instead `peakNoEarlierThanMs = 0` — "it had not
peaked by impact". Whether the metric should also look past impact is an open question for the
chart (§13.9); the owner may revisit it.

That bound is worth comparing with the one it replaces. Face-on's bound is **84 ms [57, 89]** for
the pelvis and **44 ms [40, 72]** for the thorax — "it peaked somewhere after −84 ms, where the
camera stopped being able to see it". The pair's is **0 ms** — "it had not peaked when the club
reached the ball". The second is a far tighter and far better founded statement, and it is about
the golfer rather than about the equipment.

### 13.5 The iterations, each recorded with what was wrong with it

**K1a — signed both legs, smoothed keypoints.** The geometry worked: pelvis **0 placed / 20 "not
before impact" / 1 refused on scale** (07-04 swing_0007 has 3 usable address frames, below
`pairMinExtentPx`'s denominator). But the thorax produced **12 spurious placements at a median
963 °/s**, reaching **2,300 °/s on 06-11** — club-like numbers for a chest — and two swings came
back with a false `armBeforeThorax` verdict off the back of them.

**The first diagnosis was mostly wrong, and that is the useful part.** "Left/right relabel" was the
obvious reading, and relabels are real: a pose model labels left and right by appearance, and with
the golfer's back to the lens at the top the face-on shoulder labels flip and the signed separation
steps from −108 px to +96 px in 7 ms with its magnitude intact. But the guard, once built, found
**genuine relabels on only 4 of 21 swings** — 06-11 swings 0005, 0006, 0007 and 0009, every one of
them on the **thorax**, and every one on the **face-on** leg. The down-the-line leg records **zero
relabels on every segment of every swing**, and the pelvis records zero on all 21, in both views
(`build/ks/pair/<swing>/runner.log`, the `[WristAnalysis] sequence pair:` line). The spurious
placements did not go away.

**The real cause is not a relabel.** The face-on **shoulder line genuinely crosses square to the
face-on camera** near the top, and the face-on shoulder keypoints turn to noise for about 30 ms
there: **−121, +12, −108, −26, +96 px on consecutive frames** (06-11 swing_0002, 161–194 ms before
impact) while the DTL leg runs smooth through the same instants. atan2 is perfectly conditioned at
`x = 0`; its **x argument** is not.

So the pair has **no blind band of its own** — that claim of §13.3 stands — but it **inherits the
face-on shoulders' failure at their own square-up**. The two statements are compatible and both
have to be said, because only the first of them was in the plan.

**K1c — the relabel guard, on raw keypoints.** The discriminator is physical rather than a
preference: for a line to cross square *to this camera*, `cos ψ` must pass through zero, so `|d|`
has to **collapse** on the way through. At 1500 °/s a rigid line turns ≤ 10° per frame at 150 fps
and cannot get from one side of zero to the other with `|d|` near its maximum. A sign change **with**
a collapse is kinematics and is left alone; one **without** is a relabel and is undone
(`pairSwapMinFrac` 0.35 of the view's own p95 separation, i.e. 20° from square). Where the labels
alternate faster than the body can turn — three or more flips inside 100 ms — neither reading is
trustworthy and the frames are **dropped** rather than corrected.

It works on planted swaps. **It cost the pelvis.** Reading raw keypoints to see the step took the
pelvis from 0 placements / 20 bounds to **3 doubtful placements / 13 bounds**, and **5 thorax
placements survived, all of them suspects**. That is a rule that fixed a real defect and made the
headline number worse, and it is recorded that way. The decision rule of §9 fired on it:
`sequence.pairTrunk.thoraxPlacement` defaults **FALSE**.

**K1d — what shipped.** Four changes, each aimed at a specific thing K1c got wrong:

1. **Consume smoothed where the detector finds nothing.** The relabel detector runs on **raw**,
   because the RTS smoother has already turned a step into a ramp. But where a view/segment carries
   no relabel and no flutter — the hips in both views on 21 of 21, the DTL shoulders on 21 of 21 —
   there is nothing to protect against and the smoothed track is simply the better measurement.
   Only a leg that actually relabels pays the noise cost of being read raw. `PairSegmentDiag.srcFo`
   / `srcDtl` record which tier each leg was finally read from.
2. **A rigid-body rate limit.** `|Δd| ≤ W·sin(ω_max·Δt) + 2σ` with `ω_max` = 2,000 °/s
   (`pairMaxTurnDps`), generous by nearly a factor of three on a thorax. This is arithmetic about a
   rigid body, not a smoothness preference, and it is what the face-on shoulders need at their own
   square-up: 200 px steps against a 92 px bound. The offending sample **and its two neighbours**
   are invalidated, because a step implicates both of its ends and the local quadratic reaches one
   sample further.
3. **Invalid runs are a blind band with proximity edges.** The span rung's blind band is a horizon
   at the *end* of the downswing, so "the peak is at or after where sight was lost" is its right
   edge test. The pair's invalid samples are an **interior hole**, and that test would condemn every
   peak after the hole. `PlacementGate::bandIsHole` switches the edge test to proximity: a peak
   within one derivative window of either edge was rising into the hole or emerging from it and is
   bounded; a peak well clear of it is simply a peak. **Bounds are emitted even when the ring is
   switched off** — the placement switches gate the ring only.
4. **The end-edge rule, judged on the last window.** When the rate over the last derivative window
   before impact is rising *and* is the largest valid windowed rate in the final 100 ms, the honest
   output is "it had not peaked by the end of the domain": unplaced, `peakNoEarlierThanMs = 0`. It
   is evaluated on the **last window**, not on wherever the global extremum happens to sit, so it
   stays true of a swing whose curve carries an artefact earlier on.

And one rule that is about the reader rather than the geometry: **a pair channel with neither a ring
nor a bound is WITHDRAWN** — channel and node both — so the face-on span rung runs for that segment
on that swing. If the pair cannot place *and* cannot bound, it has told the reader less than face-on
would have, and a route that replaces a worse answer with no answer is not an improvement.

### 13.6 The thorax ring is OFF, and the reason is not relabels

With the guard in, the pair places 5 thorax nodes and **every one is a suspect**: four sit 134–214 ms
earlier than the same swing's lead-arm node — a thorax peaking before the arm by that much is not a
sequence, it is the estimator — one of those reads 1,220 °/s and the fifth reads 66 °/s. Measured
with the switch forced ON over the full 21, the pair places **10 thorax nodes of which 9 are
suspects** by the same test (>1,200 °/s, or more than 80 ms before the lead arm).

So `sequence.pairTrunk.thoraxPlacement` is **false**, and it is a **separate switch from
`faceOnTrunkPlacement` and from `pairTrunkPlacement`**: the two rungs fail differently, and the two
segments on this rung fail differently, and one switch for all of them would close the thing that
works to silence the thing that does not. The thorax keeps its continuous pair **curve** and its
honest **bound**; it does not get a ring until a face-on shoulder that survives its own square-up
exists. The pelvis never crosses square to the face-on camera — **0 sign changes on 21 of 21** —
and is unaffected.

⚠ The thorax pair curves on 06-11 still show **1,600–2,700 °/s excursions** where that channel is
drawn. The node is withheld; the curve is not. That is owed (§13.9).

### 13.7 The graded 21-swing result

Two runs of the same 21 two-camera swings, same binary, same pinned face-on and down-the-line pose,
differing in **one override**: `build/ks/faceon` adds `{"sequence.pairTrunk.enabled": false}`
(`build/ks/nopair.json`, echoed in every `runmeta.json` → `params`), `build/ks/pair` adds nothing.
Re-tabulate with `python3 tools/swinglab/sequence_report.py --root build/ks/{pair,faceon} --tag …`.

| segment | run | produced | placed | bounded | neither | bound median [IQR], ms | routes |
|---|---|---|---|---|---|---|---|
| Pelvis | face-on | 21 | 0 | 16 | 5 | 84 [57, 89] | `faceOn` ×21 |
| **Pelvis** | **pair** | 21 | **0** | **21** | **0** | **0 [0, 0]** | `faceOn+dtl` ×20, `faceOn` ×1 |
| Thorax | face-on | 21 | 0 | 12 | 9 | 44 [40, 72] | `faceOn` ×21 |
| **Thorax** | **pair** | 21 | **0** | **14** | **7** | 44 [40, 87] | `faceOn` ×9, `faceOn+dtl` ×5 |
| Lead arm | both | 21 | 20 | 0 | 1 | peak 100 [87, 156] ms before impact | `faceOn` ×21 |
| Club | both | 21 | 18 | 0 | 3 | peak 50 [30, 64] ms before impact | `faceOnClub` ×21 |

**The pelvis is the result.** It bounds on 21 of 21 where face-on bounded 16, and **20 of those 21
bounds read `peakNoEarlierThanMs = 0`** — "it had not peaked when the club reached the ball" —
where face-on produced that statement **zero** times. The one pelvis still on `faceOn` is 07-04
swing_0007, where the route refused at the scale: *"address window holds 3 / 37 usable frames
(need 5)"*. That refusal is why swing_0007's `kinematicSequence` is the **one** block of the 21
that is byte-identical between the two runs.

**Beware a reading of "0" as a weaker bound. It is a tighter one.** `peakNoEarlierThanMs` is a
lower bound stated as an offset *before* impact, so face-on's 84 ms says "the peak is somewhere
after −84 ms" and the pair's 0 says "the peak is at impact or after it". The pair's statement
excludes the last 84 ms as well. On 15 pelvis swings and 3 thorax swings an existing face-on bound
is replaced by the tighter pair bound; on 5 pelvis and 2 thorax swings a segment that could say
nothing at all now says something. **No segment on any swing lost a placement or a bound**, checked
programmatically over all 21 × 2 nodes.

**The thorax is not the result, and its table row flatters it.** Of its 14 pair-run bounds, **9 are
the face-on span rung's own** — the pair channel was withdrawn on those swings — and only **5** are
`faceOn+dtl`. Counting the route-level refusal, **16 of 21 swings read their thorax exactly as the
face-on-only run does.** Of the 5 the pair produced, 3 say "did not peak before impact" and **2 are
the new bounds, both on 06-11, and both carry an implausible peak: 107 °/s on swing_0005 and
−50 °/s (negative) on swing_0006.** Those are node magnitudes the rung is not permitted to publish
(§13.9, the IMU gate), but they are a straight warning that the 06-11 thorax pair channel is not
measuring a chest.

**Nothing else moved.** The verdict histogram is identical between the runs — partial 16,
unresolved 5 — `orderResolved` holds on 16 of 21 in both, the order histogram is identical
(`leadArm→club` 17, `leadArm` 3, `club` 1), and the per-swing verdict, order, `gapsMs` and
`gainsDps` agree on all 21. The lead-arm and club nodes are byte-identical on every swing. Inside
`analysis.metrics[]` exactly two keys ever differ: `pelvisAngularSpeed` on 20 swings and
`thoraxAngularSpeed` on 5. Every other block of `analysis` — ball, club, phases, pose2d, score,
segmentation, tier, versions, schema — is identical on all 21.

**With the thorax ring forced ON for measurement** the pair places 10 thorax nodes, of which 9 are
suspects by §13.6's test. That is the measurement the default is set from, and it is why the
default is OFF.

**The pixel-scale ratio, per session** (`rVertical`, from the pair diagnostics line in each
`runner.log`):

| session | n | r range | median | face-on extent px | DTL extent px |
|---|---|---|---|---|---|
| 2026-06-11 | 9 | 1.156 – 1.246 | 1.173 | 417 – 436 | 489 – 525 |
| 2026-07-04 | 11 | 1.112 – 1.143 | 1.123 | 428 – 443 | 484 – 492 |

The two sessions separate cleanly with no overlap, which is what a per-rig constant should do.
07-04 swing_0007 is excluded: its scale was never formed.

### 13.8 What landed in the app

- **Catalogue.** `metric_catalogue_manifest.cpp`: the `faceOn+dtl` rung on `pelvisAngularSpeed` and
  `thoraxAngularSpeed` goes from `PLANNED`/Direct to **live/Estimated**, with a summary that says
  "atan2 of the pair, uncalibrated" in as many words. The method word stays `Triangulated` because
  it is the only enum value that says "two views" and a new value for one rung is not worth an enum
  entry — so the prose corrects it rather than the enum. **Metric counts do not move**: the rung was
  already in the ladder as PLANNED, and a rung changing state adds no descriptor.
- **The route glyph is matched case-insensitively now** (`chart_metrics.cpp`). `"dtl"`, `"DTL"` and
  `"Dtl"` are all natural spellings and a case slip used to fall through to `projected` — which is
  not a near miss, it is the glyph that tells a reader which chip to trust.
- **A rising node is not a ring.** `sequenceRisingAtImpact()` tests the **bound's sign**, not the
  route: `peakNoEarlierThanMs ≤ 0` means the segment was watched all the way to the ball. It draws
  as an open chevron pointing up-right at the impact edge of its own curve
  (`PpChartPlot.qml`, `objectName: "sequenceRising:<segment>"`), labelled **"rising"**, with no σ
  whisker and no offset — there is no instant to be about. The split is done on the **model**, not
  by hiding a delegate, so an invisible item with the right name cannot pass a test the user would
  fail. The row text reads **"still accelerating at impact"**.
- **The strip says the pattern.** "partial — placed nodes in order (2 of 4)" is true and useless
  here: it counts what was placed and does not say that the two it could not place are the *body*,
  that they were watched to the ball, and that they were still gaining speed when the club got
  there. When one or both trunk segments are rising and the arm or club peaked before impact, the
  line reads e.g. **"Lead arm −115 ms → Club −55 ms (+60 ms) · arms and club peak before the body —
  hips and chest still speeding up at impact"**. It is assembled from what is true rather than
  templated, because one trunk segment rising is not two.
- **The live app can now feed it.** `shot_processor.cpp` fills `job.dtlSource` from the first
  `CameraInstance::DownTheLine` track — first wins and never moves, because two cameras down the
  line is not a configuration we support and switching between them would make the inter-view angle,
  a per-rig constant, jump between shots. `ShotReplayController::shotContext` sets `hasDtl` from a
  down-the-line stream **or** from route evidence (a `faceOn+dtl` node), because the 2026-06-11
  session carries no `setup` block at all.
- **`DtlPoseStage`** (`wrist_analyzer.cpp`) runs immediately before `KinematicSequenceStage` and
  strictly after ShaftPlane, so nothing it does can move the face-on pose, ball or shaft. It builds
  its **own** runner options and never touches `ctx.runnerOpt`, which the ball and shaft stages gate
  on. Bounds are Address → Impact + 150 ms, dense from Top − 100 ms, `twoPass = false` — the time
  base is inherited, never rediscovered. About **5 s** on a live shot.

**Gates, all run:**

| gate | result |
|---|---|
| a swing with no down-the-line pose | **byte-identical** to before |
| `sequence.pairTrunk.enabled = false` | reproduces that exactly (a named test, §9f) |
| the dark switch | 6/6 identical |
| on a DTL swing, what may differ | only `kinematicSequence` and the two trunk series — 20 swings' `pelvisAngularSpeed`, 5 swings' `thoraxAngularSpeed`, nothing else |
| determinism | two runs of identical inputs bit-identical, with pinned DTL pose (§9h) |
| build + tests | the app builds; 27 affected ctest suites pass with both halves of the day's work together |

**Also found, and it is a process finding, not a code one.** `tst_chart_presets.qml`'s
`test_016` had been **red since 2026-09-18**: it asserted `sequenceChip:pelvis` /
`sequenceChip:thorax`, which the strip rewrite of that day deleted when the sequence moved onto the
plot, and it compared the strip's line against the bare verdict when the line has carried the chain
and the verdict together ever since. Two `verify`s on a null `findChild` and one failed `compare`,
for two days. **Nobody had been running `qml_ui`.** Rewritten today to assert what the strip
renders now, including the rising case.

### 13.9 Owed, and open

Plainly, and none of these was closed today.

1. **Rotation truth.** No Witmotion swing has ever passed `imuIntegrity`, so there is no measured
   trunk rate on any swing in the corpus. §9's stage 2 — one unit on the sacrum, one on the sternum
   — remains the only arbiter, both for the placement and for the magnitude.
2. **A correctly placed down-the-line camera.** Both rigs put it behind the **ball**, not on the
   hands line and not at hand height, which is what makes γ 75–84° instead of 90° and biases ψ's
   level (the pelvis reads ~20° closed at impact). A correct placement removes the bias for free.
3. **Whether to look past impact.** The producer deliberately does not (§13.4). If the chart should,
   that is a design decision to take before someone reads 21 "did not peak before impact" swings as
   a fault.
4. **The trunk magnitudes.** 640–840 °/s for the pelvis offline against Cheetham's 477 ± 53. Not
   credible uncalibrated, and not published — the rung emits a *time* with a σ. G4 stands.
5. **The 06-11 thorax pair curves** still carry **1,600–2,700 °/s** excursions where that channel is
   drawn, and its two new bounds read 107 °/s and −50 °/s. The node is withheld; the curve is not.
6. **`poseDtl` is not persisted and has no version gate.** `PairDiagnostics` is logged and never
   serialised, so nothing downstream can gate on it and a re-analysis re-poses the second camera.
   Both are owed before this route is re-run in bulk.
7. **`clubheadPeakLead` 10 → 145 ms.** Flagged in §12.4 item 5 on 18 Sept, **not chased today, still
   owed.** These runs give it a second reading: the angular club node leads the linear clubhead-speed
   peak by **−98 ms [−117, −89]** on the 18 swings that carry both, identically in the pair and
   face-on runs, so whatever moved it is not the sequence producer.
8. **A golfer whose trunk peaks inside the downswing.** Until one is measured, the rung's **bound**
   is graded and its **placement** is not graded at all — every placement path in §13.5 and §13.6
   was exercised on synthetic fixtures (`segment_rates_test` §9a–§9iii) and on nobody.

---

## 14. The club through the fused plane (2026-09-21)

`shaft_fusion_design.md` is the document; this is what it changed here. The club's face-on angle
was de-projected through a plane INFERRED from the face-on track's own ellipse (§5.3). On the 24
two-camera swings that inference wandered — node bearing −57…+63°, ratio 0.65–0.98 — while the
plane MEASURED by intersecting the two cameras' shaft angles held a ratio of 0.84–0.90 and a level
node (7-iron: 60.3° to the ground, sd 0.7°). Where the fusion offers a downswing plane
(≥ 8 frames, ≤ 5° rms) the club de-projects through it and its route reads `faceOn+dtl`; otherwise
nothing changes. The lead arm is deliberately not moved.

Graded against a fusion-off control (`docs/research/data/fusion/fuse_grade_20260921.md`): club-node
lead sd 19.6 → 9.5 ms and peak CV 7.4 → 6.2 % on 07-04 s4–15; s14's node left the domain edge
(0 → 59 ms); **one swing lost its club node** (06-11 s1, σ_t 14 → 59 ms). Only `club3d`,
`versions.shaftFusion`, `kinematicSequence` and `clubAngularSpeed` differ, 24/24. §13.9 item 7 now
has a third reading: through the fused plane the club's angular peak still leads impact by
33–64 ms on every 07-04 swing, so that lead is not a de-projection artefact.

The chart no longer dashes the curve outside transition → impact (`shaft_fusion_design.md` §4).

