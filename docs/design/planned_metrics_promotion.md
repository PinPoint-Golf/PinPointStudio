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
3. **Shoulder plane norm kept at 50 ± 10°.** It is revisited only with evidence from more two-camera swings and golfers (the swing read 31°). Re-centred to 36 ± 8 on 13 Nov (GolfTEC), Mark's decision (§6, W6).
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
| knee flexion (faceOn) | Foreshortened to noise. The down-the-line route is live for both knees. The 3-D route is live for the lead knee since 12 Nov (W2) and for the trail knee since 13 Nov (W5, §6); before W5 this row wrongly said both knees had one |

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
  - μ 50 / σLo 10 for the shoulder plane is the same sign and definition (trail higher, its own vertical plane). Whether 50° is the right centre for this golfer's 3-D reading is for the one swing to show; it is not changed here. (It was re-centred to 36 ± 8 on 13 Nov: §6, W6.)

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

## 6. Second pass (13 Nov 2026): routes the first pass missed

The first pass filled the catalogue's PLANNED rungs; it never checked each source we own against each metric it can measure (Mark, 13 Nov). The second pass adds those routes. Plan: `~/.claude/plans/planned-metrics-completion.md`.

### W4: trunk-sensor routes

Three readings the chest and hip sensors make with the bends' code (`body_rotation.cpp` `fillTrunkTilts`; definitions, signs and σ in `src/Body/trunk_sensor_series.h`, from `TrailAxis` on). Each keeps the camera reading's own definition and sign, so the camera's norms apply unchanged (`m_shoulderPlane`, `m_hipLineTiltTop` 7°, `m_hipAlignment`, `m_hipLineTiltImpact`, `m_hipLineTiltShift`, the axis-tilt measures). Each needs only its own sensor.

**It changes no stored result.** No library swing has worn trunk sensors, so every library swing analyses as before. The one stored change is provenance: `versions.skeleton3dBody` 1 → 2 on two-camera swings (§ precedence below). **Validation waits on the worn session (#109)**: until then `trunk_e2e_test` §k's synthetic body is the only check.

#### Definitions and signs

The trunk frame: +X the golfer's right, +Y anterior, +Z up (`imu_frame_contract.md` §5). s = +1 for a right-hander, whose trail side is +X, and −1 for a left-hander (the side bend's factor). The face-on plane is the vertical plane containing the target line. The sensors have no target line, so it is taken square to each sensor's **address front** (`ballPlaneFront`: Ŷ = ẑ × the segment's ML axis at this swing's address). That assumes the segment was square to the target line at address. T is that plane's horizontal axis, pointing to the trail side.

| Reading | Rung | Definition | + means | Worked example (§k: chest 8° and hips 2° toward the trail side at address; −12° / −4° at the top) |
|---|---|---|---|---|
| `shoulderPlaneAngle3d` | `thoraxImu`, Inertial, **Direct** | `asin(s·(q·x̂)_z)`: s·`obliquityRad` of the chest. This is the 3-D fit's quantity, `atan2(z_trail − z_lead, \|horizontal\|)`, the line's inclination in its own vertical plane, so there is no projection and no address front. Every stream sample; no domain (the descriptor reads the top) | the **trail** shoulder higher | address −8.0° for either hand. At the top, turned 89° about a 40° bend with the trail side up 12°: **+39.1°** RH and LH |
| `hipLineTilt` | `pelvisImu`, Inertial, **Direct** | d = s·(q·x̂), the lead → trail hip direction; `atan2(d_z, \|d·T\|)`. This is the face-on `lineTiltDeg`, `atan2(leadY − trailY, \|Δx\|)`. Like the face-on producer, a frame whose hip line along the target line falls below `lowerBody.minHipSpanRatio` (0.40) × its address value has no reading (valid 0). P1→P7: masked past Impact, as the face-on series is | the **trail** hip higher | address −2.0°. At the top, 42° turned: **+21.8°**. At impact: −30.4° (the ideal camera reads −31.1°, inside σ 7.2°). Same for both hands |
| `secondaryAxisTilt` | `thoraxImu`, Inertial, **Direct** | the chest's long axis s = q·ẑ against vertical in the face-on plane, `atan2(s·T, s_z)`. This is the face-on `atan2(towardTrail, rise)`, with no reading where s_z ≤ 0. P1→P7 | leaning **away from the target** | address **+8.0°**, top −12.0°, impact +24.9° (truth 25°), for either hand |

Sign check against the raw frame: for a right-hander, s = +1. A raw obliquity that is + with the golfer's right side up is the trail side up, so it reads +. For a left-hander, s = −1: the same raw value means the lead side is up, so it reads −. `trunk_e2e_test` §k checks the left-hander's curves against the right-hander's to 0.0001°.

#### σ, per sample, in quadrature (§8.2 terms only; `sigmaKind = Propagated`)

- **`shoulderPlaneAngle3d`**: the chest's obliquity budget (`obliquitySigmaDeg`, the thorax share of `sideBendSigmaDeg`):
  - garment 3°;
  - δ through the chest's own bend, asin(sin θ·sin σ_δ);
  - gyro scale 1.5 % of the change from address;
  - the turn's scale error leaking through the bend;
  - bias drift.

  7.1° at address (the δ term 6.4° dominates), 7.3° at the top, 7.5° at impact.
