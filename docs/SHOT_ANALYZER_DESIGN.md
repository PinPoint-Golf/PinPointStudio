# PinPoint Studio — Shot Analyzer: Design & Implementation Plan

> **Status:** Design proposal (draft) · **Date:** 2026-06-08 · **Grounded against:** `main` @ `1d6ac77`
>
> This document specifies the **shot analyzer** — the layer that turns a frozen `pinpoint::SwingWindow`
> (raw multi-camera frames + 100 Hz IMU samples + the impact marker) into 3D biomechanics, scored
> metrics, and coach-readable feedback. It replaces the four stub analyzers in
> `src/Analysis/shot_analyzer.cpp` with a nine-layer `pinpoint::analysis` pipeline that plugs into the
> **existing** `ShotAnalyzer::analyze(const SwingWindow&, const ShotAnalysisJob&)` seam without breaking
> the frozen-window const-reader contract, the EventBuffer producer contract, or the quaternions-only rule.

## Executive summary

The analyzer is built as a **degradable, additive** pipeline behind the current `analyze()` worker-thread
interface. Nine named layers — **Decode → Detection → Reconstruction → Fusion → Cleanup → Skeleton →
Metrics → Scoring → Feedback** — compose per session type. Each layer is the natural home for one concern:

- **Cameras** give metric 3D joint positions when a down-the-line + face-on pair is **calibrated** and triangulated.
- **IMUs** supply absolute segment orientation (wrist flexion, forearm rotation, club shaft) that vision cannot, and reconstruct occluded segments straight through motion blur.
- **Offline smoothing** (Kalman + RTS backward smoother over the whole window) and **IK** (fixed bone lengths + ROM limits) produce a stable `SkeletonTimeline` that also drives `BodyVizView` replay.
- **Metrics** (X-factor & stretch, kinematic sequence, wrist flex/ext, sway/thrust/lift, swing plane, tempo…) are phase-anchored on the exact impact timestamp.
- **Scoring** is **transparent and non-compensatory**: every number traces to one metric, one reference band, one weight, aggregated by a **weighted geometric mean** so a single severe fault cannot be averaged away, with ranked plain-English faults + drills.

It **degrades gracefully** across capability tiers built on two independent axes — cameras *and*
IMUs — with **`Mono3D+IMU`** (one calibrated face-on camera + sparse IMUs) as the **first-class
single-camera path**, not a degraded fallback: every orientation metric (X-factor, wrist, forearm,
shaft lean, body kinematic sequence) is IMU-derived and camera-count-independent, so a lone face-on
camera yields a complete coaching product on day one. A second (down-the-line) camera and a future
club-shaft IMU are *enhancements* that buy back the depth-axis metrics (club path, attack angle,
thrust) — never the gate to real analysis. The plan ships **standalone user value at every
milestone** (M0 persistence → M1 monocular skeleton + IMU orientation metrics + banded score →
M2 single-camera metric calibration + monocular lift → M3 second-camera triangulation + club-IMU
enhancements → M4 smoothing/IK → M5 reference-swing scoring + feedback → M6 polish/validation).

*This design was produced by reading the actual codebase (Appendix A) and researching the domain
(Appendix B); the four core sections below are the design proper, and the Implementation Plan is the
sequenced build-out.*

## Table of contents

