# Planned metrics promoted to producers (12 Nov 2026)

**Status:** built, reviewed and tested; uncommitted. Mark judges the results before any commit.
**Plan:** `~/.claude/plans/harmonic-herding-dusk.md`.
**Prompted by:** the 12 Nov audit of planned metrics. Some readings had every input delivered (down-the-line camera, club track, two-camera shaft fusion, 3-D skeleton, chest and hip sensors) but no producer. Thighs are never placed.

**Mark's rulings:**
- The new readings **grade faults now**; σ and trust records tell the user how far to rely on them.
- No corpus sweep. The evidence is unit tests plus one swing.
- Hip internal rotation uses the 3-D thigh where there is one.
- The 3-D spine side bend is included.

## 1. What is produced now

| Reading | Route | Read at | Grades | σ / trust |
|---|---|---|---|---|
| `shaftDirection` | fused shaft (`faceOn+dtl`), 3-D fit fallback | P2, top | `inside_takeaway`, `outside_takeaway`, `across_the_line`, `laid_off` (draft) | σ_dir/\|h\| ⊕ σ_yaw 9°; ⚠ `club3d.cameraAssumed`; ⚠ `club3d.shaftDirectionFromFit` when a graded reading came from the fit |
| `shoulderPlaneAngle3d` | 3-D fit | top | `flat_shoulder_plane`, `steep_shoulder_plane` | σ from the joints' posterior σ; a tilt, so no camera term |
| `shoulderLineYaw` | 3-D fit | address | `alignment_open`, `alignment_closed` | line σ ⊕ σ_yaw 9°; ⚠ `body3d.cameraAssumed` |
| `leadKneeFlexion` | 3-D fit, replacing the down-the-line reading through impact | impact → P8 | `late_buckle`, `lead_knee_folds_backswing` | joint σ. With no fit, the down-the-line reading grades exactly as before |
| `spineSideBend` | 3-D fit, only when the face-on reading is absent | impact | side-bend drafts (only on such shots) | joint σ; ⚠ `body3d.fromFit` |
| `spineForwardBend` | chest sensor (`trunkImus`) | every phase | the posture faults that read the bend | §8.2 budget ⊕ mount-yaw term; ⚠ `body.chestBendSite`, ⚠ `body.trunkMountYawAssumed` |
| `spineSideBend` | chest and hip sensors | every phase | the side-bend measures | §8.2 budget ⊕ mount-yaw ⊕ turn leak; ⚠ `body.trunkMountYawAssumed` |
| `trailHipInternalRotation`, `leadHipInternalRotation` (replace `hipInternalRotation`) | pelvis sensor − 3-D thigh turn, or a fixed thigh | top, impact | nothing (the hip faults are confirmed by the physical screen) | pelvis budget ⊕ thigh σ; ⚠ `body.thighTurnAssumed` when a fixed thigh was assumed |
| `thighTurn3dLead`, `thighTurn3dTrail` | 3-D fit | — | research-only, hidden from charts | joint σ |

**Precedence when one key has several routes:**
- IMU beats 3-D, and 3-D beats a single camera. The rule holds whatever order the stages run in.
- Two exceptions keep the camera reading. A face-in (worn-backwards) sensor never beats a camera reading. A sensor reading that lacks a phase the camera reading would be graded at doesn't replace it.
- A 3-D reading standing in for a failed single-camera route takes that route's stale refusal records off the reading (`route_substitution.h`). Where the 3-D reading itself has a gap, the failed route's real reason is still shown, in the ledger and in the posture chart, Relations and Traces.

**Correction to the audit:** `m_clubPathAtImpact` was not mis-marked. It reads `lm.clubPath` first, so the path faults are live through the launch monitor.

## 2. One-swing check (no sweep)

**Swing:** 2026-07-04 Mark-Liversedge, swing 8 (two cameras, camera moved before s4).
**Scratch copy:** `/mnt/swingdata/scratch/planned-metrics/2026-07-04_Mark-Liversedge_Wrist_01/`.

Commands:

    build/tools-parity-ninja/swinglab_run <scratch>/swing_0008 --write-back      # recorded pose, ball, shaft, ladder reused
    build/tests/Analysis/regrade_ledger --tag before-planned-metrics <scratch>   # PINPOINT_CORE_* → src/Resources/diagnostics

Logs, the dump script output and the sheets are in `build/planned-metrics-20261112/verify/`. Re-running with the final code gave identical values.

| Reading | Value ± σ | Source | Fault verdict |
|---|---|---|---|
| Shaft direction, P2 | +9.4° ± 15.9 (outside +) | 3-D fit, one view (fit tier 2) | inside/outside takeaway: clean, z +1.88 (was not assessable) |
| Shaft direction, top | −11.0° ± 15.9 (laid off −) | 3-D fit, one view | across the line / laid off: clean, z −1.37 (was not assessable) |
| Shoulder aim, address | +0.5° ± 5.2 | 3-D fit | alignment open/closed: clean, z +0.12 (was not assessable) |
| Shoulder plane, top | 31.2° ± 1.1 | 3-D fit | flat/steep shoulder plane: clean, z −1.88 (was not assessable) |
| Lead knee, impact | 20.1° ± 1.6 (down the line read 19.9°) | 3-D fit | late_buckle: clean, 20.1° (was 22.6°) |
| Lead knee, address / top / P8 | 20.4° / 36.6° / 5.5° (down the line: 21.4° / 37.0° / —) | 3-D fit | lead_knee_folds_backswing: clean |
| Thigh turn from address | lead −2.3° (top), +1.5° (impact); trail −1.9° (impact) | 3-D fit | trail unmeasured at the top: its knee was bent 1.7°, below the 12° floor |

**Source gate, W1.** At both positions no clean fused frame lay within ±25 ms, so both readings came from the fit. That is 41 of the curve's 190 frames, against 140 fused.

**Seated constant.** `kHipIrThighFixedSigmaDeg` goes from 10° to **6°**, still provisional. On this swing the thigh's own turn was within what the fit resolves (σ 3.5–6°). A worn session with the 3-D fit is what would seat it properly.

**The pattern to watch.** P2 (+9.4°) and the top (−11.0°) are nearly equal and opposite. That is exactly what a ~10° turn of the down-the-line camera produces, because the bearing at the two positions moves in opposite directions under a yaw error. The stick probe measured this session's camera turned 4–9° toward the golfer. Until calibration is live, read shaft direction as relative between shots more than absolute.

