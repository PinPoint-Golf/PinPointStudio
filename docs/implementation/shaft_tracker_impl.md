# ShaftTracker — Implementation Plan (face-on club detection + replay overlay)

> **Status:** **S0–S4 IMPLEMENTED** (as-built notes inline per stage) · S5 (hardware
> verification) pending · **Dates:** planned 2026-06-10, built 2026-06-10/11 ·
> **Grounded against:** `main` @ `474a6ba`
>
> Implements the *Club shaft detection (`ShaftTracker`, hand-anchored)* addendum of
> [`docs/design/shot_analyzer_design.md`](../design/shot_analyzer_design.md) (sections B.1–B.10)
> inside the shot analyzer. Scope per product decision: **face-on camera only**, and the result is
> **overlaid together with the skeleton on the replay video**. DTL, 3-D lift, clubface, and learned
> models stay deferred (addendum B.10).
>
> **As-built verification state:** full standalone suite 15/15 green (incl. the three new
> targets `frame_decode_test`, `shaft_tracker_test`, `shaft_track_test`); app build clean,
> zero new warnings; headless offscreen startup clean (all screens incl. the new overlay
> canvas instantiate). **Not yet verified:** anything needing a real captured swing —
> pixel-level overlay registration, detector thresholds on real shafts/backgrounds, and the
> >0.9 vision↔IMU θ̇ correlation acceptance — all fold into S5.

## What ships

After a shot, the analyzer re-runs pose on the frozen face-on frames, detects the club shaft per
frame anchored on the hands, smooths the track (Viterbi + KF/RTS, IMU-bridged through blur), and
the ¼× replay shows the **skeleton + shaft line + fading clubhead trail** drawn over the video,
scrubbing with the existing playhead. The track persists in `swing.json` and feeds shaft-lean-at-
impact plus the P6/P8 segmentation upgrades.

```
SwingWindow (frozen, face-on camera)
  └─ S0  FrameDecoder        shared demosaic → grey/luma cv::Mat per frame   (zero-copy wrap)
  └─ S0  PoseRunner          ViTPose offline → PoseTrack2D (17 kp + hands)   (anchor source)
  └─ S1  shaft_tracker_math  per-frame anchored radial transform → candidates
  └─ S2  ShaftTracker        ŝ_hand calib · Viterbi · KF+RTS → ShaftTrack2D
  └─ S3  WristAnalyzer       detail->pose2d/shaft · swing.json · metrics
  └─ S4  PpCameraFrame       replay overlay canvas (skeleton + shaft + trail)
```

## Contracts that bind every stage

- **Frozen-window const reader** — all of S0–S2 runs inside `analyze()` on the QtConcurrent
  worker; reads finish before return; `payloadOf().data` null-checks on every frame
  (exporter discipline, `swing_exporter.cpp`).
- **Quaternions-only** — IMU orientation stays `QQuaternion` end-to-end; the shaft's image
  angle θ is a genuine 2-D scalar (explicitly permitted; it is not a 3-D rotation intermediate).
- **Flat source dir** — new code in `src/Analysis/*.{h,cpp}`, standalone tests in
  `src/Analysis/tests/` (project layout rule).
- **No new dependencies** — core OpenCV `imgproc` only (no `ximgproc`/contrib), existing ORT EP
  cascade with `IntraOpNumThreads(1)` (exporter encode runs concurrently).
- **Live 60 Hz path untouched** — every estimator change is flag-gated; MoveNet/live ViTPose
  behaviour, throttle balance, and `PoseResult`'s 17-slot shape are unchanged
  (`BodyPoseAdapter` reads it by index).

---

## S0 — Offline frame decode + pose pass (anchor source)

**Deliverable:** `PoseTrack2D` for the face-on camera — per-frame 17 COCO keypoints + both hands'
centroids, on window timestamps. This is the analyzer's first camera-reading stage (a thin slice
of design-doc M2, built now because ShaftTracker needs anchors).

**Files**
- `src/Export/frame_decode.{h,cpp}` *(new, shared)* — extract the exporter's demosaic table +
  zero-copy `cv::Mat` wrap (`swing_exporter.cpp:268-294`) into one helper used by **both**
  `SwingExporter` and the analyzer, so the two decode paths can never diverge. Add a
  luma-only fast path: NV12/YUV420P expose the Y plane directly (no colour conversion);
  Bayer uses a cheap green-channel extraction; BGRA converts.
