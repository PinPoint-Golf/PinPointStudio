# Axial body rotation — one producer, two tiers

**Audience**: developers and content authors working on the rotation half of the diagnostics model
**Code**: `src/Analysis/body_rotation.{h,cpp}`, `BodyRotationStage` (`wrist_analyzer.cpp`), `BodyRotationProvider`
**Content**: `src/Resources/diagnostics/core.json` (9 measures), `norms.json`
**Status**: producer live. IMU tier written and waiting on a placement UX. The single-camera foreshortening tier was removed (September 2026). **Since 1 October 2026 the two-camera route runs on every swing with a skeleton3d fit and no trunk IMU (§7).**
**Written**: 2026-08-02

---

## Contents

1. [Why this is not simply "planned until we have body IMUs"](#1-why-this-is-not-simply-planned-until-we-have-body-imus)
2. [The two tiers](#2-the-two-tiers)
3. [The magnitude decision](#3-the-magnitude-decision)
4. [Where the camera tier is weak — stated, and propagated](#4-where-the-camera-tier-is-weak--stated-and-propagated)
5. [What Bridged means to the golfer](#5-what-bridged-means-to-the-golfer)
6. [Open questions](#6-open-questions)
7. [The two-camera route (skeleton3d)](#7-the-two-camera-route-skeleton3d)

---

## 1. Why this is not simply "planned until we have body IMUs"

Pelvis and thorax rotation head the diagnostics roadmap by a wide margin: **eight measures and
eleven characteristics** sit over them, more than any other missing producer. They also describe
rotation about the body's vertical axis, which a frontal projection cannot see directly, and
`SegmentRole::Pelvis` / `Thorax` exist in the enum but `segmentRoleForSlot()` has never mapped a
placement slot to either — so no shot recorded by this product has ever carried one.

A module built only for the ideal sensor would therefore emit nothing on every swing the product
actually records. A module built only for the camera would throw away a bound IMU the day one
arrives. The standing rule settles it:

> **Produce the measurement from whatever is available, and take the better path automatically when
> a better sensor is there.**

This module is that rule in code, and it is the reason `MetricAvailability::Bridged` exists.

---

## 2. The two tiers

Resolved **per segment, independently**. A swing with a pelvis IMU and no thorax IMU comes back with
one measured turn and one estimated one — which is the right answer. Refusing the pair because half
of it could be better measured would throw away the half that could not.

### `RotationTier::Imu`

The segment's medio-lateral axis (anatomical `+X`, per `imu_frame_contract.md` §5's shared
`solveSegment` construction) carried into world by `q_anat` and projected into the horizontal plane.
World is Z-up, so the horizontal plane is world XY, and the direction angle of that projection IS the
segment's axial orientation. Referenced to its own address direction.

This is the quantity the descriptor names, measured rather than inferred. It resolves `Measured`.
Since 12 November 2026 each IMU-tier series says so on itself: `routeId` `pelvisImu`, `thoraxImu`
or (the separation) `trunkImus`, `routeDirect` true. The two-camera tier leaves the route unstated.

The same two sensors give the trunk's **bends** (12 November 2026, §7.4): `spineForwardBend` from the
chest sensor and `spineSideBend` from the pair, with the design's §8.2 σ.

### `RotationTier::Foreshortening`

As a body line turns away from the camera its image width collapses by the cosine of the turn:

```
turn(t) = acos( clamp( w(t) / w_address , 0, 1 ) )
```

where `w` is the hip span for the pelvis and the shoulder span for the thorax, and `w_address` is a
median over the frames in the address window. It resolves `Bridged`.

The `clamp` is not defensive tidiness. A span measuring **wider** than address is noise, or a golfer
who was not square at address; without the clamp `acos` returns NaN, and with it the honest reading
is "no turn resolved" rather than an imaginary angle.

---

## 3. The magnitude decision

**The series is the UNSIGNED MAGNITUDE of turn from address, not a signed away/toward reading.**

This looks like a limitation of the camera tier — a cosine carries no sign — and it is not. It is
what the shipped corridors require, and the IMU tier follows the same convention so the two are
interchangeable to every reader:

| Measure | Reads at | `mu` |
|---|---|---|
| `m_pelvisRotP4` | the top — turned AWAY from the target | **+45°** |
| `m_pelvisRotP7` | impact — turned OPEN toward the target | **+40°** |
| `m_thoraxRotFinish` | the finish — turned fully through | **+110°** |

A signed curve cannot satisfy those without one of them being seated negative, and none is. So the
convention was already decided by the content; the producer follows it. It also means the camera
tier invents nothing: a signed camera reading would have to be manufactured from the phase ladder,
and a sign inferred from *when* rather than *what* is not a measurement.

**The cost, stated plainly.** The curve passes through zero as the body squares up in the downswing,
so a peak reducer windowed from the top to impact sees the larger of the two excursions rather than
the open one. `m_pelvisRotPeak` and `m_thoraxRotPeak` are exactly that shape. Any measure that wants
the open peak specifically must window from square to impact, not from the top. Neither of those two
carries a corridor, so nothing grades on it today — but a future author needs to know.

---

## 4. Where the camera tier is weak — stated, and propagated

**Near square the cosine is flat.** `dθ/dw = −1 / (w₀ · sin θ)` diverges as `θ → 0`, so a pixel of
span noise becomes many degrees of turn. The producer does not hide this: it propagates the span
noise through that derivative into `MetricSeries::sigma`, with `sin θ` floored at `sin 5°` so the
reported uncertainty stays finite instead of running to infinity. A reader that ignores `sigma` will
over-read small turns.

The IMU tier leaves `sigma` **unset**, and that is deliberate rather than an omission — the field's
contract is explicit that absent means "not characterised", not "zero error", and no error budget is
propagated through that path.

**Above ~70° the span has collapsed into the noise** and the estimate saturates toward the 90°
ceiling `acos` can reach. A full shoulder turn sits right at that edge, which is why the thorax
corridors are seated wide and why the unit test asserts a 90° turn lands *high* rather than exactly.

**The address frame is assumed square to the camera.** It is the only reference available, and a
golfer set open or closed biases every reading by that amount. This is a **bias, not noise**: it does
not average out across a session, and a corpus study of it is the first open question below.

**Pelvic tilt and lateral bend also shorten the apparent span** and are indistinguishable from turn
in one projection.

None of that makes the number worthless — it makes it a camera estimate, which is what `Bridged`
exists to say. It does mean **no corridor over these may be seated tighter than the method
supports**, and the shipped ones (σ 10–12°) are not.

---

## 5. What Bridged means to the golfer

`BodyRotationProvider` is the only provider that returns `Bridged`, and its reason names the
**method** rather than a missing device:

> "estimated from the face-on camera — a pelvis / thorax IMU would measure it directly"

That phrasing matters. "Needs a pelvis IMU" reads as a refusal, and a value is in fact produced.
The distinction the three states carry is: *Measured* — we measured it; *Bridged* — we produced it,
by a weaker route, and here is the route; *Unavailable* — we did not produce it, and here is what
would be needed. Collapsing Bridged into either neighbour is a lie in one direction or the other.

---

## 6. Open questions

1. ~~**How square is address, really?**~~ **MEASURED 2026-09-18** (`kinematic_sequence_design.md`
   §12.4): on all 61 corpus swings the address span sat below the downswing's own maximum — hips by
   3.6 %, shoulders by 5.6 % — which the cosine reads as 15° / 19° of turn at address. It is the
   golfer set open at the shoulders plus setup, consistent across sessions. The sequence producer
   now references the square-up span instead; nothing here changes because this tier is gone.
2. **Should the corridors be re-seated per tier?** A camera estimate and an IMU measurement of the
   same quantity have different error, and grading both against one corridor is a compromise. The
   norm set already resolves per context; a tier axis would be a natural extension, and is not worth
   building before a single shot has carried a trunk IMU.
3. **The saturation ceiling.** Above ~70° the estimate compresses. A calibrated correction is
   possible in principle but would be fitting a curve to a corpus we do not have.
4. ~~**`kinematicSequence` is one step away.**~~ **RESOLVED 2026-09-17 — but not by this
   producer.** It was never one step from the level series: a cosine is flattest where the sequence
   needs resolution, and the magnitude convention folds at square, which destroys every rate across
   impact. The sequence now has its own producer with signed RATES, a propagated timing σ and a
   verdict that is withheld inside that σ — see
   [`kinematic_sequence_design.md`](kinematic_sequence_design.md) §2 for what changed and §9 for the
   gate the face-on pelvis and thorax nodes must pass.

---

## 7. The two-camera route (skeleton3d)

**Added 1 October 2026.** Mark asked for it after seeing a stall in the 3-D swing view that the diagnostics panel could not see: on 07-04 shot 8, every hip-rotation characteristic was "not assessable" because nothing produced a turn.

**What it reads.** `skeleton3d` fits one rigid skeleton to both views. That gives the hip line (`LeftUpLeg → RightUpLeg`) and the shoulder line (`LeftArm → RightArm`) a **bearing** in the horizontal plane. That is the geometry the foreshortening tier never had: it carries a sign, and it does not flatten near square.

**What it produces.** `BodyRotationTriangulatedStage` runs after `Skeleton3DStage`. It produces, for every trunk segment **no IMU measured**:

- `pelvisRotation` and `thoraxRotation`: |Δbearing| from Address, in the magnitude convention of §3 that every corridor uses;
- `pelvisRotationSigned`: lead-relative, + open, crossing zero at the square-up. This is the first producer that series has had, and `m_pelvisRotRateP6P7` (`hip_stall`) is live because of it;
- `xFactor` and `xFactorStretch`: only when both halves come from this route. A separation of one IMU turn and one triangulated turn would mix two references for the same zero.

A frame counts only when both of a line's joints were measured or constrained by the fit (tier ≥ Constrained), never when merely inferred. The address reference is the circular median of the usable bearings within `addrWindowUs` of Address. The route refuses when no usable frame is within 100 ms of Address.

**Uncertainty.** Each phase reading carries its own σ (sigmaKind *propagated*). It combines three terms in quadrature:

1. the bearing's σ from the fit: the two joints' posterior position σ (`FitResult::sigmaM`) through the line-angle formula;
2. the address reference's σ;
3. a **camera-scale term**, `kTriScaleFrac · |turn|`, with `kTriScaleFrac` at 10%. skeleton3d solves its own camera geometry and the down-the-line camera's placement is assumed. A mis-placed camera stretches or shrinks every triangulated angle in proportion, so this error grows with the turn rather than being constant. The 10% is a stated assumption; no IMU truth exists in the corpus to calibrate it.

The diagnostics layer reads this σ (`session_diagnostics_design.md` §A8). A reading near its edge therefore fires only weakly, and the route reports as **estimated** (Bridged, catalogue route `faceOn+dtl`), never as measured.

### 7.1 This improves with camera calibration

The route's largest error term is the assumed camera placement. It will improve when the two-camera calibration (the camera calibration thread, `two_camera_capture_protocol.md`) reaches skeleton3d:

- **The geometry itself gets better.** Measured camera poses fix the scale and skew that an assumed placement puts into every triangulated angle. This includes the high thorax readings: about 103° at the top, X-factor about 66°, on 07-04. The spine chain's own twist gives the same values (105° and 67°), so it is not arm motion leaking through the shoulder joints. It is either real for this golfer, or the assumed camera geometry, and calibration is what decides which.
- **The σ shrinks.** `TriangulatedTurnInput::camerasCalibrated` switches the camera-scale term from `kTriScaleFrac` (10%) to `kTriScaleFracCalibrated` (3%). Today the stage passes `false`, because skeleton3d writes `"calibrated": false`. When skeleton3d solves from a measured calibration, that flag should flow through, and the diagnostics will grade these readings harder automatically.
- **It can be validated.** With a calibrated pair and one session wearing the pelvis IMU (not together with HackMotion), the IMU becomes the criterion. Then the route's σ can be checked by coverage rather than assumed, and its tag can move from propagated to calibrated.

### 7.2 What it does not do

- **Forearm rotation and elbow flexion stay unproduced.** They depend on hand keypoints, which skeleton3d does not observe reliably (hand confidence lies on this pose model).
- **Hip internal rotation is not produced here.** Since 12 November 2026 the fit's thigh turn is published, research-only, by the 3-D body stage (§7.3), and `HipInternalRotationStage` subtracts it from the **pelvis sensor's** turn (§7.4). The two-camera pelvis turn is not used for it: the rung is the pelvis sensor's, and the two-camera turn carries the same fit's errors as the thigh. A thigh's axial roll is read only through the plane its knee bends in, so a nearly straight leg has none (§7.3).
- **The left-handed mirror is unverified.** The sign was checked on right-handed captures only; no left-handed capture exists.
- **`kinematicSequence` keeps its own pelvis and thorax rate producer.** That producer runs before skeleton3d. The plan to give it a skeleton rung that reads the same lines is `ks_skeleton3d_route_design.md` (designed 2 October 2026, not built).

### 7.3 The other readings from the 3-D body (12 November 2026)

`Skeleton3dBodyStage` (after this stage; `src/Body/skeleton3d_body_metrics.h` and `skeleton3d_body_series.h`) reads four more quantities off the same fit, with the same rules: joints at tier ≥ Constrained only, σ from `FitResult::sigmaM` propagated to first order, route `faceOn+dtl`, Estimated (Bridged).

| Series | Read at | Definition | Sign | σ |
|---|---|---|---|---|
| `shoulderPlaneAngle3d` | Top | the shoulder line's inclination out of the horizontal, in its own vertical plane | + trail shoulder higher | √(σ_l² + σ_t²) / line length |
| `shoulderLineYaw` | Address (one reading) | the shoulder line's bearing in the ground plane against the target line; a left-hander mirrored | open −, closed + | the line's σ ⊕ σ_yaw (9° uncalibrated; the face-on camera taken as square), plus `body3d.cameraAssumed` |
| `leadKneeFlexion` | Address, Top, Impact, P8 | 180° − the angle at the knee between hip and ankle (17/18/19, or 22/23/24 for a left-hander) | + more bend | the angle's gradient over the three joints |
| `spineSideBend` | Address, Top, Impact | the hip line's frontal tilt minus the shoulder line's, both in the frontal plane square to the pelvis heading | + toward the trail side | the two tilts' σ in quadrature |
| `thighTurn3dLead` / `thighTurn3dTrail` (research only) | Address, Top, Impact | the turn from Address of the normal to the hip–knee–ankle plane; unmeasured below 12° of knee bend | lead-relative, as `pelvisRotationSigned` | the normal's heading σ ⊕ the reference ⊕ `kTriScaleFrac`·Δ |

Precedence: the 3-D lead knee **replaces** the down-the-line one where it reads through Impact → P8; elsewhere the down-the-line lead knee stays, unchanged (`dtl_posture_design.md` §3). The 3-D side bend is published only when the face-on side bend is absent. Only the aim carries the camera-yaw σ and the camera-assumed record (`body3d.cameraAssumed`: the fit works out the cameras' directions itself, so the aim carries ±5°); the inclinations do not turn with the cameras' heading.

**When the 3-D route stands in for a single camera.** The side bend wherever it is published, and the lead knee on a shot with no down-the-line series, carry `body3d.fromFit` (Downgraded; evidence `code`, `route` `faceOn+dtl`, `replaces` `faceOn` / `dtl`). The 3-D lead knee replacing a down-the-line one does not: that is the ladder's better rung winning. After every stage has spliced its records, `runProfile` runs `reconcileSubstitutedReadings` (`src/Analysis/route_substitution.h`). For each such record whose reading is present on its route, every Refused, Bounded or Masked record from another producer (its id's area differs) stops speaking for the reading: the key leaves its `covers`; where the key was its subject, the next cover becomes the subject, or the record is dropped when it covered nothing else. The moved ids are listed in the substitute record's `replacedRecords`, so the face-on `body.upperAddress` or down-the-line `posture.address` refusal no longer sits beside a graded 3-D value. Where the 3-D series has no reading — the fit reads the lead knee near address but not through impact — a measure there is still explained by the failed route's real reason: `LiveMeasureSource::gateCause` falls back to the first of the substitute's `replacedRecords` (`substitutedGapGate`) when no record refuses the key. The charts do the same for a "-": the posture chart's cells, Charts → Relations and the trace readouts word it with that record's claim words (`trust::substitutedGapFor`), never with a ⚠. A substitute must fire under a gate-id area different from the route it replaces, because the area is how the clean-up tells producers apart. `shaftDirection` needs none of this: no other producer's record covers it, and its fit fallback is inside its own producer (`club3d.shaftDirectionFromFit`).

### 7.4 The trunk sensors' bends and hip internal rotation (12 November 2026)

Planned-metrics promotion, work item W3. **No worn recording exists**: the synthetic body of
`trunk_e2e_test` §j (sensors at arbitrary mounts, through the real stand-tall solve) is the only check
these readings have had.

**The bends** (`body_rotation.cpp` `fillTrunkBends`; route `trunkImus`, Direct, on the sensors' own
samples; the trunk design's `thoraxForwardBend` and `thoraxSideBend − pelvisObliquity`, §3.2 / §8.8):

| Series | Definition (D13 projections, `trunk_angles.h`) | Sign | σ per sample (design §8.2, in quadrature) |
|---|---|---|---|
| `spineForwardBend` | the chest sensor's long axis `s = q·ẑ` in the vertical plane through the ball, `atan2(s·Ŷ, s·ẑ)`, Ŷ = ẑ × the chest's ML axis at this swing's address (`ballPlaneBendDeg`) — absolute, zeroed by stand tall | + toward the ball, through the whole swing | chest garment 3°; gyro scale 1.5 % of the change from address; bias 0.5 °/s × time from address. The mount yaw δ, through the address front's heading: FIRST ORDER in ε ≈ atan(tan δ·cos θ_addr) once the chest leans out of the ball plane (a side tilt), ε·s_z·(s·X̂)/((s·Ŷ)² + s_z²) — −3.3° at δ 10° on a 35° chest 45° open with a 30° side tilt at impact — and only second order with no tilt (−0.25° to −0.28° at address) — in the σ as that term at σ_δ, and on `body.trunkMountYawAssumed` |
| `spineSideBend` | s·(obliquity_pelvis − obliquity_thorax), each `obliquityRad` (the ML axis's elevation) | + toward the **trail** side; s = +1 right-handed, −1 left-handed | both garments (3°, 2°); each sensor's δ term asin(sin θ·sin σ_δ) at its own forward bend θ, σ_δ = 10° (§6.2, §6.5); gyro scale on the change from address and on each sensor's turn (≈ sin θ · 1.5 % · |turn|); both sensors' bias drift |

**Why not D13's forward bend.** `trunk_angles::forwardBendRad` signs the inclination by the chest's own
anterior axis, which turns with the chest: past ~90° of turn (earlier with a side tilt or a mount yaw)
a chest bent over the ball reads negative, and its size is the whole inclination, which a side tilt
inflates. The ball-plane projection is sign-stable and is what the down-the-line camera reads. D13's
reading stays for the face-in cue at address.

The forward bend needs the chest sensor only; the side bend needs both. The quantity is the
**camera's**: the chest's inclination over the ball, comparable to the down-the-line hip-to-shoulder
line (~33° at address on 4 July), and the chest's frontal tilt against the hips', as the face-on
shoulder line against the hip line. It is not thorax-relative-pelvis flexion, which the catalogue's
`trunkImus` summary said until this change. It is the inclination of the back-mounted sensor (T2–T5);
how it compares with the hip-to-shoulder chord is for the first worn two-camera session (trunk design
§14.4). The relative reading at the top (`m_spineBendChangeTop`, thorax bend less pelvis tilt) stays
held (§8.8).

The phase readings are the GRADED ones (`trunk_sensor_series.h` `stampGradedPhases`): `reduceAt` over
the published curve at the ladder's instant, its σ the median σ of the same frames.

**Records on the bends** (Downgraded, fired with the series, withdrawn with it): `body.chestBendSite`
on the forward bend (read on the upper back, not along the hips-to-shoulders line; not yet checked
against the camera), and `body.trunkMountYawAssumed` on every published sensor bend — forward and side —
while δ is at tier δ0, every swing today (evidence `deltaTier`, `sigmaDeltaDeg`, and with a side bend each
sensor's asin(sin θ_addr·sin σ_δ) at this address).

**Precedence.** The sensor bends are the answer for their keys wherever they read, with two exceptions
where another route also read the key: the sensor the key is read from is probably worn face-in (its
`body.*FaceIn` record covers the key), or the sensor reading has no stamped reading at a phase the camera
series would be graded at (a ladder phase where its curve reads, or a phase it labels). Then the camera
reading stays, the sensor bend is withdrawn and its records stop speaking for the key (the face-in covers
lose it; `body.chestBendSite` is dropped; `body.trunkMountYawAssumed` loses the key and is dropped only
when it covers nothing else). With no other route a face-in sensor's bend is published, with the ⚠. The sensor bends are
stamped at every phase on the ladder so the comparison is like for like.
`applyTrunkSensorPrecedence` runs once after every stage in `runProfile`, before
`reconcileSubstitutedReadings`, so it holds whatever order the stages run in (the down-the-line
forward bend comes from `DtlPostureStage`, which runs after `BodyRotationStage`; the face-on side
bend from `UpperBodyMetricsStage`, before it; the 3-D side bend already steps aside when any side
bend is present). It erases the other routes' series for the two keys and takes the records that
spoke for them off the keys (`route_substitution.h` `supersedeReadings`): Refused, Bounded and Masked
records (`body.upperAddress`, `body.sideBendLines`, `posture.address`), and a substitution record for
another route (`body3d.fromFit`). **No record is fired for the win**: the better rung winning is not a
fallback, so the reading carries no ⚠ for it. The rule is by effect, not by gate-id area, because the
sensors' own records (`body.thoraxFaceIn`, Downgraded, untouched) share the `body` area with the
face-on upper body's refusals.

**Face-in.** A sensor worn label-in reads both bends backward, so the bends join its record's
`covers`: `body.thoraxFaceIn` covers `spineForwardBend` and `spineSideBend`; `body.pelvisFaceIn`
covers `spineSideBend` and both hip internal rotations.

**Hip internal rotation** (`HipInternalRotationStage`, after `Skeleton3dBodyStage`;
`trunk_sensor_series.h` `buildHipInternalRotation`). Two descriptors, `trailHipInternalRotation` and
`leadHipInternalRotation`, read at the top and at impact; the single `hipInternalRotation` they replace
had only a planned thigh-sensor rung, so nothing was ever saved under it. With both turns
lead-relative (+ toward the target):

    lead  hip IR = pelvisRotationSigned − thighTurn3dLead
    trail hip IR = thighTurn3dTrail − pelvisRotationSigned

\+ = internal rotation of that hip for either hand: the backswing closes the pelvis over the trail
femur (trail +), the downswing opens it over the lead femur (lead +). The pelvis turn is read only
from the pelvis sensor (`routeId` `pelvisImu`). The thigh is the fit's (§7.3) at an IMU sample between
two read fit frames, linear between them. Elsewhere — no two-camera fit, no thigh at address, a frame
the fit did not read — the thigh is **assumed fixed** (turn 0, σ `kHipIrThighFixedSigmaDeg` = 6°,
PROVISIONAL: seated on 2026-07-04 swing 8, where the fit's thigh turn from address read lead −2.3° at the
top and +1.5° at impact and trail −1.9° at impact — the trail thigh unmeasured at the top, its knee 1.7°
bent — against a measured thigh-turn σ of 3.5–6°; a worn session with the 3-D fit seats it properly). σ per sample = the pelvis sensor's budget (garment 2°, gyro scale on the turn, bias drift) ⊕
the thigh's σ. The record `body.thighTurnAssumed` (Downgraded; evidence `code` `noFit` / `thighUnread`,
`sigmaDeg`) fires only when a graded reading's window holds a frame that assumed the thigh, and covers
exactly those readings. Its code is `noFit` only when no two-camera fit reached the stage; with a fit it
is `thighUnread` (the fit did not read that thigh there). The series' `routeId` is `pelvisImu+faceOn+dtl` when a graded reading read the
fit's thigh, else `pelvisImu`; both rungs are Estimated. No camera-assumed record: a turn from address
is a difference of two bearings, so the cameras' assumed heading cancels, and the camera-scale
assumption is in the thigh's σ (`kTriScaleFrac`), as for the two-camera pelvis turn. Nothing in the
diagnostics pack reads these; the hip faults are confirmed by the physical screen.