No trunk-sensor recording exists, so the sensor readings are checked only against the synthetic body (`trunk_e2e_test` §j, analytic truths).

## 3. Decisions (Mark, 12 Nov 2026)

1. **Camera-direction σ widened from 5° to 9°.** It covers the largest down-the-line camera turn measured (8.7°, 4 July), because that error is a one-way bias, not noise. It applies to shaft direction and shoulder aim, uncalibrated (`kDtlYawSigmaUncalDeg`). The swing-8 shaft readings now carry about ±18° rather than ±16°.
2. **Left-handers stay mirrored** for `shaftDirection` and `shoulderLineYaw`: + means across/outside, and closed, for both hands. The sign doc records the exception.
3. **Shoulder plane norm kept at 50 ± 10°.** It is revisited only with evidence from more two-camera swings and golfers (the swing read 31°).
4. **`m_spineSideBendTop` left as is, and noted.** Its norm is μ +8°, but the 3-D (about −38°) and sensor (about −26°) routes read toward the lead at a top with no lateral flexion, because the turn tilts the reading. No fault reads it. Define it before anything grades it.
5. **The 3-D side bend keeps grading** the side-bend drafts, with its ⚠, on shots where the face-on reading refused.
6. **The face-on camera is taken as square to the target line.** A camera fixed to a wall is not pointed at an angle, so it carries no σ term. The shoulder aim's camera σ is the down-the-line camera's (it supplies the depth), at 9° per decision 1. The shoulder plane's solved vertical stays an open item for calibration.
7. **The golfer wording is approved as written.**

**Norms.** Every norm these readings grade against is heuristic and provisional. None becomes proven without Mark's conscious decision and evidence.

## 4. What stays planned, and why

| Metric (route) | Why |
|---|---|
| `clubPath` (dtl) | The 3-D head slows at impact and misses the ball by 7–26 cm (28 Sept). The path faults read the launch monitor |
| `clubheadSpeed`, `handSpeed` (faceOn+dtl); `clubheadSpeed` (club sensor) | Need a session with both cameras and the launch monitor to check against. No club sensor is placed |
| `thoracicFlexion`, `lumbarExtension` (dtlContour) | Need a back-outline tracer |
| `pelvisLiftBelt` (faceOn) | Needs a belt-line tracker |
| `launchDirection`, `launchAngle`, `ballSpeed` (camera) | Need a ball-flight tracker; the launch monitor supplies them |
| `swingScore` (adherence) | The scorer is not wired in |
| knee flexion (faceOn) | Foreshortened to noise; the down-the-line and 3-D routes are live |

Held, unchanged: `m_spineBendChangeTop`, which waits on a worn session.

Found while planning, not fixed here: a `held` measure still grades on the plain signal path; only composites respect it. Filed as #176.

## 5. Per-item detail

The implementers' notes follow, after independent review and sign-off: W1 (one fix round), W2 (three), W3 (three).

---

### W1 — shaft direction (`shaftDirection`): notes for planned_metrics_promotion.md


#### Definition

Where the shaft points against the target line, read at P2 (`ShaftParallelBack`) and at the top.

    bearing = atan2(−u_y, |u_x|)     (degrees; u = butt → head, unit, fusion frame)

- Frame (`shaft_fusion.h`, same as skeleton3d's world frame, `skeleton3d_rig.h:28-32`): X = face-on
  image-right, Y = face-on view ray (camera → golfer), Z = up. The golfer faces the face-on camera
  with the ball between them, so the golfer stands on the +Y side of the target line and the far
  side (beyond the ball) is −Y, for both hands. +X is the target for a right-hander. Verified: the
  skeleton's zero pose faces the camera with its front at −Y; the DTL camera looks along +X with
  image-right = −Y, and positive yaw turns it "toward +Y, the camera standing on the BALL side"
  (`shaft_fusion.h` dtlCamera). So the brief's reading holds: + is a −Y component of u.
- It is the angle of u's ground projection to the target LINE, signed by which side the head end
  lies on. `|u_x|` reads P2 (pointing back) and the top (pointing at the target) against the half of
  the line each points along, with one rule. No phase argument: the two positions' ray-referenced
  readings meet at ±90° where u_x = 0, so one line rule is continuous through both, and a ray rule
  would read a short top that still points back as ±180°.
- A shaft within 10° of vertical (ground projection < sin 10°, `kMinHorizontalFrac`) has no bearing:
  bridged in the curve, never read at a position.

#### Sign

+ = the head end points ACROSS the target line, away from the golfer: across the line at the top,
outside in the takeaway; right of the target for a right-hander. − = toward the golfer: laid off,
inside. Agrees with `axis_direction_test` (`sig_insideTakeaway` Low, `sig_outsideTakeaway` High,
`sig_acrossTheLine` High, `sig_laidOff` Low) and with the norms (μ 0).

Left-handers are mirrored (X → −X); the far side is still −Y, so the value and its meaning are
unchanged. Pinned in `shaft_fusion_test` §SD, including a test that a left-hander's across-the-line
is NOT read as the world frame's "left of target" (−).

#### σ

σ = √((σ_dir / |h|)² + σ_yaw²), |h| the ground projection's length (the direction's σ moved onto the
ground heading). `sigmaKind = Propagated` on the series and on both phase samples.

