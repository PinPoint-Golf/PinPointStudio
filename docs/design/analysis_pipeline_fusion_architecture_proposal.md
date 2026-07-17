# PinPoint Sensor Pipelines & Fusion — Architecture, As-Built Analysis, and Improvement Proposals

*Derived from the source tree as of July 2026. All file paths are relative to `src/`. Companion
reading: `docs/design/imu_frame_contract.md`, `docs/design/club_tracking_v3_design.md`,
`docs/implementation/shot_analyzer_m1_wrist.md`.*

---

## 1. Scope and thesis

PinPoint runs two sensing pipelines — a wearable-IMU chain (WitMotion WT9011DCL over BLE) and a
camera-vision chain (FLIR face-on primary) — that converge in one post-shot analyzer
(`WristAnalyzer::analyze`, `Analysis/wrist_analyzer.cpp`). The headline architectural finding of
this document is:

> **The two pipelines today are *coordinated*, not *fused*.** Despite its name,
> `ImuVisionFuser` is an IMU-only resampler ("M1 Wrist is IMU-only — no camera fusion yet",
> `imu_vision_fuser.h`), and `ShaftTracker::track` accepts the IMU `FusedStreams` and
> `Segmentation` parameters but explicitly ignores them ("VISION-ONLY: accepted for call-site
> compatibility but unused"). Cross-modal coupling exists only at the *orchestration* level —
> a shared clock, a shared impact anchor, IMU-derived scan bounds for the heavy vision stages,
> and an either/or segmentation fallback. No estimator anywhere combines an IMU measurement and
> a camera measurement of the same physical quantity.

That is a defensible v1 position (each modality is independently validated, failures are
isolated, and the degradation model is clean), but it leaves accuracy and robustness on the
table in specific, identifiable places. Section 6 inventories the as-built coupling points,
Section 7 the gaps, and Section 8 makes concrete layer-by-layer proposals, each with an
empirical validation gate in the style the project already uses (corpus gates, byte-identical
soak contracts, per-stage dumps).

---

## 2. Combined layered architecture

```
                IMU PIPELINE                                CAMERA PIPELINE
                ────────────                                ───────────────
L0  Capture     WT9011DCL BLE 0x61 frames                   FLIR/AVF/Aravis/Qt transports
                raw accel+gyro (on-board euler discarded)   RawVideoFrame (Bayer), tUs stamp
                [IMU/wt9011dcl_ble.cpp]                     [Video/video_input_*, Gui/cameras]
                          │                                            │
                          └──────────── EventBuffer clock (tUs) ───────┘        ← shared timebase
                                              │
L1  Buffering   ImuSample ring (raw+quat) ────┤──── SourceRing → frozen SwingWindow
                                              │     [Buffer/source_ring, swing_window]
L2  State       Madgwick 6-axis → q_raw                     FrameThrottle → BGR cv::Mat (live)
    estimation  [IMU/orientation_filter.h]                  [Video/frame_throttle, preprocessor]
L3  Domain      q_anat = A·q_raw·M                          MoveNet (live) / ViTPose (offline)
    mapping     [IMU/imu_calibration.h]                     COCO-17 PoseTrack2D
                                                            [Pose/*, Analysis/pose_runner]
L4  Smoothing/  ImuVisionFuser::fuse → FusedStreams         smoothPoseTrack — 3-state RTS ×34
    resampling  (200 Hz slerp grid, IMU-ONLY)               [Analysis/pose_smoother]
                [Analysis/imu_vision_fuser]
L5  Event       PhaseSegmenter (IMU signals)                ShaftTracker v3 (hands-only phase
    detection & [Analysis/phase_segmenter]                  model, E1/E2 evidence, banded
    tracking                                                Viterbi DP, ψ-isotonic, Stage-2
                                                            head KF/RTS, ball anchor)
                                                            [shaft_tracker*, shaft_track_assembly,
                                                             clubhead_track, ball_anchor]
L6  Metrics     MetricExtractor → wrist DOF MetricSeries    image-plane landmarks (P2/P5/P6/P8),
                (FE/RUD/pronation/elbow)                    impactShaftLean series, MaxSpeed proxy
L7  Estimands   WristResemblanceScorer (R_p)                SwingScorer adherence (Swing/GRF)
    & scoring   WristAssessmentEngine (Tier-1/2)            ScoreInterval uncertainty
                          │                                            │
                          └────────── SwingAnalysis{series, phases, score, pose2d, shaft, ball}
                                      → swing.json → replay/diagnostics surfaces
```

Both spines are state trajectories in time: the IMU spine is a quaternion
(sensor→anatomical→relative→decomposed angles), the camera spine a pixel coordinate
(frame→keypoints→smoothed tracks→grip-anchored θ→landmarks). The shared foundations are the
EventBuffer microsecond clock and the frozen `SwingWindow` read contract, which both pipelines
honour strictly (const readers, deterministic replay).

---

## 3. IMU pipeline — layer summary

**L0 capture.** `wt9011dcl_ble.cpp` parses BLE `0x61` combined frames to raw accel (g) and gyro
(°/s). The device's on-board fused euler is deliberately discarded: empirically it is not a
rigid representation of motion (joint axes 15–50° off, gimbal near ±90° pitch), while the raw
inertials are faithful (gyro-derived axes orthogonal to 0.2°).

**L2 orientation.** `MadgwickFilter` (`IMU/orientation_filter.h`, behind `IOrientationFilter`)
runs canonical 6-axis gradient descent: gyro integrates, accel gravity corrects roll/pitch by
gain β (`tuned::filter::kBeta`). Yaw is unobservable without a magnetometer and drifts —
accepted per-session. Output `q_raw` is in raw hardware axes.

**L3 anatomical mapping.** `imu_calibration.h`: `q_anat = A·q_raw·M`. `M` (mounting) is a fixed
strap-convention constant for arm segments and a numerically solved constant for the dorsal
hand mount; `solveSegment` builds the segment basis from gravity-down + two functional joint
axes (Gram-Schmidt, right-handed), gated on the measured inter-axis angle ≈ 90°. `A` places the
reference pose at identity. `toAnatomical()` is the single composition shared by the live write
path and the offline scored path.

**L4 resampling.** `ImuVisionFuser::fuse` slerps every bound segment onto one fixed-rate grid
(default 200 Hz) as `SegmentStream{qAnat, gyroDps, accelG}` (inertials rotated into the
anatomical frame). Joint angles are relative quaternions between adjacent segments, so the
shared drifting yaw *cancels* between two sensors — the design reason two-sensor wrist DOFs
work without a magnetometer. An optional offline "refuse" path re-derives orientation from the
recorded raw inertials under a phase-adaptive schedule (`orientation_refuser.h`) for filter
tuning; off by default, production byte-identical.

**L5 segmentation.** `PhaseSegmenter::segment(streams, impactUs)` finds the event ladder from
IMU signals: Top by back-chaining from impact through the positive signed-plane-rate run,
Transition from pelvis axial reversal, Address as last sustained stillness, Finish from
post-impact criteria — a confidence-weighted, strictly monotone chain plus swing bounds
(`Segmentation.conf` = min over Address/Top/Impact/Finish).

**L6 metrics.** `MetricExtractor::extract` evaluates the `wrist_angles.h` math per grid frame:
ZXY Tait-Bryan for FE (atan2, about Z) + RUD (asin, about X) with forearm axial twist dropping
out on the middle axis; swing-twist about the forearm long axis for pronation + elbow flexion.
Signs hardware-locked 2026-06 for the lead/left arm; a known ~10–15° FE↔RUD leak remains from
the unobservable two-sensor heading.

**L7 estimands.** `WristResemblanceScorer` — per-archetype absolute resemblance
`R_p = 100·exp(−½·Σ((x−μ_p)/σ_p)²)` over Top+Impact, FE-only in v1, not normalised across
patterns. `WristAssessmentEngine` — pure deterministic sample→Δ-from-address→band→rules over
the `IWristAngleSource` seam; the penalty-based assessment score is now the headline, with the
§B.7 uncertainty interval attached only while the resemblance value is the headline.

---

## 4. Camera pipeline — layer summary

**L0 capture.** `VideoInputBase` + factory over Spinnaker (FLIR), AVFoundation, Aravis, Qt.
Frames carry a capture `tUs` on the EventBuffer clock; FLIR delivers Bayer mosaics debayered
lazily. `CameraFormat{width,height,fps}` fixes the face-on scale all pixel quantities share.

**L1 buffering.** `SourceRing` (lock-free, single producer) feeds continuous capture; swing
detection pauses the buffer and freezes a `SwingWindow` over the index slice. All analysis is a
const reader of the frozen window — deterministic, replayable, disk-loadable for re-analysis.

**L2 live throttle.** `FrameThrottle` bounds live-inference lag to one cycle: newest frame
overwrites `m_latest` while busy; `consumerCount=2` (pose + ball detector) must both
`clearBusy()` before release. Live path only; the offline analyzer decodes directly.

**L3 pose.** Live: MoveNet via the shared EP cascade. Offline: `PoseRunner::run` runs ViTPose-B
synchronously on the analysis worker with non-uniform sampling — dense stride in
`[impact−densePreMs, impact+densePostMs]`, ~4× sparser mid-swing, ~15-frame stride over the
address hold; a two-pass mode (coarse full-window → span-bounded dense) covers the camera-only
case with no IMU span.

**L4 smoothing.** `smoothPoseTrack` — offline non-causal RTS; 3-state `[p,v,a]` white-jerk
scalar KF ×34 (x⊥y per keypoint), pixel-domain, variable dt rebuilt per step, 3σ Mahalanobis
gate + time-based coast budget, confidence-gated measurements so occluded keypoints coast and
the per-segment RTS bridges the gap. Deterministic.

**L5 shaft/head/ball.** `ShaftTracker` derives per-frame grip anchors + lead-forearm direction
φ from pose; `shaft_tracker_math` gathers E1 (retro-band match) + E2 (polarity-aware radial
ridge sweep) evidence on a 1° θ grid; `shaft_track_assembly` decides the global track under C1
butt-termination, C2 body-overlap veto, C3 phase-signed rotation (banded Viterbi DP over a
hands-only phase model), C4 reachable-cone ψ-isotonic (exact PAVA). `clubhead_track` Stage-2
measures a gap-tolerant on-axis head terminus + segmented 1-D KF/RTS, never feeding back into
the θ DP. `ball_anchor` is additive-only: fills non-measured samples from the grip→ball line
(address hold, impact-blur bridge) and supplies measured club length; `club_length_fusion`
fuses four px-length estimators (ball/band/head/persistent-prior) by inverse variance with an
abstain path.

**L6 metrics.** Image-plane landmarks on the shared `Phase` ladder (ShaftParallelBack P2,
ArmParallelDown P5, Delivery P6, ShaftParallelThrough P8, MaxSpeed = *peak hand angular speed,
a clubhead-speed proxy*), plus the `impactShaftLean` MetricSeries built from the valid shaft
track. Deliberately no mm/mph conversions — honesty about the monocular scale.

**L7 scoring.** `SwingScorer` adherence (weighted geometric mean of band-falloff sub-scores)
for Swing/GRF sessions; `ScoreInterval` carries measurement uncertainty separately from
coaching-tolerance σ. Everything lands in `SwingAnalysis` → `swing.json`.

---

## 5. The convergence point

`WristAnalyzer::analyze` sequences both chains over one frozen window:

1. `ImuVisionFuser::fuse` → `hasImu`. IMU present ⇒ `PhaseSegmenter` + `MetricExtractor`
   produce segmentation + wrist DOF series.
2. Camera present ⇒ `PoseRunner` (span-bounded by the IMU segmentation when conf > 0, two-pass
   otherwise) → RTS smooth → `BallRunner` → `ShaftTracker` → `buildShaftLeanSeries` appended to
   the *same* `series` vector.
3. Camera-only ⇒ the tracker's vision segmentation is adopted (`strace.segmentation`) so
   Address/Top/Impact/Finish still exist at vision-grade confidence.
4. Result tier: `Mono3DPlusImu` (IMU present) vs `Angles2D`; ok=false only when *neither*
   modality produced anything.

Degradation is by device presence and is the pipeline's strongest structural property: each
modality is self-sufficient, and the fused case is their union — never a dependency chain.

---

## 6. Fusion as-built — inventory of every cross-modal coupling

| # | Coupling | Layer | Mechanism | Nature |
|---|----------|-------|-----------|--------|
| F1 | Shared clock | L0/L1 | All sources stamp `tUs` on the EventBuffer clock; `SwingWindow` indexes both | Foundation (temporal alignment) |
| F2 | Impact anchor | L5 | `job.impactUs` (shot detection) seeds both `PhaseSegmenter` and the pose dense zone | Shared prior, one-way |
| F3 | Scan bounding | L3 | IMU segmentation bounds (`swingStartUs/EndUs`, conf > 0) restrict which frames PoseRunner/ShaftTracker decode (G3) | IMU → vision, *efficiency only* — never changes a computed value, only which frames exist |
| F4 | Segmentation fallback | L5 | `!hasImu && strace.segmentation.conf > 0` ⇒ adopt vision phase model | Either/or substitution, not a merge |
| F5 | Series union | L6 | Vision `impactShaftLean` appended to IMU wrist `MetricSeries`; one phase ladder | Concatenation at the metric level |
| F6 | Tier label | L7 | `ReconstructionTier::Mono3DPlusImu` vs `Angles2D` | Metadata only |
| F7 | Ball↔shaft, length fusion | L5 | `ball_anchor` (additive θ fill) and `club_length_fusion` (inverse-variance over 4 estimators + persistent prior) | Real statistical fusion — but **vision-internal** |
| F8 | Calibration snapshot | L7 | `BindingRecord` (A/M, deviation, age) persisted per swing | Provenance only |

**What is *not* fused anywhere:** no estimator combines an IMU and a camera measurement of the
same physical quantity. `FusedStreams`/`Segmentation` are passed into `ShaftTracker` and
ignored. The wrist DOFs never see the camera; the pose/shaft tracks never see a gyro sample;
the two segmentations never merge; the live path is entirely unfused.

---

## 7. Gap analysis — where the current design costs accuracy or robustness

**G1 — Heading unobservability is the wrist pipeline's dominant error term.** The 6-axis
filter cannot observe yaw; two-sensor relative rotation cancels the *shared* drift but not the
*differential* heading error, producing the documented ~10–15° FE↔RUD cross-talk. The camera
observes exactly the missing quantity: the lead-forearm image-plane direction φ is already
computed per frame for the shaft tracker. Today that observation is thrown away for the IMU.

**G2 — Segmentation is either/or, never a merge.** When both modalities are present the IMU
ladder wins outright; the vision phase model (and the shaft track's precise image-plane
parallels, and the ball's launch instant) never refine an IMU event even when vision confidence
is higher for that specific event. Impact especially: the ball track's last-pre-launch frame is
a sharper impact observation than an acceleration heuristic, and the ladder's Top back-chaining
inherits any impact error.

**G3 — Clock alignment is assumed, not estimated.** WitMotion BLE delivery has known jitter and
batching (ODR batching above 50 Hz), so per-sample `tUs` is arrival-time-ish; the camera path is
hardware-stamped. A per-swing IMU↔camera time-offset bias directly skews every event-relative
metric sample (a 20 ms bias at ~2000 °/s hand rotation near impact ≈ tens of degrees at the
sampled instant). Nothing measures or corrects it.

**G4 — Two independent uncertainty languages.** The IMU side has the §B.7 z-score budget and
per-sample confidence/gimbal proxies; the vision side has smoother honesty tiers
(Off/Pred/Meas), shaft tiers, and per-θ support. They never combine, so the headline score's
interval cannot reflect, e.g., "impact FE is trustworthy because the vision shaft agreed."

**G5 — The shaft DP's phase model is hands-only even when a better one exists.** C3
(phase-signed rotation) uses a vision-only phase model although the IMU segmentation — with
pelvis-reversal Transition and rate-run Top — is usually higher-confidence. The parameters are
already plumbed and ignored.

**G6 — Live path has zero redundancy.** Live overlay = MoveNet only; live wrist readout = IMU
only. Neither cross-checks the other, so a slipped strap or a mis-tracking skeleton is
invisible until post-shot.

**G7 — Calibration decay is recorded but unused.** `calibAgeSec` and mount deviations ride
every swing, but no consumer downgrades confidence or prompts recalibration; a slipped sensor
silently corrupts DOFs until the user notices.

**G8 — Scale remains unresolved.** Vision metrics are honest px/image-plane quantities, but
several coaching metrics people will ask for (hand speed, path lengths) need metric scale. The
club-length fusion already produces a stable px estimate of a *known-length* object — an
unexploited mm-per-px source.

**G9 — Everything above assumes a full swing.** Chips, pitches and other partial shots break
the built-in assumptions twice over. First, detection: `PhaseSegmenter` finds Top by
back-chaining through a positive plane-rate run and Transition from pelvis reversal — both
tuned to full-swing amplitudes; a 9-o'clock pitch has a shallow, short-lived reversal and may
segment at low confidence or not at all, and several P-positions (P3/P4 arm-parallel, Top)
simply don't exist on a chip. Second, estimands: for the short game the coaching signal is not
shaft geometry but the *kinematic profile* — tempo, and above all acceleration vs deceleration
through impact (decelerating into the ball is *the* canonical chip/pitch fault) — plus the
back/through swing *length*, which both classifies the shot (clock-face vocabulary) and is the
denominator that makes speed profiles comparable across shot sizes. The ingredients exist
unread: `SegmentStream.gyroDps` is the high-rate hand angular speed, `ShaftSample2D.
thetaDotRadS` is its vision twin, and a `tempo` metric key already exists for the full swing —
but no estimand today measures the impact-window speed/acceleration profile, no product
measures swing length, and nothing classifies shot type.

---

## 8. Proposals

Each proposal names the layer, the change, why it targets derived-metric robustness/accuracy,
and an empirical gate in the project's existing validation idiom. They are deliberately
additive-first: every one has an off-switch or abstain path preserving byte-identical current
behaviour, matching the soak-contract style used by snap/head/ball stages.

### P1 — Per-swing IMU↔camera clock-bias estimation (L1, foundation)

*Change.* Offline, estimate a single per-swing offset δt between the IMU stream clock and the
camera clock, and apply it when building the `FusedStreams` grid. Two independent, cheap
observables: (a) the impact instant — accel-magnitude spike (IMU) vs ball-launch frame
(vision); (b) whole-swing correlation — the IMU lead-forearm angular rate vs the differentiated
image-plane forearm angle φ(t) from the (already-smoothed) pose, maximising normalised
cross-correlation over δt ∈ ±100 ms. Persist δt and its confidence in `swing.json`.

*Why first.* Every cross-modal proposal below assumes aligned time; and δt *alone* improves
today's metrics, because phase-sampled values (FE at Impact) are read at event timestamps that
currently mix clock domains. This is also the cheapest win: no new sensors, no estimator
changes, one pure function + one application point in the fuser.

*Gate.* On the existing corpus: distribution of estimated δt (expect stable per-device bias +
per-swing jitter); FE@Impact shift vs δt; parity harness proving δt=0 reproduces current
output byte-identically. Accept if |median δt| > 5 ms (i.e. the correction is real) and the
two observables agree within ±1 frame at 150 fps.

### P2 — Vision-anchored heading correction (L2/L4 — the highest-value fusion)

*Change.* Use the camera's per-frame lead-forearm direction φ (already derived for the shaft
tracker) as a heading observation for the forearm IMU. Concretely, in the offline path: extend
the existing `orientation_refuser` schedule with a low-rate yaw-correction step — project the
IMU forearm long axis into the image plane via the (fixed, face-on) camera orientation and
nudge the heading component of `q_raw` toward the observed φ with a small gain (a complementary
correction, or a 1-DOF error-state update on the yaw axis only, leaving the gravity-observed
roll/pitch untouched). The differential heading between forearm and hand sensors is what leaks
into RUD, so correcting the forearm heading against vision (and optionally the hand heading
against the vision hand centroid direction) directly attacks the documented 10–15° cross-talk.

*Why.* G1 is the single largest known error in the flagship wrist metrics, and the observation
needed to fix it is already computed and discarded. This is the canonical
complementary-modality fusion: IMU is high-rate/low-drift-short-term; the camera is
low-rate/absolute in the image plane.

*Design constraints.* Offline-first (refuse-path only) so production stays byte-identical until
gated; face-on only in v1 (the projection model is trivial for a fixed camera ⊥ to the swing
plane; DTL comes later with the DTL shaft work); correction gain scheduled off pose keypoint
confidence and the smoother's honesty tier — a coasted (bridged) keypoint must contribute no
heading update.

*Gate.* The wrist log + corpus protocol already exist for exactly this kind of change: (a)
FE↔RUD cross-talk on the deliberate isolation captures (pure-FE and pure-RUD wrist motions on
the sensor-check page) must drop from ~10–15° toward the ≤5° region; (b)
`filterImpactStepDeg` must not regress; (c) resemblance/assessment score deltas reviewed on the
corpus before the schedule is enabled outside SwingLab.

### P3 — Confidence-weighted event fusion in the segmentation (L5)

*Change.* Replace the either/or segmentation (F4) with a per-event merge. Keep
`PhaseSegmenter`'s IMU ladder as the backbone, then, where a vision observation of the same
event exists, fuse timestamps by inverse confidence-weighted average with an outlier veto
(disagreement beyond a physical window ⇒ keep the higher-confidence one and flag): Impact from
the ball's last-pre-launch frame (sharpest available observation, F7 already computes it);
ShaftParallel P2/P6/P8 from the decided θ track (these are *definitionally* image-plane events
— the shaft track should own them outright, with the hand-proxy as fallback, formalising what
the `Delivery` comment already admits); Address confirmation from pose stillness. Record
per-event provenance (the `PhaseEvent.provenance` field already exists) and blended confidence.

*Why.* Every phase-sampled metric (all of Tier-1 banding, the resemblance phases Top/Impact)
reads its value at these timestamps; event timing error converts directly into metric error at
swing speeds. Impact via ball is likely a several-ms improvement over the acoustic/accel
anchor, and Top back-chains from it.

*Gate.* SwingLab per-event timing scatter against the dense-corpus manual annotations (the
shaftlab truth already includes frame-accurate impact); require the fused Impact to reduce
|error| median vs both single-modality estimates, and the monotone-ladder invariant to hold on
the whole corpus. Ship behind `seg.fuseEvents` tuning key, default off.

### P4 — Feed the IMU phase model to the shaft DP (L5)

*Change.* When `segmentation.conf > 0`, map the IMU ladder onto the tracker's `SwingPhase`
labels and use it in C3 (phase-signed rotation bands) in place of — or as a prior blended with —
the hands-only phase model. The parameters are already passed into `ShaftTracker::track`;
today's behaviour is preserved exactly whenever IMU is absent.

*Why.* The DP's transition bands encode "which way the shaft can rotate now"; the hands-only
phase model is the weakest link on swings with unusual tempo or a mis-tracked hand, and phase
mislabels are precisely what produce the tracker's globally-wrong-but-locally-smooth failure
mode. The IMU pelvis/thorax signals segment transition and top far more robustly than hand
kinematics.

*Gate.* The tracker already has the strongest validation harness in the codebase (per-stage
dumps, corpus gates, θ RMSE vs annotated truth). Re-run the corpus with `shaft.phaseSource=imu`
on fused swings: accept on non-regression of θ RMSE overall + measurable improvement on the
known phase-flip failure cases; hands-only path must remain byte-identical.

### P5 — IMU-informed pose smoother scheduling (L4)

*Change.* Two narrow injections into `smoothPoseTrack`, both config-gated: (a) schedule the
white-jerk process noise σ by swing phase (higher through Transition→Impact, lower at
address/finish) using the fused segmentation, replacing one global σ; (b) during
confidence-gated coasts of the *lead wrist/hand keypoints only*, blend the IMU hand angular
rate (rotated into the image plane) into the coast prediction, so the bridge across the
top-of-backswing occlusion follows the measured motion instead of pure constant-acceleration
extrapolation.

*Why.* The smoothed wrist keypoint is the grip anchor for the entire shaft stage; its quality
at occlusion and through impact propagates into θ, head, lean, and the P-time landmarks. (a) is
nearly free; (b) is the first true measurement-level fusion in the vision chain and targets its
best-known weakness (bridged occlusion rendered as measurement).

*Gate.* Smoother aux tiers give the metric directly: on the corpus, grip-anchor error vs
annotated truth during Pred-tier (coasted) spans must improve; Meas-tier spans must be
unchanged within float noise; determinism preserved (same input ⇒ byte-identical). σ-schedule
accepted only if impact-window keypoint lag (the RTS residual at the dense zone) improves
without address-hold jitter regressing.

### P6 — Cross-modal shaft-lean corroboration and a fused lean estimand (L6)

*Change.* Where forearm+hand IMUs and a valid shaft track coexist, compute impact shaft lean
twice — the vision θ-derived value (today's `buildShaftLeanSeries`) and an IMU-derived
prediction from the hand quaternion + the calibrated grip geometry — and (a) publish the
residual as a per-swing consistency diagnostic, (b) once the residual distribution is
characterised, emit a precision-weighted fused lean with a propagated σ. The same pattern
generalises: any metric observable by both modalities gets a residual channel first, a fused
estimand second.

*Why.* This is the robustness proposal: a single-modality gross error (slipped strap, shaft
mis-lock on a blurred frame) is currently undetectable at the metric layer. Residual-first is
the empirically safe route — it produces the data that justifies (or kills) the fusion weights
before any user-facing number changes.

*Gate.* Phase 1 (residual only) ships silently and accumulates; Phase 2 (fused value) requires
the residual distribution to be zero-centred with stable σ on the corpus, and the fused σ to be
strictly ≤ the better single modality.

### P7 — Unified confidence algebra and calibration-aware degradation (L7 + G7)

*Change.* Define one per-sample/per-event confidence vocabulary spanning both chains — IMU:
calibration validity, `calibAgeSec` decay, gimbal proxy, refuse-parity; vision: keypoint conf,
smoother tier, shaft tier, θ support, ball presence — and propagate it into a single
`ScoreInterval` model so the assessment headline finally regains an honest ± (currently cleared
because the resemblance interval brackets the wrong quantity). Add the two cheap
calibration-decay consumers: downgrade IMU stream `baseConfidence` as a function of
`calibAgeSec` + mount deviation, and surface a "recalibrate sensors" prompt when a swing's
bindings cross a threshold.

*Why.* Fusion without a shared uncertainty language degenerates into ad-hoc weights; P3/P5/P6
all need these numbers, and G7 is a pure robustness hole with the data already persisted.

*Gate.* Score-invariant checks (`0 ≤ lo ≤ overall ≤ hi`) on the corpus; interval calibration
sanity — the realised error of phase-sampled FE vs re-measured truth should fall inside the
interval at roughly the nominal rate on the validation set.

### P8 — Metric scale from the club-length fusion (L6, enabler)

*Change.* When the athlete's club length in mm is known (the taped-band path already implies
it; otherwise one profile field), publish `mmPerPx = clubLenMm / fusedLenPx` with the fusion's
σ, and let *scale-safe* metrics opt in (hand speed in m/s from the smoothed grip track; carry
the σ into the metric). Keep the image-plane-only honesty for everything not opting in.

*Why.* Unlocks the physically scaled metrics users expect without new hardware, riding an
estimator (`club_length_fusion`) that already has an abstain path and a persistent per-
athlete·club·camera prior. It is also the prerequisite for the curvature-corrected clubhead-
speed work already scoped for the FLIR spec.

*Gate.* mm-per-px stability across the prior's EMA (per athlete·club·camera CV below a few
percent); hand-speed sanity vs the IMU gyro-derived hand speed (an independent cross-check that
itself becomes a P6-style residual channel).