- [Architecture & Data Pipeline](#architecture-data-pipeline)
  - [Design goals & non-goals](#design-goals-non-goals)
  - [Layered model](#layered-model)
  - [Plugging into the existing interface](#plugging-into-the-existing-interface)
  - [Canonical intermediate data structures](#canonical-intermediate-data-structures)
  - [Threading & performance](#threading-performance)
  - [Reconstruction tiers (revised)](#reconstruction-tiers-revised)
  - [Per-session-type behavior](#per-session-type-behavior)
- [Algorithms](#algorithms)
  - [0. Single-camera calibration](#0-single-camera-calibration-skeletonsolver-world-frame-revised-algorithms-0)
  - [1. Per-frame 2D detection (`PoseRunner`, `ClubBallTracker`, layer 1)](#1-per-frame-2d-detection-poserunner-clubballtracker-layer-1)
  - [2. 3D reconstruction (`Triangulator`, layer 2)](#2-3d-reconstruction-triangulator-layer-2)
  - [3. IMU–camera fusion (`ImuVisionFuser`, layer 3)](#3-imucamera-fusion-imuvisionfuser-layer-3)
  - [4. Temporal cleanup (`TrackSmoother`, layer 4)](#4-temporal-cleanup-tracksmoother-layer-4)
  - [5. Skeleton / IK (`SkeletonSolver`, layer 5)](#5-skeleton-ik-skeletonsolver-layer-5)
  - [6. Swing-phase segmentation (`PhaseSegmenter`, cross-cutting, MVP)](#6-swing-phase-segmentation-phasesegmenter-cross-cutting-mvp)
- [Metrics & Scoring](#metrics-scoring)
  - [A) Metric catalog](#a-metric-catalog)
  - [B) Scoring model](#b-scoring-model)
- [Metric time series & key-swing-point sampling](#metric-time-series-key-swing-point-sampling)
  - [1. Data model — TimeGrid, MetricSeries, PhaseSample](#1-data-model-timegrid-metricseries-phasesample)
  - [2. Time axis, units, sign, ideal band](#2-time-axis-units-sign-ideal-band)
  - [3. Persistence — unified swing.json (folded at the join)](#3-persistence-unified-swingjson-folded-at-the-join)
  - [4. Downsampling — card sparkline vs detail graph](#4-downsampling-card-sparkline-vs-detail-graph)
- [Implementation Plan](#implementation-plan)
  - [M0 — Plumbing & persistence skeleton (no biomechanics yet)](#m0-plumbing-persistence-skeleton-no-biomechanics-yet)
  - [M1 — Monocular skeleton + IMU orientation metrics + banded score](#m1-monocular-skeleton-imu-orientation-metrics-banded-score-angles2d-mono3dimu)
  - [M2 — Single-camera metric calibration + monocular metric lift](#m2-single-camera-metric-calibration-monocular-metric-lift-mono3dimu-complete)
  - [M3 — Second-camera triangulation enhancement + IMU slot-map fix](#m3-second-camera-triangulation-enhancement-imu-slot-map-fix-stereo3d-future-club-imu)
  - [M4 — Smoothing, occlusion robustness, IK skeleton](#m4-smoothing-occlusion-robustness-ik-skeleton)
  - [M5 — Reference-swing scoring + ranked feedback](#m5-reference-swing-scoring-ranked-feedback)
  - [M6 — Polish, performance, cross-platform validation](#m6-polish-performance-cross-platform-validation)
  - [New-dependencies summary](#new-dependencies-summary)
  - [Validation strategy](#validation-strategy)
  - [Risks & open questions](#risks-open-questions)
- [Appendix A — Codebase integration map](#appendix-a-codebase-integration-map)
- [Appendix B — Research bibliography](#appendix-b-research-bibliography)

---

## Architecture & Data Pipeline

### Design goals & non-goals

**Goals.** Turn a frozen `pinpoint::SwingWindow` (raw camera frames + 100 Hz `ImuSample` records + the 16-byte `shot_marker_v1`) into a metric, scored, coach-readable result that persists across app restarts — without violating the EventBuffer producer contract, the quaternions-only rule, or the existing `ShotAnalyzer::analyze(const SwingWindow&, const ShotAnalysisJob&)` worker-thread contract. The pipeline must degrade gracefully (full 3D when two calibrated cameras + IMUs are present, down to 2D angles-only when nothing is calibrated) and must remain a *pure const reader* of ring memory.

**Non-goals (v1).** No live/real-time biomechanics (this is post-shot only, on the QtConcurrent worker), no learned motion priors (deferred), no club-launch ball-flight modelling beyond geometry, and no new EventBuffer source (pose/ball stay re-derived offline, not written back to the ring).

### Layered model

All new code lives under `src/Analysis/` in namespace `pinpoint::analysis`. The pipeline is nine named layers; each is a stage the per-session analyzer composes:

| # | Layer name | Module dir | Responsibility |
|---|-----------|-----------|----------------|
| 0 | **Decode** (`FrameDecoder`) | `src/Analysis/decode/` | Zero-copy `cv::Mat` wrap + demosaic of each `payloadOf()` slot, reusing `SwingExporter`'s demosaic table (NV12/YUV420P/BGRA32/Bayer8). |
| 1 | **Detection** (`PoseRunner`, `ClubBallTracker`) | `src/Analysis/detect/` | Re-run pose (**ViTPose-L/H** body via the already-wired `PoseEstimatorViTPose`; its whole-body 133-kpt also supplies hands) + club/ball keypoints per decoded frame, per camera, via ONNX Runtime. Accuracy-first — the analyzer is offline; the live 60 Hz path keeps MoveNet/RTMPose. |
| 2 | **Reconstruction** (`Triangulator`) | `src/Analysis/recon/` | Confidence-weighted DLT (Eigen SVD) of matched 2D keypoints into metric 3D using injected camera calibration; single-view ray∩bone-sphere fallback. |
| 3 | **Fusion** (`ImuVisionFuser`) | `src/Analysis/fusion/` | Apply per-source anatomical transform `q_anat = A·q_raw·M`; fill occluded shaft/wrist segments from IMU orientation; Kabsch yaw alignment of IMU world → camera world. |
| 4 | **Cleanup** (`TrackSmoother`) | `src/Analysis/cleanup/` | Per-joint constant-acceleration Kalman + Rauch–Tung–Striebel backward smoother over the whole window (offline → future samples available). |
| 5 | **Skeleton** (`SkeletonSolver`) | `src/Analysis/skeleton/` | Weighted-LS IK over joint angles with fixed bone lengths + ROM box limits → the canonical `SkeletonTimeline` of per-bone quaternions. |
| 6 | **Metrics** (`MetricExtractor`) | `src/Analysis/metrics/` | Phase-anchored biomechanics: rotations, X-factor/stretch, kinematic sequence, wrist angles, tempo, sway/thrust/lift → `MetricSeries`. |
| 7 | **Scoring** (`SwingScorer`) | `src/Analysis/score/` | Tolerance-band clipped-z sub-scores + weighted **geometric** mean → `ScoreBreakdown`. |
| 8 | **Feedback** (`FaultRanker`) | `src/Analysis/feedback/` | Rule table keyed on (metric, phase, sign), ranked by points-lost → `Fault` list. |

A `PhaseSegmenter` (`src/Analysis/phase/`) is a cross-cutting helper used by layers 6–8, anchored on `job.impactUs`.

### Plugging into the existing interface

The four stub classes in `shot_analyzer.cpp` are replaced by real analyzers that all derive from a new `BiomechAnalyzer` base (in `src/Analysis/biomech_analyzer.h`) which owns the layer pipeline; `makeShotAnalyzer(sessionType)` dispatches to `SwingAnalyzer`/`WristAnalyzer`/`GrfAnalyzer`/`CoachAnalyzer`, each selecting which layers run and which metric/score/fault tables apply. `analyze()` keeps its exact signature, stays a const reader, and finishes all window reads before returning — `ShotProcessor::finishNowBlocking()` continues to join before window destruction.

**Extending the job.** `ShotAnalysisJob` is widened (still a UI-thread-resolved value type) with everything the worker cannot read itself:

```cpp
struct CameraCalib { Eigen::Matrix3d K; Eigen::VectorXd dist; Eigen::Quaterniond q_wc; Eigen::Vector3d t_wc; bool valid; };
struct ImuSegmentBinding { pinpoint::SourceId source; SegmentRole role; QQuaternion alignA, mountM; };
// added to ShotAnalysisJob:
std::vector<CameraCalib>        cameraCalib;     // parallel to cameraSources; valid=false ⇒ degrade
std::vector<ImuSegmentBinding>  imuBindings;     // resolves placement A/B/C → SegmentRole + anat transform
int   handedness = 0;  QString club;  QString athleteUuid;  QString swingDir;  bool cameraFixedInPlace = false;
```

`CameraCalib` is populated from new `AppSettings::cameraIntrinsics`/`cameraExtrinsics` maps (keyed on `cameraKey = description|serial`); `ImuSegmentBinding` resolves `AppSettings::imuPlacement` + the live `ImuInstance::alignA()/mountM()` snapshot. `swingDir` is where the single unified `swing.json` (raw manifest + inline analysis) is written at the join.

**Extending the result.** `ShotAnalysisResult` keeps `ok/score/metrics/tracePoints` (unchanged `ShotListModel::addShot` path — fully backward compatible) and gains an opaque-to-old-readers `std::shared_ptr<SwingAnalysis> detail`. `SwingAnalysis` carries the rich output (skeleton timeline, metric series, score breakdown, faults, phase timeline). `ShotProcessor::maybeJoin()` continues to call `addShot()` with the flat fields; a new optional role `analysisDetail` on `ShotListModel::Shot` holds the pointer for the detail UI. **Persistence:** there is **no separate `analysis.json`** — the analyzer's `SwingAnalysis` is folded **inline** into `swing.json` under an additive `"analysis"` object, written **once** at `ShotProcessor::maybeJoin()` (the GUI-thread barrier where both workers have completed). See *Persistence — unified `swing.json`* under Metrics & Scoring for the single-writer mechanism.

### Canonical intermediate data structures

```cpp
namespace pinpoint::analysis {

// Layer-0 master clock: all streams resampled onto camera-frame grid (or fixed 200 Hz).
struct TimelineSample { int64_t t_us; };                       // base; impact-relative via job.impactUs
using TimeGrid = std::vector<int64_t>;                         // ascending t_us, master timeline

enum class JointId { Pelvis, Spine, Neck, Head, LShoulder, /*…COCO+synth pelvis/neck…*/ Count };
enum class SegmentRole { Pelvis, Thorax, T12, LeadUpperArm, LeadForearm, LeadHand, TrailThigh, LeadThigh, Club };

struct SkeletonFrame {
    int64_t t_us;
    std::array<Eigen::Vector3d, (int)JointId::Count> joint;     // metric world position (m)
    std::array<float,           (int)JointId::Count> conf;      // 0..1, 0 ⇒ filled/missing
    std::array<QQuaternion, (int)SegmentRole::Count> segQuat;   // parent-local bone quaternion
};
struct SkeletonTimeline { std::vector<SkeletonFrame> frames; double boneLengthM[(int)SegmentRole::Count]; };

struct ClubSample { int64_t t_us; Eigen::Vector3d butt, mid, head; QQuaternion shaftQuat; float conf; };
struct BallSample { int64_t t_us; Eigen::Vector3d pos; bool present; };
struct ClubTrack { std::vector<ClubSample> samples; };
struct BallTrack { std::vector<BallSample> samples; };

struct MetricSeries { QString key; QString label; QString unit;            // machine-readable
                      std::vector<int64_t> t_us; std::vector<double> value; };
struct ScoredMetric { QString key; double value; double mu, sigma; double subScore;  // 0..100
                      QString band; /* green/yellow/red */ double weight; };
enum class Phase { Address, Takeaway, Top, Transition, Downswing, Impact, Release, Finish };
struct PhaseEvent { Phase phase; int64_t t_us; };

struct Fault { QString id, title, cause, drill; double pointsLost; Phase phase; };
struct ScoreBreakdown { int overall; QHash<QString,int> perRegion, perPhase; std::vector<ScoredMetric> metrics; };

struct SwingAnalysis {          // the rich detail behind ShotAnalysisResult::detail
    SkeletonTimeline skeleton; ClubTrack club; BallTrack ball;
    std::vector<MetricSeries> series; std::vector<PhaseEvent> phases;
    ScoreBreakdown score; std::vector<Fault> faults; int tier; // ReconstructionTier
};
} // namespace
```

All rotation is `QQuaternion`/`Eigen::Quaterniond`; Euler appears only at UI readout.

### Threading & performance

`analyze()` already runs on a QtConcurrent worker concurrently with `SwingExporter::run()` over the same frozen window. Heavy ONNX inference (layer 1) reuses the existing EP cascade (CoreML→CUDA→DirectML→CPU) but with `IntraOpNumThreads(1)` to avoid contending with the exporter's x264 encode; sessions are constructed per-`analyze()` call (the factory is per-shot). **Memory:** decode is zero-copy `cv::Mat` views over ring slots; only per-frame keypoint arrays and the `SkeletonTimeline` are heap-allocated (~5 s × ~120 fps × small structs ≈ a few MB). **Latency budget** for a 5 s window on a GPU EP: decode+pose dominate (~2 detector passes × 1–2 cameras × ~300 frames). Target ≈ 2–4 s wall on GPU, ≤ 10 s CPU; the shot still lands on the carousel regardless (degrade to `ok=false` → score 0, no replay), so latency never blocks the UI.

### Reconstruction tiers (revised)

The pipeline does **not** treat two cameras as the road to "real" analysis and one camera
as a degraded fallback. The common owner has **one face-on camera + sparse IMUs**, and that
configuration is a **first-class, fully-developed path** — the existing Wrist session already
lives there (`requiredCameras == 1`). Capability is the product of **two independent axes**,
not one ladder:

- **Camera axis** — `C0` no calibrated camera (2D image plane only) · `C1` **one** calibrated
  face-on camera (intrinsics + a single ground-plane `solvePnP` → metric world frame + scale,
  in-plane metric translations, monocular metric lift) · `C2` a second (down-the-line) camera
  for triangulated depth + occlusion redundancy.
- **IMU axis** — `I0` none · `I1` sparse (1–3 of pelvis / thorax / lead-wrist / lead-forearm)
  → absolute segment orientation · `I2` adds a future **club-shaft IMU** → club orientation,
  speed and shaft lean.

The key geometric fact the old tiering under-weighted: a **face-on camera images the frontal
plane** (lab `X` = target line and `Z` = vertical are *in-plane* and strongly observed), while
the down-the-line direction (lab `Y`, toward/away from ball) lies **along the optical axis** —
the worst depth axis, where monocular error is ~2–3× the in-plane error. Orthogonally, every
**segment-orientation** metric (X-factor, wrist flex/RUD, forearm pronation, shaft lean, body
kinematic-sequence timing) comes from the **IMU's absolute gravity-referenced quaternion** and
is **camera-count-independent** — full fidelity on one camera or two. So the second camera buys
back exactly the depth-axis quantities and occlusion robustness, and *nothing else*.

`enum ReconstructionTier` (chosen at job-build time from `cameraCalib[].valid` count + resolved
`imuBindings`):

1. **`Angles2D`** *(C0/C1, I0)* — no IMU. Image-plane angle series only: lead-arm flexion, tempo,
   2D turn proxies, and frontal-plane translations *once* `C1` calibration exists. The genuinely
   reduced corner — this is where the old "Angles2D" belongs, **not** single-cam+IMU.
2. **`Mono3D+IMU`** *(C1, I1)* — **the flagship single-camera tier.** One calibrated face-on
   camera (ground-plane scale + monocular lift via `MonoLift`/`MotionBERT`) **fused** with sparse
   IMUs (`ImuVisionFuser`). Delivers a *complete, validated coaching product*: **all** orientation
   metrics at full fidelity from the IMU (X-factor & stretch, wrist FE/RUD, forearm pronation,
   body kinematic sequence, pelvis/thorax turn), **plus** frontal-plane translations (sway, lift,
   secondary tilt, side-bend) and lead-arm flexion / tempo from the camera + ground plane. The IMU
   supplies the toward/away orientation the camera cannot see (von-Marcard bone-direction
   constraint), resolving the monocular depth/flip ambiguity for instrumented segments and
   reconstructing them straight through occlusion. **This is not a degraded `Full3D`** — it
   reproduces the SPI AUC-0.97 pelvic/torso-velocity separator and the HackMotion wrist bands,
   neither of which needs a second camera.
3. **`Stereo3D`** *(C2, I1+)* — the second (down-the-line) camera added as an **accuracy /
   occlusion enhancement**. Confidence-weighted DLT (`Triangulator`) recovers exactly the
   depth-axis metrics `C1` genuinely cannot do (`pelvisThrust`, `clubPath`, `attackAngle`,
   `swingPlane` azimuth) and **raises confidence** on the `Mono3D+IMU` metrics rather than making
   them appear for the first time. Positioned as *"better club delivery + fewer occlusions"*,
   never as "the real tier".
4. **`ClubInstrumented`** *(any C, I2)* — a future shaft/clubface IMU. An **orthogonal** upgrade
   to the second camera: unlocks reliable `clubheadSpeed` (`v_grip + ω×r`), shaft lean, a
   `faceAngle` proxy, and the club link of the kinematic sequence — **without** a second camera.
   A one-camera owner reaches club metrics by adding a *sensor*, not a *camera*.

`MonoLift` is therefore not a "fallback inside `Full3D`" but the **reconstruction layer of the
`Mono3D+IMU` tier**: when a view is absent or occluded, `Triangulator` swaps DLT for the
`MonoLift`/`MotionBERT` lifter, then `ImuVisionFuser` anchors it. Each metric additionally carries
its **own** provenance/confidence (see the catalog reframe), so degradation is data-driven per
metric — never a binary camera gate — and the UI prompts the *right* upgrade ("add a down-the-line
camera for club path" vs. "add a club sensor for clubhead speed").

### Per-session-type behavior

All four analyzers share layers 0–5; they differ in metric tables, scoring weights, and which sources matter:

- **SwingAnalyzer (0):** full body 3D. Pelvis/thorax rotation + velocity, **X-factor & X-factor-stretch**, kinematic-sequence efficiency, sway/thrust/lift, side-bend, tempo; club track drives swing-plane (SVD) and hand-path attack/path proxies. Heaviest tier.
- **WristAnalyzer (1):** single face-on camera + lead-wrist/hand IMU dominant. Lead-wrist flexion/extension and radial/ulnar deviation, forearm pronation/supination from `q_forearm⁻¹·q_hand` swing-twist; one-sided scoring bands (cupping penalized). Runs the `Mono3D+IMU` tier with ViTPose-wholebody hand keypoints (channels 91–132) as a vision cross-check.
- **GrfAnalyzer (2):** lead/trail-thigh + lumbar IMUs + cameras; weight-shift/sway and ground-contact timing from pelvis translation and feet keypoints. **No force-plate source exists** — GRF is estimated kinematically and clearly labelled as such until a force source is added.
- **CoachAnalyzer (3):** thorax/lumbar/T12 same as Swing but tuned weights and a DTW shape-similarity sub-score against a stored reference swing for over-the-top/casting path faults; emphasizes ranked faults over a single number.

The factory dispatch, `analyze()` signature, `ShotAnalysisJob`/`ShotAnalysisResult` flow into `addShot()`, and the frozen-window contract are all unchanged — the biomechanics is purely additive behind the existing seam.

Relevant files: `/home/markl/Projects/PinPointStudio/src/Analysis/shot_analyzer.h`, `/home/markl/Projects/PinPointStudio/src/Analysis/shot_analyzer.cpp`, `/home/markl/Projects/PinPointStudio/src/Buffer/swing_window.h`, `/home/markl/Projects/PinPointStudio/src/Buffer/imu_sample.h`, `/home/markl/Projects/PinPointStudio/src/Gui/shot_processor.cpp`, `/home/markl/Projects/PinPointStudio/src/Export/swing_exporter.cpp`.

## Algorithms

This section specifies the per-layer algorithms behind the `BiomechAnalyzer` pipeline. Layer names (`FrameDecoder`, `PoseRunner`, `Triangulator`, `ImuVisionFuser`, `TrackSmoother`, `SkeletonSolver`, `MetricExtractor`, `SwingScorer`, `FaultRanker`, `PhaseSegmenter`) refer to the architecture table. Each layer notes its MVP vs. deferred status.

### 0. Single-camera calibration (`SkeletonSolver` world frame, revised Algorithms §0)

Calibration is **not** a stereo prerequisite. It splits into four independent items, the first
two of which already give a complete metric `Mono3D+IMU` tier on **one** camera. **Metric scale
and a metric world frame are recoverable from a single camera** — a second camera is *not*
required for either.

**(a) Per-camera intrinsics (ChArUco) — valuable even alone.** Print a ChArUco board (e.g. 7×10
squares; measure the printed square with calipers — never trust the print scale). Build
`cv::aruco::CharucoBoard(Size(sx,sy), squareLenM, markerLenM, dict)` + `cv::aruco::CharucoDetector`
(OpenCV ≥ 4.7 unified API — legacy `calibrateCameraCharuco` is removed). Per view:
`detector.detectBoard(img, corners, ids)` → `board.matchImagePoints(...)`; accumulate 15–30 views
spanning all four image corners; `cv::calibrateCamera(...)`. **Gate RMS < 0.5 px**, surface it in
the wizard's calibration-status column (the IMU mount-gate pattern), re-shoot otherwise. Output
`K`, `dist`. With a **known focal length** alone, a metric-scale monocular lifter (MeTRAbs-style)
can already emit true-millimetre root-relative pose — so intrinsics are useful on their own,
before any extrinsic.

**(b) Single-camera ground-plane extrinsic → metric world frame + scale (one frame, ONE camera).**
This is **the** step that unlocks metric monocular 3D — **not** a second camera. Lay one large
ChArUco flat at the hitting spot, grab **one** frame, and run
`cv::solvePnP(boardObj_in_metres, imgPts, K, dist, rvec, tvec, SOLVEPNP_IPPE)` (IPPE exploits
planarity). The **board frame is the world frame**, so `world→camera = T(rvec,tvec)` directly and
`P = K[R|t]`. Because the object points are in **metres**, `t` is already metric — **scale and a
gravity-consistent world orientation are fixed simultaneously, with no essential-matrix scale
ambiguity**, from a single view. Bake a fixed rigid offset `T_world←board` to place the origin at
the tee with `+X` = target line, `+Z` = up. Tie re-runs to the existing `cameraFixedInPlace` flag.
QA gate: measure a wand of known length to within ~0.5 %. Caveat: a single planar board fixes the
*world frame* well but is weak **out-of-plane**, so it does *not* by itself constrain the golfer's
depth motion — that is recovered by the IMU/temporal/ground-contact terms in layers 3–4, not by
the board. (Optionally add a second board at a known height for a stronger static out-of-plane
constraint before any BA polish.)

**(c) No-board scale prior (graceful fallback).** When the user has not run the board capture (a
"one phone, no setup" capture), recover absolute scale by constraining back-projected **bone
lengths** to subject-specific or population-average values (Pavlakos-style least-squares on root
depth in `SkeletonSolver`), or by a **subject-height** field in the wizard. This keeps a *scaled*
skeleton so tempo / turn / side-bend / sway / lift still work, but feeds a **lower confidence**
than the board-calibrated metric frame and degrades when limbs foreshorten toward the camera
(again the optical-axis problem). **Gate cm-unit metrics on having a real scale source** — without
a board *or* a height/bone prior a single camera yields only an *unscaled* skeleton, so even
sway/lift are meaningless.

**(d) Second-camera extrinsic for triangulation (optional `Stereo3D` enhancement).** When a
down-the-line camera is present and **both** are already intrinsically calibrated by (a), the
relative pose is *cheap*: solve each camera's extrinsic from the **same** shared ground-plane
board via per-camera `cv::solvePnP(SOLVEPNP_IPPE)` composed through the common board frame
(board = world). Do **not** use `cv::stereoCalibrate`'s rectification path — it is numerically
degenerate at the ~90° DTL/face-on convergence. This yields `P_i = K_i[R_i|t_i]` for both
cameras for the confidence-weighted DLT in `Triangulator`. **This step is an enhancement, not a
gate:** every `Mono3D+IMU` metric is already produced without it; it adds only the depth-axis
metrics and occlusion redundancy.

**Extrinsic polish (deferred, Ceres).** One small bundle adjustment over the 12 (or 6, single-cam)
extrinsic DOF against all static board corners, intrinsics + 3D board points held constant
(`SetParameterBlockConstant`), `AutoDiffCostFunction<ReprojErr,2,6,3>` mirroring
`SnavelyReprojectionError`, `CauchyLoss`, `DENSE_SCHUR` + LM. MVP ships with raw `solvePnP`.

**Storage/UX.** `AppSettings::cameraIntrinsics` / `cameraExtrinsics` `QVariantMap`s keyed on
`cameraKey = description|serial` (single-AppSettings-instance rule), a genuine RMS-gated
`cameraCalibrated` flag on `CameraInstance`, and an optional `subjectHeightM` / per-subject
bone-length table for (c). The `CameraCalibrationFlow.qml` stub becomes capture → detect →
solve → reproj-gate → persist, mirroring `ImuCalibrationFlow`. On the UI thread,
`ShotProcessor::startAnalysis` reads these into `job.cameraCalib[]`; **one** valid calib selects
`Mono3D+IMU`, two select `Stereo3D`, none (with a height/bone prior) still yields a scaled
`Angles2D`/`Mono3D+IMU`.

### 1. Per-frame 2D detection (`PoseRunner`, `ClubBallTracker`, layer 1)

All models stay behind `PoseEstimatorBase`-style subclasses reusing the existing ORT EP cascade (CoreML→CUDA→DirectML→CPU), the pimpl `OrtState`, and `IntraOpNumThreads(1)` (to avoid contending with the exporter's x264 encode). Every subclass must honor the throttle-balance contract on all paths (the offline path bypasses `FrameThrottle`, but keep the `estimationDone()` discipline if reused live). All weights are Apache/MIT/BSD — **explicitly reject AGPL Ultralytics YOLO and CC-BY-NC GolfDB/SwingNet weights**.

**Body (MVP): ViTPose-L** (or **ViTPose-H** for maximum accuracy), top-down — already implemented as `PoseEstimatorViTPose` (currently the ViTPose-B-wholebody model; upgrade the variant). **Accuracy over speed by design:** the analyzer runs offline on a frozen window with a generous latency budget, so prefer ViTPose-L/H (~78–79 AP) over the speed-tier RTMPose-m (~75–76 AP); RTMPose/MoveNet stay on the live 60 Hz path only. A golfer is frame-stable, so run one person box (RTMDet-nano or reuse `PersonSegmenter`) at clip start and reuse it across the window; affine-warp the crop with a proper **letterbox** (*not* MoveNet's distorting square resize, which biases joint geometry), run ORT, and decode the heatmaps (argmax + sub-pixel/DARK refine) via the existing ViTPose decode path. Fill the 17-slot `PoseResult` (normalized y/x/score) unchanged. Favour the higher 384×288 input for motion-blurred downswing frames.

**Hands/wrist (camera cross-check, optional):** no separate model — the **same ViTPose-wholebody** network already emits 133 keypoints; channels 91–132 are the 21 lead-hand landmarks, currently discarded at `pose_estimator_vitpose.cpp:241`. Expose them via **new sibling result structs** with `Q_DECLARE_METATYPE` (never overload the 17-slot `PoseResult` — `BodyPoseAdapter` reads it by index) to get a camera-side hand cross-check for free. The forearm vector (elbow→wrist) vs hand vector (wrist→middle-MCP) yields a wrist-hinge sanity check — but for the Wrist session the IMU is authoritative, so this is only a cross-check, never the source.

**Club/ball (deferred):** a small custom 3–4-point shaft/clubhead keypoint model (RTMPose-tiny head retrained on self-captured, license-clean frames) seeded/validated by classical `cv::HoughLinesP`/LSD shaft-line fitting in a wrist-anchored ROI (robust through motion blur). Ball: `cv::HoughCircles`/blob on the teed-ball ROI. Until trained, club metrics are emitted as lead-hand-path approximations and clearly labelled.

### 2. 3D reconstruction (`Triangulator`, layer 2)

**Multi-view DLT — the `Stereo3D` accuracy enhancement (M3, only when a 2nd calibrated camera is present).** Per joint per master-grid instant: `cv::undistortPoints(pts_i, norm_i, K_i, dist_i)` to normalized rays, then build the confidence-weighted homogeneous system `A·X=0`. Each view `c` contributes two rows `[x_c·P_c.row(2) − P_c.row(0)]` and `[y_c·P_c.row(2) − P_c.row(1)]`, each **scaled by `sqrt(conf_c)`** (sqrt because the residual is squared — this gives the 22.6 vs 27.4 mm MPJPE edge of weighted DLT over RANSAC). Solve with `Eigen::JacobiSVD`/`BDCSVD`, take the smallest right-singular vector, dehomogenize. At ~90° convergence DLT is near-optimal, so add only **one** Gauss-Newton reprojection-refinement step (2×2 normal equations per point); skip Hartley–Sturm.

**Outlier gate.** With >2 views, triangulate, reproject, drop any view exceeding a pixel threshold, re-triangulate. Mirror swaps (respect the `isMirrored` flag in `P_i`) and left/right limb swaps surface as gross reprojection outliers and **must be rejected before smoothing**. A joint is `measured` only if both confidences exceed a floor **and** the ray-intersection angle lies in [20°,160°]; otherwise mark `missing` (set `conf=0`) — never fabricate from one confident view plus a guess.

**Monocular metric lift — the primary `Mono3D+IMU` reconstruction path (`MonoLift`, M2).** This is the **common single-camera case**, not a fallback: whenever fewer than two calibrated views are present (one face-on camera, or one view occluded), reconstruction uses MotionBERT (self-exported ONNX, fixed 243- or 81-frame clip, opset 17). Requires COCO-17→H36M-17 remap (synthesize pelvis=mid-hip, neck=mid-shoulder), per-clip 2D normalization, and a sliding buffer. Depth-dependent metrics get lower confidence. Pin to GPU EP.

### 3. IMU–camera fusion (`ImuVisionFuser`, layer 3)

**Sensor-to-segment anatomical transform.** The frozen `ImuSample` carries the **raw fused sensor-body** quaternion (accel/gyro are display-remapped, the quaternion is not). Apply the per-source anatomical transform resolved into `job.imuBindings[]`: `q_anat = A · q_raw · M` (`A=alignA`, `M=mountM` snapshotted from the live `ImuInstance` on the UI thread — they are session-lifetime and never reach the ring). `SegmentRole` comes from `AppSettings::imuPlacement[deviceId]` ('A'/'B'/'C') mapped through the session-type slot labels.

**Temporal alignment (MVP, critical).** Camera and IMU share `steady_clock` µs but are software-timestamped with multi-ms jitter; at 45 m/s clubhead 1 ms = 45 mm. Estimate a sub-frame per-stream offset by **cross-correlating IMU angular-velocity magnitude against the triangulated clubhead (or wrist) speed**, picking the fractional-frame shift that minimizes the lag-1 cross-correlation residual. Anchor everything on `job.impactUs` (the exact backdated marker). A 1-frame downswing error wrecks fusion — this step is non-optional.

**Quaternion injection.** For occluded shaft/wrist/forearm segments, place the child joint geometrically: `child = parent + L · (R_anat · segmentAxis)` — the IMU reconstructs the club and lead arm **through** full occlusion and motion blur, exactly where 2D detectors fail. Compose only via `QQuaternion`/slerp; never round-trip through Euler. Yaw drift of the 6-axis filter is removed per-shot by a Kabsch SVD alignment `R_GW` of IMU world to camera world over a still window (gravity is shared, so only yaw is free); re-zero heading-dependent metrics (X-factor, axial rotation) per shot — inclination metrics (shaft lean) are drift-free and need no re-zero.

**Tightly-coupled refinement (deferred).** A post-shot Ceres window optimizer over joint angles: reprojection + von Marcard bone-direction (the IMU segment axis must agree with the camera bone direction) + accel + smoothness residuals (~23 mm MPJPE). MVP fuses IMU as direct segment-orientation placement in layer 5's IK rather than a separate optimizer.

### 4. Temporal cleanup (`TrackSmoother`, layer 4)

**Resample to one timeline (layer 0/4 boundary).** Choose camera-frame timestamps (or a fixed 200 Hz grid) as master. **SLERP** IMU quaternions (`Eigen::Quaterniond::slerp` / `QQuaternion::slerp`, never lerp Euler) and **Catmull-Rom cubic-spline** positions/2D-keypoints between bracketing samples; linear interpolation only for scalar confidence channels.

**Kalman + RTS (MVP).** Per joint and per club point, a 9-state constant-acceleration linear KF (state `[p,v,a]`, white-noise-jerk process model, `H` selects position). Update on `measured`/IMU-filled frames with `R ∝ 1/conf`; **predict-only (skip the update)** on still-missing frames — never feed a zero or last-value as a fake measurement. Because each shot is a finished offline window, run the **Rauch–Tung–Striebel backward smoother** across the whole window (`C_k = P_k Fᵀ P_{k+1|k}⁻¹`; `x_k^s = x_k + C_k(x_{k+1}^s − x_{k+1|k})`) — strictly better than forward-only, removing lag/overshoot at impact. Tune jerk PSD **per joint**: high for hands/clubhead, low for torso (a single global value either lags the clubhead or jitters the torso). Enforce quaternion sign-continuity (`flip if dot(q_prev,q)<0`) before any differentiation.

**Gap fill order.** Before smoothing: (1) single-view ray ∩ bone-length sphere on the parent; (2) IMU-segment placement (layer 3); (3) KF coast. Learned motion priors (conv-autoencoder/ReMP) are **deferred** — only worth it for >0.3 s simultaneous all-sensor loss, rare on a 2-cam + shaft-IMU rig.

### 5. Skeleton / IK (`SkeletonSolver`, layer 5)

Bone lengths are **fixed per-subject constants**, calibrated once from a clean address frame — never re-estimated per frame. Solve weighted-LS IK over joint angles `q` minimizing `Σ wᵢ‖x_exp,ᵢ − x_model,ᵢ(q)‖²` with bone lengths baked into the forward-kinematic model and per-DOF box ROM limits; occluded joints get `wᵢ=0`. Solve via Ceres `AutoDiff` over `q` with `SetParameterLowerBound/UpperBound` for ROM, or Eigen Gauss-Newton for MVP. This guarantees constant bone lengths, pulls bad joints onto the anatomical manifold, and **emits per-bone `QQuaternion`s straight into the existing BodyPoseAdapter/BodyVizView chain** — the `SkeletonTimeline`. Biomechanical joint-limit priors (Akhter-Black conditioned limits) are a deferred penalty/reject layer; simple per-DOF box clamps ship in MVP.

### 6. Swing-phase segmentation (`PhaseSegmenter`, cross-cutting, MVP)

A hybrid signal detector over the `SkeletonTimeline` + IMU streams. **Impact** is the hard anchor (`job.impactUs`, IMU accel spike, ball-leaves-pixel — intersect for the frame). **Top/transition** = per-segment axial angular-velocity zero-crossing (pelvis crosses first — gives sequence order). **Address** = sustained angular-velocity minimum before motion onset (re-anchor the reference frame here, **not** frame 0). **Takeaway** = first rise above a noise threshold; **finish** = decay back toward zero. Typical durations gate the detectors (backswing ~1.16 s, downswing ~0.32 s, follow-through ~0.67 s). Output `std::vector<PhaseEvent>`. A learned GolfDB/SwingNet-style ONNX event detector (for the geometric toe-up/parallel events) is **deferred**; the IMU heuristic is the always-available fallback. Differentiate quaternions for angular speed (`ω ≈ 2·log(q_t⁻¹·q_{t+1})/dt`) and **Butterworth/Savitzky-Golay filter before peak-finding** — raw differentiated peaks make the kinematic-sequence order noise-dependent.

The downstream `MetricExtractor`/`SwingScorer`/`FaultRanker` (layers 6–8) consume these phase-anchored timelines; their algorithms (swing-twist axial decomposition, tolerance-band clipped-z sub-scores, weighted geometric aggregation, points-lost fault ranking) are detailed in the Metrics and Scoring sections.

## Metrics & Scoring

This section specifies the biomechanical metric catalog produced by `MetricExtractor` (layer 6) and the scoring/feedback model produced by `SwingScorer` (layer 7) and `FaultRanker` (layer 8). Every metric is computed at a phase event resolved by `PhaseSegmenter` and emitted as a `MetricSeries` (full time history) plus a single scalar at its scoring phase; the scalar feeds a `ScoredMetric`. All rotations stay `QQuaternion`/`Eigen::Quaterniond`; the angle extractions below convert to degrees only at the readout (the formulae use swing-twist decomposition and horizontal-projection `atan2`, never stored Euler intermediates).

### A) Metric catalog

Notation: `R_seg` = a segment's right-handed anatomical frame (built per the architecture's `SkeletonFrame.segQuat`); `q_seg` its quaternion; lab frame X = target line, Y = down-the-line/away-from-ball, Z = up (Cheetham convention). `twist(q, axis)` = swing-twist axial component of `q` about `axis`. `turn(e_ml)` = `atan2(dot(e_ml_h, Y), dot(e_ml_h, X))` of the segment medio-lateral axis projected to horizontal, **relative to its Address value**. "Input" states the data source. Per-metric **single-camera viability and the minimum capability** that yields each metric at full fidelity are in the companion *"Single-camera (face-on) viability"* table immediately below — not a column here, because viability is driven by camera *count* × IMU *placement*, not a single linear tier.

| Metric (key) | Definition & formula | Input | Phase | Unit | Pro / ideal range (source) |
|---|---|---|---|---|---|
| `pelvisRotation` | Axial pelvis turn vs address: `turn(e_ml_pelvis)`. | fused / IMU pelvis | Top, Impact | deg | Top ~45; Impact ~35–45 open (PMC9227529) |
| `thoraxRotation` | Axial thorax turn vs address: `turn(e_ml_thorax)`. | fused / IMU thorax | Top, Impact | deg | Top ~90 (shoulders) (PMC9227529) |
| `xFactor` | Thorax-minus-pelvis axial separation: `turn(e_ml_thorax) − turn(e_ml_pelvis)`, or `twist(q_pelvis⁻¹·q_thorax, pelvis_up)`. Label the method (acromion ≈2× spine). | fused / IMU | Top | deg | Top ~40–42 (TPI); ~48–60 shoulder-vs-pelvis (PMC9227529) |
| `xFactorStretch` | `max(xFactor over early downswing) − xFactor(Top)`. The stretch-shorten power signal — better speed predictor than static X-factor. | fused / IMU | Transition→early downswing | deg | ~5; skilled +19% vs +13% (Cheetham X-Factor Stretch) |
| `hipInternalRotation` | True hip-joint rotation: thigh axial vs pelvis, `twist(q_pelvis⁻¹·q_thigh, thigh_long)`, per side. | fused 3D | Top, Impact | deg | lead ~50 / trail ~40 amplitude (PMC9227529) |
| `leadWristFlexExt` | Lead hand flexion(+)/extension(−) vs forearm: Cardan component 1 of `q_forearm⁻¹·q_hand` about hand medio-lateral axis. Measured **relative to Address**. | **IMU (lead wrist/hand)** | Top, Impact | deg | Impact 15–30 more flexed than address; cupping at top a fault (HackMotion) |
| `leadWristRadUln` | Radial(+)/ulnar(−) deviation: Cardan component 2 of `q_forearm⁻¹·q_hand` about dorsal-palmar axis. | **IMU (lead wrist/hand)** | Top, Impact | deg | sets/holds lag; large ulnar at top normal (HackMotion) |
| `forearmPronation` | Lead-forearm pronation(+)/supination(−): `twist(q_upperarm⁻¹·q_forearm, elbow→wrist)`. | **IMU / fused** | Top→Impact | deg | rolls toward square through impact (PMC9227529) |
| `spineForwardBend` | Posture/forward bend: Cardan component 1 (flex/ext) of thorax(-rel-pelvis), fixed XYZ sequence. | camera-3D / fused | Address, Impact | deg | retained ~30–40 (irons) from address (PMC9227529) |
| `spineSideBend` | Lateral flexion (trail-side bend): Cardan component 2 of thorax(-rel-pelvis). | camera-3D / fused | Impact | deg | thorax ~32 / pelvis ~10 at driver impact (Cheetham/TPI) |
| `secondaryAxisTilt` | Spine lean away from target: angle of mid-hip→mid-shoulder vector from vertical in the frontal plane. | camera-3D | Impact | deg | ~20–25 at impact; ~6–8 at address (Cheetham) |
| `pelvisSway` | Lateral pelvis-origin displacement along X vs address: `dot(pelvis(t)−pelvis(addr), X)`. | **camera-3D only** | Backswing, Impact | cm | away in backswing, toward target downswing (Cheetham 6DOF) |
| `pelvisThrust` | Toward-ball displacement along Y; positive too early = early-extension fault. | **camera-3D only** | Downswing, Impact | cm | minimal toward-ball until late (Cheetham) |
| `pelvisLift` | Vertical pelvis displacement along Z vs address. | **camera-3D only** | Impact | cm | small controlled rise (Cheetham) |
| `kinematicSequence` | Peak axial angular-speed **order, timing gaps, magnitudes** for pelvis→thorax→lead-arm→club, from `ω = 2·log(qₜ⁻¹·qₜ₊₁)/dt` (SG/Butterworth smoothed, sign-continuity enforced). | fused (IMU body + club track) | Transition→Impact | deg/s, ms, ordinal | pelvis ~480, thorax ~605, lead-arm ~1310, club higher; transition ~50 ms; proximal-to-distal, only club accelerates through impact (TPI; PMC9227529) |
| `leadArmFlexion` | Elbow angle: `acos(dot(û_upper, û_fore))` from shoulder/elbow/wrist. **Most reliable pose-only metric** (all three joints in COCO-17). | camera-3D / 2D | Top, Impact | deg | near-straight (~165–180) through impact (PMC9227529) |
| `swingPlane` | Best-fit plane of clubhead (or lead-hand proxy) trajectory, knee-to-knee downswing: normal = smallest right-singular vector (JacobiSVD); report tilt vs ground + azimuth. | camera-3D club / hand-path proxy | Downswing | deg | tilt/azimuth per club; label hand-path if no club (Frontiers 2026; TrackMan) |
| `clubheadSpeed` | `‖v_clubhead‖` at impact, central difference of clubhead 3D position; or `v_grip + ω×r` from grip+IMU. | camera-3D club / IMU+ZUPT | Impact | mph | Driver ~113; 7-iron ~89 (TrackMan 2024) |
| `clubPath` | Horizontal angle of `v_clubhead` vs target line: `atan2(v·Y, v·X)`; +in-to-out / −out-to-in. Hand-path **proxy** if no club. | camera-3D club | Impact | deg | near 0 ± a few (TrackMan) |
| `attackAngle` | Vertical angle of `v_clubhead`: `atan2(v·Z, ‖v_horiz‖)`. | camera-3D club / hand proxy | Impact | deg | Driver −1.3 (some +3..+5); 7-iron −4.5 (TrackMan 2024) |
| `faceAngle` | Horizontal angle of clubface normal vs target line; needs club tracking or **lead-forearm+wrist proxy**. | camera-3D club / IMU proxy | Impact | deg | small open/closed; gates ball start (TrackMan) |
| `tempoBackswing` | Address→Top duration. | any (phase events) | — | s | ~0.75–0.85 (TPI 0.847±0.111) |
| `tempoRatio` | `tempoBackswing / downswingTime` (Top→Impact). | any (phase events) | — | ratio | ~3:1 (tour 2.2–3.0:1) |

\* Wrist flex/ext is **only trustworthy from the IMU** — COCO-17 lacks knuckle/thumb keypoints, so even with two cameras the camera term is a weak cross-check (ViTPose-wholebody hand keypoints) and the IMU quaternion is authoritative. † `faceAngle` is gated behind club tracking; until then it is suppressed or exposed as a forearm+wrist proxy clearly labelled `estimated`.

#### Single-camera (face-on) viability — per-metric reframe

The catalog's old single **Tier** column implied most metrics need two cameras. They do not.
Each metric now carries **`minCameras`** (`0` = IMU-delivered, camera-count-independent · `1` =
single face-on camera viable · `2` = genuinely needs the depth axis) and a viability verdict for
the common **1 face-on camera + sparse IMU** owner. Three groups fall out:

- **Camera-independent (IMU-delivered, `minCameras 0`)** — relative segment orientations. Full
  fidelity on one camera *or two*; the second camera adds nothing.
- **Strong frontal-plane monocular (`minCameras 1`)** — in-plane translations and projected
  angles a face-on camera sees in focus; the second camera adds only minor robustness.
- **Depth-axis-limited (`minCameras 2`, or a club device)** — club path / attack / thrust / face
  live along the **optical axis** (toward/away from ball), the one direction a lone face-on camera
  cannot resolve; these **must be confidence-flagged or suppressed**, never shown as a confident
  number on one camera.

| Metric (key) | single-cam(face-on)+IMU | What delivers it | 2nd camera (DTL) adds | minCam |
|---|---|---|---|---|
| `pelvisRotation` | good¹ | **IMU** pelvis axial turn vs address | nothing (IMU is authoritative) | 0 |
| `thoraxRotation` | good | **IMU** thorax axial turn vs address | low-conf cross-check only | 0 |
| `xFactor` | **full** | **IMU pair** `twist(q_pelvis⁻¹·q_thorax, up)` | nothing | 0 |
| `xFactorStretch` | **full** | **IMU pair** time series (vision is *worse* here) | nothing | 0 |
| `hipInternalRotation` | limited² | **IMU** thigh-vs-pelvis twist (needs thigh IMU) | not recovered by camera count | 0 |
| `leadWristFlexExt` | **full** | **IMU** Cardan-1 of `q_forearm⁻¹·q_hand` | ~nothing (no hand kpts in COCO-17) | 0 |
| `leadWristRadUln` | **full** | **IMU** Cardan-2 of `q_forearm⁻¹·q_hand` | ~nothing | 0 |
| `forearmPronation` | full³ | **IMU** `twist(q_upper⁻¹·q_fore, elbow→wrist)` | nothing (axial roll, not visual) | 0 |
| `spineForwardBend` | good⁴ | **IMU** thorax-rel-pelvis flex (drift-free incl.) | DTL is the natural sagittal view | 0/2 |
| `spineSideBend` | **good** | camera 2D **and** IMU incl. (frontal lean) | marginal accuracy | 1 |
| `secondaryAxisTilt` | **good** | camera 2D frontal-plane vector vs vertical | marginal robustness | 1 |
| `pelvisSway` | **good** | camera + ground-plane (lateral `X`, in-plane) | disambiguates apparent-vs-depth motion | 1 |
| `pelvisThrust` | **limited** | toward-ball `Y` = optical axis (weak) | **this is the unlock** — DTL sees it in-plane | 2 |
| `pelvisLift` | **good** | camera + ground-plane (vertical `Z`, in-plane) | robustness only | 1 |
| `kinematicSequence` | **full (body)** | **IMU** per-segment peak `‖ω‖` order/timing | only the **club link** improves | 0 |
| `leadArmFlexion` | **full** | camera 2D elbow angle (all 3 joints in COCO-17) | sharpens when arm swings into optical axis | 1 |
| `swingPlane` | limited | 2D hand-path proxy only | DTL is the classic swing-plane view | 2 |
| `clubheadSpeed` | limited | in-plane 2D × ground-plane, or `v_grip+ω×r` | adds depth component | 2 |
| `clubPath` | **none** | discriminating axis is `v·Y` = optical axis | **canonical DTL metric** | 2 |
| `attackAngle` | limited⁵ | `Z` (in-plane) vs horizontal ratio | DTL makes it fully in-plane | 2 |
| `faceAngle` | none⁶ | IMU forearm+wrist *proxy* only (labelled est.) | needs a **club device**, not a camera | (club) |
| `tempoBackswing` | **full** | phase events (any source) + impact marker | nothing | 0 |
| `tempoRatio` | **full** | phase events + impact marker (IMU sharpens) | nothing | 0 |

¹ **good** assumes a real **pelvis** IMU. Today's Swing/Coach slots are thorax/lumbar/T12 with
**no pelvis mount** — fixing the slot map to a clean pelvis+thorax pair (M3) is the actual unlock
for `pelvisRotation`/`xFactor`, *not* a second camera. Without a pelvis IMU this degrades to a
weak 2D turn proxy (`minCameras 1`, low confidence).
² Instrumentation gate (needs a thigh IMU), **not** a camera gate — femoral axial rotation is hard
even with stereo + pose-only; do not let this metric argue for a second camera.
³ Needs an upper-arm/forearm reference; with a single combined lead-wrist module, pronation is
approximated and confidence-lowered.
⁴ `good` with a thorax+pelvis IMU pair (gravity-observable inclination, drift-free, **no** yaw
re-zero); pose-only single-cam it is `limited` (sagittal = optical axis).
⁵ Less severe than `clubPath` because the informative axis (`Z`) is in-plane; still gated on a
club detector and biased by the unknown depth component — flag confidence.
⁶ A true measurement needs club/clubface tracking. The IMU forearm+wrist **proxy** is
camera-count-independent and `estimated`-labelled; this metric argues for a **club device**, never
a second camera.

> **Quantitative rationale.** For a face-on camera the optical axis is the toward/away (`+Y`)
> direction; monocular depth error along it is ~2–3× the in-plane error (clinical ~72–122 mm
> in-plane vs ~146–249 mm with depth; MuPoTS absolute A-MPJPE ~248 mm even for *metric-scale*
> lifters). Therefore `attackAngle = atan2(Vy, √(Vx²+Vz²))` and `clubPath = atan2(Vx, Vy)` are
> computed almost entirely from the camera's **worst** axis — they are `minCameras 2` and must be
> suppressed or flagged on a lone face-on view. Conversely the IMU measures `‖ω‖` and gravity-
> referenced inclination *natively*, so the kinematic sequence and all tilt/side-bend/X-factor
> metrics are **more** reliable from the IMU than from any single-camera lift (validated: trunk/
> pelvis rotation ICC ≈ 1.0, X-factor ICC 0.94, mean error ~1° vs optical mocap, Sensors 2023).

**Revised MVP scope — what a 1 face-on camera + IMU user gets.** The single-camera + sparse-IMU
owner is a **first-class MVP target**, not a degraded one. **At MVP, pre-calibration** (no ChArUco
board needed): `leadArmFlexion`, `tempoBackswing`, `tempoRatio`, and 2D turn proxies from the lone
camera — *plus*, the moment an IMU is worn, the **full orientation metric set at full fidelity**
because it is pure quaternion math needing no camera calibration and no triangulation:
`leadWristFlexExt`, `leadWristRadUln`, `forearmPronation` (Wrist session scores *for real* at MVP),
and with a pelvis+thorax pair `xFactor`, `xFactorStretch`, `pelvisRotation`, `thoraxRotation`,
`spineSideBend`, `spineForwardBend`, and the **body** `kinematicSequence`. **After single-camera
calibration** (intrinsics + one ground-plane board — *one* camera, not two): the frontal-plane
metric translations and tilts — `pelvisSway`, `pelvisLift`, `secondaryAxisTilt`, ground-plane-
scaled in-plane speeds — completing the `Mono3D+IMU` tier. **Only the depth-axis club / translation
metrics are deferred to later** (`pelvisThrust`, `clubPath`, `attackAngle`, `swingPlane`,
`clubheadSpeed`, `faceAngle`): these wait on the second (down-the-line) camera **or** a future
club-shaft IMU, and are surfaced via `SwingAnalysis.tier` + per-metric confidence so the UI prompts
the *right* upgrade — never shows a falsely-confident number, and never implies the lone-camera
user is getting a "fake" analysis.

### B) Scoring model

The score is **transparent and non-compensatory** by design: every number a coach sees traces to one metric, one reference band, one weight. No black-box ML.

**1. Per-metric sub-score (tolerance band, not naive z-score).** Each `ScoredMetric` maps its value to `subScore ∈ [0,100]` against a reference `(mu, sigma)` via a **deadband + bounded falloff**, so any tour-acceptable value scores 100 and deviations are never punished to absurdity:

```
z = (value - mu) / sigma
if |z| <= zIn:            subScore = 100            // green band, deadband
elif |z| <= zOut:        subScore = 100 * (1 - ((|z|-zIn)/(zOut-zIn))^p)   // yellow→red ramp
else:                    subScore = 0
```

with `zIn≈1.0`, `zOut≈3.0`, `p≈2` (or a Gaussian `100·exp(−½((|z|−zIn)/k)²)`). **One-sided** metrics (a fault only in one direction — e.g. cupped `leadWristFlexExt` at impact, negative `tempoRatio` deviation) use a half-band: the "good" side is clamped to 100. Band membership sets the `band` string (`green`/`yellow`/`red`) for the UI strip. This piecewise form is the explicit cure for over-penalization that pure z-scores cause (jitter + demoralizing zeros).

**2. Phase-aware comparison.** Every metric is read at its **matched phase event** (see catalog), never frame-aligned blindly. Reference `(mu,sigma)` are also phase-specific (`leadWristFlexExt` at Top ≠ at Impact). The `PhaseSegmenter` anchors on `job.impactUs` (the hard timestamp) and detects Top via hand-velocity reversal and transition via proximal-to-distal peak-velocity ordering.

**3. Optional DTW trajectory similarity.** `CoachAnalyzer` (and any tier with a stored reference) adds a shape sub-score: body-scale-normalized (shoulder-width), pelvis-centered joint trajectories warped against a recorded reference swing with an **impact anchor + Sakoe-Chiba band** (DTW_cpp, MIT, header-only — band added in our DP fill). The per-phase/per-region warp cost maps through the same band falloff to a `dtwShape` sub-score, catching path faults (over-the-top, casting) that discrete positions miss. Distance is attributed per joint/phase so faults are localizable.

**4. Weighting & aggregation (weighted geometric mean).** Sub-scores aggregate in two levels — into per-**region** (`perRegion`) and per-**phase** (`perPhase`) scores, then into `overall` — using the **weighted geometric mean**, never arithmetic:

```
agg = exp( Σ wᵢ·ln(max(sᵢ, 1)) ),   Σ wᵢ = 1
```

Geometric mean is **weakest-link**: one catastrophic fault (over-the-top at 10) drags the overall down ~2× harder than averaging and **cannot be masked** by good posture/tempo elsewhere — the precise failure of naive averaging. Sub-scores are floored at 1 to avoid `log(0)`. Weights live in a **versioned config table** the coach can see and tune; default rationale: impact > transition > top > address by phase; kinematic-sequence/rotation > wrist > tempo by region (sequence is the most coach-trusted, AUC ~0.97 pro-vs-amateur in SPI). Per-session-type weight tables differ (Wrist weights wrist metrics heavily; GRF weights sway/weight-shift).

**5. Reference-target sourcing.** Tier 1 ships now: a hardcoded versioned `(metric, phase, mu, sigma, oneSided, weight)` table from the literature (SPI rotational velocities; TPI/accelerometer tempo; HackMotion wrist bands; TrackMan club). Band widths scale by **skill level and club** so a mid-handicap is not scored against scratch SDs. Tier 2 (later): recorded reference swings per athlete (`athleteUuid`) for the DTW term and "vs your best swing" comparison.

**6. Ranked faults.** `FaultRanker` keys a rule table on `(metric, phase, sign-of-deviation)`, computes `pointsLost = weight × (100 − subScore)`, sorts descending, and emits the **top 1–3** `Fault`s with plain-English cause + drill. Examples: arm peak-velocity time before thorax → "Arms firing early (over-the-top tendency)"; `tempoRatio` ≪ 3:1 → "Quick from the top"; positive `leadWristFlexExt` at impact → "Cupped lead wrist / open face — loss of compression"; negative pelvis→thorax sequence gap → "No separation — upper and lower firing together". The score earns attention; the ranked faults are the actionable coaching.

**Concrete C++ types** (in `pinpoint::analysis`, matching the architecture's intermediate structs):

```cpp
struct ScoredMetric {
    QString key;            // e.g. "xFactor"  — matches MetricSeries::key
    double  value;          // measured scalar at scoring phase
    double  mu, sigma;      // reference band centre/spread (skill+club scaled)
    bool    oneSided;       // half-band (directional fault) if true
    double  subScore;       // 0..100 after band falloff
    QString band;           // "green" | "yellow" | "red"
    Phase   phase;          // phase the value was read at
    double  weight;         // aggregation weight (from config table)
};

struct Fault {
    QString id, title, cause, drill;   // rule + human-readable coaching
    double  pointsLost;                // weight * (100 - subScore), ranking key
    Phase   phase;
};

struct ScoreBreakdown {
    int                      overall;       // 0..100, weighted geometric mean
    QHash<QString,int>       perRegion;     // "rotation","wrist","tempo","sequence","posture"
    QHash<QString,int>       perPhase;      // by Phase name
    std::vector<ScoredMetric> metrics;      // full audit trail
};
```

`ScoreBreakdown` and the `std::vector<Fault>` live inside `SwingAnalysis` (the `ShotAnalysisResult::detail` shared_ptr); `overall` is copied into the flat `ShotAnalysisResult::score` for the unchanged `addShot()` path, and folded inline into `swing.json`'s additive `"analysis"` object at `maybeJoin()` for cross-restart persistence. **Validation gates:** reproduce SPI pro-vs-amateur separation (AUC ~0.97) on the rotational-velocity sub-scores, and check `overall` correlates with coach ratings (DMSM reached κ≈0.71) before the score is trusted in the UI.

## Metric time series & key-swing-point sampling

Every biomechanics metric is **two things at once**: a *continuous per-frame curve* over the swing window's master timeline, and a *sparse set of labelled samples* at each key swing point. The graph needs both — the curve to draw the line, the phase samples to place markers, pin checkpoint readouts, and drive scoring. Both live in the committed `pinpoint::analysis` structs (`docs/SHOT_ANALYZER_DESIGN.md:146-161`); this subsection makes them buildable and persistent. None of it exists in code yet — `src/Analysis/swing_analysis.{h,cpp}` is **to be ADDED**; `ShotAnalysisResult` (`src/Analysis/shot_analyzer.h:47-53`) today carries only `score`/`metrics`/`tracePoints`.

### 1. Data model — TimeGrid, MetricSeries, PhaseSample

**Master clock (`TimeGrid`).** All streams are resampled onto one ascending `std::vector<int64_t> t_us` (`TimeGrid`, design line 128) built by the analyzer worker. Cadence rule:

- **Primary grid = the face-on camera-frame timeline** (each frame's `IndexEntry::timestamp_us`, `src/Buffer/types.h:56-57`). This guarantees one curve sample per displayed/replayed frame, so the playhead lands exactly on a series sample with no interpolation — and it tolerates variable capture rate (the same nearest-preceding model the replay uses, `shot_processor.cpp:484-492`).
- **Fallback grid = fixed 200 Hz** when no usable camera stream exists (IMU-only Wrist shots): `t_us = startUs + k·5000` across `[SwingWindow::startTimestampUs(), endTimestampUs()]` (`src/Buffer/swing_window.h:47-48`). 200 Hz over-samples the 100 Hz IMU so the curve is smooth and phase peak-finding is precise.

**IMU 100 Hz → wrist angles → grid.** The WT901 delivers quaternions at 100 Hz. The analyzer reads them via `SwingWindow::interpolateImu(SourceId, target_us, …)` (`src/Buffer/swing_window.h`), which slerps between the two bracketing in-window samples (no extrapolation, per the quaternions-only rule — never lerp Euler). For each grid `t_us` it interpolates the lead-wrist and lead-forearm quaternions, applies the swing-twist decomposition (M1 §4), and emits the scalar angle. Result: one `MetricSeries::value[i]` per `TimeGrid` index.

**Extend `MetricSeries` with phase samples** (the curve alone has no markers):

```cpp
namespace pinpoint::analysis {

struct PhaseSample {                 // ADD — a labelled point on one metric's curve
    Phase   phase;                   // Address … Finish (design line 150)
    int64_t t_us;                    // == the PhaseEvent t_us for this phase
    double  value;                   // metric value sampled at t_us (grid-interpolated)
    QString band;                    // "green"/"yellow"/"red" at scored phases, else ""
};

struct MetricSeries {                // EXTEND the committed struct (design line 146)
    QString key, label, unit;
    std::vector<int64_t> t_us;       // == the shared TimeGrid
    std::vector<double>  value;      // one per t_us — the continuous curve
    std::vector<PhaseSample> phaseSamples;   // ADD — sparse, ≤8 markers
    std::optional<double> bandLo, bandHi;    // ADD — ideal/tour band (unit-space), for shaded plot
    bool flexPositive = true;        // ADD — canonical sign flag (see §2)
};

}   // namespace
```

The **`PhaseEvent` timeline is shared across all metrics** — it is computed once per shot (`std::vector<PhaseEvent> SwingAnalysis::phases`, design line 158) by the PhaseSegmenter, and each metric's `phaseSamples` simply samples its own curve at those eight `t_us`. So all curves' markers line up vertically on a common x-axis. Impact is the hard anchor: `PhaseEvent{Phase::Impact, job.impactUs}` taken verbatim from the ShotMarker (`ShotAnalysisJob::impactUs`, `shot_analyzer.h:37`), never re-derived. The other seven phases are segmenter output (Butterworth/SG-filtered peak-finding, design line 356); low-confidence phases are flagged so QML can fade their ticks.

### 2. Time axis, units, sign, ideal band

- **Impact-relative time axis.** Plot x as `t_us − job.impactUs`, so **impact = 0**, backswing negative, follow-through positive. This makes the playhead, phase ticks, and (later) multi-shot overlay trivially correct, and matches impact-normalised comparison conventions. Store absolute `t_us` in `MetricSeries`; subtract at render time.
- **Units** carried per-series in `MetricSeries::unit` ("°", "°/s", "m"). The y-axis labels and the value-at-playhead readout read it directly; never overlay different-unit metrics on one axis.
- **Sign convention (canonical, stored).** The M1 internal convention is **flexion +, extension −** (`flexPositive = true`). HackMotion-trained coaches expect the opposite (extension/cupping up). The fix: **store the canonical M1 sign in `value[]`** and flip *only at the QML readout/axis label* — never re-sign the stored series (it would invert scoring). `flexPositive` documents the stored polarity so the chart knows which way to flip.
- **Ideal/tour band (optional).** `bandLo`/`bandHi` (unit-space, one-sided allowed: leave one `nullopt`) drive a semi-transparent shaded region drawn **only across the scored phases** (Top, Impact for Wrist), not the full axis. For one-sided metrics (cupping = fault, bowed = fine) shade only the fault side, matching `SwingScorer`'s good-side clamp.

### 3. Persistence — unified `swing.json` (folded at the join)

The full series + phase markers must survive replay-window destruction (`finishShot()` destroys `m_swingWindow`, `shot_processor.cpp:537-548`) **and** app restart — persisted in the **single** `swing.json` document, never a separate file.

**One document, one writer, written once.** Raw manifest and derived analysis live in the same `swing.json` (schema bumped **`pinpoint.swing/1` → `pinpoint.swing/2`**; the additive `"analysis"` object is the only new content, so `/1` readers stay forward-compatible). Because the exporter and analyzer run concurrently over the frozen window, **neither writes the file**:

- `SwingExporter::run()` writes **media only** (MP4s, `thumb.jpg`) and **returns** its already-assembled raw manifest by value in `SwingExportResult.manifest` (`QJsonObject`) — the JSON tree it builds in memory (`swing_exporter.cpp:341-456`) is handed out instead of written (the `QSaveFile` block at `:458-468` is deleted).
- `ShotAnalyzer::analyze()` stays pure-return (`ShotAnalysisResult`), touching no disk.

The single authoritative write happens on the **GUI thread** at `ShotProcessor::maybeJoin()` (`shot_processor.cpp:395-435`) — the existing barrier that runs only after **both** `QFutureWatcher`s resolve. There, `SwingDocWriter::writeSwingJson(swingDir, m_exportManifest, analysisOk ? &result : nullptr, thumbTusOffset)` composes raw + derived into one tree and commits **atomically** (`QSaveFile` temp + `commit()` rename). **No parallel-write race is possible** — the analyzer writes nothing, the exporter writes only media, and the lone `swing.json` write is serial on the GUI thread after both workers have delivered.

The analyzer's `SwingAnalysis` is serialised **inline** under `"analysis"` (no `{file}` indirection):

```jsonc
{ "schema": "pinpoint.swing/2",
  "swing": {...}, "athlete": {...}, "clock": {...}, "window": {...},
  "thumbnail": { "file": "thumb.jpg", "t_us": ... },
  "streams": [ /* raw video/imu entries — UNCHANGED from /1 */ ],
  "analysis": {                                  // additive, present iff analysis ok
    "schema": "pinpoint.analysis/2",             // versions the embedded block
    "impactUs": 172834905123,                    // x-axis zero
    "timeGrid": [ /* ascending t_us */ ],
    "phases": [ {"phase":"Top","t_us":172834700000,"conf":0.82}, ... ],
    "metrics": [ { "key":"leadWristFlexExt", "label":"Lead-Wrist Flex/Ext", "unit":"°",
                   "flexPositive": true, "bandLo": -5, "bandHi": 5,
                   "value": [ /* one per timeGrid index */ ],
                   "phaseSamples": [ {"phase":"Impact","t_us":172834905123,"value":-18.4,"band":"green"}, ... ] } ],
    "score": { /* ScoreBreakdown */ } } }
```

**Degradation (always write what is available).** `addShot()` is unconditional, so `writeSwingJson()` writes whatever exists: export ok + analysis ok -> raw + `"analysis"`; export ok + analysis fail -> raw only (`"analysis"` omitted); export fail + analysis ok -> a minimal header synthesised from `SwingExportJob` values + `"analysis"`, `streams: []`; both fail -> nothing written. **Teardown caveat:** `finishNowBlocking()` bypasses `maybeJoin()`, so an interrupted shot persists nothing unless `finishNowBlocking()` itself calls `writeSwingJson()` with the results in hand.

**Reload.** One reader, `SwingDocReader::readSwingJson(dir)`, rehydrates **both** raw and derived from the single document (media paths + thumbnail from the top level; score/metrics/series from `"analysis"`), feeding a new `ShotListModel::addPersistedShot(swingDir, ..., rating, note, trashed)`. *(Reload is future work; the unified document makes it one read, not two.)*

**In-memory → QML** — widen `ShotAnalysisResult` with `std::shared_ptr<SwingAnalysis> detail` (design line 156), forward it through `ShotProcessor::maybeJoin()` → `ShotListModel::addShot()` (`shot_processor.cpp:411-417`), and add a **`SeriesRole`** to `ShotListModel::Roles` (`src/Gui/shot_list_model.h:48-61`) that exposes the metrics as a `QVariantList` of typed maps `{key,label,unit,t_us,value,phaseSamples,bandLo,bandHi,flexPositive}` plus a `PhasesRole` (`[{phase,t_us,conf}]`). Register `Q_DECLARE_METATYPE(std::shared_ptr<SwingAnalysis>)`. This is **distinct from the existing `TracePointsRole`** (`shot_list_model.h`), which stays the lossy normalised 0..1 sparkline for the card.

### 4. Downsampling — card sparkline vs detail graph

- **Detail graph** consumes the **full `value[]`** at native grid density (~120–200 pts over 5 s — cheap for a `Shape` `PathPolyline`). No downsampling; the playhead must land on real samples.
- **Card sparkline** keeps the existing `TracePointsRole` (normalised 0..1, `PpTrace.qml`). The analyzer derives it once by **LTTB-downsampling the primary metric to ~24 points** then normalising t and value into 0..1 (y down). This is backward-compatible — `PpShotCard`/`PpTrace` are unchanged — and decouples the tiny card glyph from the heavy detail series.

---

> See **SHOT_ANALYZER_M1_WRIST.md → Replay-synchronized graphing** for how these series drive the lockstep detail view.

## Implementation Plan

This plan takes the team from the four stub analyzers in `src/Analysis/shot_analyzer.cpp` to the full nine-layer `pinpoint::analysis` pipeline. It is sequenced so **every milestone ships standalone, user-visible value** and never breaks the existing `analyze()` → `addShot()` → carousel path. Each phase keeps the `ShotAnalyzer::analyze(const SwingWindow&, const ShotAnalysisJob&)` signature, the frozen-window const-reader contract, and the quaternions-only rule. Phases build strictly on the layer model: M0 plumbing → M1 monocular skeleton + naive score → M2 calibration + stereo → M3 IMU fusion + advanced metrics → M4 smoothing/occlusion → M5 reference scoring + feedback → M6 polish/validation.

### M0 — Plumbing & persistence skeleton (no biomechanics yet)

**Goal / outcome.** The result struct, job struct, the unified `swing.json` writer/reader, and module scaffold exist and round-trip; the carousel score now *persists across restart* in the single document. No new math, but the seam every later phase plugs into is locked.

**New files / classes.**
- `src/Analysis/biomech_analyzer.{h,cpp}` — `BiomechAnalyzer` base owning the (initially empty) layer pipeline; `SwingAnalyzer`/`WristAnalyzer`/`GrfAnalyzer`/`CoachAnalyzer` derive from it.
- `src/Analysis/swing_analysis.{h,cpp}` — the canonical intermediate structs (`SkeletonTimeline`, `MetricSeries`, `ScoreBreakdown`, `Fault`, `SwingAnalysis`, `ReconstructionTier`), `Q_DECLARE_METATYPE(std::shared_ptr<SwingAnalysis>)`.
- `src/Export/swing_doc.{h,cpp}` — `SwingDocWriter::writeSwingJson(swingDir, rawManifest, const SwingAnalysis*, thumbTusOffset)` + `SwingDocReader::readSwingJson(swingDir)` for the single unified document (**replaces** the never-built `analysis_io`/`analysis.json`).

**Changes to existing files.**
- `src/Analysis/shot_analyzer.h` — widen `ShotAnalysisJob` (add `cameraCalib`, `imuBindings`, `handedness`, `club`, `athleteUuid`, `swingDir`, `cameraFixedInPlace`); add `std::shared_ptr<SwingAnalysis> detail` to `ShotAnalysisResult`; `makeShotAnalyzer()` returns the new classes (still producing the old stub `score`/`metrics` so the carousel is unchanged).
- `src/Gui/shot_processor.{h,cpp}` — in `startAnalysis()` populate the new job fields on the UI thread (read `AppSettings`, `ImuInstance::alignA()/mountM()`, `SwingPaths` swingDir); `onSwingSaveFinished()` stashes the exporter's returned `m_exportManifest`; `maybeJoin()` calls `SwingDocWriter::writeSwingJson()` once (both done) **then** `addShot()` with the flat fields + `result.detail`.
- `src/Gui/shot_list_model.{h,cpp}` — add an `analysisDetail` role holding the pointer + an `addPersistedShot(swingDir, ...)` entry; on app start, `SwingDocReader::readSwingJson()` rehydrates `score`/`metrics` from the single `swing.json`'s `"analysis"` object.
- `src/Export/swing_exporter.{h,cpp}` — `run()` no longer writes `swing.json`; it writes **media only** and returns the assembled raw manifest in `SwingExportResult.manifest` (`QJsonObject`) + `thumbnailTusOffset`.
- `CMakeLists.txt` — add the new `src/Analysis/**` sources to the target. Eigen is already on the include path (`${eigen_SOURCE_DIR}`, line 482) — **no CMake dependency change**.

**Exit criteria.** A manual SHOT writes one `swing.json` containing both the raw manifest and the `"analysis"` block; restarting the app shows the same score on the carousel card. Unit test: serialize→deserialize a populated `SwingAnalysis` round-trips losslessly through `swing_doc`.

### M1 — Monocular skeleton + IMU orientation metrics + banded score (`Angles2D` → `Mono3D+IMU`)

**Goal / outcome.** Real numbers on **one camera, possibly pre-calibration**: camera 2D angles
(lead-arm flexion, turn proxies, tempo) **and** the full IMU orientation metric set, with a
transparent banded score replacing the timestamp-hash. Every single-camera owner benefits
immediately; the Wrist session scores for real.

**New files / classes.**
- `src/Analysis/decode/frame_decoder.{h,cpp}` — `FrameDecoder`, zero-copy `cv::Mat` wrap +
  demosaic, factored out of `swing_exporter.cpp`'s demosaic table.
- `src/Analysis/detect/pose_runner.{h,cpp}` — `PoseRunner` reusing the `PoseEstimatorBase` ORT EP
  cascade, run **offline** per decoded frame (no `FrameThrottle`). Use the already-wired `PoseEstimatorViTPose` and **upgrade the model to ViTPose-L/H** — the analyzer is offline, so favour accuracy over the live path's MoveNet.
- `src/Analysis/phase/phase_segmenter.{h,cpp}` — `PhaseSegmenter` anchored on `job.impactUs`,
  detecting Top/Transition from **IMU angular-velocity zero-crossings** (sharper than 2D) with a
  2D keypoint-velocity fallback when no IMU.
- `src/Analysis/fusion/imu_vision_fuser.{h,cpp}` *(orientation-only slice, pulled forward)* — apply
  `q_anat = A·q_raw·M` from `job.imuBindings`; **no camera calibration needed for orientation
  metrics**, so the swing-twist extractions land at M1.
- `src/Analysis/metrics/metric_extractor.{h,cpp}` — 2D `MetricSeries` (elbow angle, axial-turn
  proxy, tempo) **plus** the camera-independent IMU metrics: `xFactor`, `xFactorStretch`,
  `leadWristFlexExt`, `leadWristRadUln`, `forearmPronation`, `pelvisRotation`, `thoraxRotation`,
  `spineSideBend`/`spineForwardBend`, and the **body** `kinematicSequence` (per-segment `‖ω‖`
  peak ordering/timing).
- `src/Analysis/score/swing_scorer.{h,cpp}` — tolerance-band sub-scores + **weighted geometric
  mean**; `reference_tables.h`.

**Changes.** `BiomechAnalyzer` runs layers 0→1→3(orientation)→6→7 with `tier = Angles2D` when no
IMU, `Mono3D+IMU` (orientation subset) when an IMU is bound — **no layer-2 triangulation and no
camera calibration required for the IMU metrics**. Per-shot heading re-zero (Kabsch yaw, gravity
shared) for heading-dependent metrics; inclination metrics are **not** re-zeroed (drift-free).
`ScreenWrist.qml` metric keys populated from real values.

**New dependency.** **ViTPose-L (or -H) ONNX** (Apache-2.0) — `PoseEstimatorViTPose` already exists
(ViTPose-B); add the larger variant through the existing `HAVE_VITPOSE` download/copy block. No
separate hand model: the same whole-body network's channels 91–132 supply the optional hand cross-check.

**Exit criteria.** Wrist FE/RUD within HackMotion-plausible bands on a hand-checked clip; X-factor
inside published tour bands on a pro reference; tempo within ±10 %; faults panel empty but
score-breakdown present. Deterministic metric output on a saved `SwingWindow` fixture.

### M2 — Single-camera metric calibration + monocular metric lift (`Mono3D+IMU` complete)

**Goal / outcome.** Promote the lone face-on camera to a **metric** sensor: intrinsics + **one**
ground-plane board → metric world frame + scale, monocular metric lift anchored by the IMU. Ships
the **complete `Mono3D+IMU` flagship tier on ONE camera** — frontal-plane translations and tilts
go live. **No second camera, no DLT, no triangulation in M2.**

**Calibration UX work item (ships with M2).**
- Replace the `CameraCalibrationFlow.qml` stub: (1) per-camera ChArUco intrinsic capture (15–30
  views, `cv::calibrateCamera`, **gate RMS < 0.5 px**); (2) **one** laid-down ground-plane ChArUco
  → per-camera `cv::solvePnP(SOLVEPNP_IPPE)`, board = world, **metric scale free from one camera**;
  (3) optional `subjectHeightM` / bone-length **no-board fallback** feeding lower confidence.
  Mirror the `ImuCalibrationFlow` host contract.
- `src/Gui/camera_calibrator.{h,cpp}` — the OpenCV calib3d solver behind the wizard.
- `src/Gui/app_settings.{h,cpp}` — `cameraIntrinsics`/`cameraExtrinsics` maps, RMS-gated
  `cameraCalibrated` flag, `subjectHeightM`/bone table (single-AppSettings-instance rule).

**New analyzer files.**
- `src/Analysis/recon/triangulator.{h,cpp}` — at M2 `Triangulator` runs its **`MonoLift` path**:
  `MotionBERT` (self-exported ONNX, fixed 243/81-frame, opset 17) with COCO-17→H36M-17 remap, the
  ground-plane `solvePnP` scale, and the no-board scale prior; depth-dependent metrics carry lower
  confidence. *(DLT is wired but dormant until a second calib is present.)*
- `src/Analysis/fusion/time_grid.{h,cpp}` — SLERP-quaternion / Catmull-Rom resample onto the
  master grid; **angular-velocity cross-correlation** for sub-frame IMU↔camera sync (1 ms = 45 mm
  — non-optional before any fused per-frame term).

**Changes.** `startAnalysis()` fills `job.cameraCalib` from the new maps (gated on
`cameraFixedInPlace`); `BiomechAnalyzer` selects `Mono3D+IMU` on **one** valid calib and runs the
monocular lift + IMU global anchoring (`ImuVisionFuser` supplies the toward/away bone-direction
constraint the face-on camera cannot see, resolving depth/flip for instrumented segments).
`MetricExtractor` gains `pelvisSway`, `pelvisLift`, `secondaryAxisTilt`, and ground-plane-scaled
in-plane speeds.

**New dependency.** OpenCV calib3d/objdetect (already linked; target the 4.7 `objdetect` ChArUco
API). MotionBERT ONNX (Apache-2.0, self-exported) — gate behind `WITH_FUSION`/GPU EP.

**Exit criteria.** Wand-of-known-length scales within 0.5 % from **one** camera; ground-plane
ChArUco corner reprojects < 1 px; 3D pelvis sway/lift plausible vs hand annotation; the full
`Mono3D+IMU` metric set produced **without a second camera**.

### M3 — Second-camera triangulation enhancement + IMU slot-map fix (`Stereo3D`; future club IMU)

**Goal / outcome.** Add the **down-the-line** camera **purely as an accuracy / occlusion
enhancement** that recovers the depth-axis metrics `Mono3D+IMU` genuinely cannot do — confidence
on existing metrics rises rather than metrics appearing for the first time. Also fix the IMU slot
map so X-factor/pelvis turn get a clean pelvis-vs-thorax pair.

**New / changed.**
- `Triangulator` **DLT path** goes live: `cv::undistortPoints` → confidence-weighted DLT (Eigen
  `JacobiSVD`, rows ×√conf) → one Gauss-Newton reprojection step; per-joint gating on
  dual-confidence + ray-angle [20°,160°] + reprojection residual; mirror/limb-swap rejection. The
  second camera's extrinsic comes from the **shared** M2 ground-plane board (both already
  intrinsically calibrated) — cheap relative pose, **no** `stereoCalibrate`.
- `BiomechAnalyzer` selects `Stereo3D` when **≥ 2** valid calibs; `MetricExtractor` now emits the
  depth-axis set — `pelvisThrust`, `clubPath`, `attackAngle`, `swingPlane` azimuth — and triangu-
  lation **upgrades the confidence** of every M2 in-plane/IMU metric (occlusion redundancy).
- **IMU slot-map fix (the real unlock for `xFactor`/`pelvisRotation`).** Add a true **pelvis** IMU
  option distinct from the thorax/lumbar/T12 triple, so the pelvis-vs-thorax pair is clean. This
  is an **instrumentation** change, *not* a camera gate.
- **(Parallel, future) `ClubInstrumented` hook.** A shaft/clubface IMU (`I2`) is the **orthogonal**
  upgrade to the second camera: `clubheadSpeed` via `v_grip + ω×r`, shaft lean, a `faceAngle`
  proxy, and the **club link** of the kinematic sequence — delivered **without** a second camera.
  Wire `job.imuBindings` to accept a `SegmentRole::Club` source; gyro FSR and shaft-flex caveats
  apply (label club state a coaching **proxy**, not launch-monitor truth).

**Changes.** `startAnalysis()` resolves `AppSettings::imuPlacement` + `ImuInstance::alignA()/
mountM()` into `job.imuBindings` (incl. the new pelvis / future club roles). `GrfAnalyzer` consumes
thigh/lumbar IMUs (GRF kinematically estimated, clearly labelled — no force-plate source).

**Exit criteria.** With the DTL camera, `clubPath`/`attackAngle`/`pelvisThrust` populate within
published bands and the M2 metrics' confidence rises; **without** it, the full `Mono3D+IMU` set
remains complete (proving the second camera is an enhancement, not a gate). Sign convention
validated against `mirroredSource`/handedness; occluded segments stay IMU-reconstructed.

> **Knock-on to M0 and M4–M6.** **M0** is unchanged except that `ReconstructionTier` now enumerates
> `{Angles2D, Mono3DPlusImu, Stereo3D, ClubInstrumented}` and `ShotAnalysisJob` adds
> `subjectHeightM` + per-subject bone lengths (for the §0(c) no-board prior) alongside `cameraCalib`
> / `imuBindings`. **M4** (`TrackSmoother` + `SkeletonSolver` IK) is unchanged but now operates on a
> pipeline whose **single-camera** output is already a complete product before any second camera
> exists; bone lengths feed both the IK and the §0(c) scale prior. **M5/M6** are unchanged in scope;
> M6's degradation-tier matrix exercises the **two-axis** capability model (camera × IMU), and the
> recalibration prompt is per-tier ("add a DTL camera for club path" vs. "add a club sensor for
> clubhead speed") rather than a generic "recalibrate".

### M4 — Smoothing, occlusion robustness, IK skeleton

**Goal / outcome.** Jitter-free, anatomically valid 3D suitable for replay overlay and stable metrics; the `SkeletonTimeline` drives `BodyVizView`.

**New files / classes.**
- `src/Analysis/cleanup/track_smoother.{h,cpp}` — `TrackSmoother`: per-joint constant-acceleration Kalman + Rauch-Tung-Striebel backward smoother over the whole window (offline ⇒ future samples available; predict-only on missing frames, R∝1/confidence, per-joint jerk PSD).
- `src/Analysis/skeleton/skeleton_solver.{h,cpp}` — `SkeletonSolver`: weighted-LS IK over joint angles with fixed bone lengths (calibrated from a clean address frame) + ROM box limits → per-bone `QQuaternion` `SkeletonTimeline`.

**Changes.** `BodyPoseAdapter` gains an offline-skeleton consumption path (replay shows the analyzed swing, not live keypoints); `ShotProcessor` replay can drive it. Bone-length and joint-limit constants in `reference_tables.h`.

**New dependency.** **Ceres Solver** (BSD-3, pulls Eigen) for the IK nonlinear solve and the extrinsic bundle-adjustment polish — `find_package(Ceres)` in CMakeLists.txt, gated behind `WITH_FUSION`; CPU-only is fine. Kalman/RTS/Kabsch are pure Eigen (no new dep).

**Exit criteria.** Reconstructed clubhead trajectory is smooth through impact with no RTS overshoot; bone lengths constant ±2 mm; reprojection of smoothed skeleton < 3 px both views.

### M5 — Reference-swing scoring + ranked feedback

**Goal / outcome.** A trustworthy 0-100 score with red/yellow/green per-metric strip, a kinematic-sequence graph, and the top 1-3 ranked faults with drills — the coaching product.

**New files / classes.**
- `src/Analysis/feedback/fault_ranker.{h,cpp}` — `FaultRanker`: rule table keyed on (metric, phase, sign), ranked by points-lost (weight × deviation).
- `src/Analysis/score/dtw_similarity.{h,cpp}` — phase-anchored, body-scale-normalized, Sakoe-Chiba-banded multivariate DTW shape sub-score against a stored reference swing (`CoachAnalyzer` especially: over-the-top/casting).
- Reference-swing storage under the athlete library (`athleteLibraryPath`); a "set as reference" action in the shot detail UI.

**Changes.** `SwingScorer` adds the DTW term + per-region/per-phase aggregation; `swing.json`'s `"analysis"` object carries the full `ScoreBreakdown` + faults. Detail UI (QML) reads `analysisDetail`.

**New dependency.** **DTW_cpp** (MIT, single header) dropped into `src/Analysis/score/` — add the Sakoe-Chiba band ourselves; no CMake change.

**Exit criteria.** Overall score correlates with a coach's manual ranking on ≥10 recorded swings (target Cohen's κ ≈ 0.7); a deliberate over-the-top swing surfaces the matching ranked fault; geometric-mean aggregation demonstrably drags one severe fault down.

### M6 — Polish, performance, cross-platform validation

**Goal / outcome.** Productionized: latency-budgeted, degradation-tier-complete, cross-platform GPU-verified.

**Work.** `IntraOpNumThreads(1)` on all analyzer ORT sessions to avoid contending with the exporter's x264 encode; per-`analyze()` session construction confirmed; latency profiling (target ≈2-4 s GPU / ≤10 s CPU on a 5 s window). Full degradation-tier matrix exercised. UI prompts for recalibration when reprojection RMS spikes (calibration drift).

**Exit criteria.** All four session types run end-to-end on macOS (CoreML), Linux (CUDA/CPU), Windows (DirectML/CPU); shot always lands on carousel even on `ok=false`.

---

### New-dependencies summary

| Dependency | Phase | Purpose | License | Platform / acquisition |
|---|---|---|---|---|
| Eigen 3.4 | M0 | DLT SVD, Kalman/RTS, Kabsch, quaternion math | MPL-2.0 | **Already fetched** (`${eigen_SOURCE_DIR}`) — no change |
| OpenCV calib3d/objdetect | M2 | ChArUco, calibrate/solvePnP, undistort/triangulate | Apache-2.0 | **Already linked** |
| **ViTPose-L/H ONNX** | M1 | Offline 2D body keypoints — accuracy-first (analyzer is offline) | Apache-2.0 | `PoseEstimatorViTPose` already wired (ViTPose-B); swap to the larger variant via the existing `HAVE_VITPOSE` download/copy block |
| ViTPose-wholebody hands | M1 (optional) | Lead-hand 133-kpt camera cross-check | Apache-2.0 | **Same** ViTPose model — expose discarded channels 91–132; no separate model or download |
| MotionBERT ONNX | **M2** | Single-camera metric lift (`Mono3D+IMU`) | Apache-2.0 | Self-exported fixed-243-frame, GPU EP, `WITH_FUSION` |
| Ceres Solver | M4 | IK solve + extrinsic bundle adjustment | BSD-3 | `find_package(Ceres)`, CPU, `WITH_FUSION` |
| DTW_cpp | M5 | Reference-swing shape similarity | MIT | Single header into `src/Analysis/score/` |

**Explicitly rejected (licensing):** Ultralytics YOLOv8/v11 (AGPL-3.0, incompatible), GolfDB/SwingNet weights and Roboflow golf datasets (CC-BY-NC, non-commercial). MediaPipe is Apache-2.0 but adds a second runtime — ViTPose keeps everything on ONNX Runtime.

### Validation strategy

- **Offline unit tests on saved `SwingWindow` fixtures.** Capture a handful of real shots, serialize the frozen window (or replay-track + payloads) as a test fixture; assert deterministic metric/score output per phase. These run headless, no buffer/GUI.
- **Reprojection error gates.** Calibration RMS < 0.5 px; static ChArUco corner < 1 px; smoothed-skeleton reprojection < 3 px in both views. Wired into the wizard status column and as test assertions.
- **Known-geometry rigs.** Wand of measured length triangulated within 0.5%; a printed protractor / known elbow angle on a held pose validates angle extraction sign and magnitude.
- **Tour-range regression.** Assert X-factor, X-factor-stretch, peak segment velocities, tempo ratio fall inside the published literature bands on labelled pro reference clips; catches sign/sequence-convention bugs immediately.
- **Coach-agreement validation (M5).** Score ≥10 recorded swings against a coach's ranking; target κ ≈ 0.7 (DMSM) and reproduce SPI-style pro-vs-amateur separation (AUC ≈ 0.97) on the rotational-velocity metrics.
- **Cross-platform smoke (M6).** Each session type end-to-end on all three GPU paths.

### Risks & open questions

1. **Self-occlusion (worst single-camera).** A lone face-on camera cannot see trail-side limbs through the body (and a DTL-only view hides the lead side); with two cameras each fills the other. Mitigated by IMU segment-fill + per-joint gating + freeze-on-low-confidence (mirror `BodyPoseAdapter` policy) — but joints with no IMU and single-view occlusion stay unreliable. Surface per-joint confidence to the UI rather than fabricate.
2. **IMU count & placement.** Current wiring supports 3 A/B/C slots; there is **no club/shaft IMU**, so clubhead speed and face angle are wrist-IMU/vision proxies (~12% error from shaft flex). **Resolution:** M3 adds a clean pelvis-vs-thorax pair (the real unlock for `xFactor`/`pelvisRotation`) and a `SegmentRole::Club` shaft-IMU hook — both *instrumentation* upgrades, orthogonal to (and cheaper than) a second camera. The single-camera owner reaches club + clean-rotation metrics by adding a *sensor*, not a *camera*.
3. **Model latency on a 5 s window.** ~2 detectors × 1–2 cameras × ~300 frames per shot. `IntraOpNumThreads(1)` plus the exporter contend for GPU. Budget ≈2-4 s GPU; if exceeded, frame-decimate the pose pass in slow phases or cap detector resolution.
4. **Calibration drift.** Extrinsics silently corrupt 3D if a camera moves after calibration. Gate the 3D path on `cameraFixedInPlace`, monitor reprojection RMS at runtime, and prompt recalibration on spikes.
5. **Cross-platform GPU.** ONNX has no Vulkan EP — a Vulkan-only Linux box (AMD/Intel, no CUDA) falls back to CPU for the analyzer. Verify the CPU latency budget there; never trust `#ifdef WITH_CUDA` for the runtime decision.
6. **Temporal sync.** Software-timestamp-only; 1 ms skew = 45 mm clubhead error. The IMU impact instant is the gold-standard common clock; sub-frame camera offset must be estimated (M3 angular-velocity cross-correlation) before any club/ball metric is trusted.
7. **No force-plate for GRF.** `GrfAnalyzer` estimates ground reaction kinematically and must label outputs as estimates until a force source is added.

---

## Appendix A — Codebase integration map

_Verified facts gathered by reading the current source (commit `1d6ac77`). These ground every design decision above; consult them before implementing a phase._

### SwingWindow / EventBuffer data model — the shot analyzer's frozen-window INPUT contract

The analyzer's sole input is a `pinpoint::SwingWindow` (frozen, zero-copy, read-only view of a Paused EventBuffer) plus a value-type `ShotAnalysisJob` resolved on the UI thread. It is handed to `ShotAnalyzer::analyze(const SwingWindow&, const ShotAnalysisJob&)` on a QtConcurrent worker (src/Gui/shot_processor.cpp:244). The window is valid ONLY while the buffer stays Paused; ShotProcessor owns its lifetime and guarantees both workers join before it is destroyed (finishShot/finishNowBlocking), so all reads over ring memory are stable for analyze()'s duration.

READ MODEL. A window is an ordered `std::vector<IndexEntry>` (src/Buffer/types.h:56) covering [start_us, end_us], sorted ascending by timestamp_us across ALL sources combined (TimelineIndex::snapshot, timeline_index.cpp:111). Each IndexEntry = {int64 timestamp_us, SourceId source_id, uint64 source_sequence, uint64 global_sequence, uint32 flags}. To enumerate sources: iterate `window.entries()`, dedup `e.source_id`, and classify via `window.formatOf(id)` — `FormatDescriptor.device` (DeviceKind) tells Camera_* vs IMU_* vs Marker_App, and `std::holds_alternative<ImuFormat>/<CameraFormat>(fd.format)` discriminates the variant. There is NO source-list accessor; the entry list IS the enumeration (this is exactly how startAnalysis classifies sources, shot_processor.cpp:230-240). Per-source entries in timestamp order: `window.entriesFor(SourceId)` (preserves global ascending order, filtered to that source). Payload bytes: `window.payloadOf(const IndexEntry&)` → `SourceRing::ReadHandle{const std::byte* data, size_t bytes, int64 timestamp_us, uint64 generation_snapshot}`; data==nullptr if the slot was overwritten/in-flight (caller MUST skip these). Format: `window.formatOf(id)` → const FormatDescriptor&.

TIMESTAMP MODEL. Single unified timeline: int64 MICROSECONDS in the `steady_clock` domain (EventBuffer::nowMicros, event_buffer.cpp:313). Camera frames (publishFrameToBuffer) and IMU samples are BOTH stamped with nowMicros() at write time (camera_instance.cpp:1485, imu_instance.cpp:280) — software-timestamp sync (SyncSource::SoftwareTimestamp); there is no hardware PTS/trigger. Per-source strict monotonicity is ENFORCED by the merger (MergerState::enforceMonotonicity clamps ts<=last to last+1, event_buffer.cpp:83-89). Cross-source alignment is timestamp-only: the merger reorders within a `reorder_window_us` and the global vector is timestamp-sorted, but there is NO frame-accurate cross-device sync beyond shared steady_clock µs. To align an IMU sample to a camera frame's instant, use `window.interpolateImu(imu_id, target_us, out, sizeof(ImuSample))` (slerps quaternion shortest-arc, lerps accel/gyro; returns false if target_us is outside the [prev,next] bracket for that source).

VIDEO PAYLOAD. CameraFormat carries pixel_format (PixelFormat enum), width, height, fps, max/typical_payload_bytes, plane_strides[4]. Bytes are stored plane-contiguous (plane0 then plane1...) in one slot. Formats actually produced today: NV12 + YUV420P (webcams), BGRA32 (BGR8 industrial), and 8-bit Bayer RG/BG/GR/GB (GenICam). Bayer12/16, MJPEG, H264_NAL are enum-defined but NOT yet exported/decoded. Canonical decode (swing_exporter.cpp:268-294): rawRows = height * rowsNum/rowsDen (3/2 for NV12/I420), stride = plane_strides[0] ? : width*bpp, then zero-copy `cv::Mat(rawRows, width, CV_8UC1, handle.data, stride)` + cvtColor.

IMU PAYLOAD. Fixed 40-byte `pinpoint::ImuSample` (src/Buffer/imu_sample.h): 10 floats — accel_x/y/z (g), gyro_x/y/z (°/s), quat_w/x/y/z (unit quaternion). Already remapped to display/world frame at write time; rotation is quaternion-only. Read alignment-safely via memcpy into a local ImuSample (swing_exporter.cpp:392). schema "imu_sample_v1".

SHOT MARKER. `ShotController::ShotMarker` (src/Gui/shot_controller.h:47), exactly 16 bytes: {uint32 version=1, uint16 source(=ShotController::Source Manual/Imu/Pose/Ball), int16 session_type(SessionController::Type, -1=none), int64 impact_ts_us}. Source = DeviceKind::Marker_App, schema "shot_marker_v1". CRITICAL: the IndexEntry.timestamp_us of the marker entry EQUALS impact_ts_us (shot_controller.cpp:130) — so `window.entriesFor(markerId).front().timestamp_us` locates impact WITHOUT parsing the payload. The job also carries impactUs directly (ShotAnalysisJob.impactUs) and markerSourceId.

**Integration points**

- ShotProcessor::captureWindowAndLaunch (src/Gui/shot_processor.cpp:167) is the producer: pauses the buffer, calls captureSwingWindow(5s), builds replay tracks, then launches analysis ∥ export. The new analyzer is invoked from startAnalysis (shot_processor.cpp:215-249) via QtConcurrent::run capturing the SwingWindow* and the job.
- ShotAnalysisJob (src/Analysis/shot_analyzer.h:34) is the resolved UI-thread context the analyzer receives alongside the window: sessionType, shotSource(int), impactUs, cameraSources (face-on first), imuSources, markerSourceId. Extend THIS struct for any new UI-thread-resolved inputs (e.g. athlete handedness, club).
- ShotAnalysisResult (shot_analyzer.h:47) is consumed by ShotProcessor::maybeJoin → ShotListModel::addShot (shot_processor.cpp:411): score(int 0-100), metrics(QVariantMap of label/value), tracePoints(QVariantList of QPointF). The 0-100 swing score is r.score; richer biomechanics would extend metrics/tracePoints or a new result field.
- SwingExporter::run (src/Export/swing_exporter.cpp) is the parallel consumer of the SAME frozen window and the canonical reference for reading frames (zero-copy cv::Mat wrap + demosaic, lines 268-294) and IMU streams (lines 388-398). The analyzer should mirror these read loops. swing.json it writes (pixelFormat/width/height/demosaic + imu_sample_v1 streams) is the on-disk twin of the window contract.
- BodyPoseAdapter (src/Gui/body_pose_adapter.*) already converts 17 COCO keypoints → per-bone quaternions for live viz; the per-frame pose for biomechanics is NOT in the EventBuffer (no pose source registered) — it comes from CameraInstance pose estimation on live frames, so an offline analyzer would need to re-run pose on decoded frames or have a pose source added.
- CameraInstance::publishFrameToBuffer (src/Gui/camera_instance.cpp:1459) and the imu_instance.cpp:264-282 quaternionUpdated handler are the WRITE sides — they define the exact byte layout (multi-plane contiguous frames; remapped 40-byte ImuSample) the analyzer reads back.

**Constraints to respect**

- Window is valid ONLY while the buffer is Paused. SwingWindow ctor sets EventBuffer::swing_window_live_=true, dtor clears it (swing_window.cpp:38,43). resumeBuffer() and deregisterSource() are hard-blocked while a window is live (swingWindowLive() backstop). The analyzer MUST NOT outlive analyze() return — ShotProcessor joins both workers (finishNowBlocking) before destroying the window.
- analyze() runs on a QtConcurrent WORKER thread, not the UI thread. The job is a value type resolved on the UI thread (cameraSources/imuSources/markerSourceId/impactUs already computed). Do not touch QObjects/Qt GUI from analyze(); read only the const SwingWindow + job.
- payloadOf() can return data==nullptr for any entry (slot overwritten by ring wrap, or gen=odd mid-write). Every read loop MUST null-check handle.data and skip — the exporter does exactly this (swing_exporter.cpp:277, 390). Also check handle.bytes >= expected size before reinterpreting.
- Read IMU samples alignment-safely: memcpy handle.data into a local ImuSample (swing_exporter.cpp:392) — do NOT reinterpret_cast the ring pointer directly except inside the buffer's own interpolateImu (which controls alignment). The ring payload has no guaranteed alignment for float access.
- Cross-source sync is software-timestamp only (steady_clock µs); there is no hardware trigger/PTS. Frames from different cameras are NOT guaranteed sample-locked — align by timestamp_us. Per-source monotonicity is guaranteed (merger clamps), but two sources can share or interleave timestamps freely.
- Video bytes are plane-contiguous in a single slot (plane0, then plane1...). NV12/YUV420P payloads are 3/2 image-height rows; you must reconstruct planes using width/height/plane_strides[0] from CameraFormat. Bayer is single-plane 8-bit needing demosaic (pattern from PixelFormat). Use plane_strides[0] if non-zero else width*bpp.
- Only NV12, YUV420P, BGRA32, and 8-bit Bayer (RG/BG/GR/GB) are actually produced & decodable today. PixelFormat also enumerates Mono8/12/16, Bayer12/16, YUV422/YUYV/UYVY, RGB24/BGR24/RGBA32, MJPEG, H264_NAL — but these are NOT exercised by the current capture pipeline; an analyzer must handle PixelFormat::Unknown / unsupported gracefully.
- Max 16 sources total (EventBuffer::MAX_SOURCES). Ring is 5 s deep per source (window_duration=5000ms); the captured window can be shorter than 5 s if capture started recently or Stop truncated the follow-through.
- The marker source's entry timestamp equals impact_ts_us — but a window could contain the marker with NO surrounding camera/IMU data near impact if those sources stalled; do not assume frames/samples exist exactly at impactUs (bracket and interpolate, handle false from interpolateImu).

**Gaps to fill**

- NO per-frame 2D/3D pose in the buffer. There is no registered pose/keypoint source — COCO keypoints exist only on the live CameraInstance (BodyPoseAdapter consumes them in real time) and are CLEARED on replay. An offline biomechanics analyzer must re-run pose estimation on decoded frames itself, or a new EventBuffer pose source must be added.
- NO ball/club tracking data in the buffer. CameraInstance::ballPresentChanged is signal-only (feeds overlays) and never written to the EventBuffer (per CLAUDE.md 'Ball detection is signal-only'). Club is a hardcoded 'DRIVER' stub. No ball position/velocity, launch, or clubhead-speed data exists as analyzer input.
- NO camera intrinsics / extrinsics / multi-camera calibration in the window. FormatDescriptor carries only pixel_format/width/height/strides — no focal length, principal point, distortion, or stereo/relative pose. 3D triangulation from multi-camera 2D would require calibration data sourced elsewhere (not in SwingWindow).
- NO frame-accurate cross-device hardware sync. Alignment is steady_clock-µs software timestamps only (SyncSource::HardwarePts/HardwareTrigger are placeholder enums, never used). Sub-frame multi-camera 3D reconstruction is limited by software-timestamp jitter and independent camera shutters.
- interpolateImu handles ONE imu_id at a time and only between samples that exist in-window; it has no extrapolation past the first/last sample (returns false). There is no equivalent frame-interpolation helper for video, and no helper to resample multiple sources onto a common time grid — the analyzer must build that itself.
- Unsupported pixel formats in the analyzer path: MJPEG, H264_NAL, and 12/16-bit Bayer are not decoded by the existing demosaic table (swing_exporter.cpp:62) and would arrive as raw bytes the analyzer cannot interpret. Mono8/16, YUV422/YUYV/UYVY, RGB24 are enum members with no current producer/decoder either.
- ShotAnalysisResult is intentionally thin (score + flat QVariantMap metrics + a single QVariantList trace) to match the existing ShotListModel/addShot UI. A full biomechanics output (per-joint angle time series, kinematic sequence, 3D trajectories) has no carrier field yet — the result struct (or a sidecar artifact like swing.json) needs extending.
- No IMU mounting/anatomical calibration or sensor-to-segment transform is delivered in the window. ImuSample is remapped to a generic display/world frame at write time, but the device→body-segment alignment (anatCalibrated state mentioned in app memory) lives outside the buffer and is not part of this input contract.
- The window's start/end bounds are the snapshot REQUEST window, not actual data extent; the analyzer must derive true temporal coverage from entries() (a source may have zero entries, or coverage shorter than 5 s if Stop truncated the follow-through). impactUs may sit near the end of coverage with little post-impact data.

### Current ShotAnalyzer pipeline + output data model (src/Analysis, src/Gui/shot_processor, SwingExporter, ShotListModel, swing.json)

The "shot analyzer" today is a stub scaffold, not a real biomechanics engine. The architecture is fully wired but every analyzer is a placeholder returning deterministic fake data.

PIPELINE (ShotProcessor, src/Gui/shot_processor.cpp): On ShotController::shotDetected → POSTROLL (500ms, buffer keeps capturing) → pauseBuffer + captureSwingWindow(5s) → PROCESSING: two QtConcurrent::run workers launched over the SAME frozen const SwingWindow (zero-copy, producers stopped while Paused) — startAnalysis() (line 215) runs makeShotAnalyzer(sessionType)->analyze(window, job); startSwingSave() (line 251) runs SwingExporter::run(window, job). Both watched by QFutureWatcher; maybeJoin() (line 395) waits for BOTH outcomes, then ALWAYS calls ShotListModel::addShot() (degrading to hasVideo=false/score 0 on failure), then replays at ¼× only if analysis AND export both succeeded. finishShot() destroys the window and resumes the buffer.

EXTENSION POINT (the place a real analyzer plugs in): the single abstract method `ShotAnalyzer::analyze(const SwingWindow&, const ShotAnalysisJob&) -> ShotAnalysisResult` and the factory `makeShotAnalyzer(int sessionType)`. Replace the four stub classes (SwingStubAnalyzer/WristStubAnalyzer/GrfStubAnalyzer/CoachStubAnalyzer in shot_analyzer.cpp) with real implementations. The worker lambda (shot_processor.cpp:244-248) constructs a fresh analyzer per shot inside the worker thread, so a real analyzer can be heavy/stateful per-call. Inputs available to analyze(): the frozen SwingWindow (all camera frames + IMU samples + shot marker, by SourceId) and a value-type ShotAnalysisJob (sessionType, shotSource, impactUs, face-on-first cameraSources, imuSources, markerSourceId).

CURRENT STUB OUTPUT: score = 40 + (impactUs/1000)%56 (range 40–95); tracePoints = 13-point deterministic pseudo-random normalised (0..1) QPointF curve; metrics = {} for Swing/GRF/Coach, hardcoded 4-key wrist mock for Wrist. ok=true always.

CRITICAL GAP for 3D biomechanics: pose/skeleton data is NOT in the buffer. The SwingWindow contains ONLY raw camera frames (Bayer/YUV/BGR), 40-byte ImuSample records (accel/gyro/quat), and the 16-byte shot marker. Pose keypoints (17 COCO, PoseResult) live ONLY as a live 60Hz QVariantList signal on CameraInstance for QML overlays — never written to EventBuffer, never frozen, never reachable from analyze(). A real analyzer must run pose estimation itself on the frozen camera frames inside analyze(), or a new pose-into-buffer source must be added. There is also NO place to persist 3D skeleton / joint-angle / metric traces: ShotAnalysisResult carries only score+QVariantMap metrics+normalised QPointF tracePoints (all in-memory, lost on app exit — never written to swing.json), and SwingExporter writes only video+IMU+thumbnail. swing.json has no analysis/score/metrics/skeleton block at all.

**Integration points**

- ShotController::triggerShot -> shotDetected(Source, qint64 timestampUs, int sessionType) -> ShotProcessor::onShotDetected (the entry trigger; impact instant is on the EventBuffer steady_clock-us timeline)
- Shot marker: ShotController writes a 16-byte ShotMarker {u32 version, u16 source, i16 session_type, i64 impact_ts_us} into the buffer with IndexEntry.timestamp_us == impact instant (shot_controller.cpp:130). ShotAnalysisJob.markerSourceId points at it so analyze() can locate impact via entriesFor(markerSourceId) without parsing payload.
- ShotProcessor::startAnalysis builds the job: face-on cameras inserted at front of cameraSources (perspective()==CameraInstance::FaceOn); imuSources/markerSourceId discovered by scanning window.formatOf() per distinct source (DeviceKind::Marker_App vs std::holds_alternative<ImuFormat>).
- maybeJoin -> ShotListModel::addShot is the sole path from analyzer output into the QML carousel (PpShotCarousel/PpShotCard read score/tracePoints/metrics/thumbnailSource roles). Wrist metric keys consumed by ScreenWrist.qml:120 metricKeys:["wristAngleTop","impactConditions","trailWristExtension","transition"] — the WristStubAnalyzer's metrics map keys must match these.
- SwingExporter::run runs in parallel and independently writes swing.json + MP4s + thumb.jpg; its SwingExportResult.thumbnailPath becomes the shot's thumbnailSource QUrl. Analyzer and exporter currently DO NOT share results — the analyzer's score/metrics are never written into swing.json.
- Per-session-type split is via makeShotAnalyzer(sessionType) only: Swing 0 / Wrist 1 / GRF 2 / Coach 3 (matching SessionController::Type). Each maps to its own analyzer class — the natural seam for distinct biomechanics pipelines per session type.

**Constraints to respect**

- analyze() runs on a QtConcurrent worker thread while EventBuffer is Paused — it may ONLY read the frozen const SwingWindow (zero-copy) and the value-type job. It must NOT touch AppSettings, controllers, or any UI-thread object (same rule as SwingExportJob). Anything from settings/controllers must be pre-resolved into ShotAnalysisJob on the UI thread (currently the job carries almost nothing — no athlete/handedness/calibration data is forwarded).
- The SwingWindow is destroyed ONLY in finishShot()/finishNowBlocking(), strictly after BOTH workers return. A real analyzer must finish reading window memory before returning — no deferred/async reads holding the SwingWindow pointer past analyze()'s return.
- Analysis and export read the SAME frozen window concurrently as pure readers — a real analyzer must remain a const reader (no mutation of ring memory).
- ShotAnalysisResult shapes are constrained to mirror ShotListModel roles: score is an int 0-100, metrics is QVariantMap of key->{label:QString, value:QString} (display strings, not numbers), tracePoints is a normalised (0..1) QPointF QVariantList. There is no numeric/structured metric channel — values are pre-formatted strings.
- makeShotAnalyzer is called fresh inside the worker lambda per shot (shot_processor.cpp:246) — the analyzer object is short-lived and per-call; any model/weights must be loaded on each analyze() unless the factory is changed to cache.
- The shot ALWAYS lands on the carousel even when analysis fails (maybeJoin degrades to score 0 / empty metrics / empty trace). A real analyzer signalling ok=false suppresses replay but never blocks the shot row.
- Only 8-bit pixel formats are demosaicable by the existing path (BayerRG/BG/GR/GB8, Mono8, BGR24, YUYV/UYVY, NV12, YUV420P). MJPEG, H264_NAL, and 12/16-bit Bayer are unsupported — a real analyzer reading camera frames inherits the same format limitation unless it adds decoders.

**Gaps to fill**

- NO 3D biomechanics anywhere. Every analyzer is a stub returning fake deterministic data (shot_analyzer.cpp). The real per-type analysis pipelines are explicitly flagged as future work in the header comment.
- Pose/skeleton data is NOT available to analyze(). 17 COCO keypoints (PoseResult, pose_estimator_base.h) exist only as a live 60Hz QVariantList signal on CameraInstance for QML overlays — they are never written to the EventBuffer and therefore never appear in the frozen SwingWindow. A real analyzer must either (a) re-run pose estimation on the frozen camera frames inside analyze(), or (b) a new pose-result EventBuffer source must be added so keypoints get frozen alongside frames. No multi-view triangulation / 3D lift exists.
- ShotAnalysisResult is in-memory only — it is NEVER serialised. swing.json (schema pinpoint.swing/1) contains schema/swing/athlete/session/clock/window/streams(+thumbnail) but NO score, metrics, skeleton, joint-angles, or analysis block. Re-opening a saved swing loses all analysis; the carousel score/metrics vanish on app restart. A real analyzer needs an analysis.json (or an additive swing.json block) plus a writer — neither exists.
- ShotAnalysisJob is thin: it carries sessionType/shotSource/impactUs/cameraSources/imuSources/markerSourceId but NO athlete handedness, NO IMU mount/anatomical calibration, NO camera intrinsics/extrinsics/calibration, NO club selection (club is hardcoded "DRIVER" in maybeJoin). Real biomechanics needs camera calibration (for 3D from 2D), IMU-to-segment calibration, and athlete anthropometry — none are plumbed into the job.
- metrics channel is display-strings only (QVariantMap key->{label,value} as formatted QString). There is no numeric metric model, no units, no per-metric confidence — a real analyzer producing e.g. clubhead speed / attack angle would have to stringify and lose machine-readable values.
- tracePoints is a single normalised 2D curve (0..1 QPointF). There is no schema for which signal it represents, no multi-trace support, no time axis in real units — insufficient to carry a real biomechanical time series.
- No scoring model. The 0-100 score is score=40+(impactUs/1000)%56 — a timestamp hash. There is no rubric, no per-metric contribution, no session-type-specific scoring logic.
- SwingWindow exposes interpolateImu() and per-source frame/imu counts but no higher-level helpers (e.g. nearest-entry-to-timestamp across sources, multi-camera time alignment) — a real analyzer must build its own time-alignment over raw IndexEntry timestamps.
- Only one IMU schema (imu_sample_v1, 40-byte accel/gyro/quat) and one camera frame format are decodable; GRF (ground reaction force) session type has no force-plate source in the buffer at all — the GrfStubAnalyzer has no real input to analyze.

### Pose / detection stack (src/Pose, src/Gui/body_pose_adapter, BallDetector) — grounding the shot analyzer

The live detection stack is monocular 2D only. The ONLY estimator actually wired in is PoseEstimatorMoveNet (single-person COCO-17, 2D normalized keypoints + per-keypoint score), running per-frame on a dedicated QThread inside CameraInstance, throttled to ~half camera rate, gated by poseEnabled. Output is published frame-by-frame as CameraInstance.poseKeypoints (QVariantList of {x,y,score}) and consumed live by BodyPoseAdapter, which converts COCO-17 into 10 per-bone parent-local quaternions for a Y-bot 3D viz (cosmetic, image-plane only). PoseEstimatorMediaPipe (BlazePose, 33 landmarks w/ z) and PoseEstimatorViTPose (COCO-WholeBody 133 incl. face/hands/feet) are COMPILED but NEVER instantiated — they are the intended upgrade path. BallDetector is classical OpenCV (Hough + blob on a white HSV mask) restricted to a user ROI, emitting one normalized circle per frame; signal-only, never persisted. CRITICAL GAP for a real shot analyzer: pose/ball results are live-only and NOT stored in the EventBuffer/SwingWindow — ShotAnalyzer::analyze() receives only the frozen raw video+IMU window and has ZERO access to keypoints. There is no club detection, no hand/finger keypoints, no 3D lifting, no multi-camera triangulation, no temporal pose buffering, and MoveNet's plain (distorting) resize loses aspect ratio.\n\nFor the new analyzer you must re-run pose offline over the frozen window (per-source frames in SwingWindow), and either add ViTPose for hands/feet or build a club/wrist model; nothing in the current pipeline gives you 3D biomechanics out of the box.

**Integration points**

- BodyPoseAdapter consumes the pose contract: any QObject exposing poseKeypoints (QVariantList of {x,y,score}) + poseKeypointsChanged(). An offline analyzer could expose the same shape to reuse the COCO-17→quaternion math (src/Gui/body_pose_adapter.cpp:104-135 parseKps/zRot) for a 3D viz of the analyzed swing.
- CameraInstance wires preprocessor→FrameThrottle→{PoseEstimatorMoveNet, BallDetector}, all on dedicated QThreads (camera_instance.cpp:306-371). An offline analyzer can reuse PoseEstimatorBase subclasses directly on frames decoded from SwingWindow without the throttle.
- ShotAnalyzer::analyze(const SwingWindow&, const ShotAnalysisJob&) (src/Analysis/shot_analyzer.h:60) is the funnel the new analyzer implements; ShotAnalysisResult{score, metrics(QVariantMap), tracePoints(QVariantList)} is the output the ShotListModel already consumes — biomechanics metrics should be packed into metrics/tracePoints.
- PersonSegmenter::segment(bgr)->CV_32F mask (src/Pose/person_segmenter.h) is available to clean frames before offline pose (background golfers/clutter).
- mirroredSource convention (per-camera AppSettings camera/isMirrored, default webcam=true) governs left/right handedness in zRot — any new analyzer must honor it for correct golfer side.
- PoseEstimatorViTPose (133-kpt incl. hands/feet) and PoseEstimatorMediaPipe (33-kpt incl. z) are already in the build (HAVE_VITPOSE/HAVE_MEDIAPIPE) and subclass PoseEstimatorBase — drop-in for the analyzer with no new model-loading code needed.

**Constraints to respect**

- Monocular 2D only: MoveNet outputs normalized (x,y,score) in the image plane. No depth, no z. BodyPoseAdapter's zRot produces image-plane (Z-axis) rotation ONLY — the 3D body viz is cosmetic, not a real 3D pose.
- Single person: MoveNet is single-pose; no multi-person handling. PersonSegmenter exists (src/Pose/person_segmenter.h, HAVE_SEGMENTER) to isolate the subject pre-pose but is NOT wired into CameraInstance.
- COCO-17 only via the live path: no wrists-beyond-the-wrist-joint, no fingers, no feet detail, no club. Hands end at PoseJoint::L/RWrist (9/10); ankles at 15/16; no toe/heel.
- No club / shaft / clubhead detection anywhere in src/. BallDetector finds only the (stationary) ball, classically, inside a manual ROI — not a tracked moving object across frames.
- Pose & ball are LIVE-ONLY and ephemeral: results emit per-frame to QML overlays; nothing writes keypoints or ball positions into the EventBuffer. SwingWindow / ShotAnalyzer cannot read them. The analyzer must re-run pose offline over the frozen window.
- Aspect-ratio loss: MoveNet uses plain cv::resize to a square (192/256) with NO letterbox/padding (camera_instance preprocessor delivers full-res; pose_estimator_movenet.cpp:219). Non-square frames are distorted, biasing keypoint geometry. ViTPose (256x192) and BlazePose use proper aspect handling but are unused.
- Throttled rate: FrameThrottle skipFactor=2 → pose runs at ~half camera fps, and only on the freshest frame (drops intermediate frames). Not every captured frame is posed; temporal resolution for fast swing phases is limited live.
- Confidence gating: BodyPoseAdapter requires both hips+both shoulders >= 0.30 to be active and freezes at last pose otherwise — a golfer addressing/at-impact with occluded joints yields stale geometry.
- ORT EP is per-process best-effort (CoreML/CUDA/DirectML/CPU); IntraOpNumThreads=1. CUDA gated on /proc/driver/nvidia/version presence on Linux.

**Gaps to fill**

- No 3D biomechanics: everything is 2D image-plane. No monocular 3D lifting, no multi-view triangulation despite multi-camera capture (face-on + DTL). The two-camera setup is NOT fused — each CameraInstance poses independently; nothing triangulates the two 2D skeletons into 3D.
- No persisted pose/ball timeline: keypoints/ball are not in the EventBuffer, so the frozen SwingWindow has no pose track. The analyzer must DECODE frames from the window and RE-RUN a pose estimator offline (none of this plumbing exists yet).
- No club model at all — no shaft line, clubhead path, face angle, or impact-location detection. Essential for swing metrics; entirely missing.
- No hand/finger/grip keypoints in the live path (COCO-17 stops at wrist). Wrist hinge/cock — central to a wrist-session score — cannot be measured from MoveNet; needs ViTPose-wholebody or a dedicated hand model.
- No feet/ground keypoints from the model (feet hidden in viz). GRF/weight-shift session scoring has no kinematic ground-contact source from pose; only IMU + (future) force data.
- No temporal smoothing/tracking across the swing offline; live path uses per-frame MoveNet + a viz-only slerp. No velocity/acceleration of joints, no swing-phase segmentation (address/top/impact/follow-through).
- No camera intrinsics/extrinsics or calibration data feeding pose — normalized image coords are never converted to metric/world space, so no real angles, distances, or speeds can be derived from pose alone.
- No mapping from PoseResult/keypoints into ShotAnalysisResult — the analyzer→model contract exists but the biomechanics computation (angles, kinematic sequence, 0-100 scoring rubric) is entirely unimplemented (v1 analyzers are stubs).
- Aspect-distortion bug to fix before serious geometry: MoveNet's non-letterboxed square resize must be corrected (or switch to ViTPose) for accurate joint angles.

### Camera calibration & geometry (for metric stereo triangulation from DTL + face-on pair)

There is NO geometric camera calibration in the codebase. No intrinsics (focal length, principal point, distortion), no extrinsics (inter-camera relative pose), no checkerboard/ChArUco/AprilTag solver, no triangulation, no solvePnP/stereoCalibrate/triangulatePoints anywhere (a repo-wide search of *.cpp/*.h/*.qml/*.md/*.py/*.json for cameraMatrix|focalLength|principalPoint|distCoeffs|projectionMatrix|stereoCalibrate|triangulatePoints|solvePnP returned zero hits). What exists is only a coarse semantic/state layer:

1. "Camera calibration wizard" = a pure STUB. src/Gui/CameraCalibrationFlow.qml is a placeholder Item whose own header comment says "The real pipeline and its QML-exposed validity property don't exist yet." Its Start button is hard-disabled (enabled: false). Body text: "Capture a ChArUco target from both cameras to solve the stereo extrinsics. This step isn't available yet." It NEVER navigates and stores NOTHING. It only mirrors the shape (layoutMode/completed()/cancelled()) of the real IMU flow.

2. "is calibrated" for a camera is just a boolean toggle, NOT a geometric solve. cameraFixedInPlace (QVariantMap<cameraKey,bool> in app_settings.h:85) is set by a Settings → Cameras toggle (CamerasPanel.qml:579-630) labelled "FIXED IN PLACE" / "Camera is attached to the wall or immovable." There is no cameraCalibrated property at all — that name does not exist. cameraKey = description + "|" + (serialNumber||id) (camera_manager.cpp:662-665).

3. Perspective enum (CameraInstance::Perspective, camera_instance.h:106): None=0, DownTheLine=1, FaceOn=2, Other=3. NOTE: the task hint "DTL=?" → DTL=1, NOT 2; only FaceOn=2 was given correctly. Stored per-camera in cameraPerspective QVariantMap, restored at connect (camera_manager.cpp:681-686, only applied if p>0). This is a human label used to pick which camera drives the body-viz / which is face-on; it carries NO geometry.

4. isMirrored (CameraInstance, camera_instance.h:85; persisted in cameraIsMirrored QVariantMap, restored camera_manager.cpp:688-690). Used only to flip the sign of the x-component in BodyPoseAdapter's 2D-keypoint→bone-quaternion math (webcam mirrored=true, industrial=false). Not a calibration; a single mirror bit.

5. Time sync is nominal only. cameraSyncEnabled (bool, default true) is stored and shown in CamerasPanel.qml:1907 ("Lock frame timing across all enabled cameras") but is NEVER read by any C++ — no hardware trigger / PTP / genlock enforcement. The real cross-source time base is the EventBuffer monotonic clock: every IndexEntry carries timestamp_us (types.h:57) in EventBuffer::nowMicros() domain (event_buffer.h:185), set at frame arrival. SwingWindow::interpolateImu() (swing_window.h:71) already slerps IMU quaternions to an arbitrary target_us — i.e. per-source temporal interpolation exists, but cross-camera frames are only software-timestamped at capture (webcam mapping latency on macOS is a known multi-ms hazard per EventBuffer merger notes), so sub-frame stereo sync is NOT guaranteed.

IMU side (for contrast, this one IS real): anatomical calibration exists and works. ImuInstance exposes calibrated, anatCalibrated, mountDeviationDeg, mountGravityErrorDeg (imu_instance.h:62-79). q_anat = A * q_raw * M (imu_calibration.h). A two-phase functional wizard (ImuCalibrationFlow.qml: arm-down capture → T-pose/abduction capture, stillness-gated at 15°/s for 2000ms, slerp-averaged) solves A and refines M about the long axis, then validates with a two-part mount gate: mountDeviationDeg ≤ 15° (strap rotation) AND mountGravityErrorDeg ≤ 25° (flip/upside-down). Mount thresholds are the 15°/25° in ImuCalibrationFlow.qml:341. Nominal mounts are fixed constants nominalArmMount()=(0.5,-0.5,-0.5,-0.5) and nominalHandMount() (imu_calibration.h:71-85). IMU calibration is session-lifetime only (not persisted to AppSettings imuCalibration in the live path; QVariantMap exists but the flow stores quats on the instance). So the project already has a working "anatomical/functional calibration" pattern to copy for cameras — but the camera analogue is entirely unbuilt.

The shot analyzer that this feeds is itself a stub: makeShotAnalyzer() returns immediate placeholder score/metrics/trace (shot_analyzer.h:70-73). ShotAnalysisJob already plumbs cameraSources (face-on first) and imuSources SourceIds into the worker (shot_analyzer.h:39-41), and SwingWindow gives zero-copy frozen access to frames per camera + FormatDescriptor (resolution) per source — so the data plumbing for triangulation inputs is present; only the geometry is missing.

**Integration points**

- CameraCalibrationFlow.qml is the pre-cut UI slot for the real flow — it already mirrors ImuCalibrationFlow's host contract (layoutMode/completed()/cancelled()) and is hosted by PpCameraPanel; replacing its stub body with a real ChArUco capture+solve flow needs no host changes.
- CameraInstance is where per-camera intrinsics/extrinsics should live (it already owns perspective, isMirrored, frameWidth/Height, FormatDescriptor) and is the EventBuffer source for that camera — a CameraInstance::calibration() accessor would be the natural read point for the analyzer job.
- AppSettings is the persistence layer: add cameraIntrinsics / cameraExtrinsics QVariantMaps alongside cameraPerspective/cameraIsMirrored/cameraFixedInPlace, keyed by cameraKey, following the existing QVariantMap getter/setter/NOTIFY pattern.
- ShotAnalysisJob is where calibration must be injected: extend it with per-cameraSource intrinsics + a stereo extrinsic (or a world-frame pose per camera) resolved on the UI thread, so the worker can triangulate from the frozen frames.
- makeShotAnalyzer()/ShotAnalyzer::analyze is the consumer: the (currently stub) per-session analyzers would call OpenCV (HAVE_OPENCV is already a build flag used by Pose/Ball) to undistort + triangulate 2D pose keypoints from face-on (perspective==2) and DTL (perspective==1) cameras.
- Pose keypoints are already produced per camera (CameraInstance::poseKeypoints, MoveNet/MediaPipe in src/Pose) — these 2D keypoints are the triangulation correspondences; they're available live and the COCO-17 set is what BodyPoseAdapter consumes.
- imu_calibration.h is the reusable, quaternion-only, header-only, unit-testable pattern to imitate for a camera calibration solver (keep it pure, no UI/AppSettings deps).

**Constraints to respect**

- Hard project rule: all rotation representation must be quaternions (QQuaternion/glm::quat), never Euler angles except for last-moment UI display. Any extrinsic camera pose must be stored/composed as a quaternion + translation, not roll/pitch/yaw.
- EventBuffer producer contract: registerSource/deregisterSource require the buffer paused; the hot write path is lock-free SPSC — do NOT add mutexes. A new calibration data source (if any) must follow the camera/IMU stop-barrier pattern.
- Analysis workers (ShotAnalyzer::analyze) run on a QtConcurrent thread over a FROZEN const SwingWindow and may NOT touch AppSettings or controllers — all calibration data must be resolved on the UI thread into the value-type ShotAnalysisJob before the worker starts (same rule as SwingExportJob).
- cameraKey identity = description + '|' + (serialNumber || id) — any persisted calibration must key on this exact string to survive reconnects (camera_manager.cpp:662-665).
- AppSettings single-shared-instance rule: writes must go through the global AppSettings* passed into managers, never a local AppSettings, or QML bindings won't update.
- cameraSyncEnabled is stored but NEVER enforced in C++ — there is no hardware genlock/trigger/PTP; do not assume frame-synchronous capture across cameras.
- Most webcams report sensorWidthMm/HeightMm = 0 and no focal length; intrinsics cannot be derived from existing CameraCapabilities — they must be measured by a real calibration routine.

**Gaps to fill**

- NO camera intrinsics anywhere: no focal length (px or mm), no principal point, no distortion coefficients, no per-camera 3x3 K matrix. CameraCapabilities has sensorWidthMm/HeightMm but they are typically 0 and there is no focal length field at all.
- NO extrinsics / inter-camera relative pose: no R|t or quaternion+translation between the DTL and face-on cameras, no world origin, no per-camera pose. Metric stereo triangulation is impossible today.
- NO calibration target detector or solver: no checkerboard, no ChArUco, no AprilTag, no OpenCV calibrateCamera/stereoCalibrate/stereoRectify/solvePnP/triangulatePoints — searched repo-wide, zero hits. The 'ChArUco' word appears only in the disabled stub's body text.
- NO scale reference: nothing anchors pixel measurements to metric units (no known target square size, no baseline length, no reference object). A 0-100 swing score needing real velocities/angles in metric space has no length scale.
- NO real camera 'is calibrated' state: cameraFixedInPlace is a wall-mount boolean, not a geometric-validity flag; there is no cameraCalibrated property and no calibration-residual/RMS-reprojection-error quality metric (contrast the IMU's mountDeviationDeg/mountGravityErrorDeg which DO exist).
- NO enforced time synchronization across cameras: cameraSyncEnabled is stored but never acted on; frames are only software-timestamped at arrival (multi-ms jitter possible, esp. macOS), so sub-frame stereo correspondence in time is not guaranteed — a temporal-sync/interpolation step (like SwingWindow::interpolateImu but for cameras) is missing.
- NO persisted camera calibration in AppSettings: there is no cameraIntrinsics/cameraExtrinsics map; the existing imuCalibration QVariantMap is also effectively unused by the live IMU flow (calibration is session-lifetime), so even the persistence convention is unproven for calibration data.
- NO 2D→3D lifting / triangulation utility and NO biomechanics math: shot_analyzer is a stub; there is no code to undistort keypoints, triangulate, fit a 3D skeleton, or fuse the camera-derived 3D with IMU anatomical quaternions.
- TO ADD for metric stereo triangulation: (1) per-camera intrinsics K + distortion (run OpenCV calibrateCamera on a ChArUco capture, store keyed by cameraKey); (2) stereo extrinsics — quaternion R + translation t between face-on(2) and DTL(1), from a shared-target stereoCalibrate, with a known target square size as the SCALE reference; (3) a real CameraCalibrationFlow replacing the stub (target detection, multi-view capture, solve, reprojection-error gate analogous to IMU's mount gate, persist to AppSettings, expose a genuine cameraCalibrated flag on CameraInstance); (4) cross-camera temporal sync/interpolation so the two views correspond at impactUs; (5) inject all of the above into ShotAnalysisJob on the UI thread; (6) an OpenCV undistort+triangulatePoints step in the analyzer to lift the per-camera COCO keypoints into metric 3D, then fuse with IMU anatQuat.

### IMU subsystem as a fusion substrate (src/IMU + src/Gui/imu_instance + src/Gui/imu_manager) — grounding for the new multi-camera + multi-IMU shot analyzer

The IMU substrate delivers ~100 Hz quaternion-only orientation per device into the shared EventBuffer, ready for the frozen-window analyzer. CRITICAL CORRECTION to stale CLAUDE.md notes: orientation is NOT taken from the WT901's native/Euler output. The WT901BLE67 streams a single 20-byte 0x61 combined frame per BLE notification (accel + gyro + device-Euler, all int16 LE, no checksum); the device Euler proved non-rigid (joint axes off 15-50deg, gimbal-locks near pitch=+-90), so orientation is fused LOCALLY from raw gyro+accel via a runtime-selectable Madgwick (default) or ESKF filter in ImuBase::fuseRawImu(). The eulerToQuat() axis-mapping override (Roll->X, Yaw->Y, -Pitch->Z, gated on |pitch|>=85deg) is LEGACY/dead for this hardware and runs only on the old 0x55/0x53 serial path. See src/IMU/wt9011dcl_base.cpp:216 dispatchCombinedPacket() and src/IMU/wt9011dcl_ble.cpp:172.\n\nWRITE PATH: ImuInstance registers one EventBuffer source (DeviceKind::IMU_WitMotion, schema "imu_sample_v1", 100 Hz, 10000us interarrival, 5 s window, SoftwareTimestamp). On every quaternionUpdated it writes a fixed 40-byte pinpoint::ImuSample {accel xyz (g), gyro xyz (deg/s), quat wxyz} via a DirectConnection on the BLE thread (lock-free SPSC), gated on isCapturing(). Both accel AND gyro are axis-remapped at write time to a display/world frame: sensor X->X, sensor Z->Y, -sensor Y->Z (src/Gui/imu_instance.cpp:271-281). The stored quaternion is the RAW fused sensor-body quaternion (NOT remapped, NOT anatomical).\n\nCALIBRATION: Anatomical orientation q_anat = A * q_raw * M (imu_calibration.h) lives ONLY on the live ImuInstance QObject (m_alignA fusion-world->anat-world, m_mountM anat-body->sensor-body, exposed as anatQuat/anatCalibrated/mountDeviationDeg/mountGravityErrorDeg Q_PROPERTYs). M is a fixed strap-enforced nominal (nominalArmMount=(0.5,-0.5,-0.5,-0.5) or nominalHandMount); A is solved so q_anat=identity at the arm-down reference. Driven by ImuCalibrationFlow.qml two-pose flow (arm-down + abduction) calling setNominalCalibration()/refineMountAboutLongAxis(). Device-side CALSW zeroing (zeroToCurrentPose: CALSW 0x0008->0x0004->0x0000) is now vestigial; ORIENT=0x0001 (vertical mount) + AXIS6=0x0001 (6-axis) are still applied on every connect via initializeDevice().\n\nSEGMENT MAPPING: An IMU is bound to a body segment ONLY via appSettings.imuPlacement[deviceId] = 'A'|'B'|'C' (QVariantMap, QSettings key imu/placement), with per-session-type slot->segment labels hardcoded in ScreenSessionWizard.qml:227. Wrist: A=Wrist/forearm, B=Hand, C=Upper arm (optional). Swing/Coach: A=Thorax, B=Lumbar spine, C=T12. GRF: A=Lead thigh, B=Trail thigh, C=Lumbar spine. There is NO club/shaft IMU and no pelvis IMU is wired beyond these labels. This placement map is QML/QSettings-only -- it never reaches C++ analysis.

**Integration points**

- EventBuffer source: ImuInstance::sourceId() (pinpoint::SourceId) is the handle; the analyzer discovers IMU sources from the frozen window by testing std::holds_alternative<ImuFormat>(window.formatOf(id).format) — exactly what ShotProcessor::startAnalysis (src/Gui/shot_processor.cpp:230) and SwingExporter (src/Export/swing_exporter.cpp:371) already do.
- Analyzer entry: makeShotAnalyzer(sessionType)->analyze(const SwingWindow&, const ShotAnalysisJob&) on a QtConcurrent worker (src/Analysis/shot_analyzer.h). v1 ships stubs. The new biomechanics/score code plugs in here per session type.
- Frozen-window read: SwingWindow::entriesFor(sourceId)+payloadOf(entry) (memcpy 40-byte ImuSample) for raw walk, or interpolateImu(sourceId,target_us,...) for time-aligned (impact-relative) sampling. job.impactUs gives the impact instant to align IMU + camera + marker streams.
- Impact location: ShotController writes a 16-byte shot_marker_v1 (DeviceKind::Marker_App) with IndexEntry.timestamp_us = impact instant; job.markerSourceId + window.entriesFor(markerId) locate impact without payload parsing.
- Live anatomical transform / mount-quality gates: ImuInstance::anatQuat, anatCalibrated, mountDeviationDeg, mountGravityErrorDeg, calibrationAngleValid Q_PROPERTYs — usable for live viz (BodyVizView, ArmVizView) but must be snapshotted on the UI thread into the job if the analyzer needs them.
- Segment mapping bridge: AppSettings::imuPlacement() (QVariantMap, key imu/placement) + the ScreenSessionWizard imuRequirements table is the only deviceId->segment source; a C++ resolver on the UI thread would convert {placement[deviceId], serialNumber} into job-side segment roles.
- Per-device serial identity: SourceDescriptor.identifier / FormatDescriptor.device_serial = imuCapabilities.serialNumber (fallback device.id) — the stable key linking a window SourceId back to a placement slot and alias.
- Fusion algorithm selection: ImuManager::setOrientationFilter('Madgwick'|'ESKF') persists to AppSettings (imu/orientationFilter) and pushes to all live instances; affects the quaternion the analyzer will see in stored samples.

**Constraints to respect**

- HARD RULE (CLAUDE.md): rotations are quaternions only, never Euler in computation. ImuSample stores unit quaternion; device Euler is emitted for UI labels only. SwingWindow::interpolateImu already slerps. Any analyzer math must compose via quaternion multiply / slerp.
- Orientation source is LOCAL Madgwick/ESKF fusion of raw gyro+accel, NOT the device quaternion and NOT the device Euler. The CLAUDE.md eulerToQuat axis-mapping note is stale for the WT901BLE67 0x61 path.
- Stored quaternion in ImuSample is the RAW fused sensor-body quaternion. accel and gyro ARE display-frame remapped (X->X, Z->Y, -Y->Z) but the quaternion is NOT — a consumer mixing them must account for this asymmetry.
- EventBuffer producer contract: IMU writes are lock-free SPSC on the BLE thread via DirectConnection; deregister requires buffer paused AND the producer severed first (stop() disconnects quaternionUpdated before deregisterFromBuffer). The analyzer reads a FROZEN window only (const, zero-copy, valid only while buffer Paused).
- Fixed measurement ranges: accel +-16 g (/32768*16), gyro +-2000 deg/s (/32768*2000). Default rate 100 Hz; supported 10/20/50/100/200. WT901BLE67 reports NO magnetometer and NO temperature on the 0x61 frame; 6-axis fusion (no mag) is forced for fast swing dynamics.
- Anatomical calibration q_anat=A*q_raw*M is session-lifetime only and lives on the live ImuInstance QObject. It is destroyed when the instance is, and is never written into the EventBuffer, ShotAnalysisJob, or swing.json export.
- Segment-to-device binding is appSettings.imuPlacement (QVariantMap deviceId->'A'/'B'/'C') consumed only in QML; slot->segment labels are hardcoded per session type in ScreenSessionWizard.qml. No club/shaft or standalone pelvis IMU exists.

**Gaps to fill**

- NO segment/role reaches the analyzer. ShotAnalysisJob carries only bare imuSources (SourceIds). The deviceId->'A'/'B'/'C'->anatomical-segment mapping (appSettings.imuPlacement + ScreenSessionWizard.qml labels) is QML/QSettings-only. The analyzer cannot tell which SourceId is the lead wrist vs hand vs thorax. NEEDED: resolve placement on the UI thread and add a segment field to ShotAnalysisJob (e.g. map<SourceId, SegmentRole>).
- NO anatomical transform is persisted. q_anat = A*q_raw*M (A=alignA, M=mountM) exists only on the live ImuInstance and is never written to the ImuSample, ShotAnalysisJob, or swing.json. The frozen window contains ONLY raw fused sensor-body quaternions. The analyzer cannot compute anatomical/joint angles without A,M. NEEDED: snapshot alignA()/mountM() per source into the job (or extend the buffer schema to imu_sample_v2 / write a sidecar), so the worker can apply A*q_raw*M.
- NO joint-angle / relative-segment math exists in C++. Joint angles are relative rotations between adjacent anatomical segments (e.g. wrist flexion = forearm vs hand) — none of this is implemented. solveSegment/anatQuat are the building blocks but the inter-segment composition lives nowhere on the analysis side.
- NO clubhead-speed pathway. There is no club/shaft IMU and no derivation of clubhead speed from body-segment angular velocity. gyro (deg/s) per segment is available, but the kinematic-chain model to estimate clubhead speed is absent.
- Angular velocity available but under-used: ImuSample stores gyro (deg/s) directly; ImuInstance also derives angularVelocityDps from successive quaternions (UI/stillness gating only). The analyzer would use stored gyro, but note gyro is display-frame remapped while the stored quaternion is not — frame consistency must be handled.
- Timestamp domain: IMU samples use SyncSource::SoftwareTimestamp (BLE-thread nowMicros at packet arrival), not hardware-synced to cameras. Cross-modal (IMU<->camera) alignment at impact relies on a common software clock; sub-frame sync accuracy for biomechanics is unquantified.
- Open design question (per the task): how MANY IMUs and WHERE. Current wiring supports up to 3 slots (A/B/C) per session with fixed labels — Wrist needs forearm+hand (+optional upper arm) for wrist flexion & forearm rotation; Swing/Coach place thorax+lumbar+T12 for shoulder/hip/spine rotation and X-factor; GRF uses lead/trail thigh+lumbar. There is no pelvis-vs-thorax pairing wired for X-factor as distinct from the thorax/lumbar/T12 triple, and no per-segment count enforcement in C++.
- ImuFormat in the descriptor is fixed at 100 Hz / imu_sample_v1; there is no version bump path documented for adding anatomical fields — schema evolution (imu_sample_v2) or a per-source metadata sidecar is an open decision.

### Build, dependencies, math libs, model + data infrastructure (grounding a 3D-biomechanics shot analyzer)

PinPoint Studio is a Qt6/C++20 app built from a single root CMakeLists.txt (1801 lines) using FetchContent for nearly all heavy deps, downloaded/built at configure time. The analyzer skeleton already exists and is wired: ShotProcessor::startAnalysis() builds a value-type ShotAnalysisJob (sessionType, impactUs, cameraSources face-on-first, imuSources, markerSourceId) and runs makeShotAnalyzer(sessionType)->analyze(const SwingWindow&, job) on a QtConcurrent worker concurrently with SwingExporter::run() over the same frozen, paused EventBuffer window. Today makeShotAnalyzer() returns per-session-type STUBS (src/Analysis/shot_analyzer.cpp) producing placeholder score (40-95 derived from impactUs) + deterministic fake trace + canned metrics. The analyzer reads zero-copy from a SwingWindow: 2D-only COCO keypoints are NOT in the window (pose runs live per-frame, results are not buffered), but raw camera frames (payloadOf) and 40-byte ImuSample records (accel g, gyro deg/s, unit quaternion wxyz; interpolateImu() lerps/slerps) ARE. Math/ML inventory: Eigen 3.4.0 (FetchContent, header-only, used by vendored ESKF IMU filter) is the ONLY linear-algebra lib; rotation math elsewhere uses Qt QQuaternion/QVector3D. ONNX Runtime 1.26.0 (prebuilt, per-platform GPU binary) drives all pose models. OpenCV (system/Homebrew, REQUIRE >=3.0) and FFmpeg (pkg-config, GPL+libx264 OK since project is GPLv2+). NO glm, Ceres, g2o, Sophus, or DTW library present. CRITICAL GAP for full 3D analysis: there is no camera intrinsics/extrinsics/calibration, no solvePnP/triangulation/stereo, no multi-view fusion, and the swing.json schema (pinpoint.swing/1) stores video + IMU streams but NO 3D pose, joint angles, or metrics block.

**Integration points**

- src/Analysis/shot_analyzer.cpp — replace SwingStubAnalyzer/WristStubAnalyzer/GrfStubAnalyzer/CoachStubAnalyzer with real implementations; the factory dispatch and interface are stable.
- src/Gui/shot_processor.cpp:215-249 (startAnalysis) — already constructs ShotAnalysisJob and joins the result via m_analysisWatcher (QFutureWatcher<ShotAnalysisResult> at :89); result flows to ShotListModel::addShot(). Extend ShotAnalysisResult + the join to carry 3D pose/joint-angle traces.
- src/Export/swing_exporter.cpp:428-456 — swing.json root is additive; append a 'biomechanics'/'analysis' block (3D pose per frame, joint angles, metrics, score) alongside existing video/imu streams. Schema string 'pinpoint.swing/1' would bump or stay (readers ignore unknown keys).
- ONNX model acquisition: mirror the ViTPose block (CMakeLists.txt:1215-1270) — file(DOWNLOAD) into build/_deps/<model>/, HAVE_<X> + <X>_MODEL_FILE compile defines, POST_BUILD copy to <exe>/models/. Runtime load via the EP cascade in pose_estimator_vitpose.cpp:89-185.
- Eigen is already on the include path (eigen_SOURCE_DIR at CMakeLists.txt:482) and linked transitively via the ESKF filter — a new analyzer can #include <Eigen/Dense> immediately with no CMake change.
- Camera frames for re-running pose offline: SwingWindow::payloadOf() + formatOf() give raw bayer/NV12/YUV420P frames; the exporter's demosaic table (swing_exporter.cpp) shows how to decode them to cv::Mat (handles bayer + NV12/YUV420P; MJPEG/H264/12-16bit bayer unsupported).
- AppSettings athleteLibraryPath + imuCalibration/imuPlacement/imuMountOrientation maps (app_settings.h) — read on the UI thread, pass into the job, for per-athlete anatomical calibration and baseline/scoring reference data.

**Constraints to respect**

- GPLv2+ project (root LICENSE = GPL v2; every source header is GPLv2-or-later). Any new dependency MUST be GPLv2-compatible: MIT/BSD/Apache-2.0/MPL OK; GPLv3-only is NOT (incompatible with the GPLv2 files). Ceres(BSD-3)=OK, g2o(BSD/LGPL mix)=OK, dtaidistance/dtw-cpp(MIT)=OK, Eigen(MPL2)=already in. Bundled FFmpeg+libx264(GPL) is explicitly noted as fine because project is GPLv2+.
- All heavy deps are fetched/built at CMake CONFIGURE time via FetchContent or file(DOWNLOAD): whisper.cpp v1.7.2, libsamplerate 0.2.2, Eigen 3.4.0, ONNX Runtime 1.26.0 (1.20.1 on Intel macOS), ORT-GenAI 0.13.1, espeak-ng 1.52.0. Models via file(DOWNLOAD) from HuggingFace/GitHub into build/_deps/<name>/, then copied POST_BUILD to <exe>/models/ (or .app/Contents/Resources/models on macOS). A new analyzer ONNX model must follow this exact pattern (download+cache+copy+HAVE_X define+filename define).
- ONNX Runtime EP selection is per-platform at CMake time AND runtime: Windows picks gpu/gpu_cuda13/std zip by detected CUDAToolkit major version; Linux picks gpu vs cpu tgz by WITH_CUDA; macOS arm64 always has CoreML, Intel macOS pinned to ORT 1.20.1 CPU. Defines WITH_CUDA/WITH_COREML reach C++ only where supported; DirectML define is NOT set (NuGet discontinued after ORT 1.24 — CUDA covers Windows GPU). At runtime use the try/catch EP cascade + actual GPU probe, NEVER trust #ifdef WITH_CUDA alone (binary may be downloaded on a GPU-less machine).
- GPU story is Vulkan-PREFERRED only for whisper.cpp (GGML_VULKAN if Vulkan SDK found, else CUDA via nvcc, else CPU). ONNX Runtime does NOT use Vulkan — its EPs are CoreML/CUDA/DirectML/CPU. A new pose/biomech ONNX model inherits the ORT (CUDA/CoreML) acceleration path, NOT Vulkan.
- SwingWindow is valid ONLY while the EventBuffer is Paused; payload pointers are dangling after resume(). The analyzer worker MUST finish (joined via QFutureWatcher in ShotProcessor) before the window is destroyed in finishShot()/finishNowBlocking(). Any heavier 3D pipeline must respect this freeze window (currently a 5s trailing ring) or copy data out first.
- analyze() runs on a worker thread and is forbidden from touching AppSettings or controllers — all inputs must be pre-resolved into the value-type ShotAnalysisJob on the UI thread (same rule as SwingExportJob). New per-athlete calibration / model-selection state must be threaded through the job struct.
- Analyzer + exporter run CONCURRENTLY as two QtConcurrent readers of the same const window. Heavy ONNX inference in the analyzer will contend with the exporter's H.264 encode for CPU/GPU — EP/thread budget must be considered (ViTPose sets IntraOpNumThreads(1)).

**Gaps to fill**

- NO camera calibration whatsoever: no intrinsics (focal length/principal point/distortion), no extrinsics (per-camera pose), no stereo/multi-view geometry. cameraFixedInPlace is a plain bool flag and CameraCalibrationFlow.qml is an explicit STUB. CameraCapabilities has sensorWidthMm/sensorHeightMm fields but they are metadata, not a calibrated intrinsic matrix. True 3D triangulation from the 2 cameras (face-on + DTL) is impossible without adding a calibration step + solvePnP/triangulatePoints (OpenCV calib3d is available since OpenCV is linked, but no code uses it).
- NO 3D pose: PoseResult/PoseKeypoint are 2D normalized x/y only. There is no lifting model (2D->3D), no SMPL/SMPL-X body model, no monocular-3D ONNX model. The Y-bot BodyVizView is driven by BodyPoseAdapter doing 2D-keypoint->bone-quaternion heuristics, not metric 3D.
- Pose keypoints are NOT buffered: pose runs live per-frame and results are ephemeral (cleared during replay). The offline analyzer receives no pose in the SwingWindow — it must re-run ONNX inference on the buffered raw frames itself (which means instantiating a pose estimator on the worker thread, contending with the live pipeline and the exporter).
- NO temporal-alignment / sequence library: no DTW, no swing-phase segmentation (address/top/impact/follow-through), no per-frame event detection beyond the single impact marker (ShotMarker shot_marker_v1, 16 bytes). A reference-swing comparison or phase detector would need a new DTW dep (MIT options exist) or hand-rolled code.
- NO scoring/metrics infrastructure: ShotAnalysisResult.score is an int 0-100 and metrics is an opaque QVariantMap of label/value strings — there is no biomechanics metric registry, no per-session-type metric schema, no reference baseline storage. The 0-100 score has no defined rubric.
- NO optimization library: no Ceres/g2o for bundle adjustment, IMU-camera fusion, or kinematic-chain fitting. Only Eigen (linear algebra) is present. A full multi-sensor 3D fusion (camera 2D + IMU orientation -> metric 3D skeleton) would likely want one.
- NO IMU-camera temporal/spatial cross-calibration: camera and IMU sources share the EventBuffer monotonic clock (good for time alignment) but there is no spatial registration between the IMU anatomical frame and any camera frame, so fusing the two coordinate systems is unsolved.
- Vulkan is NOT available to ONNX models — only whisper.cpp uses it. A new analyzer ONNX model gets CUDA/CoreML/DirectML/CPU only; on a Vulkan-only Linux GPU box (AMD/Intel without CUDA) the analyzer model would fall back to CPU.


---

## Appendix B — Research bibliography

_Sources consulted during the domain research phase, grouped by topic. Recommended approaches above are distilled from these._

### Golf swing biomechanics metrics: computing each from 3D joint positions and segment-orientation quaternions (for a Qt6/C++/Eigen/OpenCV/ONNX analyzer with DTL+face-on video and ~100 Hz IMU quaternions)

- Golf Swing Biomechanics: A Systematic Review and Methodological Recommendations for Kinematics (PMC9227529) — https://pmc.ncbi.nlm.nih.gov/articles/PMC9227529/ (X-factor methods, Cardan sequence recommendation, segment landmark definitions, pro value ranges, peak angular velocities)
- Phil Cheetham, 'Basic Biomechanics for Golf — Selected Topics' (2014) — https://philcheetham.com/media/Basic-Biomechanics-for-Golf-Selected-Topics-by-Phil-Cheetham-2014.pdf (the global lab-frame convention, 6DOF sway/thrust/lift + bend/side-bend/turn taxonomy, kinematic sequence figures and transition/downswing/follow-through timing)
- Cheetham et al., 'The importance of stretching the X-Factor in the downswing of golf: The X-Factor Stretch' — https://www.philcheetham.com/wp-content/uploads/2011/11/Stretching-the-X-Factor-Paper.pdf (X-factor stretch definition; ~19% vs ~13% downswing increase, top-of-backswing difference not significant)
- TPI 'Kinematic Sequence Revisited' — https://www.mytpi.com/articles/biomechanics/kinematic-sequence-revisited (pelvis->thorax->arm->club peak order, deceleration pattern, only club accelerates through impact)
- McNally et al., 'GolfDB: A Video Database for Golf Swing Sequencing' (CVPRW 2019) + SwingNet — https://ar5iv.labs.arxiv.org/html/1903.06528 (precise geometric definitions of the 8 swing events; CNN+biLSTM event detector you can run via ONNX)
- Golf Swing Segmentation from a Single IMU Using Machine Learning (PMC7472298) — https://pmc.ncbi.nlm.nih.gov/articles/PMC7472298/ (heuristic IMU signals/thresholds per phase boundary; backswing 1.16 s / downswing 0.32 s / follow-through 0.67 s / full 2.15 s)
- ISB recommendation on joint coordinate systems, Part I (ankle, hip, spine), Wu et al. 2002 — https://media.isbweb.org/images/documents/standards/Wu%20et%20al%20J%20Biomech%2038%20(2005)%20981-992.pdf (anatomical frame definitions and Cardan sequence standard for trunk/pelvis/hip)
- TrackMan Club Data Definitions — https://www.trackman.com/blog/golf/club-data-definitions and What is Swing Plane — https://www.trackman.com/blog/golf/what-is-swing-plane (definitions of club speed, path, attack/face angle, dynamic loft; SVD-style 3D swing plane vs 2D shaft plane)
- TrackMan Updated PGA/LPGA Tour Averages 2024 — https://www.trackman.com/blog/golf/introducing-updated-tour-averages (driver ~113 mph / AoA -1.3; 7-iron ~89 mph / AoA -4.5; full club table)
- HackMotion — Wrist Position at Impact / Address — https://hackmotion.com/wrist-position-at-impact-in-golf/ and https://hackmotion.com/wrist-position-at-address/ (lead-wrist flexion/extension ranges, address-to-impact deltas, clubface relationship)
- The crunch factor's role in golf-related low back pain (ScienceDirect S1529943013015593) and Cheetham/TPI side-bend values — crunch factor = lateral-bend angle x axial-rotation speed at impact; side bend driver impact thorax ~32 deg / pelvis ~10 deg
- Frontiers (2026): biomechanical characteristics of swing planes for driver and 7-iron — https://www.frontiersin.org/journals/bioengineering-and-biotechnology/articles/10.3389/fbioe.2026.1847830/full (SVD best-fit plane definition, tilt/azimuth interpretation)

### Metric 3D reconstruction from a 2-camera orthogonal (down-the-line + face-on) setup for human and small-object golf-swing motion: intrinsics, extrinsics, triangulation, scale/world frame, temporal sync, accuracy, and bundle-adjustment refinement — implementable in C++ with OpenCV / Eigen / Ceres / ONNX.

- OpenCV calib3d (calibrateCamera, stereoCalibrate CALIB_FIX_INTRINSIC, solvePnP SOLVEPNP_IPPE/SQPNP, triangulatePoints, undistortPoints): https://docs.opencv.org/4.x/d9/d0c/group__calib3d.html
- OpenCV ChArUco calibration tutorial (CharucoBoard, CharucoDetector::detectBoard, matchImagePoints): https://docs.opencv.org/4.13.0/da/d13/tutorial_aruco_calibration.html
- Lee & Civera, 'Triangulation: Why Optimize?' (DLT adequate for well-conditioned geometry; optimal needed only for ill-conditioned): https://arxiv.org/pdf/1907.11917
- Ceres Solver non-linear least squares / bundle-adjustment tutorial: http://ceres-solver.org/nnls_tutorial.html
- Ceres SnavelyReprojectionError reference functor (AutoDiffCostFunction<,2,9,3>, AngleAxisRotatePoint): https://github.com/ceres-solver/ceres-solver/blob/master/examples/snavely_reprojection_error.h
- MC-Calib (MIT) generic robust multi-camera calibration toolbox, OpenCV+Ceres+ChArUco: https://github.com/rameau-fr/MC-Calib
- Nakano et al., markerless OpenPose multi-camera 3D accuracy (47% <20mm, 80% <30mm; 5 cameras, 1080p/120Hz vs 4K/30Hz): https://pmc.ncbi.nlm.nih.gov/articles/PMC7739760/
- Pose2Sim Part 2: Accuracy (joint-angle errors 3-4 deg; robustness to 4 vs 8 cameras and 1cm calibration error): https://pmc.ncbi.nlm.nih.gov/articles/PMC9002957/
- Pose2Sim Part 1: Robustness (robust triangulation + outlier rejection, calibration-error sensitivity): https://pmc.ncbi.nlm.nih.gov/articles/PMC8512754/
- Rolling Shutter Camera Synchronization with Sub-millisecond Accuracy: https://arxiv.org/pdf/1902.11084
- Subframe-Level Synchronization in Multi-Camera System Using Time-Calibrated Video (MDPI Sensors 2024): https://pmc.ncbi.nlm.nih.gov/articles/PMC11548676/
- Video Synchronization Using Temporal Signals from Epipolar Lines (sub-frame, fundamental-matrix-based): https://link.springer.com/chapter/10.1007/978-3-642-15558-1_2
- Spatiotemporal Bundle Adjustment for Dynamic 3D Human Reconstruction (jointly solves time offset + structure): https://arxiv.org/pdf/2007.12806
- ChArUco vs checkerboard accuracy/detection comparison (100% vs 92% detection, 50% fewer images): https://www.oklab.com/blog/charuco-calibration-boards-complete-guide-to-professional-camera-calibration
- Stereo convergence-angle conditioning: ~90 deg yields best relative depth uncertainty (MIT Foundations of CV, stereo error models): https://visionbook.mit.edu/3d_scene_understanding_stereo.html

### Pose & object models for offline golf-swing analysis, deployable via ONNX Runtime in C++ (PinPoint Studio)

- https://arxiv.org/abs/2303.07399 (RTMPose: Real-Time Multi-Person Pose Estimation based on MMPose)
- https://github.com/Tau-J/rtmlib (rtmlib — RTMPose/RTMW/RTMO/DWPose, Apache-2.0, ONNX auto-download)
- https://arxiv.org/abs/2407.08634 (RTMW: Real-Time Multi-Person 2D and 3D Whole-body Pose Estimation)
- https://github.com/IDEA-Research/DWPose (DWPose two-stage distillation wholebody)
- https://github.com/open-mmlab/mmpose/tree/main/projects/rtmpose (RTMPose project: model zoo, ONNX/SDK export, SimCC decode)
- https://github.com/HW140701/RTMPose-Deploy (C++ ONNX Runtime + TensorRT RTMPose deployment example with SimCC decode)
- https://github.com/ViTAE-Transformer/ViTPose (ViTPose, Apache-2.0)
- https://github.com/Walter0807/MotionBERT (MotionBERT 3D lifter, Apache-2.0; DSTformer, 243-frame clip, H36M-17)
- https://arxiv.org/pdf/2203.00859 (MixSTE: Seq2seq Mixed Spatio-Temporal Encoder for 3D Human Pose)
- https://arxiv.org/pdf/2310.16288 (MotionAGFormer: Transformer-GCNFormer 3D pose network)
- https://saic-violet.github.io/learnable-triangulation/ (Learnable Triangulation of Human Pose — DLT vs volumetric/learned multi-view)
- https://github.com/wmcnally/golfdb (GolfDB / SwingNet — 8-event taxonomy; CC-BY-NC 4.0, reference only)
- https://arxiv.org/pdf/1903.06528 (GolfDB: A Video Database for Golf Swing Sequencing)
- https://arxiv.org/pdf/2012.09393 (Efficient Golf Ball Detection and Tracking Based on CNNs and Kalman Filter)
- https://universe.roboflow.com/club-head-tracking/golf-club-tracking (golf club tracking dataset — note Roboflow/YOLO licensing before any use)
- https://github.com/ultralytics/ultralytics/issues/6789 (Ultralytics AGPL-3.0 applies to ONNX-exported models / third-party inference — licensing hazard)

### Sparse IMU + camera 3D pose fusion; golf IMU quantities

- https://arxiv.org/abs/1703.08014
- https://ar5iv.labs.arxiv.org/html/1810.04703
- https://github.com/Xinyu-Yi/PIP
- https://ar5iv.labs.arxiv.org/html/2003.11163
- https://arxiv.org/pdf/2208.11960
- https://arxiv.org/html/2404.17837v1
- https://pmc.ncbi.nlm.nih.gov/articles/PMC7730686/
- https://pmc.ncbi.nlm.nih.gov/articles/PMC6783441/
- https://pmc.ncbi.nlm.nih.gov/articles/PMC11035581/
- https://arxiv.org/pdf/2203.17024
- https://vqf.readthedocs.io/en/stable/

### Robust reconstruction of human + golf-club motion from noisy/missing multi-camera + IMU data (occlusion, dropped/low-confidence keypoints, multi-rate sensors)

- Iskakov et al., Learnable Triangulation of Human Pose, ICCV 2019 — confidence-weighted DLT vs RANSAC+Huber, quantitative: https://arxiv.org/pdf/1905.05754 (HTML: https://ar5iv.labs.arxiv.org/html/1905.05754)
- Generalizable Human Pose Triangulation, CVPR 2022 — weighted triangulation > RANSAC on Human3.6M / Panoptic: https://arxiv.org/pdf/2110.00280
- Structural Triangulation: A Closed-Form Solution to Constrained 3D Human Pose, ECCV 2022 — Lagrangian closed-form bone-length-constrained triangulation: https://www.ecva.net/papers/eccv_2022/papers_ECCV/papers/136650685.pdf
- Ceres Solver — Non-linear Least Squares tutorial (AutoDiffCostFunction, robust losses, ScaledLoss, bundle adjustment): https://ceres-solver.readthedocs.io/latest/nnls_tutorial.html and http://ceres-solver.org/nnls_tutorial.html
- Ceres Solver robust loss reference (HuberLoss/CauchyLoss/TukeyLoss/ScaledLoss): https://github.com/ceres-solver/ceres-solver/blob/master/docs/source/nnls_tutorial.rst
- OpenSim — How Inverse Kinematics Works (weighted least-squares marker tracking, occluded-marker weighting): https://opensimconfluence.atlassian.net/wiki/spaces/OpenSim/pages/53090047/How+Inverse+Kinematics+Works
- OpenSim core (Apache-2.0 C++ libraries): https://github.com/opensim-org/opensim-core
- AUKSMIKT — Musculoskeletal Inverse Kinematics via Adaptive Unscented Kalman Smoother (UKF + RTS for mocap), Ann. Biomed. Eng. 2025: https://link.springer.com/article/10.1007/s10439-025-03807-x and https://www.ncbi.nlm.nih.gov/pmc/articles/PMC12575601/
- Akhter & Black, Pose-Conditioned Joint Angle Limits for 3D Human Pose Reconstruction, CVPR 2015 (biomechanical limit prior + public data): https://openaccess.thecvf.com/content_cvpr_2015/papers/Akhter_Pose-Conditioned_Joint_Angle_2015_CVPR_paper.pdf
- Learning Realistic Joint Space Boundaries (one-class SVM ROM, CC-BY-SA): https://arxiv.org/pdf/2311.10653
- FusePose: IMU-Vision Sensor Fusion in Kinematic Space, 2022: https://arxiv.org/pdf/2208.11960
- Hi-ROS open-source multi-camera sensor fusion for real-time people tracking, CVIU 2023: https://www.sciencedirect.com/science/article/pii/S1077314223000747
- Convolutional Autoencoders for Human Motion Infilling (long-gap learned infilling), 2020: https://arxiv.org/pdf/2010.11531
- ReMP: Reusable Motion Prior for Multi-domain 3D Human Pose Estimation and Motion Inbetweening, 2024: https://arxiv.org/pdf/2411.09435
- Learning Motion Priors for 4D Human Body Capture in 3D Scenes (occlusion-aware infilling), ICCV 2021: https://arxiv.org/pdf/2108.10399
- GolfPose: From Regular Posture to Golf Swing Posture, ICPR 2024 (golf-specific fast/occluded pose): https://minghanlee.github.io/papers/ICPR_2024_GolfPose.pdf
- Markerless 3D golf swing analysis via truncation-robust heatmaps, MIT 2024: https://dspace.mit.edu/handle/1721.1/162530

### Scoring a golf swing against an ideal/reference swing and reducing to a single transparent 0-100 score with actionable coaching feedback (C++/OpenCV/ONNX/Eigen stack with synced DTL + face-on video and ~100Hz IMU quaternions)

- Swing Performance Index (single-score index from pelvic+torso rotational velocities, PCA distance from mean pro swing, scaled pro mean 100/SD 10, AUC 0.97): https://pmc.ncbi.nlm.nih.gov/articles/PMC9816382/ and https://www.frontiersin.org/journals/sports-and-active-living/articles/10.3389/fspor.2022.986281/full
- Dynamic Golf Swing Analysis / DMSM (7 phases, body-scale normalization, trajectory-integral distance vs DTW+cosine, kappa=0.71 with coaches): https://www.mdpi.com/1424-8220/25/22/7073 and https://pmc.ncbi.nlm.nih.gov/articles/PMC12656346/
- Golf Swing Biomechanics systematic review (kinematic methodology, metric definitions): https://pmc.ncbi.nlm.nih.gov/articles/PMC9227529/
- TPI Kinematic Sequence Revisited (proximal-to-distal ordering, peak-velocity timing): https://www.mytpi.com/en/articles/biomechanics/kinematic-sequence-revisited
- Comparison of Kinematic Sequence Parameters between Amateur and Professional Golfers (sequence stats, pro vs amateur variability): https://www.researchgate.net/publication/252236426_Comparison_of_Kinematic_Sequence_Parameters_between_Amateur_and_Professional_Golfers
- Phil Cheetham — Kinematic Sequence: Signature of Efficiency and Power: http://philcheethamthe3dguy.blogspot.com/2009/11/kinematic-sequence-signature-of.html
- HackMotion target ranges (lead-wrist bands relative to address; top -30/+5, impact -15/-40): https://hackmotion.com/learning-center/understanding-hackmotion-target-ranges/ and tour patterns https://hackmotion.com/scott-cowx-wrist-patterns/
- Tour Tempo / TPI tempo benchmarks (backswing 0.847±0.111s, ~3:1 ratio): https://tourtempo.com/blogs/tips/what-the-numbers-mean
- Towards a Biomechanical Understanding of Tempo in the Golf Swing (731±21ms backswing, 258±8ms downswing, 2.8:1): https://arxiv.org/pdf/physics/0611291
- Aggregating Composite Indicators through the Geometric Mean: A Penalization Approach (why geometric mean is non-compensatory / weakest-link): https://www.mdpi.com/2079-3197/10/4/64
- Weighted geometric vs weighted arithmetic mean in AHP aggregation: https://www.researchgate.net/publication/325951246_Aggregation_in_the_analytic_hierarchy_process_Why_weighted_geometric_mean_should_be_used_instead_of_weighted_arithmetic_mean
- Early Improper Motion Detection in Golf Swings Using Wearable Motion Sensors (PCA reference deviations / fault detection): https://pmc.ncbi.nlm.nih.gov/articles/PMC3715223/
- DTW_cpp single-header N-dimensional C++ DTW (MIT): https://github.com/cjekel/DTW_cpp
- linmdtw — Sakoe-Chiba constrained / multiscale DTW reference (algorithms to mirror for the band constraint): https://github.com/ctralie/linmdtw
- Sportsbox AI 3D motion analysis (markerless ~30 keypoints, red/yellow/green vs pro benchmarks, kinematic sequence): https://www.getsgolf.com/post/sportsbox-ai-3d-motion-analysis-kinematic-ai-technology and Foresight/GCQuad fusion https://www.sportsbox.ai/press-releases/sportsbox-ai-unveils-partnership-with-foresight-sports-to-integrate-launch-monitor-data-into-its-3dgolf-app
- K-Motion / K-Vest evaluation (efficiency graph, speed creation/consistency scores, vs Tour/peer/own-best): https://www.k-motion.com/evaluation/
- GEARS 3D motion capture (34 markers, <0.2mm, compare to any tour swing — reference-swing source): https://gearssports.com/frequently-asked-questions