- **`hipLineTilt`**: the hips' obliquity budget, CARRIED THROUGH THE FACE-ON PROJECTION, ⊕ the address front's heading term.
  - An obliquity error e moves `atan2(sin φ, cos φ·|cos ρ|)` (ρ = the hips' heading off T) by half the spread at φ ± e. That is e at address and ≈ e/|cos ρ| as the hips turn.
  - The front term is evaluated NUMERICALLY: the reading with T turned ±σ_δ about vertical, half the spread. It is second order at address (0) and first order once the hips have turned.
  - 3.95° at address, 6.0° at the top (obliquity budget alone 4.0°), 7.2° at impact.
- **`secondaryAxisTilt`**: the chest's LONG-AXIS budget ⊕ the front term.
  - The long-axis budget is garment, gyro scale on the reading's change from address, and drift (`forwardBendSigmaDeg`'s terms).
  - **No δ-through-bend term**: δ is a turn about the long axis and does not move it (`trunk_angles.h`; design §6.2's forward-bend row). δ reaches this reading only through the address front, which the front term carries. There is no turn-leak term for the same reason.
  - The front term is **first order even at address**: a long axis bent θ toward the ball, seen in a plane turned ε, leans atan(tan θ·sin ε). That is 8.2° on this 40° chest.
  - So σ is 8.7° at address, 8.7° at the top and 8.2° at impact.

**A 10° mount yaw on both sensors** (§k): every reading's error against the ideal camera stays inside its σ at address, the top and impact:

| Reading | Address | Top | Impact |
|---|---|---|---|
| shoulder plane | −6.3° in 7.0° | +1.9° in 7.4° | −0.5° in 7.2° |
| hip tilt | −3.4° in 3.9° | −3.2° in 5.9° | −0.8° in 7.3° |
| axis tilt | +6.3° in 8.2° | +6.7° in 9.1° | +5.4° in 7.1° |

**Is σ_δ the right size for the front's heading?** The front's heading has two parts:
- **The mount yaw's share.** It is ε ≈ atan(tan δ·cos θ_addr): 9.4° on hips at 20°, 7.7° on a chest at 40°. σ_δ (10°) is that share, rounded up.
- **The golfer's own address alignment.** Hips or chest set open or closed to the target line. No existing constant measures it. `dtlCamera::kDtlYawSigmaUncalDeg` (9°) is a camera's yaw, not a golfer's, and the face-on camera is ruled square.

So σ_δ is a fair single size for now: it slightly overstates δ's share on the chest, and leaves about √(10² − 9.4²) ≈ 3.4° (hips) to 6.4° (chest) for alignment. No constant was added. Once δ is refined from the swings (§6.5), the alignment share needs its own number. That number should come from the worn session (#109), with the face-on camera as the reference.

**Known omissions in the hip tilt's σ** (small while δ is at δ0; to revisit once δ is refined):
- **Correlated δ terms.** The δ-through-bend term and the front term come from one δ but are added in quadrature as if independent.
- **The turn's errors through ρ.** The turn's gyro-scale error and its heading drift move ρ (|cos ρ|) but are not propagated. That is about 0.6° against the ±10° front term.
- **Even-order effects.** The half-spread misses them: the front term is exactly 0 at address, against a true second-order effect of about 0.03°.

#### Trust records (all Downgraded; the readings stay, are judged, and carry the ⚠)

- **`body.chestShoulderSite`** (new; `gate_catalogue_body.h`, `gate_words.json`): subject and covers `shoulderPlaneAngle3d`, evidence `code` `upperBack`. Golfer words: "the chest sensor reads the tilt of your upper back, not of the shoulder joints themselves, which move with the shoulder blades, and the two have not yet been checked against each other".
- **`body.chestBendSite`** now also covers `secondaryAxisTilt`. Its description and golfer words now name the lean as well as the bend: the upper back's lean, not the hip-to-shoulder line the face-on camera reads.
- **`body.trunkMountYawAssumed`** covers all three. Its description and words say "bends and tilts", and that a turned sensor turns the direction it takes as the target line.
- **Face-in.** Each tilt reads backward under a half turn about the spine, because the ML axis reverses and so does the address front. `body.thoraxFaceIn` covers `shoulderPlaneAngle3d` and `secondaryAxisTilt`; `body.pelvisFaceIn` covers `hipLineTilt`. Their descriptions say "bends and tilts".
- **"Square at address" is now its own record** (W4c, below): `body.targetFromAddress` on the forward bend and both face-on tilts. W4 had left the assumption inside the front term's σ; Mark ruled (13 Nov) that a square set-up must never be assumed silently.
- **No record for the hip tilt's edge-on frames.** Those frames are not valid, exactly as the face-on producer's are. The face-on producer's mask record (`body.hipLine`, Masked) is not fired by the sensor: `supersedeReadings` removes Masked records from a key the sensor wins, so a sensor-fired one would be erased as it was fired. Within P1→P7 the hips stay under 66° of turn on a normal swing, so it does not bite. When it does and a camera reads the position, the camera stays the answer (fewer positions).

#### Precedence

The bends' rule (Mark, 12 Nov), extended to the three keys (`applyTrunkSensorPrecedence`, `trunkSensorKeys()`, run once after every stage). The sensor reading is the answer wherever it reads. The 3-D or face-on series of the key is erased, and the Refused / Masked records and weaker stand-ins that spoke for it come off the key. The camera stays the answer in two cases:
- the sensor is probably worn face-in (its face-in record covers the key);
- the sensor reading lacks a phase reading the camera series would be graded at.

In those cases the sensor reading and its own records (`body.chestBendSite`, `body.chestShoulderSite`, `body.trunkMountYawAssumed`, and since W4c `body.targetFromAddress`) are withdrawn from the key.

- **Stage order.** `BodyRotationStage` runs before `Skeleton3dBodyStage`, whose `mergeSkeleton3dBody` used to skip a key that was already present. That would have left no 3-D fallback once the precedence withdrew the sensor's shoulder plane. It now publishes the 3-D `shoulderPlaneAngle3d` beside a `thoraxImu` one, and still skips if another 3-D one is present. `skeleton3d_body_metrics_test` §10 checks both orders and the face-in fallback. `kSkeleton3dBodyStageVersion` is 2.
- **Also changed.** A sensor reading that stands now takes a camera route's refusal off its key even when that route wrote no series. Before this, the sensor-alone case skipped `supersedeReadings`, so a face-on `body.upperAddress` / `body.lowerAddress` refusal kept covering a key the sensor read. This applies to the two bends as well.

#### Catalogue

- Each descriptor gains its sensor rung FIRST. The camera rung stays the floor (`baselineRoute`), so what the directory says a golfer "needs" is unchanged, as is each descriptor's stereo gain.
- **Direct, not Estimated** for `hipLineTilt` and `secondaryAxisTilt` (the brief said Estimated). `metric_catalogue_test` holds every ladder best-first: no Direct rung below an Estimated one. So an Estimated sensor rung cannot sit above the Direct face-on rung, and the sensor must be first because it is the answer. This matches the bends' `trunkImus` rungs, which are Direct on the same address front. The plane's assumption is carried as σ and as Downgraded records.
- `howToRead` gains one sentence each. The manifest header's sensor-route paragraph names the three. `BodyRotationProvider::provides()` claims the three keys, beside the camera providers.
- Signs (`signPositive` / `signNegative`) are unchanged, so `tools/metrics/gen_sign_table.py` has no row to regenerate.

### W4c: the address-front ⚠ (interim, until #178)

Mark, 13 Nov: "a common fault is setup at address being faulty and one of the most important reasons we need to measure it." Three trunk-sensor readings take the target line, or the vertical plane through the ball, from each sensor's OWN heading at that swing's address (`trunk_sensor::ballPlaneFront`). They therefore take the golfer as square at address: an open or closed set-up is absorbed and not seen. Until the target line comes from the alignment stick, with its drift σ (#178), that is said on every such reading.

**The record.** `body.targetFromAddress`, Downgraded, fired by `body_rotation.cpp` `recordTrunkAssumptions` with the series. Its subject is the first of its covers; its covers are exactly the readings that were emitted from an address front:

| Reading | Front | How the front enters |
|---|---|---|
| `spineForwardBend` (`trunkImus`) | the chest's | the ball plane is square to the chest's ML axis at address (`ballPlaneBendDeg`). Second order with no side tilt, first order once the chest leans out of the plane |
| `hipLineTilt` (`pelvisImu`) | the hips' | the face-on plane's target line is the hips' ML axis direction at address (`trailAxisOf`). Second order at address, first order once the hips turn |
| `secondaryAxisTilt` (`thoraxImu`) | the chest's | the same, from the chest. First order even at address: a 40° chest set 12° open reads 10.2° less lean than the face-on camera sees (`trunk_e2e_test` §l) |

**Readings that do NOT depend on the address front, and why** (none is covered):
- `spineSideBend` and `shoulderPlaneAngle3d`: obliquities, `asin((q·x̂)_z)`, the height of the ML axis. No heading moves that. The front reaches only their σ, through |sin θ| of a forward bend (the δ and turn-leak terms), which is a size, not a direction.
- `pelvisRotation`, `thoraxRotation`, their signed twins, `xFactor`, `xFactorStretch`: heading CHANGES from this swing's own address (D13 `turnFromRad`). The address heading cancels, and every route (the two-camera one too) defines them from address, so no target line is assumed. They do not show an open set-up either, by definition; see the open question in W4c's notes.
- `trailHipInternalRotation` / `leadHipInternalRotation` (`HipInternalRotationStage`): the pelvis sensor's signed turn from address less the thigh's own turn from address. Both from address, so the address heading cancels.
- The KS rates (`ks.pelvis`, `ks.thorax`, `segment_rates.cpp`): the gyro's own S-I component, which has no heading at all.

**Evidence.** `code` `squareAtAddress`; `chestAddressHeadingDeg` (when a chest reading is covered) and `hipAddressHeadingDeg` (when the hip tilt is covered): that sensor's ML-axis heading at address in the stance frame (`trunk_angles::turnHeadingRad`, zero at stand tall), lead-relative, + open. **The headings are kept in the record for diagnostics and are NOT shown**: the evidence sentence is only "Your address was taken as square to the target line." The heading is not an aim measurement. It holds the golfer's turn between stand tall and address, the heading drift since stand tall (a 17° drift reads 17°, §l) and the mount yaw's share, so a square golfer can read 17° "open". Shown, it would read as a set-up fault, the fault Mark cares most about (W4c review). A shown aim waits on #178's stick target line with its drift σ.

**Golfer words** (`gate_words.json`): "your target line is taken from how you stood at address, so an open or closed set-up isn't seen in this reading yet". Catalogue (`gate_catalogue_body.h`): title "Target line taken from your address"; the description says the bend, the hip tilt and the spine's lean are read against the golfer's own address line, kept and judged a step lower in trust.

**Precedence.** As the sensors' other records: when the camera stays the answer (a face-in sensor, or fewer positions), `applyTrunkSensorPrecedence` takes the key off the record, moves its subject to the next cover, and drops the record when it covers nothing. A heading whose readings have all gone goes too: `hipAddressHeadingDeg` with the hip tilt, `chestAddressHeadingDeg` with the last chest reading.

**Not "Not assessable".** The readings stay graded with the ⚠, as the brief rules. No σ, norm, `core.json` status or stage version changed (`BodyRotationStage` always recomputes). No library swing has worn trunk sensors, so no stored result changes.

**Tests.** `trunk_e2e_test` §l: a golfer set up 12° open reads the same as square on every trunk-sensor reading (to 0.0001°, P1→P7 and at every graded position, both hands) while the ideal face-on camera moves by up to 8.2° (hip tilt) and 10.4° (axis tilt); the record's covers, code and headings (0° square, +12° open, for both hands; 17° under a 17° drift); each sensor alone; and six precedence cases. `body_rotation_test`: the IMU-only record count is 4 and the new record covers the three keys. `gate_catalogue_test`: the evidence sentence states the assumption alone, the same with both headings in the evidence (one of them 17°) as with none.

### W5: trail knee from the 3-D fit

`trailKneeFlexion` now has the route `leadKneeFlexion` got on 12 Nov: the two-camera fit's hip, knee and ankle (`skeleton3d_body_series.h`, `buildSkeleton3dBody` / `mergeSkeleton3dBody`; the per-frame angle is `body3d::kneeFlexDeg`, already generic over the leg). The same work item makes the 3-D side bend a fallback for the trunk sensors' one, which W4 found missing.

**Mark's rule (13 Nov), after the one-swing sheet.** On 2026-07-04 s8 the fit pulled the trail knee straight at impact: 21.8° against the down-the-line camera's 30.5°. In the face-on view at impact the hands and the banded shaft cover the trail knee, and the uncalibrated fit followed that view. Mark: "the 3d fit is likely to improve with calibration … with calibration likely the 3d fit, without calibration at impact the camera pose inference is better." So:
- **Cameras uncalibrated** (every swing today: `Skeleton3dBodyInput::calibrated`, the flag that also decides `body3d.cameraAssumed`, is always false because no calibration reaches the fit). The down-the-line `trailKneeFlexion` is the answer whenever it exists. The 3-D series only fills when there is none.
- **Cameras calibrated:** the replace rule below.
- **The lead knee is unchanged.** The down-the-line camera cannot see the lead leg through much of the swing.

**What it changes in stored results.** Two-camera swings that have a down-the-line trail knee keep it, so their trail-knee measures (`m_kneeFlexAddress`, `m_trailKneeFlex`, `m_trailKneeFlex_p2` … `_p8`) grade as before. Only a two-camera swing with NO down-the-line trail knee (the down-the-line posture refused) gains the fit's, with its ⚠ `body3d.fromFit`. `versions.skeleton3dBody` becomes 3 on every two-camera swing.

#### Definition, sign, σ

- **Value:** 180° minus the interior angle at the TRAIL knee between the hip and the ankle, in 3-D, at every fit frame. Joints `RightUpLeg` / `RightLeg` / `RightFoot` (22/23/24) for a right-hander, `LeftUpLeg` / `LeftLeg` / `LeftFoot` (17/18/19) for a left-hander: `kneeFlexDeg(fit, t, leftLeg = leftHanded)`, the lead knee's call with the other leg.
- **Sign:** the down-the-line series' (`dtl_posture.h` `kneeFlexDeg`, the same formula on the image): **+ = more bend, 0 = a straight leg**, for either hand. Worked example (`skeleton3d_body_metrics_test` §12): a right-hander whose right knee bends 17° at address and 27° at the top reads 17° / 27°, so `m_trailKneeFlex` (P1 → P4) is +10°. The mirrored left-hander, whose LEFT knee does the same, reads the same numbers. The lead knee (20° throughout) is not read for it.
- **σ:** the lead knee's gradient formula over the three joints' posterior σ (`FitResult::sigmaM`): σ_h²/a² + σ_a²/b² + σ_k²(1/a² + 1/b² − 2cos φ/(ab)), a and b the thigh and shin, φ the interior angle. 3.28° on the synthetic leg with 1 cm joint σ. Propagated on the series and every phase sample. No camera term: an angle between two segments does not turn with the cameras' heading, so no `body3d.cameraAssumed`.
- **Frame validity:** the lead knee's. Each of the three joints at tier ≥ Constrained with a finite σ, each segment longer than `kTriMinBaselineM`. A frame with no reading between two that have one is bridged (valid 0). A position is read when a valid frame lies within the grader's ±`kWindowHalfUs` (15 ms) of the ladder instant (`readAt`, the grader's `reduceAt`).
- **Stamped positions:** address, the top and impact, the down-the-line series' own. The grader reads the curve at P2, P3, P5, P6 and P8.

#### The replace/keep rule (calibrated cameras), and why both ends share a route

Built and tested, but it does not run on any swing until a calibration reaches the fit. Every trail-knee measure is the reading at address or a CHANGE from address (`delta`, anchor P1). A change taken from the fit at one end and the down-the-line camera at the other would grade the two routes' disagreement as the knee moving. So the rule is decided once per swing, for the whole series:

- **Replace** when the 3-D series has a reading at Address AND at the Top (the window `m_trailKneeFlex` grades): the down-the-line series is erased and the 3-D one published. No record: the ladder's better rung winning is not a fallback.
- **Keep** otherwise: the down-the-line series stays exactly as its producer made it, and the 3-D one is dropped, not published beside it.
- **Fill** when there is no down-the-line series at all: the 3-D series is published whatever it covers, with `body3d.fromFit` (Downgraded, `replaces: "dtl"`, `route: "faceOn+dtl"`), as the lead knee does. `reconcileSubstitutedReadings` then moves the down-the-line refusal (`posture.address`) off the key into `replacedRecords`.
- **Uncalibrated:** there is no replace. Keep whenever a down-the-line series exists (`trailKneeKeptUncalibrated`); fill as above when there is none.
- `Skeleton3dBodyMerge` records the outcome (`trailKneeReplaced` / `trailKneeKeptDtl` / `trailKneeKeptUncalibrated`) and, on a replace, the positions P2 … P8 the down-the-line series read and the 3-D one does not (`trailKneeLost`), and the reverse (`trailKneeGained`). The stage logs the two counts.

**The positions between.** A replaced series valid at P1 and P4 but not at, say, P6 makes P1 → P6 not assessable on that swing, although the down-the-line series could have graded it (§12 pins this). How often:

- **Not from the fit's coverage.** The fit's grid spans address − 150 ms … P8 + 25 ms (stage version 16), so every P1 … P8 is inside it. On 2026-07-04 s8 its spacing is 6.4–27 ms, so a read leg always has a frame within ±15 ms.
- **Only from a tier drop on one of the three trail-leg joints.** The trail leg is the one nearest the down-the-line camera, and the face-on camera sees it square. On 2026-07-04 s8 the three joints are tier 3 (measured) on all 115 frames, and the 3-D trail knee reads at all eight positions (read-only probe of the stored fit, no re-analysis).
- **So it should be rare** once the replace rule runs (calibrated cameras only). The brief's rule is implemented as written. The sweep's log line (`positions lost N, gained M`) counts it across the library. If losses turn out common, the proposed fix is a stricter replace rule: replace only when the 3-D series reads every position P1 … P8 that the down-the-line series reads. That rule can never lose a grading, at the cost of keeping the camera's reading on more swings.

**What the one-swing check showed** (my read-only probe of 2026-07-04 s8: stored fit joints against the stored down-the-line series, the grader's ±15 ms median, not the stage's output. The orchestrator's sheet read 21.8° / 30.5° at impact):

| | P1 | P2 | P3 | P4 | P5 | P6 | P7 | P8 |
|---|---|---|---|---|---|---|---|---|
| down the line | 17.3° | 12.8° | 3.0° | 1.7° | 20.3° | 28.4° | 30.7° | 34.1° |
| 3-D fit | 21.1° | 15.1° | 5.4° | 2.5° | 22.1° | 28.1° | 21.9° | 35.1° |

The two agree within about 2–4° except at impact (P7), where the fit reads 8.8° less bend. Replacing would have moved P1 → P7 from +13.4° to +0.8°. Under the plan's gate rule that disagreement went to Mark, whose ruling is above.

#### The 3-D side bend as the trunk sensors' fallback

`mergeSkeleton3dBody` skipped the 3-D `spineSideBend` whenever any `spineSideBend` series existed. That included the trunk sensors' one (route `trunkImus`), which `BodyRotationStage` writes first and `applyTrunkSensorPrecedence` may later withdraw: a sensor worn face-in, or a sensor with fewer positions. With no face-on side bend, the swing then had none. W4 fixed this exact shape for `shoulderPlaneAngle3d`; the same fix is applied here:

- The 3-D side bend is published unless ANOTHER route's series is present: the face-on one (the better rung, the answer wherever it read) or another 3-D one. A `trunkImus` series no longer stops it.
- Beside the sensors' reading, the precedence decides. If the sensors' reading stands, `supersedeReadings` erases the 3-D series and its `body3d.fromFit` record (a weaker stand-in). If it is withdrawn, the 3-D series is the answer, with its `body3d.fromFit`.
- `skeleton3d_body_metrics_test` §13 checks both stage orders (the sensors' series before the merge, as in the pipeline, and after it). Sensors face-in with no face-on reading: the 3-D side bend fills. Sensors trusted: the sensors' reading wins. A face-on reading present: it stays the answer and no 3-D series is added. It also checks the fewer-positions case, and that a second 3-D series is still blocked.

#### Catalogue, provider, version

- **The ladder:** `dtl` (Triangulated, **Direct**, both cameras: unchanged), then the new `faceOn+dtl` (Triangulated, Estimated, both cameras: "180° less the angle at the trail knee between the hip and the ankle, from the two-camera 3-D fit of the body (uncalibrated: the fit solves its own cameras), where the down-the-line reading is missing"), then `faceOn` (planned, unchanged).
- **Why the 3-D rung sits BELOW the down-the-line one.** The route vocabulary (`MetricRequirement`) cannot say "calibrated cameras". It states devices; `minTier` is never raised by a second camera, and the manifest rules it out as a stand-in for kit. So the catalogue states the rule that holds on every rig today, the uncalibrated one: the camera first, the fit as the fallback. This is the same shape as `spineSideBend` (face-on Direct, the 3-D rung below it). When a calibration requirement is added, the rung moves above `dtl` and needs it. No requirement kind was added.
- **What a rig resolves to:**
  - **Two cameras (uncalibrated, as every rig is today):** `dtl`, **Measured**, as before W5. The setup summary's "Setup and stance" row and the model browser are unchanged from before W5. (Round 1 of W5 had put the 3-D rung first, which made this Bridged; Mark's rule undid that.)
  - **Two cameras, calibrated (not possible yet):** the catalogue would still resolve `dtl`, Measured, while the stage lets the fit replace the down-the-line series. The catalogue would then understate the route until the calibration requirement exists.
  - **Down-the-line camera only:** unchanged. It was Unavailable before and is still, because the `dtl` rung also needs the face-on camera (a known, older gap).
  - **Face-on only:** Unavailable, as before (the face-on rung is planned).
- **The floor** is now `faceOn+dtl`, the last live rung. It needs the same two cameras as `dtl`, so the floor's requirement ("needs" both cameras) and the stereo gain (Unlocks) are unchanged.
- `howToRead`: the down-the-line camera reads it even with the face-on camera as well. The 3-D fit stands in only where that reading is missing, because until the cameras are calibrated the fit follows the face-on view, where the hands and the shaft cover the trail knee at impact. Once they are calibrated, the fit will read it wherever it sees the knee at both address and the top, so every change from address is read by one route. The sign sentence ("HIGHER MEANS MORE BEND") is unchanged.
- `Skeleton3dBodyProvider::provides()` claims `trailKneeFlexion`, beside `DtlPostureProvider`.
- `kSkeleton3dBodyStageVersion` 2 → 3, with a history line; `swing_doc_test` pins 3.
- No `core.json` status, norm or gate changed. `body3d.fromFit` is the existing record.

### W6: shoulder-plane norm

**Mark's decision, 13 Nov 2026.** The `m_shoulderPlane` norm (`norms.json`) goes from μ 50 / σLo 10 to **μ 36 / σLo 8**, still `"source": "heuristic"`, provisional until it is seated at #109 (the worn session). Same measure, sign and definition: the shoulder line's inclination out of the horizontal at the top, in its own vertical plane, + with the trail shoulder higher (`shoulderPlaneAngle3d`, off the 3-D fit or, since W4, the chest sensor).

- **Where 36 comes from.** The GolfTEC SwingTRU motion study (vest sensors, more than 13,000 golfers): tour players about 36° of shoulder tilt at the top and about 28° at shaft-parallel back; high handicappers 25–30° at the top. That is a commercial dataset on a different instrument, so it is not proven to be the same scale as the 3-D fit or the chest sensor.
- **The brand stays out of `norms.json`.** `core_pack_test`'s brand rule forbids the study's company name in the three content files, so the citation there calls it "a commercial teaching chain's published motion study" and points here. This doc is the one place it is named.
- **The library on 13 Nov** (medians): Nick 36.5°, Mark 30.1°, Jess 26.9°, Paddy 14.3° (an upright posture), Chris 11.2° (a suspected reading failure). Recorded, not used to set the centre.
- **The 50° it replaces had no source.** The citation says so, and keeps the reading's history: what it grades, how it is read, and the face-on foreshortening that retired the P4 image reading on 14 Sep.
- **What grades against it.** The corridor moves with the norm; the flat/steep faults' conditions and signals are unchanged here. W7 (below) moved those faults to a relative reading, the shoulder plane at the top against the forward bend at address; this norm stays as the absolute reading's context, and no fault grades it.
- **Also fixed: the stale ±5° camera yaw** left by cab410f7. The `m_shoulderAlignment` citation, `body_rotation_estimation.md` §7.3 and the `shaftDirection` and `shoulderLineYaw` rows of `metric_catalogue_developer_guide.md` Appendix A (Club delivery, Alignment) now say 9°, `tuned::dtlCamera::kDtlYawSigmaUncalDeg`.
- **Tests.** No test or golden pinned 50 for this norm (every consumer reads `norms.json` through the provider and checks shape, not value). `model_browser_test`'s list at the "derived measure labels" block names the three measures that carry `label: ""` in `core.json`, not the ones with no producer; `m_shoulderPlane` still carries an empty label, so the list is right and stays.
- No `core.json` status, condition, signal, edge, σ constant or stage version changed. No stored result changes; only the grade read against the norm does, at the next diagnostics pass.

### W7: shoulder tilt against posture

**Mark's decision, 13 Nov 2026** (design B, as approved): option (a), a producer-side point reading; the flat and steep faults grade the RELATIVE reading; norm μ −3, σLo 5; the two posture → shoulder-plane edges removed.

**The idea.** A golfer who turns the shoulders square to the spine finishes the backswing with the shoulder line inclined by about the spine's forward bend. GolfTEC (Josh Troyer): tour players tilt about **3° less than their forward bend at address** (about 40° of bend, about 37° of tilt). An absolute norm (36°, W6) calls an upright golfer flat and a bent-over one steep for the same turn. The relative reading asks whether the shoulders turned on the plane this golfer's posture set.

**Evidence from the library** (ledgers at cab410f7, graded shots only, read 13 Nov):

| Golfer | Shots | Tilt at the top (median) | Forward bend at address (median) | Tilt − bend (median) | Middle half |
|---|---|---|---|---|---|
| Jess | 23 | 26.9° | 29.7° | **−2.6°** | −4.3 … +2.3 |
| Nick | 37 | 36.5° | 37.9° | **−3.2°** | −9.1 … +2.0 |
| Paddy | 11 | 14.3° | 18.1° | **−3.9°** | −5.3 … −2.5 |
| Mark | 127 | 30.1° | 36.5° | **−6.1°** | −10.6 … −2.9 |
| Chris | 8 | 11.2° | 35.2° | **−24.0°** | −25.9 … −19.5 |

- Four of the five centre within 3° of Troyer's −3°. That is a different instrument, so this is agreement, not proof.
- Paddy's low absolute tilt is his upright posture (18° of bend), not a flat turn: the absolute norm calls him flat, the relative one does not.
- Mark is the flattest of the four against his own posture (−6°), Jess the closest to it (−2.6°), which matches what Mark sees.
- Chris (−24°) is not a swing: a 35° bend with an 11° tilt is a reading failure. Check his top frames before trusting any shoulder-plane reading for him.

#### The reading

- **`shoulderTiltVsBend`** = `shoulderPlaneAngle3d` at the top − `spineForwardBend` at address, in degrees (`src/Body/shoulder_tilt_vs_bend.h`, `deriveShoulderTiltVsBend`). One phase sample at the Top and no curve, as the setup readings are written.
- **Sign:** + means the shoulders turned more steeply than the posture sets them up to; − means flatter. Neither half flips for a left-hander (the tilt is + with the trail shoulder higher, the bend + toward the ball), so neither does the difference. Worked: a 37° tilt on a 40° bend reads **−3°** for either hand. On `trunk_e2e_test` §m's body (the chest bent 40° and leaning 8° to the trail side at address, the trail side up 12° at the top) the chest sensor reads 39.05° − 40.28° = **−1.22°**, the same for both hands.
- **Where it is made: one step in `runProfile`, after `applyTrunkSensorPrecedence` and `reconcileSubstitutedReadings`**, not a stage. Every stage runs before the precedence, so a stage would see the halves before the precedence settled which route won; the step sees exactly the series and records that will be written. It is header-only, so `shoulder_tilt_vs_bend_test` and `trunk_e2e_test` reach it without the pipeline.
- **Each half is read exactly as the grader reads it** (`graderReadingAt`, the code of `measure_sample.cpp` `buildPhaseGrid` + the `at` reducer): the median of the valid samples within ±`kWindowHalfUs` (15 ms) of the ladder instant, one sample enough (`reduceAt`, or the instant on a fitted curve); else the phase sample the series labels at that phase; for a phase the ladder lacks, the curve at the label's own instant first. The ladder's first event for a phase wins, as in the grid; an empty ladder reads nothing. So `m_shoulderTiltVsBend` is exactly `m_shoulderPlane` − (`spineForwardBend` `at p1`) on the same swing; `shoulder_tilt_vs_bend_test` §2 checks that through the grid itself, on the document the pipeline writes.
- **Not produced** when either half has no reading at its phase: no series, no zero, no half value.
- **The reason given is the missing half's** (W7 review). Every Refused, Bounded or Withheld record that names the missing half (subject or covers: the effects `gates::primaryRecord` ranks) also covers `shoulderTiltVsBend`, so `LiveMeasureSource::gateCause` words the half's real refusal. An example is `posture.address` on a swing whose down-the-line posture refused at address, never the generic "metric not produced on this capture". Downgraded and Masked records are not extended: a kept-value warning or a frame count is not a reason for absence. A missing half with no such record touches nothing (the generic reason then stands, and it is true). Idempotent: the clean-up strips the key from every record first.

#### σ

- The two halves' graded σ (the phase sample's own, else the series σ: the grader's rule) **in quadrature**, on the phase sample and the series, `sigmaKind` Propagated (AssumedInput when either half's is). No σ on a half: no σ. A half whose σ is only a noise floor (a series σ with no `sigmaKind`, which the grader tags Noise) keeps the result a noise floor: a series σ with no kind, no phase-sample σ.
- **Both halves from the chest sensor share one term, the mount yaw δ.** The tilt carries it through the chest's bend (`mountYawObliquityDeg`, asin(sin θ·sin σ_δ), 6.41° at 40°); the bend carries it through its address front (`ballPlaneMountYawDeg`: second order with no side lean, 0.63° on §m's body with its 8° lean, so the bend's σ there is 3.065° against the garment's 3°). The other terms do not correlate: the garment moves the bend about the chest's left-right axis and the tilt about its front-back one, the gyro-scale and drift terms are zero at address, and the turn leak is the tilt's alone.
- **It cannot be taken exactly here.** The series carry σ, not signed sensitivities, and the two δ effects' signs depend on the turn and the lean.
- **Quadrature is conservative in practice.** The tilt's δ term is budgeted at its address worst case and not reduced as the chest turns. On a 40° bend the true δ effect on the obliquity is 6.41° at no turn, 5.4° at 45°, 4.3° at 60° and 0.73° at 90° (numerically, ±10°). The largest cross term the two can add is 2 × that × 0.63°. The budget's surplus covers it for any chest turn at the top beyond about 37°; at a top with no chest turn at all the joint value could exceed quadrature by at most 0.55° (7.66° against 7.11°). Tops turn 80–100°.
- **Mixed instruments** (a camera half and a sensor half, or the 3-D fit and the down-the-line camera): independent errors, so quadrature is the joint value.
- **On every two-camera swing today the reading has NO σ.** The down-the-line `spineForwardBend` carries none (`dtl_posture.h` stamps σ only on the ball-reach readings), and "no σ on a half, no σ" then drops the 3-D tilt's propagated σ too. That is honest (not characterised, never a smaller σ), and the mixed-sources ⚠ is then the ONLY uncertainty marker the reading shows. `howToRead`, the record's explanation and its golfer words say so, and none implies a margin. The uncertainty census (`uncertainty_census_design.md` §1) lists it. A σ on the down-the-line bend fixes both readings.

#### Trust records

- **The halves' records cover it.** Every Downgraded or Withheld record whose subject or covers name either half also covers `shoulderTiltVsBend`, as `xFactor` sits under each turn's records. On a worn swing: `body.chestBendSite`, `body.chestShoulderSite`, `body.trunkMountYawAssumed`, `body.targetFromAddress` (and the face-in record when it still covers a half). Withheld is included because a half that may not be judged takes the difference with it (the series' `withheld` is copied too). Refused, Bounded and Masked records are not: they count a half's frames or refuse a half's value, and the reading exists only when both values do. Because this runs after the precedence, a sensor's records already withdrawn from a half (the camera kept the answer) do not reach it.
- **`body.tiltBendMixedSources`** (new, Downgraded, subject and covers `shoulderTiltVsBend`, producer `shoulder_tilt_vs_bend.h`): fired whenever the halves are not both from the chest sensor. Evidence `tiltFrom` / `bendFrom` (the halves' routes; a bend with none stated is the down-the-line camera's, `dtl`). Sentence: "Your shoulder tilt was read from the 3-D fit of both cameras and your posture by the down-the-line camera." Golfer words (`gate_words.json`): "your shoulder tilt and your posture were measured by different instruments that haven't been checked against each other, so the comparison is less certain than either reading alone". It names no margin, because on two-camera swings there is none (σ above).
- **Two more warnings on the tilt (FB, plane stack, 13 Nov 2026; `docs/design/plane_stack_design.md` §11 FB).** Both are Downgraded, never a refusal: the reading is kept and judged a step lower in trust.
  - **`body3d.shoulderViewsDisagree`** (producer `skeleton3d_body_series.h`, fired by `Skeleton3DStage` after the fit): at the Top ± 15 ms, the down-the-line keypoints' shoulder line (both confidences ≥ 0.7) against the fit's shoulder joints projected into that camera; a median angle above 7° (`body3d.shoulderViewsDisagreeDeg`) fires it. It covers `shoulderPlaneAngle3d`, `shoulderLineYaw` and the two-camera thorax turn (`thoraxRotation`, `thoraxRotationSigned`, `xFactor`, `xFactorStretch`), scoped after the precedence to the readings that stand and came from the fit (`scopeShoulderViewsRecord`, `runProfile`). `shoulderTiltVsBend` inherits it from its tilt, by the rule above. Evidence `medianDeg`, `limitDeg`, `frames`, `fromMs`, `toMs`. Golfer words: "the two cameras disagree about where your shoulders are at the top, so this reading is less certain".
  - **`body.tiltBendOutOfRange`** (producer `shoulder_tilt_vs_bend.h`): the backstop. A reading below −20° (`body3d.tiltBendFloorDeg`) is outside what a turn can make: with a 35° forward bend it would need under ~27° of shoulder turn. It is the library's clean 1st percentile. Subject `shoulderTiltVsBend`, covers it and `shoulderPlaneAngle3d`; evidence `valueDeg`, `floorDeg`. Golfer words: "this shoulder tilt is outside what a full turn can make, so it is probably a misreading". Chris's −24 (§ above) gets it.
  - `versions.shoulderTiltVsBend` 2.

#### Routes (the catalogue's rungs, best first) and precedence

| Rung | Tilt | Bend | Method, quality | Record | When |
|---|---|---|---|---|---|
| `trunkImus` | chest sensor (`thoraxImu`) | chest sensor (`trunkImus`) | Inertial, Direct | none extra | sensors worn and trusted |
| `thoraxImu+dtl` | chest sensor | down the line | Fused, Direct | mixed sources | the sensor's bend withdrawn (face-in, or fewer positions) and no 3-D tilt to replace its tilt |
| `faceOn+dtl` | 3-D fit | down the line | Triangulated, Estimated | mixed sources | two cameras, no chest sensor (every library swing today) |
| `trunkImus+faceOn+dtl` | 3-D fit | chest sensor | Fused, Estimated | mixed sources | the sensor's tilt withdrawn, its bend kept |
| — | (none) | — | — | — | one camera only: no tilt at the top, not assessable |

- The precedence is the trunk-sensor rule (W3/W4), unchanged: each half is whatever route it left. The reading takes no route decision of its own.
- `routeDirect` is true on the two Direct rungs. The two rungs that need more kit than one above them (`thoraxImu+dtl`, `trunkImus+faceOn+dtl`) never resolve in the catalogue ahead of `trunkImus`; they name what the precedence can leave. The floor is `faceOn+dtl`, as `shoulderPlaneAngle3d`'s.
- `BodyRotationProvider::provides()` claims the key: its precedence settles both halves.

#### Catalogue, pack and norm

- **Descriptor** `shoulderTiltVsBend`: "Shoulder tilt against posture", Body rotation, PointInTime at Top, `orchid` (the shoulder plane's; a point reading may share). `howToRead` states the definition, the sign and the published 3° rule without the brand. It also says that with two cameras the down-the-line bend states no margin, so the warning is the only sign of uncertainty there, and that a missing half's reason is the one given. `usedBy`: the two faults; `shoulderPlaneAngle3d` no longer lists them.
- **Measure** `m_shoulderTiltVsBend` (`core.json`): `provided`, `at` p4, live. Golfer name "your shoulder turn against your posture"; high "your shoulders turn more steeply than your posture sets them up to"; low "your shoulders turn flatter than your posture sets them up to".
- **Signals:** `sig_flatShoulderPlane` (low) and `sig_steepShoulderPlane` (high) read it.
- **`m_shoulderPlane` stays live and shown**, against its 36 ± 8 norm, as context; no signal reads it. The pack validator then lists it as `unusedMeasure`, a warning (`characteristic_pack.cpp`); no test pins that, and no integrity rule needs a live measure to be read. Its descriptor names no trace corridor, and the new reading is a point reading, which cannot borrow one (`metric_catalogue_test`'s R31 guard: only TimeSeries, only with Mark's say-so), so no corridor moved.
- **Conditions:** the golfer lines say the relative meaning: flat "your shoulders turn flatter than your posture sets them up to", steep "your shoulders turn more steeply than your posture sets them up to". `golferWell`, `cue`, `check`, `golferWhy`, `consequence` and `drills` are kept: none becomes false, and the down-the-line "line through your shoulders points at the ball" check is already the relative test.
- **Edges:** `posture_too_upright → flat_shoulder_plane` and `posture_too_bent → steep_shoulder_plane` are deleted (Mark: under the relative meaning a posture cannot produce them; posture's own effects are linked directly already). Every other edge stays, including `c_posture → flat_shoulder_plane`, `s_posture → steep_shoulder_plane` and `loss_of_posture_backswing → flat_shoulder_plane`. 665 → 663 edges.
- **Norm** (`norms.json`): `m_shoulderTiltVsBend`, μ **−3**, σLo **5**, heuristic, provisional until seated at #109 with both parts from the chest sensor. The corridor is −8 … +2. Graded as every norm is (`norm.h` `GradePolicy`: amber past one tolerance, a fault past two), it is **amber from −8 / +2 and fires below −13 (flat) and above +7 (steep)**. (Corrected 13 Nov 2026, Mark's decision: the norm stays −3 ± 5 and only the words change. They had said "flat below −8, steep above +2", which are the amber edges, not the firing ones.) The citation gives the published rule without the brand (the content files carry none; this doc names GolfTEC and Troyer), the library medians above, and Mark's decision. On the library: Jess, Nick and Paddy's medians sit inside; Mark's median (−6.1°) is inside, but his flatter quarter (below −10.6°) reaches amber, and only his shots below −13° fire; Chris fires on every shot until his reading is fixed.

#### Version, tests

- **`versions.shoulderTiltVsBend` 1** (`kShoulderTiltVsBendVersion`, new): the step has no stage, so it has its own stamp, written only when the reading was; never reused. `swing_doc_test` pins 1 and its absence.
- **Tests:** `shoulder_tilt_vs_bend_test` (new: every route pair; the grader's own reading through `buildPhaseGrid` on the document the real writer (`SwingDocWriter::writeSwingJson`) writes; missing halves; σ cases; records; the mixed record and its sentence; idempotence; §7: a refused bend, a refused / bounded / withheld tilt and a bridged top each name the reading, `LiveMeasureSource::gateCause` on the written document gives `posture.address`, and a missing half with no record touches nothing); `trunk_e2e_test` §m (the real chest-sensor series and records after the precedence, both hands; a face-in chest beside the cameras gives the cameras' pair); `metric_catalogue_test` (the ladder, resolution, claim, usedBy; descriptor 134 → 135, PointInTime 64 → 65, Body rotation 10 → 11); `live_measure_source_test` (measures 212 → 213); `axis_direction_test`'s two rationale lines.
- **What it changes in stored results.** Every library swing with a 3-D shoulder plane at the top and a down-the-line forward bend at address gains `shoulderTiltVsBend` (route `faceOn+dtl`, with the mixed-sources ⚠) at the next re-analysis. A swing missing either half has no reading, so its flat/steep faults are not assessable where the absolute tilt alone used to grade them. The commonest case is a two-camera swing whose down-the-line posture refused at address. Its "not judged" reason is that refusal (`posture.address`), carried onto the reading's key, so it is shown with its real reason, not hidden behind "metric not produced", and the flat/steep faults grade it instead of the absolute tilt. The flat/steep counts per golfer before and after come from the orchestrator's one-swing check, sweep and regrade; nothing was swept here.
