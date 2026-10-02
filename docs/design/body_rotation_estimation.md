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

- **Forearm rotation, elbow flexion and hip internal rotation stay unproduced.** They depend on hand keypoints or thigh axial roll, which skeleton3d does not observe reliably (hand confidence lies on this pose model).
- **The left-handed mirror is unverified.** The sign was checked on right-handed captures only; no left-handed capture exists.
- **`kinematicSequence` keeps its own pelvis and thorax rate producer.** That producer runs before skeleton3d.