### P9 — Live-path cross-checks (L2/L3 live, robustness only)

*Change.* Two lightweight monitors, no live fusion: (a) during the sensor-check wizard, compare
`liveWrist` IMU forearm direction against the live MoveNet forearm segment (when the camera is
up) and warn on sustained gross disagreement — catching swapped/slipped sensors *before* a
session; (b) at session start, a one-shot address-pose consistency check between the IMU
anatomical pose and the pose skeleton. Both are advisory UI, never gating capture.

*Why.* G6: the cheapest possible insurance against the most common real-world failure (sensor
placement error), using code paths that already run concurrently on that screen.

*Gate.* False-positive rate near zero on correct setups across the team's capture sessions;
detection demonstrated on deliberately swapped/rotated sensors.

### P10 — Shaft foreshortening ξ fused with the hand path: monocular out-of-plane recovery (L5/L6)

*Terminology first.* The tracker's ψ is **not** foreshortening — `reconcilePsi` computes
ψ = θ − φ, the image-plane wrist-cock angle, and rails it isotonic around Top. The
out-of-plane state proposed here is a new variable, **ξ** — the shaft's elevation out of the
image plane (equivalently its component along the face-on camera axis).

*The unread signal.* A face-on camera projects the shaft, so the visible length encodes ξ:

```
cos ξ(t) = r_vis(t) / L_px        ⇒        |ξ(t)| = acos(clamp(r_vis / L_px))
```

Every quantity on the right is already emitted with uncertainty: `r_vis` is E2's per-frame
ridge extent (`ShaftSample2D.visibleLenPx`) — or, better, the Stage-2 measured head radius
with its own posterior σ (`headSigmaPx`) — and `L_px` is the `club_length_fusion` posterior.
Today this ratio is treated purely as a nuisance (`RidgeConfig.minLenPx` exists to *tolerate*
foreshortening; the length ladder median-filters `visibleLenPx`). P10 promotes it to a
measurement.

*Why the hand path is the other half.* |ξ| from the ratio is sign-ambiguous (toward vs away
from camera) and ill-conditioned near ξ = 0 (σ_ξ ≈ σ_r / (L·sin ξ) blows up when the shaft is
near the image plane). The grip track supplies exactly the missing structure:

1. **Sign from phase + handedness.** The hands-only phase model (or the P3 fused ladder) plus
   `job.handedness` fixes which side of the image plane the clubhead occupies in each phase —
   backswing carries the head camera-away for a right-hander filmed face-on, downswing brings
   it back through — so sign flips are pinned to the r_vis ≈ L_px crossings the phase model
   predicts.