- Fused frame: σ_dir = `Sample3D.sigmaDeg` (uncertainty §4.7).
- A position's σ is the median of the σs of the frames its value is the median of.
- Skeleton frame: the fit publishes no shaft σ, so PROVISIONAL `kShaftDirSkeletonSigmaDeg` = 5°
  (tier 3, both views: the fit's 3° face-on and 4° DTL shaft-angle observation σs in quadrature) and
  `kShaftDirSkeletonOneViewSigmaDeg` = 15° (tier 2, one view: the swing-plane term's σ_plane, which
  alone holds the depth — i.e. the bearing — when one camera sees the shaft). Seat on the one swing.
- σ_yaw = `kDtlYawSigmaUncalDeg` 9° (was 5°; widened 12 Nov, decision 1) (4 July stick probe, 4.4–8.7° toward the golfer by distance,
  `tools/shaftlab/dtl_yaw_probe.py`, `dtl_posture_design.md` §4) uncalibrated; `kDtlYawSigmaCalDeg`
  1° (stated, provisional) calibrated. Calibration = `ShaftFusionStage::configFor(job).calibrated`.
- The series σ is the median of its valid frames' σ. With `uncertainty.enabled` off no σ is written.
  A fused frame whose `sigmaDeg` was not assessed gets no σ (never the yaw term alone).

#### Source rule (per position, deterministic)

1. Nearest fused sample within ±25 ms (`kFusionWindowUs`) that is face-on `Measured`, has none of
   `SignDisagree | IllConditioned | Reflected` (`kFusionRejectFlags` = 0x13, static_assert-pinned to
   the enum), and |h| ≥ sin 10°. `OffPlane` is not refused; `DtlAnchored` frames are Bridged, so
   refused by the Measured rule.
2. Else the nearest skeleton frame in the same window with `shaftTier` ≥ 2 (`kSkeletonMinShaftTier`)
   and |h| ≥ sin 10°, from a valid two-camera fit (`dtlUsed`).
3. Else no reading at that position.

The rule picks each position's SOURCE. The curve: every usable fused frame (valid = 1); skeleton
frames only where no usable fused frame lies within ±25 ms (valid = 1); refused fused frames between
readings bridged (valid = 0, the straight line), those inside a skeleton stretch dropped,
leading/trailing ones trimmed.

The curve agrees with the readings (W1 review fix). The grader (`measure_sample.cpp` →
`series_reduce.h` `reduceAt`) takes the median of the curve's valid frames within ±15 ms
(`sampler::kWindowHalfUs`) and falls back to the phase sample only when that window is empty, while
always stamping the phase sample's σ. So inside each position's ±15 ms only the chosen source's
frames are valid (the other source's bridged; every usable skeleton frame there joins the curve when
the skeleton was chosen), and the position's value is that same median, its σ the median of those
frames' σs. With no valid frame in the window, the value is the nearest chosen frame within ±25 ms,
which is what the grader falls back to. `shaft_direction_test` reproduces the grader and checks
graded == stamped, with the right source, for fusion-chosen and fit-chosen positions.

#### Trust records (both Downgraded, subject `shaftDirection`, covers `[shaftDirection]`)

- `club3d.cameraAssumed` — whenever the camera is uncalibrated. Evidence `sigmaYawDeg`. Words: "The
  down-the-line camera's direction is assumed, so where the shaft points carries ±9°."
- `club3d.shaftDirectionFromFit` — only when a GRADED position reading came from the 3-D fit (code
  `p2` / `top` / `both`). Skeleton frames elsewhere on the curve do not fire it; their share is in
  the evidence (`fitFrames`, `ofFrames`) and the stage log.
- Both fired in `src/Club/shaft_direction_series.h` (the per-shot assembly the stage calls),
  registered in `gate_catalogue_club.h` with that producer, and given golfer clauses in
  `gate_words.json`.

#### Precedence

One rung only (`faceOn+dtl`, Triangulated, Estimated → Bridged), so no cross-route precedence; inside
the producer, fusion beats the skeleton per position.

#### Where

`ShaftDirectionStage` (new, `analysis_pipeline.cpp`), after `DtlSynth3D` (so after `Skeleton3D`); its
per-shot assembly (series, readings, records) is `buildShaftDirection` in `src/Club/shaft_direction_series.h`;
`decl` reads `kShaft3d, kSkeleton3d, kSeg`, appends `kSeries`. Runs with a DTL camera and either a
valid fused track or a valid two-camera fit. Version `kShaftDirectionStageVersion = 1`, stamped as
`analysis.versions.shaftDirection` (provenance only, never reused). Claimed by `ShaftFusionProvider`.

#### Constants and provenance (all in `pp_tuned_constants.h`)

| constant | value | provenance |
|---|---|---|
| `dtlCamera::kDtlYawSigmaUncalDeg` | 9° (was 5°) | the largest 4 July stick-probe turn, 4.4–8.7° (`dtl_yaw_probe.py`, `dtl_posture_design.md` §4) |
| `dtlCamera::kDtlYawSigmaCalDeg` | 1° | stated, provisional |
| `shaftDirection::kFusionWindowUs` | 25 ms | the plan |
| `shaftDirection::kFusionRejectFlags` | 0x13 | the plan's flag set |
| `shaftDirection::kSkeletonMinShaftTier` | 2 | the plan ("≥ constrained": one view) |
| `shaftDirection::kShaftDirSkeletonSigmaDeg` | 5° | PROVISIONAL: √(3² + 4²), the fit's shaft observation σs |
| `shaftDirection::kShaftDirSkeletonOneViewSigmaDeg` | 15° | PROVISIONAL: σ_plane (`skeleton3d_shaft_branch_design.md` §3.1) |
| `shaftDirection::kMinHorizontalFrac` | sin 10° | design value: below it σ_bearing > 5.8 σ_dir, wider than either corridor |

#### What stays planned

Nothing of `shaftDirection`. The plane headings (`PlaneFit.azimDeg`) stay unclaimed.

---

### W2 — readings from the 3-D body: notes for planned_metrics_promotion.md


#### Where

- `src/Body/skeleton3d_body_metrics.h`: the per-frame geometry over `skeleton3d::FitResult`. Pure, no Qt.
- `src/Body/skeleton3d_body_series.h`: the per-shot assembly, `buildSkeleton3dBody` (series, phase
  readings, the record) and `mergeSkeleton3dBody` (precedence). Header-only, so
  `skeleton3d_body_metrics_test` reaches all of it without the pipeline (the W1 pattern).
- `Skeleton3dBodyStage` (`analysis_pipeline.cpp`) is thin glue. It runs after `BodyRotationTriangulated`,
  so after `Skeleton3D`, `DtlPosture` and the face-on body stages. It runs when `skeleton3d.valid && dtlUsed`.
  `decl` reads `kSkeleton3d, kSeg, kSeries` and WRITES `kSeries`, because it erases the down-the-line lead knee.
  So the executor runs it in authored order on `detail->series` itself.
- Version `kSkeleton3dBodyStageVersion = 1`, stamped as `analysis.versions.skeleton3dBody`. Provenance only.
- `kDtlPostureStageVersion` is unchanged at 2 (review fix: the down-the-line lead knee is not withheld).
- A post-run step in `runProfile`, `reconcileSubstitutedReadings` (`src/Analysis/route_substitution.h`), takes the failed route's refusals off a reading the 3-D route supplied (see Trust records).
- Claimed by the new `Skeleton3dBodyProvider`: `shoulderPlaneAngle3d`, `shoulderLineYaw`, `leadKneeFlexion`, `spineSideBend`.

#### Rules common to every reading

- **Joints.** A joint is read only at tier ≥ Constrained (2), with its `sigmaM`. This is the same rule as `skeletonLines()`.
- **Short lines.** A line or segment shorter than `kTriMinBaselineM` (8 cm) is not read. This floor is shared with the two-camera turn.
- **σ.** σ is first-order from `sigmaM`, which is per axis (`trace(Σ)/3`, `skeleton3d_fit.cpp`).
  Only the component of a joint's error that moves the angle counts.
  It is checked against 4000-draw Monte Carlo in `skeleton3d_body_metrics_test` §7:
  - shoulder plane: 2.701 vs 2.762°;
  - aim: 2.980 vs 2.979°;
  - knee: 3.919 vs 3.878°;
  - side bend: 5.631 vs 5.768°;
  - thigh heading: 7.736 vs 7.872°.
- **Series metadata.** Every series has `routeId "faceOn+dtl"`, `routeDirect false` and `sigmaKind Propagated`,
  on the series and on every phase sample.
  - The series σ is the median of its read frames' σ.
  - With `uncertainty.enabled` off, no σ is written.
- **The curve.** It is the fit's frame grid from the first read frame to the last.
  An unread frame between read frames is BRIDGED: the straight line, `valid = 0`.
- **Phase readings equal what is graded** (the W1 lesson).
  - A phase reading is `series_reduce.h reduceAt` over the published curve at the LADDER instant.
    That is the grader's own call: the median of the valid frames within ±15 ms.
  - Its σ is the median σ of those same frames.
  - A position with no read frame in its window gets NO phase sample. The grader then has nothing to fall back to, so there is no reading there.
  - §8 of the test grades every stamped sample through the real `reduceAt` and finds graded == stamped:
    16 samples across 5 series, plus the curve-less aim, which is graded by fallback.

#### The readings

| Series | Read at | Definition | Sign | σ |
|---|---|---|---|---|
| `shoulderPlaneAngle3d` | Top | `atan2(z_trail − z_lead, |horizontal|)` of the shoulder line (joints 8/13): its inclination in its own vertical plane | + the TRAIL shoulder higher. The body-line convention in 3-D, which does not flip with hand | √(σ_l² + σ_t²) / line length. No σ_yaw |
| `shoulderLineYaw` | Address, one phase sample, no curve (a PointInTime) | `atan2(−d_y, d_x')`, d = lead − trail shoulder, d_x' toward the target (mirrored for a left-hander) | OPEN −, CLOSED +, for both hands. Open is the lead end toward the golfer's own side of the target line (+Y) | (the line σ over its ground length) ⊕ σ_yaw, plus `body3d.cameraAssumed` |
| `leadKneeFlexion` (3-D) | Address, Top, Impact, P8 | 180° − the angle at the knee between hip and ankle. Joints 17/18/19 for a right-hander, 22/23/24 for a left-hander | + more bend; 0 = straight | the angle's gradient: σ_h²/a² + σ_a²/b² + σ_k²(1/a² + 1/b² − 2cos φ/(ab)) |
| `spineSideBend` (3-D) | Address, Top, Impact | hip tilt − shoulder tilt. Each tilt is `atan2(trail-end up, |along|)` in the frontal plane square to the PELVIS heading (spanned by the hip line's ground direction and the vertical) | + toward the TRAIL side. This is the face-on route's sign and formula (`upper_body_metrics.cpp`). A pelvis turn does not leak in (test §4) | the two tilts' σ in quadrature, each joint σ over the line's length in that plane |
| `thighTurn3dLead` / `thighTurn3dTrail` (research only) | Address, Top, Impact | the turn from Address of the heading of the normal to the hip–knee–ankle plane, which is the knee hinge's plane and turns with the femur | LEAD-RELATIVE, like `pelvisRotationSigned`: + toward the target, for either hand. W3 subtracts it from the pelvis's signed turn | see below |

The thigh turn's σ is √(σ_heading² + σ_ref² + (kTriScaleFrac·Δ)²):

- σ_heading: each joint's out-of-plane error tips the plane about the line through the other two, by σ / (the joint's distance from that line). Each tip is carried exactly onto the normal's heading.
- σ_ref: the reference's σ, taken by `triReference`'s rule (the median within `addrWindowUs` of Address, the nearest frame ≤ `triAddrMaxGapUs`).
- kTriScaleFrac·Δ: the camera-scale term, exactly as the two-camera pelvis turn carries it, so W3's difference composes.

On the §8 synthetic swing it is about 6–11° at 1 cm joint σ.

A thigh frame is unmeasured below `kThighTurnMinKneeFlexDeg` of knee bend, or when the plane's normal is more than 60° from horizontal.

#### Trust records

- `body3d.cameraAssumed` is Downgraded. Its subject is `shoulderLineYaw` and it covers `[shoulderLineYaw]`; its evidence is `sigmaYawDeg`.
  - It fires only when the shoulderLineYaw reading was published, which needs Address on the ladder and read shoulders within ±15 ms of it.
  - It is registered in `gate_catalogue_body.h` with producer `skeleton3d_body_series.h`, and has a golfer clause in `gate_words.json`.
  - Its words: "The 3-D fit works out the cameras' directions itself, so where the shoulder line points carries ±9°." No promise that calibrating removes it: no calibration reaches the fit, so it fires on every published aim.
  - The tilts, the knee bend and the side bend do not carry it; they do not turn with the cameras' heading.
- `body3d.fromFit` is Downgraded. Its subject and covers are the reading K; its evidence is `code` / `replaces` (`faceOn` or `dtl`), `route` (`faceOn+dtl`) and, after the run, `replacedRecords`.
  - It fires when the 3-D route supplied K because the single-camera route had none: the side bend wherever it is published (the face-on series was absent), and the lead knee when there was no down-the-line series.
  - It does NOT fire when the 3-D lead knee replaces a down-the-line one. That is the better rung winning, not a fallback.
  - It is registered with golfer words. It is W1's `club3d.shaftDirectionFromFit` pattern.
- **The clean-up** (`reconcileSubstitutedReadings`) runs after every stage has spliced its records, on both the sequential and the parallel path, in `runProfile`.
  - It finds each substitution record S (Downgraded, with `route` + `replaces` in its evidence) whose series K is present with `routeId == S.route`.
  - For each, every Refused, Bounded or Masked record R from ANOTHER producer (its id's area — the part before the first '.' — differs from S's) stops speaking for K:
    - K in `R.covers` → K is removed;
    - K == `R.subject` → the next cover becomes the subject, or R is dropped when it covered nothing else.
  - R's id goes into `S.replacedRecords`.
  - Masked is included beside the two effects the review named: a mask record counts frames of the failed route's curve, which is not the curve published.
  - Downgraded and Withheld records, and the 3-D producer's own records, are untouched.
  - Live cases today: `body.upperAddress` (face-on, covers `spineSideBend`), `body.sideBendLines` (face-on Masked, subject `spineSideBend`) and `posture.address` (down-the-line, covers `leadKneeFlexion`).
  - Test §11 checks the clean-up, and that afterwards only Downgraded records speak for the 3-D readings.
  - **A gap the substitute did not fill keeps its real reason (re-review fix).** `LiveMeasureSource::gateCause` (`live_measure_source.cpp`) falls back, when `primaryRecord` finds nothing for the key, to `substitutedGapGate`: the first of the substitute's `replacedRecords`, put into words by `notJudgedClause`. So a `m_leadKneeFlex` with no value (the fit read the knee near address only, the down-the-line posture refused) still says "address not read", not "phase not segmented". `replacedRecords` survives `gateRecordsToJson` / `gateRecordsFromJson`. Test §11b covers this.
  - **Producer identity is the gate-id area**, so a substitute must fire under an area different from the route it replaces (`body3d` replaces `body` / `posture`).
  - **The GUI's "-" words (fix round 3).**
    - **Posture chart:** `session_posture.cpp` gets `substitutedGapFor`. It is asked only for a cell with no value and no other gate words. It gives the replaced record's `claimWords` and never a ⚠.
    - **Charts → Relations and Traces:** `charts_relations_presenter.cpp` `gateOfPicked` and `charts_traces_presenter.cpp` `gateOf(refusing)` fall back to the new `trust::substitutedGapFor` when no refusal names the key.
    - **Relations `cautionOfPicked` (a value):** already right. `trust::gateFor` returns the substitute's own Downgraded `body3d.fromFit` words under the value.
    - **Trust panel:** fine as it is. `trustReadings` lists readings by record subject, so a substituted reading is one row carrying `body3d.fromFit` (Estimated), with its own evidence. It has no per-position "-".
    - **Tests:** `session_posture_test::substitutedGapKeepsItsReason` and a block in `trust_readings_test`.
    - `route_substitution.h` now needs only `gate_record.h`; the clean-up is a template over the series type.
- **W1's `shaftDirection` needs no clean-up.** No other producer's record covers it (grep: only `shaft_direction_series.h` names it in a record), and its fit fallback lives inside its own producer.

#### Precedence

`mergeSkeleton3dBody` applies the order IMU > 3-D > single camera.

- `shoulderPlaneAngle3d` and `shoulderLineYaw` have no other producer.
- `leadKneeFlexion`:
  - The down-the-line series is exactly as at HEAD (not withheld; review fix), so shots without a fit grade as they did.
  - The 3-D series REPLACES it (the down-the-line one is erased) when the 3-D series has a valid frame in [Impact, P8]. That window is the one `m_leadKneeFlex` takes its peak over; with no P8 on the ladder it is Impact ± 15 ms.
  - Otherwise the down-the-line series stays, and the 3-D one is not published beside it.
  - With no down-the-line series at all, the 3-D one is published, with `body3d.fromFit`.
- `spineSideBend`: the 3-D series is published only when no face-on series exists. The face-on rung stays the answer wherever it read.
- The thigh turns are published unless already present. They are hidden by `isResearchOnlySeries`, and have no descriptor and no provider claim.

#### Catalogue

- **Shoulder rungs.** `shoulderPlaneAngle3d` and `shoulderLineYaw` `faceOn+dtl`:
  - PLANNED dropped; Direct → Estimated;
  - the summaries say the fit's cameras are uncalibrated;
  - the howToReads no longer say "Needs the calibrated camera pair".
  - `shoulderLineYaw`'s sign glosses now say "right of the target for a right-hander", as W1 did for shaftDirection.
- **`spineSideBend` `faceOn+dtl`:** the literal `true` (planned) dropped, Estimated; summary and howToRead updated.
- **`leadKneeFlexion`:**
  - A new FIRST rung, `faceOn+dtl` (Triangulated, Estimated).
  - The existing `dtl` rung keeps its id (no clash) and its summary, but goes **Direct → Estimated**. It has to: an Estimated rung may not sit above a Direct one (`metric_catalogue_test` best-first sweep). Two-camera rigs therefore count `leadKneeFlexion` as estimated rather than measured (`setup_capability_test` output, Feet & stance 13 measured / 1 estimated). That suite pins invariants only, so no count moved.
- **Planned descriptors 11 → 9.** The manifest header was updated: THE TRIANGULATED PAIR group emptied.
- **`MetricDescriptor::baselineRoute()` (the floor) has one new exception: a live rung that needs strictly MORE kit than a live rung above it is never the floor.**
  - Without it, `spineSideBend`'s live two-camera rung becomes the "last live rung". The directory would then say side bend needs two cameras, and its stereo gain would flip from Refines to Unlocks.
  - No other descriptor has that shape today. `refines` stays 32, and `metric_catalogue_test` pins `spineSideBend`'s floor as `faceOn`.
  - This is a small semantic change in a shared header. The orchestrator may prefer another fix; see the report.

#### Pack (`core.json`) and norms

- `m_shoulderPlane` and `m_shoulderAlignment` go from noProducer to `live`; `m_leadKneeFlex` goes from held to `live`.
- All three `gapReason`s were removed. No live measure in the pack carries one.
- `golferHigh` / `golferLow` were added to the two shoulder measures. `core_pack_test` requires them on producing measures, and they pass its wording rule.
- `m_shoulderAlignment`'s `highMeans` now reads "closed — right of the target for a right-hander".
- **History moved to the norm citations.** The face-on tilt history for `m_shoulderAlignment` and the foreshortening history for `m_shoulderPlane` went into the citations in `norms.json`. The stale "nothing produces it" citation was rewritten.
- **Norm scales.**
  - μ 0 / σLo 4 for the aim and μ 25 / σLo 8 for the lead knee peak are on the same definition and sign.
  - μ 50 / σLo 10 for the shoulder plane is the same sign and definition (trail higher, its own vertical plane). Whether 50° is the right centre for this golfer's 3-D reading is for the one swing to show; it is not changed.

#### Constants and their provenance (`pp_tuned_constants.h`)

| constant | value | provenance |
|---|---|---|
| `body3d::kThighTurnMinKneeFlexDeg` | 12° | PROVISIONAL, the plan's starting value. Below it the normal's σ is more than about 5× the joints' angular σ (1/sin θ). To be seated on the one swing against the knee bend the fit reads at address and the top |
| `body3d::kThighTurnMinNormalHorizFrac` | 0.5 | design value: the plane more than 60° from vertical has no usable heading. No swing reaches it |
| reused: `bodyRotation::kTriMinBaselineM` 8 cm, `kAddrWindowUs` 250 ms, `kTriAddrMaxGapUs` 100 ms, `kTriScaleFrac` 10 % (via `BodyRotationConfig` overrides); `sampler::kWindowHalfUs` 15 ms; `dtlCamera::kDtlYawSigmaUncalDeg` 9° / `kDtlYawSigmaCalDeg` 1° (W1) | | |

#### What stays planned

- Nothing of W2's four descriptors.
- `leadKneeFlexion` and `trailKneeFlexion` keep their planned face-on rungs.

---

### W3 — readings from the trunk sensors: notes for planned_metrics_promotion.md

Implementer's section, 12 Nov 2026, revised after the W3 review (CHANGES NEEDED → fix round 1).

**No worn recording exists.** Every number below has been checked only on the synthetic body of
`trunk_e2e_test` (§j: sensors at arbitrary mounts, through the real stand-tall solve → q_anat →
body_rotation). Nothing here has met a real swing.

#### Where

- `src/Body/body_rotation.cpp` `fillTrunkBends` — the bends, on `BodyRotationResult::forwardBend` /
  `sideBend` (+ per-sample σ), emitted by `buildBodyRotationSeries`. BodyRotationStage is unchanged
  glue (it now passes `uncertainty.enabled` as `BodyRotationConfig::withSigma`).
- `src/Body/trunk_sensor_series.h` (new, header-only, the W1/W2 pattern): the §8.2 σ budget, the
  graded phase readings (`stampGradedPhases`), hip internal rotation (`buildHipInternalRotation`) and
  the precedence rule (`applyTrunkSensorPrecedence`).
- `src/Analysis/route_substitution.h` — new `supersedeReadings` (the better rung winning).
- `HipInternalRotationStage` (new, `analysis_pipeline.cpp`, after `Skeleton3dBodyStage`): thin glue.
  `decl` reads `kSkeleton3d, kSeg, kSeries`, appends `kSeries`. Runs when a `pelvisRotationSigned`
  with `routeId` `pelvisImu` exists. Version `kHipInternalRotationStageVersion = 1`, stamped as
  `analysis.versions.hipInternalRotation` (provenance only). BodyRotationStage has no version constant
  (it always recomputes); none added.
- `runProfile`: `applyTrunkSensorPrecedence` runs after every stage, BEFORE
  `reconcileSubstitutedReadings`.
- Claimed by `BodyRotationProvider` (the two bends, beside the camera providers' claims) and the new
  `HipInternalRotationProvider` (the two hip keys).

#### Definitions and signs

D13 projections (`trunk_angles.h`) on the chest / hip `q_anat` at every stream sample. Absolute: the
stand-tall solve zeroes both inclinations at standing tall, so these are comparable to the camera
readings they outrank.

| Series | Definition | Sign |
|---|---|---|
| `spineForwardBend` (`trunkImus`) | the chest's long axis s = q·ẑ in the vertical plane through the ball: `atan2(s·Ŷ, s·ẑ)`, Ŷ = ẑ × the chest's ML axis at this swing's address (`ballPlaneBendDeg`) — the trunk design's `thoraxForwardBend` as the down-the-line camera reads it. Chest sensor only | + toward the ball, through the whole swing |
| `spineSideBend` (`trunkImus`) | s·(`obliquityRad(q_pelvis)` − `obliquityRad(q_thorax)`), s = +1 right-handed, −1 left-handed (raw obliquity is + with the golfer's RIGHT side up) = `thoraxSideBend − pelvisObliquity` in the lead-up sense. Both sensors | + toward the TRAIL side (the chest's lead side up against the hips') — the face-on route's sign |
| `leadHipInternalRotation` | `pelvisRotationSigned` (pelvis sensor) − `thighTurn3dLead` | + internal rotation of the lead hip (the pelvis opening over the lead femur) |
| `trailHipInternalRotation` | `thighTurn3dTrail` − `pelvisRotationSigned` | + internal rotation of the trail hip (the pelvis closing over the trail femur) |

Both turns in the hip rows are lead-relative (+ toward the target, either hand), so the formulas hold
for a left-hander unchanged; pinned in `trunk_e2e_test` §j (left-hander mirrored: identical bends,
identical hip IR). On the synthetic swing with the thigh fixed: trail IR +42.1° at the top, lead IR
+49.2° at impact (the descriptors' reference amplitudes are ~40° trail, ~50° lead).

**The quantity mismatch the brief asked me to report.** The catalogue's `trunkImus` rung summary on
`spineForwardBend` said "thorax-relative-pelvis flexion". That is not what the camera reads (the
down-the-line hip→shoulder line, ~33° at address on 4 July) and not what §3.2 / §8.8 define as this
route (`thoraxForwardBend`, the chest's inclination). The series is the chest inclination; the
summary now says so. The sensor sits on the upper back (T2–T5), so it reads the chest's tilt, not the
hip-to-shoulder chord; how far apart the two are is for the first worn two-camera session (§14.4).
The relative reading (thorax bend − pelvis tilt) remains what `m_spineBendChangeTop` waits for; it
stays held.

**The forward bend departs from D13 (review ruling).** D13's `forwardBendRad` takes its sign from the
chest's own anterior axis, which turns with the chest: on §j's body it read about −41° through the top
window, and −44.8° at an impact 45° open with a 30° side tilt and a 35° bend, where the camera reads
+39°; and its size is the whole inclination, which a side tilt inflates. The published bend is the
ball-plane projection: sign-stable, what the camera and the norms read. A mount yaw δ moves it through
the address front's heading, ε ≈ atan(tan δ·cos θ_addr) (7.7° at δ 10°, θ 40°): only second order while
the chest stays in the ball plane (−0.25° to −0.28° at address), but FIRST ORDER once a side tilt leans it out,
ε·s_z·(s·X̂)/((s·Ŷ)² + s_z²) — −3.3° on a 35° chest, 45° open, 30° side tilt at impact (re-review). That
term at σ_δ is in the forward bend's σ, and `body.trunkMountYawAssumed` covers the forward bend too.
An open or closed chest at address makes the sensor bend differ from the camera's by a degree or two
(the camera's line is a fixed plane; the sensor's front is the chest's own); `body.chestBendSite` speaks
for it. D13 stays for the face-in cue at address. `trunk_e2e_test` §j pins it against
the body's ANALYTIC long axis, atan2(sin θ, cos θ·cos γ): > 0 at every sample from address to the finish
on that 35° / 45°-open / 30°-tilt body, +38.96° at impact, matching to 0.02° over the swing.

**The side bend at the top is the D13 obliquity of a chest turned ~90° about a bent spine.** That is
the projection's definition (D13), and it is NOT what the other routes read there: the face-on route has
no P4 reading at all (its lines are edge-on), and the 3-D route reads about −38° against the sensor's
−26° on a top with no lateral flexion. Which of the three the norm (`m_spineSideBendTop`, μ +8) means is
Mark's question; no condition reads that measure.

#### σ (design §8.2, per sample, in quadrature; `sigmaKind = Propagated` on the series and every phase sample)

| Term | Value | Applies to |
|---|---|---|
| Garment / soft tissue | 3° chest, 2° hips (§8.2 placeholders) | both bends; the pelvis turn in hip IR |
| Gyro scale | 1.5 % of the change from address | both bends (on the bend's change); the pelvis turn |
| Turn scale leaking into obliquity | ≈ sin θ · 1.5 % · \|turn from address\| per sensor (`turnLeakObliquityDeg`, the same named `kImuGyroScaleFrac`) | side bend |
| Bias drift | 0.5 °/s × time from address | each sensor used |
| Mount yaw δ | asin(sin θ·sin σ_δ), σ_δ = 10° (δ0), θ = that sensor's ball-plane bend at the sample | side bend (both sensors). Forward bend: ε·s_z·(s·X̂)/((s·Ŷ)² + s_z²), ε = atan(tan σ_δ·cos θ_addr), per sample (`ballPlaneMountYawDeg`) — first order with a side tilt, second order without. "Near zero" for turn |
| Thigh | the fit's per-frame thigh σ (W2), or `kHipIrThighFixedSigmaDeg` 6° when assumed fixed | hip IR |

Not included, and why: the ceremony's axis residuals and a slip flag do not reach the analysis
(`FusedStreams` carries neither). The turn series themselves (`pelvisRotationSigned`, …) still carry
no σ; hip IR adds the pelvis turn's budget inside its own σ.

On the synthetic body: forward bend σ at address 3.000°; side bend σ at address 8.13° (chest ball-plane
bend 40.27°, hips 20.03°); a δ = 10° chest mount moves the side bend at address by 6.409° (= asin(sin 40° sin 10°),
§6.2's 6.4°), inside that reading’s σ (8.08°); the forward bend moves 0.25° at address with no side tilt (39.75°, second order); with a 30° side tilt at
impact on a 35° chest the δ error is first order and lies inside the published σ (§j).

The series σ is the median of its valid frames' σ; a phase reading's σ is the median σ of the frames
it is the median of. With `uncertainty.enabled` off no σ is written.

#### Graded == stamped

The bends are now stamped at EVERY phase on the ladder (so the precedence rule compares like with like).
Every phase sample of the four new series is `series_reduce.h reduceAt` over the published curve at
the ladder instant (the grader's own call), its σ the median σ of the same frames. `trunk_e2e_test`
§j grades each stamped sample through the real `reduceAt`: max |graded − stamped| = 0 for both bends
and both hip sides, thigh fixed and with a 3-D thigh. Hip IR is stamped at Top and Impact (its
descriptor's phases).

#### Trust records

- **Face-in** (`body.thoraxFaceIn` / `body.pelvisFaceIn`, existing, Downgraded): the bends a label-in
  sensor reads backward joined `covers` — chest: `spineForwardBend`, `spineSideBend`; hips:
  `spineSideBend`, `trailHipInternalRotation`, `leadHipInternalRotation` (the hip IRs are built from
  that sensor's signed turn; named in body_rotation.cpp because a stage cannot read another stage's
  records during the run, and HipInternalRotationStage always runs when that turn exists).
- **`body.chestBendSite`** (new, Downgraded; producer body_rotation.cpp; subject and covers
  `spineForwardBend`; evidence `code` `upperBack`): on every published sensor forward bend — read on the
  upper back, not along the hips-to-shoulders line, not yet checked against the camera.
- **`body.trunkMountYawAssumed`** (new, Downgraded; producer body_rotation.cpp; covers whichever sensor
  bends were emitted — `spineForwardBend` and/or `spineSideBend`, subject the first; evidence `deltaTier`
  "δ0", `sigmaDeltaDeg` 10 and, with a side bend, `thoraxAddrDeg` / `pelvisAddrDeg` = asin(sin θ_addr·sin
  σ_δ) of each sensor at this address): on every published sensor bend while δ is unrefined — every swing
  today. Golfer words: which way each sensor faces on your back is taken from how it is worn, because
  standing tall cannot measure it, so the bends it reads are a few degrees less certain. When the camera
  stays the answer for one bend, the record loses that key and keeps the other.
- Both are withdrawn with their reading when the camera stays the answer (below).
- **`body.thighTurnAssumed`** (new, Downgraded; producer `trunk_sensor_series.h`; evidence `code`
  `noFit` — only when no two-camera fit reached the stage (`HipInternalRotationInput::fitUsed`) — or
  `thighUnread` — the fit did not read that thigh there; `sigmaDeg`): fires only when a GRADED hip reading (Top or Impact) has a frame
  in its window whose thigh was assumed fixed, and `covers` exactly those readings (§j: a trail thigh
  lost after 4.0 s fires it on the trail hip only). Registered with golfer words; golfer clause has no
  digits (core_pack_test passes).
- **The sensors winning: no record** (W2's rule — the better rung winning is not a fallback).
- Hip IR from the fit: no camera-assumed record — a turn from address is a difference of two bearings
  (the cameras' heading cancels) and the camera-scale assumption is already in the thigh σ
  (`kTriScaleFrac`), as for the two-camera pelvis turn.

#### Precedence

IMU > 3-D > single camera, implemented so it holds WHATEVER THE STAGE ORDER, with two exceptions
(review rulings) where another route also read the key — the camera reading then stays the answer, the
sensor bend is withdrawn, the face-in covers lose the key and the sensor's assumption records for it are
dropped:
- the sensor the key is read from is probably worn face-in (`body.thoraxFaceIn` / `body.pelvisFaceIn`
  covers the key; the hips' face-in touches only the side bend). With no other route the sensor bend is
  published with the face-in ⚠, as before;
- the sensor series has no stamped reading at a phase the camera series would be graded at — a ladder
  phase where the camera curve reads (`reduceAt`), or a phase it labels (a gap with no reason); no record
  is fired for it.

Otherwise:
`applyTrunkSensorPrecedence` runs once in `runProfile` after every stage (both sequential and
parallel paths), before `reconcileSubstitutedReadings`. For `spineForwardBend` and `spineSideBend`,
when a `trunkImus` series is present it (route_substitution.h `supersedeReadings`):
- erases every other series of the key (DtlPosture's forward bend, which runs AFTER BodyRotation; the
  face-on side bend, BEFORE it; a 3-D side bend — which Skeleton3dBody already does not publish when
  any side bend exists);
- takes every Refused / Bounded / Masked record (`body.upperAddress`, `body.sideBendLines`,
  `posture.address`) and every substitution record for another route (`body3d.fromFit`) off the key —
  same removal as W2's clean-up (key leaves covers; re-subject or drop).

**Area choice.** The rule is by EFFECT, not by gate-id area: the sensors' own records
(`body.thoraxFaceIn`) share the `body` area with the face-on upper body whose refusals must go, so W2's
area rule cannot tell them apart. It relies on the winning producer firing no Refused / Bounded /
Masked record on the key (true of body_rotation.cpp); the face-in Downgraded records are untouched.
The new `body.thighTurnAssumed` keeps the `body.` area the brief named: it covers keys no other
producer makes, so nothing needs cleaning. Tested in §j with the series in stage order and with the
sensors last: identical outcome; with no sensor series, nothing moves; a face-in chest beside camera
readings keeps both camera bends; a face-in chest with no camera reading publishes the sensor bend with
its record; a face-in hip sensor keeps only the side bend on the camera; a sensor lacking the camera's
top reading keeps the camera forward bend, and one with every camera position wins.

Hip IR reads the pelvis turn only from the pelvis sensor: the IMU tier's series now carry `routeId`
(`pelvisImu`, `thoraxImu`, `trunkImus` for the separation; `routeDirect` true); the two-camera tier
leaves it unstated as before (body_rotation_test checks both).

#### Catalogue and pack

- `spineForwardBend`: `trunkImus` rung moved FIRST, PLANNED dropped, requirement `{ Thorax }` (what the
  producer reads), summary rewritten (the chest's inclination); howToRead gains a sentence. Floor
  stays `dtl`.
- `spineSideBend`: `trunkImus` (first already) — literal `true` dropped, summary rewritten; howToRead
  gains a sentence about the sensors and the δ uncertainty. Floor stays `faceOn`.
- `hipInternalRotation` → `trailHipInternalRotation` (lavender) + `leadHipInternalRotation` (sky),
  Body rotation group, TimeSeries, °, phases Top/Impact; rungs `pelvisImu+faceOn+dtl` (Fused,
  Estimated) then `pelvisImu` (Inertial, Estimated); the `pelvisThighImus` rung deleted.
- Manifest header: 134 descriptors, PLANNED 8, "SENSORS WE DO NOT PLACE" emptied.
- `metric_catalogue_test`: 134 / TimeSeries 62 / Body rotation 10 / planned 8, plus a W3 block.
- core.json: no change. `m_spineBendChangeTop` stays held; its gapReason is still true (it waits on the
  sensors corroborating it), so it is unchanged.
- `hipInternalRotation` grep: manifest, developer guide (sign table regenerated; Appendix A rows
  replaced), `shot_analyzer_design.md`, `upper_body_face_on_metrics.md` — all updated.
  `screen.*HipInternalRotation` ids (screens.json, core.json, core-model.json) are physical-screen ids,
  not this key — untouched. `src/Gui/tests/data/charts/session_0704.json` `readAt.hipInternalRotation`
  is a frozen extraction of the manifest's readAt in a test fixture with no series under that key —
  left as is (its extraction script is not in the repo).

#### Constants (`pp_tuned_constants.h`, `tuned::bodyRotation`)

| constant | value | provenance |
|---|---|---|
| `kImuTissueSigmaThoraxDeg` | 3° | design §8.2 placeholder, to be replaced by §13's truth session |
| `kImuTissueSigmaPelvisDeg` | 2° | design §8.2 placeholder |
| `kImuGyroScaleFrac` | 1.5 % | design §8.2 |
| `kImuResidualBiasDps` | 0.5 °/s (= `calib::kTrunkStillGyroMaxDps`) | PROVISIONAL: §8.2 names the term with no number; the stand-tall gate's bound taken as 1σ |
| reused `calib::kTrunkMountYawSigmaDeg` | 10° | δ0 tier, design §6.5 |
| `kHipIrThighFixedSigmaDeg` | 6° (was 10°) | PROVISIONAL — seated by the orchestrator on 2026-07-04 swing 8 (scratch copy, `swinglab_run --write-back`, recorded pose reused): the fit's thigh turn from address read lead −2.3° at the top, +1.5° at impact, trail −1.9° at impact; the trail thigh unmeasured at the top (trail knee 1.7° bent, below `kThighTurnMinKneeFlexDeg` 12°); measured thigh-turn σ 3.5–6°, so the thigh's own turn was within the fit's resolution and 6° covers it. One swing, one golfer: a worn session with the 3-D fit seats it properly |

#### What stays planned / not done

- `m_spineBendChangeTop` held (no fault reads it; it waits on a worn session and the relative reading).
- `pelvisObliquity`, `pelvisTilt`, `thoraxSideBend` are not published as series of their own.
- The turn series' own σ (§8.2 on `pelvisRotation*` / `thoraxRotation*`) is not built.