- `src/Analysis/pose_runner.{h,cpp}` *(new)* — owns a `PoseEstimatorViTPose`, calls the
  **synchronous** `estimatePose(const cv::Mat&)` (`pose_estimator_base.h:88`) and captures
  `poseEstimated(PoseResult)` via a direct connection on the worker. Walks
  `window.entriesFor(faceOnSource)`, decodes, runs pose, emits
  `PoseTrack2D { std::vector<PoseFrame2D> }` with
  `PoseFrame2D { int64_t t_us; std::array<QPointF,17> kp; std::array<float,17> conf;
  QPointF leadHand, trailHand; float handConf; }` (normalized 0..1 frame coords, matching
  `PoseResult`).
- `src/Pose/pose_estimator_vitpose.cpp` — decode channels 91–132 (both hands' 21-keypoint sets)
  into a **sibling** `WholeBodyHands` struct behind an opt-in flag (`setDecodeHands(true)`),
  default off so the live path is byte-identical. Per the design rule: never widen the 17-slot
  `PoseResult`.
- `src/Analysis/shot_analyzer.h` — `ShotAnalysisJob` gains `int faceOnCameraCount = 0` (today
  "face-on first" in `cameraSources` is unverifiable from the worker) and
  `double clubLengthM = 1.12` (driver default until club selection is real). Resolved in
  `ShotProcessor::startAnalysis` on the UI thread, like every other job field.

**Adaptive sampling (CPU-survivable):** pose every frame inside
`[Top − 150 ms, Impact + 300 ms]` (the blur-critical zone where anchor accuracy matters most) and
every 4th frame elsewhere, anchors linearly interpolated between pose frames for the in-between
shaft detections. GPU: ~100–300 pose calls ≈ 1–3 s, inside the design's 2–4 s budget. CPU-only:
~10–30 s — acceptable for a degraded tier, and the impact-zone-only fallback (S2) still yields
shaft lean + P6/P8 if the user finds it too slow in practice.

**Tests:** `frame_decode` unit test against synthetic NV12/Bayer/BGRA payloads (golden pixel
checks); PoseRunner verified by integration (headless app, captured swing) — ORT in a standalone
harness is not worth the build cost.

**Acceptance:** analyzing a captured swing logs a `PoseTrack2D` with ≥ 90% of frames having
wrist conf > 0.3; wall-time delta logged.

> **As built (S0).** Shipped as planned, with these deviations/findings:
> - **BGRA32 was NOT in the exporter's original demosaic table** — the plan's "exactly the
>   exporter's current behaviour (… BGRA32 …)" was wrong, and industrial BGRA32 shots were
>   silently skipped at MP4 export. The shared table includes BGRA32, so the extraction
>   *fixed a real export bug* as a side effect; everything else is byte-identical.
> - **No intra-op-threads knob added** — `PoseEstimatorViTPose::load()` already pins
>   `SetIntraOpNumThreads(1)`, exactly what the offline path requires.
> - `decodeToLuma` covers **every** format the BGR table supports (incl. Mono8/BGR24/
>   YUYV/UYVY), not just the four named — the two paths can never diverge in coverage.
> - `pose_runner.cpp` is guarded by `HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME` with
>   a `ppWarn` + empty-track fallback when built without them.
> - The track types (`PoseFrame2D`/`PoseTrack2D`) later moved to `swing_analysis.h` in S3
>   (canonical home); `pose_runner.h` keeps only `ShotAnalysisRunnerOptions` + the class.
> - `frame_decode_test`: 40+ checks — exact BGRA32 goldens incl. padded stride, hand-computed
>   BT.601 NV12/YUV420P goldens, zero-copy aliasing asserts, false on short/null/unsupported.

## S1 — Detection math core (pure, standalone-tested)

**Deliverable:** the per-frame anchored radial transform of addendum B.3/B.4 as pure functions —
no Qt, no window access, fully unit-testable.

**Files**
- `src/Analysis/shaft_tracker_math.{h,cpp}` *(new)* — `detectShaft(const cv::Mat &luma,
  const ShaftDetectConfig&, const AnchorPrior&) → std::vector<ShaftCandidate>`:
  top-hat + black-hat ridge response → `cv::warpPolar` about the grip anchor →
  per-column score `S(θ)` = mean-strength × coverage over `ρ ∈ [ρ_min, ρ_vis]` →
  clutter masks (±12° of `g→elbow` directions) → soft prior weighting (inter-hand direction,
  temporal/IMU prediction) → NMS top-K with parabolic sub-bin refinement →
  per-candidate visible extent `L`, head-blob seed, **wedge plateau handling** (centroid θ +
  half-width as σ). 3×3 anchor-perturbation rescore + proximal TLS line refit.
  `ShaftCandidate { double thetaRad, sigmaTheta, visibleLenPx, score; QPointF headPx;
  bool wedge; }`.
- `src/Analysis/tests/shaft_tracker_test.cpp` *(new, standalone)* — synthetic renderer:
  tapered bright/dark lines over noise and real background crops, parameterized blur wedge,
  rolling-shutter shear, anchor error, alignment-stick/shadow clutter. Asserts addendum B.9
  tolerances: θ ≤ 0.5° (slow frames), ≤ 2° (wedge frames), L within 5%, clutter rejected.
  Links system OpenCV by absolute path (standalone-harness gotchas memory).

**Acceptance:** test suite green; single-frame detect ≤ 2 ms on a 600² ROI (measured in-test).

> **As built (S1).** All planned behaviour shipped; four evidence-driven algorithm deviations
> (each documented at the code site):
> - **`ridgeKernelPx` default 9, not 7, and RECT, not ellipse** — a 6 px line + AA fringe
>   survives a 7 px opening, killing the ridge response exactly where the run must start
>   (rule: kernel strictly wider than the widest ridge); RECT is separable (~6× faster
>   morphology) and equivalent for sub-kernel-width ridges.
> - **No `cv::warpPolar`** — 18 polar warps measured 14–21 ms/call. Replaced by a fused
>   multi-anchor ray scan sampled straight off the Cartesian response maps
>   (`cv::parallel_for_` over θ, early-exit per column). Same θ convention and outputs.
> - **Wedge θ = half-peak window midpoint + twin-horn merge** (`wedgePairMaxSepDeg`) — a fan
>   whose solid interior exceeds the kernel responds at its two *edges*; the naive intensity
>   centroid dragged −2.2° on an 8° fan. Wedge winners from a perturbed anchor are
>   re-estimated about the true grip (no TLS stage for wedges).
> - **TLS refit is iterated** (≤5 passes, band recentred each fit) — a single pass beside a
>   *tapered* ridge clips it asymmetrically (−0.65° measured; converges to −0.15°).
>
> Measured: **~2.0 ms mean** per detect on a 600² ROI. Margins: clean θ −0.004° (tol 0.5°),
> wedge centroid 0.00° (tol 2°), all clutter/mask/anchor-perturbation checks pass.

## S2 — Track assembly: ŝ_hand, Viterbi, KF + RTS

**Deliverable:** `ShaftTrack2D` (addendum B.6 struct) from per-frame candidates + the lead-hand
IMU stream — smooth, unwrapped, confidence/flag-annotated, IMU-bridged through blur dropouts.

**Files**
- `src/Analysis/shaft_tracker.{h,cpp}` *(new)* —
  `ShaftTracker::track(const SwingWindow&, const PoseTrack2D&, const FusedStreams&,
  const std::vector<PhaseEvent>&, const ShotAnalysisJob&) → ShaftTrack2D`. Stages:
  1. **ŝ_hand auto-calibration** (B.2): over Address→Takeaway frames with high vision conf,
     fit the constant shaft-in-hand-frame unit vector against measured θ (5° spherical grid +
     Gauss-Newton, orthographic projection). Skipped when no `LeadHand` IMU is bound —
     vision-only mode.
  2. **Viterbi association** over per-frame top-K + missing nodes; transition cost uses the
     IMU-predicted θ̇ when calibrated, finite-difference otherwise.
  3. **Unwrap → const-accel KF (`[θ, θ̇, θ̈]`, white-jerk, clubhead-grade PSD) + RTS backward
     pass**, two measurement channels (vision, IMU), predict-only when both absent. Light 1-D
     KFs for `L(t)` and the head point. Geometry-driven confidence: `L` collapse (shaft toward
     camera) suppresses θ measurements via the IMU-predicted foreshortening profile.
  4. **Validity gate** (B.8): ≥ 60% of swing-span frames measured/bridged, else the whole track
     is marked invalid — consumers must see all-or-nothing.
- `src/Analysis/tests/shaft_track_test.cpp` *(new)* — synthetic candidate sequences: dropout
  bridging, clutter-capture recovery (Viterbi must reject a 5-frame strong false ridge),
  wrap-around continuity, ŝ_hand fit convergence against synthetic `q_anat` streams (reuse
  `imu_test_stubs.cpp` provisioning).

**Acceptance:** tests green; on a captured swing the IMU↔vision θ̇ correlation (computed anyway —
it is the fusion layer's temporal-alignment signal) exceeds 0.9, logged per shot as the
in-production health metric.

> **As built (S2).** All planned behaviour shipped, restructured into two files and with
> these algorithm deviations:
> - **Split: `shaft_track_assembly.{h,cpp}` + `shaft_tracker.{h,cpp}`** (plan had one file).
>   The assembly is pure over plain vectors (`ShaftFrameObs` in → `ShaftTrack2D` out, no
>   `SwingWindow` access) so the whole ŝ_hand→Viterbi→KF/RTS pipeline is standalone-testable;
>   `ShaftTracker::track()` keeps only the window-driving orchestration. The canonical output
>   types (`ShaftSample2D`/`ShaftTrack2D`/flags) live in `swing_analysis.h` (Qt-only) so
>   `SwingAnalysis` consumers never pull the OpenCV-typed detection headers.
> - **Driver specifics** (`shaft_tracker.cpp`): detection runs over **every** camera frame
>   inside pose coverage, grip/elbow anchors lerped between the sampled pose frames; the
>   search radius comes from the median pose-silhouette height × assumed 1.70 m stature →
>   `1.25 · clubLengthM · px/m`, clamped [80 px, min(w,h)], half-frame fallback when pose
>   never sees a full body. `qHand` is the nearest 200 Hz fused-grid sample within 25 ms.
>   Coverage span = Address→Finish phase events when present, else the full obs range.
> - **ŝ_hand fit gained two parameters the plan missed:** `sign ∈ {±1}` (mirrored-camera
>   chirality — mirroring is not a rotation, no constant offset can express it) and `δ`
>   (constant image-angle offset absorbing IMU-world↔image yaw/camera roll, closed-form
>   circular mean per candidate direction). Solver is a Fibonacci sphere lattice (800 dirs
>   ≈ 7°) + two local tangent-plane refinement grids (±7° @ 1.7°, ±1.7° @ 0.6°) — no
>   Gauss-Newton stage needed at that resolution. Eligibility: valid quat, non-wedge best
>   candidate, wrapped rate ≤ 3 rad/s; gates: ≥ 8 frames, ≥ 15° θ span (identifiability),
>   RMS residual ≤ ~7° or the channel is disabled (degrade, never fabricate). Per-shot fit,
>   never persisted (re-gripping moves ŝ).
> - **Unwrapping is velocity-aware inside the filter, not a pre-pass** — each wrapped vision
>   measurement is lifted against the prediction (`z = x̂₀ + wrap(θ_z − x̂₀)`), so downswing
>   gaps cannot alias; the IMU channel is unwrapped from the fit per frame and re-anchored to
>   the filter domain by one constant per-run offset.
> - **`L(t)`/head simplified vs the planned "light 1-D KFs":** L = median-of-5 over measured
>   values + hold-last between; head = measured blob when present, else projected
>   `grip + L·dir(θ)` with a `ShaftHeadProjected` flag. Deliberate — θ is the precision
>   channel. **The planned foreshortening-driven θ suppression (L-collapse via the
>   IMU-predicted profile) was NOT built** — wedge σ and the coverage gate carry that risk
>   for now; revisit in S5 if real face-on swings show θ corruption when the shaft points
>   at the camera.
>   **Update 2026-07-09 (`df76fe9`, Phase B below):** the head is now measured, and with it
>   `L(t)` collapse arrives **via measurement** (the Stage-2 KF's `r_h` state directly tracks
>   the shrinking radial distance as the shaft forshortens toward the camera) rather than via
>   the originally-planned IMU-predicted profile model. The θ-suppression side of this bullet
>   is unaffected — θ still comes from the Stage-1 filter untouched, confirmed bit-identical
>   with/without the ball (Phase A note below) and with/without the head pass (Phase B gate).
> - **Phase A length ladder (2026-07-09, clubhead_length plan §A1–A3).** The projected-head
>   length `L` for coasted/predicted frames no longer falls back to a flat `0.55·frameH`.
>   `decideTrack(...)` now takes an optional `const BallTrack2D* ball` and, before head
>   placement, measures `out.measuredClubLenPx` = median grip→ball px over the **address hold
>   proper** — the last contiguous quasi-still run ending at/spanning `bs0`, clipped to
>   `[0, bs0+collar)`; trailing-collar fallback without stillness info (`medianGripBallLenPx`,
>   shared out of `ball_anchor`; the old post-hoc computation there is removed). The whole
>   pre-takeaway window was tried first and measured teeing/setup (hands at the ball) 107–178 px
>   short on the 2026-07-04 corpus. Mis-locks are rejected by an order-independent two-pass
>   cluster gate (component-wise median ball position, accept within 6 px) — the chained
>   first-accepted gate let one warm-up lock veto every later good sample — and the measurement
>   abstains (−1) below 5 accepted samples. A golf-prior plausibility gate then catches
>   CONSISTENT mis-locks the cluster gate keeps (gateA-0704: driver-head lock 130–175 px above
>   the truth ball shorted rung 1): the median lock must sit below the median ankle line
>   (margin `0.02·frameH`) and inside the ankle x-extent ± `0.1·frameW` (ball always between
>   the feet, face-on); rejection ⇒ −1 ⇒ honest ladder degradation, reason logged in
>   `ShaftDecideTrace.lPxRejected` (0 accepted / 1 ankle / 2 feet, dumped by swinglab_run).
>   Ankles come from the smoothed body joints at the `decideTrack` call site; pose-free
>   callers skip the gate. The per-frame θ-anchor path in `applyBallAnchor` has NO equivalent
>   gate yet (TODO in code). `projectedClubLenPx(...)` then
>   picks `L` from the first available scale source — **rung 1** ball `L_px` · **rung 2** band
>   scale grip-corrected `sTypical·(clubLenMm−r0Med)` (was the overshooting `sTypical·clubLenMm`)
>   · **rung 3** pose-scale surrogate (still shoulder-mid→ankle-mid px ÷ `0.83·lenStatureM`,
>   × `clubLengthM−lenGripDownM`) · **rung 4** `0.45·frameH` — then clamps: floor `1.05×` still
>   shoulder→grip (arm floor), ceiling `1.1·L_px` (ball) else `0.62·frameH`. Chosen rung + px
>   land in `ShaftDecideTrace.projLenRung/projLenPx`; new config `shaft.lenStatureM` (1.70) /
>   `shaft.lenGripDownM` (0.13). **`applyBallAnchor` A3:** the address-hold and impact anchors
>   now move `headPx` onto the ball and clear `ShaftHeadProjected` (the head *is* measured
>   there), instead of leaving the stale projected terminus. **θ is untouched by all of this** —
>   the ball feeds only length/head; a `decideTrack` regression test asserts θ is bit-identical
>   with `ball=nullptr` vs a populated ball.
> - **Phase B — Stage-2 measured head (2026-07-09, `bd9c47e` corpus-gate fixes,
>   `df76fe9` default ON).** The head is no longer projection-only: a second decode pass in
>   `decideTrack` (after `reconcilePsi`), `src/Analysis/clubhead_track.{h,cpp}`, ROI-bounded
>   Sobel + running-bg EMA, ports the exemplar's H1 gap-tolerant on-axis terminus + H2 1-D KF
>   `[r,ṙ]`/per-segment RTS/meas-pred-off tiers/arm floor/180° flip check + H3 posterior-σ
>   tier — see `clubhead_exemplar_plan.md`'s closing note for what did and didn't carry over
>   from the Python exemplar. The exemplar's self-fit length model was **not** ported; the
>   ball-measured `L_px` prior above (annulus ceiling `1.15·L̂`, hard floor `0.8·L_px` at
>   still/impact, Gaussian still-frame prior, meas-acceptance floor 0.5·L̂ ramped to 0.8·L̂ at
>   takeaway/impact) replaces it. Backswing streak confidence capped at 0.45 (motion-blur
>   streaks corpus-proven to short-lock). Output: `ShaftSample2D.headConf`/`headSigmaPx`
>   (−1 = not run), flags bit `0x80 ShaftHeadOffFrame` (edge-clamp render, not a position),
>   additive `swing.json` keys, QML meas/pred/off tier rendering. Tuning under `shaft.head.*`.
>   BAND-tier (taped) heads are never overwritten by the head pass. **Gated on the dense
>   2026-07-05 studio corpus** (10 taped 7i swings, 40–121 dense labels each): honesty clause
>   passes 9/10 (the allowed fail, swing_0001, traces to Stage-1 θ-quality, not the head pass),
>   meas-tier median error 0.8–2.4 px/swing, θ bit-identical head-on vs head-off, head pass adds
>   6–15% to shaft-stage compute, all 35 analyzer unit tests pass (3 new suites). Known
>   limitation: fast-phase Stage-1 θ degradation (>8°, beyond the ±5° budget Stage-2 was
>   decoupled against) is now the binding accuracy constraint, not the head measurement itself.
> - Viterbi transition cost: deviation of Δθ from the IMU-predicted delta (slack 200 rad/s²)
>   when calibrated, else the path's own velocity trend (slack 800 rad/s², extra ~20° on the
>   first transition), plus a ΔL deviation term; node cost from per-frame-normalised scores
>   with a missing hypothesis at fixed penalty.
> - `imuVisionCorr` (Pearson over consecutive measured-pair θ̇, dt < 0.2 s, ≥ 8 pairs) is
>   computed in `assemble()` and rides the track — the >0.9 acceptance itself pends S5.
> - `shaft_track_test` (4 scenarios over a synthetic 110-frame swing): IMU run — fit
>   reproduces the projected angle everywhere (the gauge-invariant statement; ŝ itself has a
>   one-parameter family), mid-backswing strong-clutter rejection, 8-frame downswing dropout
>   bridged, unwrapped top→post-impact sweep ≈ −300°; mirrored camera — fit sign = −1;
>   vision-only — coasting, trend-based clutter rejection, corr stays 0; coverage gate —
>   every-other-frame dropout with no IMU ⇒ invalid. Only test target linking Qt **and**
>   OpenCV; `-O2` like the S1 target.

## S3 — Analyzer integration + persistence

**Deliverable:** the track rides `SwingAnalysis`, persists in `swing.json`, reloads, and feeds
metrics/segmentation.

**Files**
- `src/Analysis/swing_analysis.h` — additive: `PoseTrack2D pose2d; ShaftTrack2D shaft;` on
  `SwingAnalysis` (both may be empty/invalid).
- `src/Analysis/wrist_analyzer.cpp` — after `fuse`/`segment` (`wrist_analyzer.cpp:108-116`):
  when `job.faceOnCameraCount > 0`, run PoseRunner + ShaftTracker; store both tracks on
  `detail`; add the `impactShaftLean` metric (θ vs image-vertical at the impact sample, signed
  toward-target using `job.handedness`) to the series so it scores like any other metric. The
  same call sequence is the seam the future `SwingAnalyzer` (type 0) reuses verbatim.
- `src/Analysis/phase_segmenter.*` — when segmentation v2 lands, `Delivery`/`Release` upgrade
  from hand-proxy to measured shaft-parallel crossings. Until then the track is stored but not
  consumed by phases — **no ordering dependency between the two work streams**.
- `src/Export/swing_doc.cpp` — additive `analysis.pose2d` + `analysis.club` blocks
  (addendum B.6 shapes; pose downsampled to kp+conf per frame — ~300 frames × 17 kp is small).
  `SwingDocReader` maps them back; missing blocks ⇒ empty tracks (old files reload unchanged).
- `src/Gui/shot_processor.cpp` — `toAnalysisDetail()` (the `m_replayAnalysisDetail` builder,
  `shot_processor.cpp:623`) gains two arrays, **pre-normalized to 0..1 frame coords** so QML
  never sees pixel spaces: `pose2d: [{t, kp:[{x,y,c}×17]}]` and
  `club: [{t, gx,gy, hx,hy, theta, conf, flags}]`.

**Tests:** swing_doc round-trip (write → read → compare) for the new blocks in the existing
doc-writer test pattern; analyzer integration verified headless.

**Acceptance:** a captured shot's `swing.json` contains both blocks; reload populates the detail;
`impactShaftLean` appears in the metrics map.

> **As built (S3).** Shipped as planned, with these deviations/clarifications:
> - **`impactShaftLean` is in the series but UNSCORED** — the plan's "scores like any other
>   metric" was premature: there is no validated reference band, so the scorer's band table
>   simply doesn't list the key (series + impact phase-sample only). The toward-target
>   **sign is provisional** pending the hardware sign-lock pass (same treatment the wrist
>   metrics needed). Lean = deviation of the grip→head ray from image-vertical (+90° in the
>   y-down convention), wrapped near zero; `wristMetricLabel` renders forward/back/neutral.
> - The shaft stage runs **after** the cheap IMU stages in `WristAnalyzer::analyze()`, gated
>   on `job.faceOnCameraCount > 0`; every failure degrades to empty/invalid tracks on the
>   detail, never to a failed analysis (the shot lands on the carousel regardless).
> - `swing.json`: `analysis.pose2d` (per frame: flat `kp [x,y,c]×17` + lead/trail/handConf)
>   and `analysis.club`. **The club block is written only for a VALID track** (all-or-nothing
>   consumer contract); grip/head are normalized 0..1 at serialization by the camera dims,
>   which are persisted too (`frameWidth/Height`). `SwingDocReader` maps both blocks into
>   `analysisDetail` as variant maps with shapes IDENTICAL to the live `toAnalysisDetail`
>   (`shot_processor.cpp`) — reloaded shots drive the replay overlay unchanged; missing
>   blocks ⇒ absent keys (old files reload as before). Round-trip covered in
>   `swing_doc_test` (pose frame + 2-sample valid club track, exact value checks).
> - `ShotProcessor::startAnalysis()` counts face-on cameras while building the face-on-first
>   `cameraSources` (making the ordering verifiable from the worker); `clubLengthM` rides the
>   job at its 1.12 driver default. `PoseRunner` options (`impactUs`, `handedness`) are
>   resolved from the job on the worker — value types only, per the job rule.
> - `phase_segmenter.*` untouched, exactly as planned — the track is stored, not yet consumed
>   by phases.

## S4 — Replay overlay: skeleton + shaft on the face-on frame

**Deliverable:** during REPLAYING, the face-on `PpCameraFrame` draws the offline skeleton and the
shaft (grip→head line + head dot + fading ~10-sample clubhead trail), scrubbing with
`shotProcessor.replayPositionUs` — the same playhead `ScreenWrist`'s metric graphs already follow.

**Files**
- `src/Gui/PpCameraFrame.qml` — new `replayOverlayCanvas` *sibling* of the live
  `skeletonCanvas` (`PpCameraFrame.qml:240`), behind a new screen-config property
  `showReplayOverlay` (default true on Wrist/Swing screens, panel-wireable later like every
  other per-screen overlay):
  - **Visible** when `shotProcessor.isReplaying && root.instance && root.instance.perspective === 2`
    (live `skeletonCanvas` is already hidden in replay — `isRecording`/live-kp gating — so the
    two never co-draw). Every subscribed face-on tile gets the overlay for free via the
    existing frame pub/sub fan-out.
  - **Data**: cache `shotProcessor.replayAnalysisDetail.pose2d/club` into local
    `property var` arrays on `replayAnalysisDetailChanged`; `onReplayPositionChanged` →
    binary-search the nearest sample ≤ playhead → `requestPaint()`. ~60 repaints/s over
    ≤ 300-element arrays is trivial.
  - **Drawing**: reuse the live canvas's contentRect/debayer mapping (`PpCameraFrame.qml:284-290`)
    and `kEdges` table verbatim (factor `kEdges` to a shared `readonly property` on the root so
    the two canvases share one definition). Skeleton in the live style but at reduced alpha;
    shaft as a 2 px `Theme.colorAccent` line grip→head with a small head dot; trail as a
    polyline over the last ~10 club samples with alpha fading to 0 — muted, half-alpha-over-media
    styling (subtle-chrome preference), no chrome until the replay actually has a valid track
    (invalid/empty club array ⇒ skeleton only; empty pose2d ⇒ nothing, never a guess).
- `src/Gui/ScreenWrist.qml` / session screens — set `showReplayOverlay: true` on their face-on
  `PpCameraFrame` instances (one-line per screen; per-screen video architecture pattern).

**Verification:** QML visual harness (offscreen `grabWindow` from C++, timer-stepped replay,
TEMP blocks removed before commit) — capture frames at Address/Top/Impact and eyeball
skeleton+shaft registration against the video; then a real on-hardware replay.

**Acceptance:** replay of a captured swing shows skeleton + shaft tracking the playhead with no
visible lag or misregistration; toggling `showReplayOverlay` hides it; non-face-on tiles and
no-analysis shots show plain video.

> **As built (S4).** `replayOverlay` canvas in `PpCameraFrame.qml` (z 21, sibling of the
> live skeleton canvas) per the plan, with these deviations:
> - **`showReplayOverlay` defaults `true` on `PpCameraFrame` itself — the planned per-screen
>   one-liners don't exist.** Every face-on tile gets the overlay with zero screen edits;
>   panel wiring stays future work like the other per-screen overlay toggles.
> - `kEdges` factored to a root `readonly property var kSkeletonEdges` shared by both
>   canvases, as planned.
> - Visibility = `showReplayOverlay && shotProcessor.isReplaying && perspective === 2 &&`
>   **cached arrays non-empty**; `_rebuildCache()` (on `replayAnalysisDetailChanged` and on
>   becoming visible) caches `club.samples` only when `club.valid` — so invalid track ⇒
>   skeleton only, no pose ⇒ nothing, exactly the planned degrade ladder.
> - Scrubbing: `_indexFor()` binary search (greatest `t_us ≤ playhead`) over both arrays;
>   repaint on `replayPositionChanged` only while visible.
> - Drawing: skeleton in the live style at 0.55 × conf-scaled alpha; shaft = 2 px
>   `Theme.colorAccent` grip→head line, alpha `0.35 + 0.5·conf`; 4 px head dot; head trail =
>   alpha-fading polyline over the last 10 samples. Coord mapping reuses the live canvas's
>   contentRect/debayer split verbatim.
> - **The planned QML visual-harness pass was NOT run** — there is no captured swing with a
>   club on disk yet, so a grabbed frame could only show the canvas not drawing. Pixel-level
>   registration is verified headless only to the extent that the canvas instantiates clean;
>   the eyeball check folds into S5 (as the header note records).

## S5 — Hardware validation + tuning

- Label shaft endpoints every ~10th frame on the pending hardware-verification swings
  (shot-detection P4 session); regression-test track RMS and the B.9 tolerances.
- Tune `ShaftDetectConfig` thresholds (ridge kernel vs px/m, ρ_min, NMS separation) against
  steel + graphite clubs, indoor mat + outdoor grass backgrounds.
- Confirm wall-time on the 12-core/GPU box and the CPU-only path; decide whether the CPU tier
  keeps full-span tracking or drops to impact-zone-only.
- Log per-shot health: vision↔IMU θ̇ correlation, % frames measured/bridged/coasted.

---

## Order & sizing

| Stage | Depends on | Size | Parallelizable |
|---|---|---|---|
| S0 decode + pose runner | — | M | with S1 |
| S1 math core + tests | — | M | with S0 |
| S2 track assembly | S0, S1 | M | — |
| S3 analyzer + persistence | S2 | S | — |
| S4 replay overlay | S3 | S | — |
| S5 hardware tuning | S4 + captured swings | M | — |

Segmentation v2 (`PhaseSegmenter` addendum) is **independent**: ShaftTracker runs on the full
window without it (truncation only saves cost), and the P6/P8 upgrade is a two-line consumer
change whenever both have landed.

## Risks

- **CPU-only pose latency** is the main schedule risk (10–30 s worst case). Mitigated by
  adaptive sampling (S0) and the impact-zone-only degradation; the shot lands on the carousel
  regardless (existing degrade contract).
- **ViTPose hand-channel quality** on blurred frames is unproven — the wrist keypoints are the
  fallback anchor, and S1's anchor-perturbation rescore tolerates ±10 px.
- **`ŝ_hand` orthographic projection** error grows with wide-FOV webcams; it is a prior/second
  channel, never the sole source, and C1 intrinsics upgrade it transparently later.
- **GPU contention** analyzer-vs-exporter: already managed by `IntraOpNumThreads(1)`; measure in
  S0 and, if encode stalls, serialize pose after the exporter's frame-read pass.

## Out of scope (per addendum B.10)

DTL-camera shaft, 3-D shaft lift, clubface angle, learned keypoint models, club identification.