2. **A smoothness rail like the codebase already builds.** ξ(t) gets the house temporal
   treatment — segmented KF + per-segment RTS (the clubhead Stage-2 sibling, one more
   instance of `HeadKf1D`'s idiom) with heteroscedastic R(t) from the acos conditioning, so
   near-in-plane frames contribute almost nothing and strongly foreshortened frames (mid-
   backswing, delivery) dominate.
3. **A pseudo-3D club from two 2D tracks.** With grip pixel position g(t) (smoothed pose),
   image angle θ(t), and ξ(t), the head's 3D position up to the grip's own depth is
   `head₃D = g₃D + L·(cos ξ·[cos θ, sin θ], sin ξ)`. Treating the grip depth as slowly varying
   (hands stay near the body plane at address scale) — or, better, taking the lead-forearm IMU
   orientation to bound the hands' depth excursion — yields a monocular swing-plane fit and a
   through-impact head depth-rate.

*Metrics unlocked (the point of the exercise).* These are precisely the estimands a face-on-
only system nominally "cannot do": **functional swing-plane angle** (plane fit to the pseudo-3D
head track, reported vs the P8 scale or as a pure angle, which needs no scale); **club path at
impact** (in-to-out vs out-to-in is, for a face-on camera, the head's depth rate — the sign and
magnitude of d(L·sin ξ)/dt through the impact window); a **shaft-pitch curve ξ(t)** feeding
laid-off / across-the-line reads at Top. Each ships with the propagated σ — honestly wide near
in-plane instants, tight where foreshortening is strong.

*Fusion character.* This is vision–vision fusion (E2/Stage-2 length channel × pose grip track ×
phase model) with an IMU assist (forearm orientation bounding hand depth; P2's corrected heading
sharpens it), i.e. it composes with P1 (aligned clocks), P3 (better phase pins for sign), P5
(better grip track), and P8 (scale for m/s path numbers) rather than competing with them. Like
the ball anchor, it is strictly additive: ξ never enters the θ DP/reconcile — θ stays
byte-identical with P10 on or off.

*Honest caveats.* (a) Conditioning: near address and any in-plane crossing, |ξ| is genuinely
unobservable from the ratio — the KF must coast there and the σ must say so; a metric sampled at
such an instant (impact is usually *near* in-plane for a good swing) leans on the depth-*rate*
through the window, not the instantaneous ξ, and on the RTS bridging from the well-conditioned
flanks. (b) `r_vis` is contaminated by motion blur and head-vs-shaft terminus ambiguity in the
fast phases — prefer the Stage-2 head radius (blur-aware, σ-tagged) over raw E2 rEnd wherever
`headConf ≥ 0`. (c) The projection model assumes the fixed face-on geometry; a lens/tilt
calibration residual maps directly into a ξ bias, so the plane-angle estimand should be
validated before the path estimand.

*Gate.* The studio already contains the ground truth: the **DTL camera directly measures the
out-of-plane angle** that face-on ξ estimates. On dual-camera corpus swings, compare the
monocular ξ(t) (and the impact depth-rate sign) against the DTL shaft annotation
(`dtl_shaft_annotate` truth): accept if the sign of club path at impact matches DTL on ≥ 95% of
swings where |path| exceeds the σ-implied dead zone, and ξ RMSE in the well-conditioned regions
beats the null (assume-in-plane) model by a factor ≥ 2. This also positions P10 as the bridge
until DTL shaft tracking ships in-app — and, after that, as the redundancy/cross-check channel
between the two views.

### P11 — Short-game stroke profile: tempo, acceleration through impact, and swing-length classification (L5/L6, addresses G9)

*The estimand shift.* For chips and pitches the question is not "where is the shaft" but "how
did the speed evolve" — was the stroke accelerating through the ball or decelerating into it,
what was the back:through tempo, and how long was the swing back and through. P11 adds three
products and one classifier, deliberately assigning each to the modality that measures it best
and fusing where they overlap:

1. **Speed/acceleration profile (IMU-primary).** The hand/forearm angular-speed curve
   `ω(t) = |gyroDps|` (already on the 200 Hz grid, anatomical frame) and its smoothed
   derivative α(t) over an impact-centred window. Headline scalars: `impactAccel` (signed α
   integrated over, say, impact − 80 ms → impact), a `decelFlag` when α is materially negative
   into the ball, peak-ω timing relative to impact (peaking *before* impact is the same fault
   in different clothes), and a normalised-jerk smoothness figure for the through-stroke. The
   IMU owns this: 200 Hz, blur-free, and strongest exactly where the vision path is weakest
   (the impact-adjacent frames — blur, dense-stride cost).
2. **Swing length back and through (vision-primary).** Maximum shaft-angle excursion θ_back
   and θ_through relative to address off the decided θ track, expressed in the coaching
   clock-face vocabulary (shaft at 9 o'clock back, 3 through), with the smoothed hand-path arc
   as the fallback when the shaft track is invalid (obscured shaft, mishits) and IMU
   integrated rotation as the no-camera fallback. Vision owns this because length is an
   *absolute geometric* quantity — what drifting rate-integration is worst at and a fixed
   camera is best at.
3. **Tempo (either, cross-checked).** Backswing:through duration ratio from the segmentation
   events — whichever ladder ran, fused per P3. Extends the existing full-swing `tempo` key
   rather than inventing a parallel one.
4. **Shot-type classification (fused).** {θ_back, θ_through, peak ω, tempo} classifies the
   stroke — chip / pitch / knock-down / full — as *context data*, not a new session type: in
   the §10 architecture it selects the reference-band set and which estimands apply (a chip
   scores the decel profile and length symmetry; it does not score X-factor or P4), exactly
   the mechanism `SessionProfile` + band providers already imply.

*The fusion element (new — nothing like it exists today).* Length and speed are complementary
across the modalities, and their combination is the coaching quantity: a **length-normalised
speed profile** — was *this much* speed appropriate for *this length* of swing, the classic
"long backswing + decelerate" vs "short backswing + accelerate" diagnosis — needs vision's
absolute swing length and the IMU's high-rate ω together. Additionally `thetaDotRadS` (vision)
vs `|gyroDps|` (IMU) over the mid-stroke is a P6-style residual channel on angular speed —
free mutual validation on every fused swing, and the empirical basis for trusting the IMU-only
profile on camera-less captures.

*Segmentation prerequisite (the G9 detection half).* The segmenter needs an amplitude-adaptive
mode: scale Top/Transition detection thresholds by observed peak plane-rate — or run a
dedicated low-amplitude ladder (Address → Back-extreme → Impact → Through-extreme → Finish)
when peak ω falls below the full-swing regime — and let inapplicable P-positions grey out
exactly as the wrist assessment already greys missing checkpoints. Segmenter config plus a
ladder variant, not a new segmenter.

*Stage placement.* Two stages in the §10 architecture: `StrokeProfileStage` — requires
`streams{LeadHand|LeadForearm}` **or** a valid shaft/pose track (each modality degrades to its
own half; fused adds the normalised profile and the residual) — and `ShotClassStage`, whose
output conditions the scoring tail. Both additive; full-swing output is untouched whenever the
classifier says "full".

*Gate.* The simulator is the truth source: GCQuad ball data labels shot size and quality, and
deliberate capture protocols are trivial ("ten chips decelerating, ten accelerating; 9-to-3
pitches"). Accept when `decelFlag` separates the deliberate decel/accel sets cleanly (the
signal is large — near-perfect separation expected), θ_back clock labels match video review
within half an hour-mark, and the ω-vs-θ̇ residual is zero-centred on fused swings. The
amplitude-adaptive segmentation gates on ladder validity (monotone, conf > 0) across a
chips/pitches corpus where today's segmenter fails or low-confs.

---

## 9. Prioritised roadmap

Ordered by (derived-metric impact × confidence it works) ÷ effort, respecting dependencies:

| Order | Proposal | Depends on | Effort | Metric impact |
|-------|----------|------------|--------|---------------|
| 1 | **P1** clock bias | — | S | Direct: every phase-sampled value; enables all below |
| 2 | **P3** event fusion (impact-via-ball first) | P1 | S–M | Direct: all phase-sampled metrics, Top chain |
| 3 | **P2** vision heading correction | P1 | M | Direct: FE/RUD cross-talk — the flagship metrics |
| 4 | **P7** unified confidence + calib decay | — (parallel) | M | Honest intervals; feeds P3/P5/P6 weights |
| 5 | **P4** IMU phase → shaft DP | P1, P3 | S | Shaft θ robustness → lean, P-times, head |
| 6 | **P6** lean residual → fused lean | P1, P7 | S then M | Robustness (gross-error detection), then accuracy |
| 7 | **P5** IMU-informed smoother | P1, P3 | M | Grip anchor at occlusion → whole shaft stage |
| 8 | **P8** metric scale | — (parallel) | S | New capability (m/s), cross-check channel |
| 9 | **P9** live cross-checks | — (parallel) | S | Session-level robustness |
| 10 | **P10** foreshortening ξ + hand path | P1; sharpened by P3/P5/P8 | M–L | New estimands: swing plane, club path at impact — DTL-gated |
| 11 | **P11** short-game stroke profile | P1; better with P3; amplitude-adaptive segmentation | M | New estimands: tempo/accel-decel/length; opens chips-pitches coaching |

Suggested phasing: **Phase A** (P1 + P3-impact + P9) is all-additive, small, and immediately
measurable. **Phase B** (P2 + P7) attacks the wrist pipeline's dominant error with the refuse
harness that exists for exactly this purpose. **Phase C** (P4 + P5 + P6) hardens the vision
chain using the corpus gates. **P8** slots in whenever the profile field lands. **P10** is
Phase D — the largest new-capability item, deliberately last because its gate needs the
dual-camera corpus and it benefits from nearly everything before it. **P11** floats: its
IMU-primary half (accel/decel profile + adaptive segmentation) is independent of Phases B–D
and could ship alongside Phase A if the short game is a product priority — only its fused
length-normalised profile waits on the vision length product.

Every proposal keeps the two prime architectural invariants that make this codebase tractable:
*frozen-window determinism* (same input ⇒ byte-identical output, off-switch parity) and
*degradation by device presence* (no proposal makes either modality depend on the other to
produce its current output — fusion only ever adds).

---

## 10. Orchestration architecture — capability-gated stages over a typed context

Sections 6–8 describe *what* to fuse; this section defines *where fusion lives structurally*,
so that camera-only, IMU-only (any role subset), and every fused permutation are one code
path — and so the same machinery carries the Swing and GRF session types when they land, with
IMUs on different segments (pelvis/thorax/spine/thighs) and different camera combinations
(face-on, DTL, both). The codebase already anticipates this: `SegmentRole` defines
Pelvis/Thorax/T12/thighs/Club today, `segmentRoleForSlot` maps only sessionType 1 "until their
placement UX lands", and `ReconstructionTier` names `Stereo3D`/`ClubInstrumented` tiers nothing
produces yet.

### 10.1 Pattern

A **capability-gated stage pipeline over a shared typed context** (Blackboard, constrained).
Three rules carry all the weight:

1. **Device presence is data, not control flow.** A `CaptureCapabilities` value — resolved on
   the UI thread at job-build time, alongside everything `ShotAnalysisJob` resolves today —
   states which camera placements and which bound `SegmentRole`s (with calibration
   validity/age) this swing has. No stage ever asks `hasImu`; it asks for the specific
   *products* it needs.
2. **Stages declare `requires` / `optionals` / `produces`.** The orchestrator runs a fixed,
   authored stage order, executing each stage iff its requirements are satisfiable from the
   context, and recording *why* each skipped stage was skipped. Degradation-by-presence stops
   being code and becomes an emergent property; `ReconstructionTier` becomes a derivation over
   which products exist rather than a ternary.
3. **The optional-absence contract.** A stage given none of its optionals must produce
   byte-identical output to today. This is the existing soak-contract discipline (snap, head,
   ball, refuse) promoted to an architectural invariant — and mechanically testable: run every
   stage with optionals stripped, diff the products.

Under these rules, *coordination vs fusion* — the distinction §6 had to establish by reading
comments — becomes structural: a stage consuming another modality's product as an **optional**
is coordination (IMU scan bounds into the pose pass: efficiency, output unchanged); a stage
whose **requires span both modalities** and which produces a new product is fusion (P1 clock
bias, P2 heading correction, P3 event fusion, P6 residuals). Every P-proposal in §8 lands as
one stage with an off-switch, rather than another conditional inside `WristAnalyzer::analyze`.

### 10.2 Core types

```cpp
// Resolved on the UI thread; immutable for the job's lifetime.
struct CaptureCapabilities {
    QSet<CameraPlacement>  cameras;      // FaceOn, DownTheLine, ...
    struct BoundImu { SegmentRole role; bool calibValid; double calibAgeSec; };
    std::vector<BoundImu>  imus;
    bool hasRole(SegmentRole r) const;
    bool hasRoles(std::initializer_list<SegmentRole> rs) const;   // joint pairs
    bool hasCamera(CameraPlacement p) const;
};

// The typed blackboard. Slots are std::optional products with provenance +
// confidence; camera-keyed products are maps over placement, so a DTL pose
// pass later is a new map entry, not a new field. SwingAnalysis is a
// projection of this at the end — not the working state.
struct AnalysisContext {
    CaptureCapabilities caps;
    const ShotAnalysisJob &job;                    // knobs, impactUs, overrides
    std::optional<double>                clockBiasUs;          // P1
    FusedStreams                         streams;              // empty = no IMU
    std::optional<Segmentation>          segImu, segVision;    // producers
    std::optional<Segmentation>          seg;                  // resolved (P3 fuses)
    QHash<CameraPlacement, PoseTrack2D>  pose;                 // per placement
    std::optional<BallTrack2D>           ball;
    std::optional<ShaftTrack2D>          shaft;
    std::vector<MetricSeries>            series;               // append-only
    // ... findings, score, timings, skip trace
};

class AnalysisStage {                    // house ABC + factory style
public:
    virtual ~AnalysisStage() = default;
    virtual QString name() const = 0;
    virtual bool    canRun(const AnalysisContext &) const = 0;   // requires()
    virtual void    run(AnalysisContext &) = 0;                  // pure over ctx
    // Invariant: absent optionals ⇒ byte-identical produces().
};
```

The context is **typed, not a keyed map** — product shapes are compile-checked, and adding a
slot is a deliberate act. The orchestrator is deliberately dumb: a `std::vector` of stages in
authored order, `canRun` gate, wall-clock per stage into `AnalysisTimings`, skip reasons into
the trace. No topological sort, no dynamic registration, no priorities — the DAG is small,
stable, and best kept legible in one place.

### 10.3 Wrist session — concrete stage inventory (the refactor spec)

Every row is an existing block of `WristAnalyzer::analyze` (or a §8 proposal, marked P*). The
migration is mechanical because the blocks are already pure calls over plain data — this table
writes down their implicit contracts.

| # | Stage | Requires | Optionals | Produces | Today lives at |
|---|-------|----------|-----------|----------|----------------|
| 1 | ClockBias (P1) | ≥1 IMU stream **and** ≥1 camera | ball launch frame | `clockBiasUs` | — |
| 2 | ImuResample | ≥1 bound IMU, role ≠ Unknown, ≥2 samples | `clockBiasUs`; refuse cfg | `streams` | `ImuVisionFuser::fuse` |
| 3 | ImuSegmentation | `streams`, `job.impactUs` | — | `segImu` | `PhaseSegmenter::segment` |
| 4 | WristMetrics | `streams{LeadForearm, LeadHand}`, `segImu\|seg` | `streams{LeadUpperArm}` (pronation/elbow) | wrist `series` | `MetricExtractor::extract` |
| 5 | Pose(FaceOn) | camera FaceOn | `seg*` bounds (efficiency only), `impactUs` | `pose[FaceOn]` | `PoseRunner::run` |
| 6 | PoseSmooth | `pose[FaceOn]` | segmentation (P5 σ-schedule); `streams` (P5 coast blend) | smoothed pose + aux | `smoothPoseTrack` |
| 7 | Ball | camera FaceOn decode | `pose[FaceOn]` | `ball` | `BallRunner` |
| 8 | Shaft | `pose[FaceOn]` | `ball` (anchor); `segImu` (P4 phase source) | `shaft`, `segVision` | `ShaftTracker::track` |
| 9 | SegResolve | `segImu \|\| segVision` | both + confidences (P3 merge) | `seg` (provenance-tagged) | the `!hasImu` adoption branch |
| 10 | ShaftLeanMetric | `shaft.valid`, `impactUs` | P6: `streams{LeadHand}` residual | lean `series` (+residual) | `buildShaftLeanSeries` |
| 11 | ForeshortenXi (P10) | `shaft.valid`, smoothed pose, `seg` | `streams{LeadForearm}`, scale (P8) | ξ track, plane/path metrics | — |
| 12 | Resemblance | wrist `series` | — | `score` (R_p) | `WristResemblanceScorer` |
| 13 | Assessment | wrist `series`, `seg`, `runAssessment` | — | findings, v2 score | `WristAssessmentEngine` |
| 14 | Uncertainty | `score` | P7 unified confidences | `ScoreInterval` | `ScoreUncertainty` |
| 15 | Project | any product | — | `SwingAnalysis`, ok/error | tail of `analyze()` |

The "no products at all" failure (`series.empty() && pose.empty()`) becomes the Project stage's
one rule, identical to today's contract. `WristAnalyzer` shrinks to: build capabilities, build
context, run the Wrist stage list.

### 10.4 Cross-session extension — Swing and GRF are stage lists, not classes

The critical design choice for the coming session types: **a session is a `SessionProfile` —
a stage list plus configuration — not an analyzer subclass.** Three mechanisms make the same
stages serve all three sessions:

**Role-parameterised joint stages.** Stage 4 hardcodes LeadForearm+LeadHand today. Its general
form is a `JointAngleStage{proximal, distal, decomposition, metricKeys}` — the wrist is
`{LeadForearm, LeadHand, ZXY-FE/RUD, ...}`, and the same class instantiates
`{Pelvis, Thorax, axial-twist, "xFactor"}` or `{Thorax, LeadUpperArm, ...}` for the Swing
session, and pelvis/thigh pairs for GRF. `wrist_angles.h`'s swing-twist and Tait-Bryan
decompositions are already the reusable math; the stage supplies role bindings and metric
naming. `requires` becomes *data* (`caps.hasRoles({proximal, distal})`), so "user strapped
pelvis+thorax but no wrist sensors" runs the X-factor instance and skips the wrist instance
with a recorded reason — no permutation code anywhere.

**Placement-keyed camera stages.** `pose[]` being a placement map means the DTL camera arrives
as `Pose(DTL)` + `DtlShaft` stage instances (the `dtl_shaft_annotate` port), and a
`StereoResolve` fusion stage (requires both placements) eventually produces the 3D products the
`Stereo3D` tier names. Face-on-only sessions simply never satisfy those stages' requirements.

**Shared backbone, session-specific tail.** Stages 1–3, 5–9 (clock, resample, segmentation,
pose, smooth, ball, shaft, resolve) are session-agnostic — the phase ladder and pose/shaft
products mean the same thing everywhere. Sessions differ in their *metric and estimand tail*:
Wrist appends resemblance+assessment; Swing appends its `JointAngleStage` set +
`SwingScorer` adherence; GRF appends pelvis/thigh kinematic stages + its own scorer. Scoring
kind is already modelled (`ScoreKind::Adherence|Resemblance`) — the profile picks the scorer
stage, nothing else changes.

```cpp
struct SessionProfile {
    QString name;                                    // "wrist", "swing", "grf"
    std::vector<std::unique_ptr<AnalysisStage>> stages;   // authored order
};
// wristProfile(): {ClockBias, ImuResample, ImuSeg, JointAngle(wrist…),
//                  Pose(FaceOn), PoseSmooth, Ball, Shaft, SegResolve,
//                  ShaftLean, Resemblance, Assessment, Uncertainty, Project}
```

This also answers the mobile question by construction: a phone build is the same profiles over
a smaller `CaptureCapabilities` (one camera, Low pose tier) — stages skip, nothing forks.

### 10.5 Migration path (additive, gated)

1. Introduce `CaptureCapabilities` + `AnalysisContext` + `AnalysisStage`; wrap each existing
   `analyze()` block as a stage **without moving logic** — the stage bodies call the same free
   functions with the same arguments.
2. Golden-corpus parity gate: staged Wrist output byte-identical to `analyze()` on the full
   corpus (the frozen-window determinism makes this a plain diff). Ship dark behind a flag.
3. Delete the monolith body; `WristAnalyzer` = wristProfile() runner.
4. Land §8 proposals as stages (each with its own gate); extract `JointAngleStage` from
   stage 4 when the Swing session's placement UX starts — not before.

**As-built (2026-07-16).** Steps 1–3 are done and shipped. `analysis_stage.h` plus the
Wrist stages / `wristProfile()` / `projectResult()` in `wrist_analyzer.cpp` passed the
byte-identical gate over the full 61-swing blessed corpus — staged vs. monolith with only
`analysis.timings` stripped, zero diffs. Live-CUDA pose is run-to-run nondeterministic at
~1e-9, so the gate pinned pose per swing via `--pose` track injection (the OFF-vs-OFF
determinism baseline confirmed the residual was pose, not the refactor). The monolith body
and the temporary `analyzer.staged` flag are now deleted; `WristAnalyzer::analyze()` is the
profile runner. Five deliberate deviations from §10.2/§10.3, carried for parity: (1)
`AnalysisContext` owns the `shared_ptr<SwingAnalysis>` in place — slot purification
("SwingAnalysis as projection") is deferred to the first P-stage that needs
provenance-tagged slots; (2) `projectResult()` is the profile-runner tail, not a stage (a
Project *stage* would need special halted-handling in the orchestrator); (3) Uncertainty
stays fused into the Resemblance stage, preserving the monolith's write order (the §B.7
interval is computed before assessment may clear it); (4) stages exist for blocks the §10.3
table omits — HeadTrack, FootMetrics, Bindings, PoseAssessment; (5) no placeholder stages
for unimplemented P-proposals — they land stage-by-stage in later sessions (step 4).

### 10.6 Anti-goals

The known failure mode of this pattern is growing a workflow engine. Explicitly out: dynamic
stage registration/discovery, runtime reordering, priority schedulers, inter-stage messaging,
parallel stage execution (the QtConcurrent job is already the concurrency boundary; stages run
sequentially inside it). If a stage list ever seems to need runtime reordering, that is a
design smell to investigate, not a feature to build. The orchestrator should stay small enough
to read in one screen — the value is in the declared contracts and the invariant, not in the
machinery.

---

## 11. Amendment — ChArUco calibration and stereo triangulation

Per-camera ChArUco intrinsic calibration plus a stereo extrinsic (triangulation) calibration
is planned. This introduces two new first-class inputs — a **metric camera model** per
placement and a **metric baseline** between placements — and it materially amends four things
above. It reorders nothing: calibration mostly *upgrades* estimators and gates rather than
replacing proposals.

**A. New capability + products (§10).** `CaptureCapabilities` gains per-placement calibration
state (`intrinsicsValid`, and `stereoValid` for the pair — with age, like `calibAgeSec`); a
`CameraCalibration` product (K, distortion, extrinsics; principal-point shift already applied
per the runtime-crop rule) loads at job-build time. Two stage consequences: existing pixel
stages gain undistortion via an *optional* (absent calibration ⇒ byte-identical today-path,
per the §10.1 invariant), and the previously hypothetical `StereoResolve` becomes a concrete
`Triangulate` stage — requires `pose[FaceOn] + pose[DTL] + stereoValid`, produces `Pose3D`
(OpenCV DLT, per-keypoint reprojection-residual σ). `Pose3D` is what finally populates
`ReconstructionTier::Stereo3D` and is the feed the in-memory OpenSim IK pipeline expects.
This is why §10.4's placement-keyed products were worth insisting on: triangulation arrives
as one stage and one slot, no restructure.

**B. P8 (metric scale) gains a second, stronger estimator.** The stereo baseline yields
distances in mm directly — `Triangulate` supersedes the club-length-derived `mmPerPx` wherever
`stereoValid`. Amendment: P8 becomes a two-rung scale ladder — stereo scale (with propagated
triangulation σ) when available, club-length scale as the single-camera/uncalibrated fallback —
and the two become mutual cross-checks: triangulated grip→head at address vs the known club
length in mm is a *scale self-test* that validates the whole calibration chain every swing. A
persistent disagreement flags a stale calibration (see D). `club_length_fusion` optionally
gains an E-stereo candidate under its existing inverse-variance/abstain machinery.

**C. P10 (foreshortening ξ) is demoted-by-success where stereo exists.** Triangulation
measures the 3D shaft direction directly, superseding the monocular ξ estimate — exactly the
lifecycle §8/P10 anticipated for DTL, now sharper: P10 is the **primary** out-of-plane
estimator for single-camera and uncalibrated setups, and the **redundancy/residual channel**
(P6-style) when stereo runs. Two upgrades even before stereo shaft tracking lands: P10 caveat
(c) — the lens/tilt bias mapping into ξ — is closed by the intrinsic calibration alone
(undistorted, calibrated projection replaces the assumed-perpendicular face-on model); and
P10's validation gate improves from DTL 2D annotation truth to triangulated 3D truth.

**D. P2's projection model and P7's confidence algebra sharpen.** P2's heading correction
currently projects the IMU forearm axis under an assumed fixed face-on geometry; with
intrinsics + extrinsics that projection is exact, and the correction extends to the DTL view
(two bearings on the same axis over-determine heading — better still). P7's vocabulary gains
calibration terms: calibration age/reprojection RMS join `calibAgeSec` as confidence inputs,
and the scale self-test in B becomes a per-swing consistency input. The camera side thereby
acquires the same "calibration decays, confidence follows, UI prompts recalibration" loop G7
demanded for the IMUs — one policy, both modalities.

Net effect on the roadmap: no reordering. Calibration lands as capability + products + the
`Triangulate` stage (naturally alongside the DTL work), P8 gains its stereo rung, P10 keeps
its priority but inherits a better gate, and the scale self-test becomes the cheapest
always-on health check the vision pipeline has.

---

## 12. The consistency graph — systematic corroboration across N cameras × N IMUs

Sections 8 and 11 establish *pairwise* corroborations (P2 heading, P6 lean residual, P11
ω-vs-θ̇, the §11 scale self-test, P9's live checks). This section generalises them: with
multiple IMUs and multiple calibrated cameras, every sensor pair that shares an observable is
a **residual edge**, and the set of edges forms a consistency graph whose structure does two
jobs the pairwise view cannot — *fault isolation* (which device is wrong, not merely that
something disagrees) and a principled source for P7's confidence weights.

### 12.1 Three edge families

**IMU × IMU — kinematic-chain constraints.** Adjacent instrumented segments share a joint,
and rigid-body kinematics makes their raw gyros mutually predictive:
`ω_distal = ω_proximal + ω_joint`, with `ω_joint` confined to the joint's anatomical axes.
The elbow is the sharp case — a near-hinge, so the forearm and upper-arm angular velocities
must agree up to rotation about (approximately) one axis; the component of their difference
*orthogonal* to the best-fit hinge axis is a constraint residual that should sit at gyro-noise
level. Sustained violation is a **strap-slip detector requiring no camera at all**, and the
same functional-axis identification that `solveSegment` already performs at calibration time
supplies the axis (drift in the *identified* axis direction across a session is itself a slip
signal — a second, slower channel on the same edge). For the Swing/GRF sensor sets this
compounds along the serial chain: pelvis↔T12↔thorax gives two more hinge-like edges
(constrained-DOF spinal joints), and thigh↔pelvis edges bracket the hips. Note these edges
compare *angular rates in segment frames*, so they are immune to the heading-drift problem —
they corroborate at L1/L2, below where drift lives.

**Camera × camera — epipolar consistency before triangulation.** §11's `Triangulate` stage
should be preceded by a per-keypoint gate: each FO keypoint must lie within a σ-scaled band of
the epipolar line induced by its DTL counterpart (and vice versa). A pose hallucination in one
view — the classic occluded-joint high-confidence miss — violates epipolar geometry and is
rejected *before* it poisons the 3D product, leaving that keypoint to the single-view path for
that frame. Nearly free once the stereo calibration exists; the residual (Sampson distance)
also grades the calibration itself — a slow, global rise in epipolar residuals across all
keypoints indicates the *rig* moved, not a sensor.

**IMU × camera — the projection lattice.** Every calibrated view can observe every IMU'd
segment: project the segment's anatomically-mapped IMU orientation (its long axis) through
that camera's calibration and compare against the view's 2D limb direction from pose. P2 is
one cell of this matrix (forearm × face-on, used *correctively*); the general N_imu × N_cam
matrix used *diagnostically* is new. Each cell is cheap (one projection, one angle), phase-
gated (skip frames where the limb is near the view's optical axis — ill-conditioned exactly
as P10's ξ), and confidence-weighted by the pose smoother's honesty tier.

### 12.2 Fault isolation — the property pairwise checks lack

A single residual says "these two disagree" — it cannot say which is wrong. The graph can:
a slipped forearm IMU elevates its chain edge (vs upper arm), its projection edges (vs every
camera), and the P6/P11 metric residuals it feeds — while the rest of the graph stays quiet.
The isolation rule is graph-structural and simple: **the faulty sensor is the vertex whose
incident edges are jointly elevated while the complement subgraph remains consistent.** This
turns G7's passive `calibAgeSec` decay into an *evidence-based* recalibration prompt naming
the specific device ("your lead-forearm sensor appears to have moved — recalibrate"), and it
generalises P9's live wizard check into the same machinery run per-swing offline.

Practical scope discipline: with today's Wrist hardware (2–3 IMUs, 1–2 cameras) the graph is
tiny — at most ~8 edges — and vertex isolation is a max-over-averages, not an inference
engine. It should stay that way; the value is in *having* the edges, not in sophisticated
graph algorithms over them.

### 12.3 Placement in the architecture

One stage: `ConsistencyStage` — requires ≥2 products that share an observable (so it runs on
any multi-sensor capture, in any permutation), produces a `ConsistencyReport` (per-edge
residual summaries, per-vertex health, provenance) consumed by three parties: P7's confidence
algebra (edge residuals → stream/product confidence adjustments), the UI recalibration prompt
(vertex isolation), and `swing.json` (persisted, so corpus analysis can characterise residual
distributions before any of them gate anything). It sits after `SegResolve`/`Triangulate` and
before the metric tail, and is **diagnostic-only in v1** — per the additive discipline, no
edge modifies a measurement until its residual distribution has been characterised on the
corpus; correction remains the business of the dedicated fusion stages (P2, P3, P5, P6).

The consistency graph is also the right frame for a closing observation about the whole
proposal set: P1 aligns the clocks so edges compare like with like; the calibrations (§11 and
the IMU anatomical solve) define the transforms edges are computed through; the P-proposals
are the *corrective* subset of edges promoted to estimators; and the graph is everything else
— cheap, always-on, additive corroboration that makes every added sensor increase the
system's *self-knowledge*, not merely its channel count.

---

## Appendix A — file map

| Concern | Files |
|---------|-------|
| IMU capture / filter / calibration | `IMU/wt9011dcl_ble.cpp`, `IMU/orientation_filter.h`, `IMU/orientation_refuser.h`, `IMU/imu_calibration.h` |
| Camera capture / buffer | `Video/video_input_*`, `Video/frame_throttle.*`, `Video/video_preprocessor_opencv.*`, `Buffer/source_ring.*`, `Buffer/swing_window.*` |
| Pose | `Pose/pose_estimator_*`, `Analysis/pose_runner.*`, `Analysis/pose_smoother.*` |
| Shaft / head / ball | `Analysis/shaft_tracker*.{h,cpp}`, `Analysis/shaft_track_assembly.*`, `Analysis/clubhead_track.*`, `Analysis/ball_anchor.*`, `Analysis/ball_runner.*`, `Analysis/club_length_fusion.h` |
| Fusion & segmentation | `Analysis/imu_vision_fuser.*`, `Analysis/phase_segmenter.h` |
| Wrist math / metrics / estimands | `Analysis/wrist_angles.h`, `Analysis/metric_extractor.*`, `Analysis/wrist_resemblance.*`, `Analysis/wrist_assessment_*`, `Analysis/score_uncertainty.h` |
| Orchestration / output | `Analysis/wrist_analyzer.cpp`, `Analysis/swing_analysis.h`, `Gui/shot/shot_processor.cpp`, `Gui/diagnostics/wrist_diagnostics_model.*` |
