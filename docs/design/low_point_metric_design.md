# PinPoint — Low-Point-Ahead-of-Ball: Technical Design

**Status:** v1 **enabling work landed** (this change): the ball-diameter scale
constant and the persisted, co-registered ball position/radius are now in
`swing.json`. **The metric computation and any UI display are deferred** — see
§6. No user-facing number ships in v1.

**Scope:** define, as a drop-in contract, the club-delivery metric *low-point
distance ahead of the ball* — how far target-side of the ball the clubhead
reaches the lowest point of its swing arc — estimated from the face-on shaft
track + the ball position, reported in **signed inches** (+ = ahead / target
side, − = behind).

**Why:** it's a coaching-valuable delivery metric that the GCQuad does not
provide, and we already track the (approximate) clubhead trajectory and the ball.

---

## 1. Definition

`lowPointAheadIn` = signed horizontal (target-line) distance, in inches, from the
ball centre to the clubhead's arc low point.

- **+** the low point is ahead of the ball (target side) — the descending-blow
  ball-then-turf pattern good iron players want.
- **−** the low point is behind the ball (fat/scoop pattern).

Per-session, per-shot; never aggregated. Criterion-referenced (a real geometric
distance), not scored against a band in v1.

## 2. Inputs — all already persisted after this change

Everything the computation needs is in `swing.json`, so it can run **offline**
(re-analysis over a corpus) with no re-capture:

| Input | Source in swing.json | Notes |
|-------|----------------------|-------|
| Clubhead trajectory | `analysis.club.samples[].head` = `[x,y]` normalized 0..1 by `frameWidth`/`frameHeight`; per-sample `t_us`, `theta`, `flags` | y is **image-down**. `flags & 0x10` (`ShaftHeadProjected`) marks projected (not measured) frames. Only written when the shaft track is valid. |
| Ball centre | `setup.ballDetection.center = [x,y]` (full-frame normalized) | co-registered with `head`; per face-on camera. Omitted when uncalibrated. |
| Ball scale | `setup.ballDetection.radiusNorm` (normalized to frame **width**) | `radiusPx = radiusNorm · frameWidth` |
| Impact | `capture.impactUs` (IMU jerk-peak; also `analysis` Impact phase) | anchors the low-point search window |
| Handedness | `athlete.handedness` | target-direction sign (with mirror) |
| Scale constant | `pinpoint::ballcal::kBallDiameterMm = 42.67` (`src/Pose/ball_model.h`) | R&A/USGA ball diameter |

The `setup.ballDetection` position is the **calibrated address ball**
(`BallCalProfile.ball.calibCenter`/`radiusPx` resolved to full-frame coords by
`CameraInstance::applyBallCalProfile`), i.e. where the ball sat before the swing
— the correct stationary reference, at the ball's ground-plane depth (so the
ball-diameter scale is exact at the measurement point).

## 3. Geometry (the deferred computation)

All in the face-on image plane (`ReconstructionTier::Angles2D`; no 3-D/ground
plane exists). Let `W = frameWidth`.

1. **px→mm scale** at the ball's depth: `mmPerPx = kBallDiameterMm / (2 · radiusNorm · W)`.
2. **Low-point frame**: over `head` samples in an impact-anchored window
   `[impactUs − Δ, impactUs + Δ]` (Δ ≈ one downswing-to-early-follow-through
   span; irons bottom out at/just after impact, driver before), take the sample
   with maximal `head.y` (lowest in the image). Prefer a **parabola-vertex fit**
   of `head.y` vs `head.x` (or vs `t`) across the few samples around that
   minimum for a sub-frame low-point `head_x_lp`.
3. **Signed distance**: `aheadMm = (head_x_lp − ball_x) · W · mmPerPx · chirality`;
   `lowPointAheadIn = aheadMm / 25.4`.
   - `chirality` (±1) folds handedness × mirror. Reuse the shaft tracker's
     resolution (`autoChirality` from pose hand-centroid ordering,
     `shaft_tracker.cpp:455-546`); handedness-only fallback `= (handedness==2) ? −1 : +1`.

## 4. Accuracy caveats (why compute is deferred)

- **The clubhead is projected, not measured.** `ShaftSample2D.headPx` is a
  ridge-terminus seed or `grip + visibleLenPx·dir(θ)` (flagged
  `ShaftHeadProjected`). The vertical arc it traces is dominated by shaft angle ×
  visible length, so its low point is a weak proxy for the true clubhead
  ground-strike low point — especially near impact (motion blur, foreshortening,
  specular dropout shrink `visibleLenPx`).
- **The real unlock is the measured clubhead detector** (Stage 2 —
  [clubhead_detection_design.md](clubhead_detection_design.md); Python exemplar
  only, not yet in the app). Once `head` is measured, this metric is a small,
  trustworthy addition; on the projected head it is an experimental estimate.

## 5. Where it slots (compute phase)

Model it on `buildShaftLeanSeries()` (`src/Analysis/wrist_analyzer.cpp:56-82`):

- Compute inside the real analyzer — **`WristAnalyzer`** today (the only analyzer
  that runs pose+shaft; Wrist sessions hit balls on a face-on camera). Emit a
  `MetricSeries` (key `lowPointAheadIn`, unit `in`) with a single phase sample at
  the low-point frame, plus the scalar in `ShotAnalysisResult.metrics`
  (key→{label,value}) → shot-card carousel. Not the session summary.
- If/when Swing (type 0) becomes a real analyzer, share the computation via a
  small helper consuming `ShaftTrack2D` + ball, callable from both.

## 6. Wiring to add AT compute time (NOT in this change)

- **`ShotAnalysisJob` ball fields** (centre normalized, `radiusNorm`, face-on
  camera source) — resolve on the UI thread in `ShotProcessor::buildAnalysisJob`
  from the face-on `CameraInstance` (`ballCalHasPosition()`/`ballCalCenterX/Y()`/
  `ballCalRadiusNorm()`), and **restore in `swing_reanalyzer.cpp`** from
  `setup.ballDetection`. (Adding these fields now would be unread dead state.)
- Close the known reanalyzer gaps this metric also wants: `clubLengthM` and
  `setup.mirrored` are not restored into the offline job today.

## 7. Validation

The GCQuad does **not** provide this metric, so ground truth is a **physical**
measurement: divot-start / mat strike-line distance from the ball. Corpus-gated
on SwingLab exactly like the shaft / ball / segmentation work
(single labelled swing = development data only; accuracy gates are corpus-scale,
per pipeline_validation_and_tuning.md). Report Limits-of-Agreement vs the
physical reference across many swings and multiple clubs before surfacing the
number in the UI.

## 8. v1 as-built (this change)

- `kBallDiameterMm = 42.67` — `src/Pose/ball_model.h`.
- `CameraInstance` resolves the calibrated ball to full-frame-normalized
  `center`/`radiusNorm` (+ `hasPosition`) — `src/Gui/cameras/camera_instance.*`.
- Persisted to `swing.json` `setup.ballDetection.{center, radiusNorm,
  positionSource}` per camera (additive; omitted when uncalibrated) —
  `src/Export/swing_exporter.*`, populated in `src/Gui/shot/shot_processor.cpp`.
- Clubhead trajectory persistence (`analysis.club.samples[].head`) was already
  present — unchanged.
