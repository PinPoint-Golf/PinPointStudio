# Segmentation v3 — Implementation Plan (inertial ladder + shaft refinement + window truncation)

> **Status:** PLANNED · **Date:** 2026-06-11 · **Grounded against:** `main` @ `7a3a160`
>
> Implements the *Swing segmentation v2* and *Swing segmentation v3* addenda of
> [`docs/design/SHOT_ANALYZER_DESIGN.md`](../design/SHOT_ANALYZER_DESIGN.md) (sections A.1–A.9 and
> C.1–C.6) in one programme: v2's inertial ladder is v3's pass 1, so there is no point building
> them separately. Primary motivations, in the user's priority order: **(1) bound the heavy
> camera stages to the detected swing span** (the pose pass dominates analysis wall-time and the
> swing occupies ~half the 5 s ring — expect ~2× fewer pose inferences and shaft detections),
> **(2)** trim export/replay/metric grids to the swing, **(3)** upgrade the geometric P-positions
> (P2/P6/P8) from proxies to shaft-measured events.
>
> `ShaftTracker` is implemented but not yet hardware-proven (S5 pending). Stage order below is
> deliberate: G0–G3 deliver the full performance win using **inertial signals only** and ship
> independently; G4 (shaft refinement) layers on top and is skippable per shot by design
> (`shaft.valid` gate), so an unproven shaft can never regress segmentation below v2 behaviour.

## What ships

After a shot, a milliseconds-cheap inertial pass finds the full event ladder (address → takeaway →
top → … → finish, each with confidence + provenance) and the swing start/end. The replay and
metric graphs span address→finish, and the analyzer's pose + shaft stages scan only the swing
span. **Exports are never trimmed** (product decision, 2026-06-11): the saved MP4 preserves every
captured frame — a wrong Address/Finish would clip frames irrecoverably; truncation is a
playback/analysis concern only. After the shaft track is built, the geometric club checkpoints
(P2/P6/P8) are re-measured from θ(t) and the metric extraction reads the refined instants. Tempo
metrics (backswing/downswing/ratio) come free from the events.

```
PASS 1 (ShotProcessor pre-stage + analyzer, IMU-only, ~ms)
  └─ G0  SegmentStream v2     gyro/accel channels + derived signals (envelope, plane, inclination)
  └─ G1  PhaseSegmenter v2    impact-anchored back/forward chaining → Segmentation{events, bounds}
  └─ G2  plumbing             pre-stage gating, post-roll 1250 ms, export/replay/metric-grid trim
  └─ G3  heavy-stage bounding PoseRunner + ShaftTracker scan [swingStart−pad, swingEnd+pad]
PASS 2 (analyzer, after ShaftTracker)
  └─ G4  shaft refinement     P2/P6/P8 measured from θ(t), MaxSpeed from θ̇·L, extractor reorder
  └─ G5  vision-only ladder   camera-only sessions get a conf-capped event ladder (optional)
  └─ G6  hardware validation  folds into the pending S5 session (labelled goldens, telemetry)
```

## Contracts that bind every stage

- **Pass 1 never touches camera data; pass 2 never moves Impact.** The monotone-chain rule
  (design A.2.5) is enforced in one place — events that violate ordering are dropped by
  confidence, never reordered.
- **The frozen `SwingWindow` is never trimmed** — truncation is metadata (`Segmentation`)
  consumed downstream (design A.6). The 5 s capture contract and zero-copy reads are untouched.
- **Degrade, never fabricate** — missing sensor ⇒ event omitted or low-conf; failed segmentation ⇒
  full-window bounds (today's behaviour) everywhere.
- **Quaternions-only** for orientation math; signals derived from quaternions are scalars
  (inclination, signed plane rate) computed via quaternion rotation, never Euler intermediates.
- **Flat source dir** — new code in `src/Analysis/*.{h,cpp}`, tests in `src/Analysis/tests/`.
- **No new dependencies** — the swing-plane normal (largest eigenvector of a 3×3 outer-product
  sum) is closed-form/power-iteration, not Eigen; filtering is a ~20-line biquad applied
  forward-backward, not a DSP library.

---

## G0 — Signal preparation (`FusedStreams` v2)

**Deliverable:** the fuser stops discarding the measured inertials; every detector input the
design's A.4 lists is computed once, in one tested place.

**Files**
- `src/Analysis/imu_vision_fuser.h/.cpp` — `SegmentStream` gains `std::vector<QVector3D> gyroDps,
  accelG` (body-frame, lerped from the 40-byte `ImuSample` exactly like `qAnat`; the fuser already
  interpolates the full sample and keeps one field — this is plumbing, not new math).
- `src/Analysis/phase_signals.{h,cpp}` *(new, pure)* — free functions over `SegmentStream` +
  `timeGrid`, all per design A.4: zero-phase 2nd-order Butterworth (forward-backward biquad,
  ≈10 Hz), energy envelope `Ω(t) = ‖gyro‖` filtered, world-frame gyro `q·g·q⁻¹`, swing-plane
  normal (outer-product accumulation over [impact−300 ms, impact−30 ms], power iteration,
  sign-fixed), signed plane rate `dot(g_w, n̂)`, gravity-referenced inclination
  `asin(dot(q·ŷ_seg, ẑ_w))`, pelvis/thorax signed axial rate, the `still(t)` predicate
  (Ω < 15 °/s ∧ |‖accel‖−1| < 0.08 g across all bound segments), and parabolic sub-grid peak/
  zero-crossing refinement.

**Tests:** `phase_signals_test` (pp_add_test, Qt-only): filter is zero-phase on a synthetic chirp
(no lag vs analytic), envelope/inclination/plane-normal against closed-form trajectories,
stillness predicate boundary cases, parabolic refinement recovers a known sub-sample crossing to
< 1 ms on a 200 Hz grid.

**Acceptance:** test target green; fuser change is byte-identical for `qAnat` consumers.

## G1 — Pass-1 inertial ladder (`PhaseSegmenter` v2)

**Deliverable:** the v2 API and all seven detectors of design A.5, replacing the v1 heuristic.

**Files**
- `src/Analysis/phase_segmenter.h/.cpp` — rewrite:
  ```cpp
  struct SegmentationConfig { /* thresholds + duration gates, design A.5 defaults */ };
  struct Segmentation {
      std::vector<PhaseEvent> events;
      int64_t swingStartUs = 0, swingEndUs = 0;   // Address−250ms / Finish+250ms pads, clamped
      float   conf = 0.f;                          // min over {Address, Top, Impact, Finish}
      int     version = 2;                         // 3 once pass 2 refined it
  };
  static Segmentation segment(const FusedStreams&, int64_t impactUs,
                              const SegmentationConfig& = {});
  ```
  Detectors in search order (A.5): Top backward from impact (signed-plane-rate run), Transition
  (pelvis, omitted when unbound), Takeaway (back-chain with hysteresis + dip tolerance), Address
  (stillness end), geometric checkpoints from inclination (MidBackswing/Downswing/Release/
  FollowThrough; Delivery as labelled hand-proxy at conf ≤ 0.4), MaxSpeed, Finish forward.
  Multi-segment voting (hand + forearm, thorax cross-check). Monotone-chain enforcement.
- `src/Analysis/swing_analysis.h` — `Phase` enum appends `MidBackswing = 8, Delivery = 9,
  MaxSpeed = 10, FollowThrough = 11` (append-only; `swing.json` stores raw ints). `PhaseEvent`
  gains `SegmentRole provenance` (default `Unknown`). Tidy-up: the reader's magic `5` →
  `int(Phase::Impact)`.
- `src/Analysis/wrist_analyzer.cpp` — calls the new API; v1 call sites updated (the old
  4-event vector is the `Segmentation::events` subset, so consumers keep working).

**Tests:** `phase_segmenter_test` *(new)* — the design A.8 synthetic generator: per-phase
quaternion trajectories with **consistent finite-difference gyro** (the generator derives gyro
from the quaternion sequence so the two can never disagree). Adversarial: waggle + regrip before
takeaway, truncated finish, missing pelvis, hand-only, forearm-only, continuous pre-shot motion,
100 vs 200 Hz grids. Tolerances ±10 ms kinematic / ±25 ms geometric. `pipeline_test` updated for
the new signature.

**Acceptance:** all synthetic cases green; v1's failure cases (A.1: waggle pins Address, Finish =
window edge at conf 1.0) demonstrably fixed in-test.

## G2 — Sequencing, post-roll, and consumer truncation

**Deliverable:** the pass-1 result reaches every consumer; the window actually contains the finish.

**Files**
- `src/Gui/shot_processor.cpp/.h` — post-roll: auto-detector constants (`kPostRollImuMs`,
  `kPostRollAcousticMs`, `kPostRollPoseMs`, `kPostRollBallMs`) 500 → **1250 ms**; manual stays 500
  (impact is back-dated, the user presses after the swing). Budget per design A.6: impact ~3.75 s
  into the 5 s ring. **Pre-stage:** a third small QtConcurrent stage runs fuse + segment on the
  frozen window *before* the two heavy workers launch (`captureWindowAndLaunch` chains it; its
  future gates `startAnalysis`/`startSwingSave`); the `Segmentation` is value-copied into
  `ShotAnalysisJob` and `SwingExportJob`. Failure ⇒ full-window bounds + v1-equivalent fallback
  events (today's behaviour). Replay span: `m_replayWindowStartUs/EndUs` clamp to the bounds, and
  per-track `entries` are sliced to it (less frame churn for free). The analysis progress bar's
  elapsed time drops with the span — no change needed there.
- ~~`src/Export/swing_exporter.cpp` + `swing_exporter.h` — encode span~~ **Built, then removed
  (product decision, 2026-06-11): exports preserve every captured frame. The MP4 and the raw IMU
  streams both stay full-window; segmentation bounds trim playback and analysis only.**
- `src/Analysis/metric_extractor.cpp` — the metric `TimeGrid` is restricted to
  [swingStartUs, swingEndUs] so every graph spans address → finish.
- `src/Export/swing_doc.cpp` — persist the `segmentation` block (A.7 shape:
  swingStartUs/EndUs/conf/version) + `phases[].segment` provenance + the new enum ints; reader
  maps them back, missing block ⇒ full-window (old files unchanged). Round-trip in
  `swing_doc_test`.

**Tests:** swing_doc round-trip; headless shot-cycle smoke (existing harness) confirming the
export MP4 duration ≈ swing span and replay starts at address.

**Acceptance:** a captured shot exports ~2.5 s instead of 5 s; replay starts at address; metric
graphs span the swing; old swing.json files reload unchanged.

## G3 — Heavy-stage bounding (the performance payoff)

**Deliverable:** `PoseRunner` and `ShaftTracker` scan only the swing span.

**Files**
- `src/Analysis/wrist_analyzer.cpp` — passes `swingStartUs − 150 ms … swingEndUs + 150 ms` (pad
  per design C.1) into the runner options and the tracker; falls back to full window when
  segmentation conf is 0 (failed pass 1).
- `src/Analysis/pose_runner.h/.cpp` — `ShotAnalysisRunnerOptions` gains `int64_t scanStartUs/
  scanEndUs` (0 = unbounded); the entry loop skips outside them. Adaptive dense/sparse sampling
  unchanged *within* the span. Progress fractions are span-relative (the bar still sweeps 0→1).
- `src/Analysis/shaft_tracker.cpp` — takes its span from the same bounds instead of deriving it
  from Address/Finish events (`spanLo/spanHi` today); detection loop already restricts to pose
  coverage, which is now the bounded span. ŝ_hand calibration inherits the correct Address for
  its slow-frame eligibility window — no code change, better input.

**Tests:** pose_runner/shaft_tracker behaviour is integration-verified (headless captured shot,
wall-time logged before/after); the bounding logic itself is trivial timestamp comparisons
covered by the analyzer-level smoke.

**Acceptance:** logged analysis wall-time on the same captured swing drops roughly with the
span ratio (expect ~2× on the pose pass); identical metric values to the unbounded run (the
discarded frames carried no swing).

## G4 — Pass-2 shaft refinement

**Deliverable:** the C.3 refinement table — measured P2/P6/P8, club-measured MaxSpeed, Top
cross-check — and metric extraction reading refined events.

**Files**
- `src/Analysis/phase_segmenter.h/.cpp` — second entry point:
  `static void refine(Segmentation&, const ShaftTrack2D&, int64_t impactUs,
  const SegmentationConfig&)`. Gates: `shaft.valid`, `imuVisionCorr ≥ 0.5` when an IMU channel
  was fused, bracketing samples `Measured` (or `ImuBridged` at conf × 0.7), never `Coasted`.
  Rules per the C.3 table: `ShaftParallelBack` (new, P2 proper), `Delivery`/`Release` from
  `θ ≡ 0 (mod π)` crossings (linear interp between samples), `MaxSpeed` from argmax `θ̇·L`,
  Top conditional cross-check (±40 ms, `Measured`, L above ~60 % running median). Monotone-chain
  enforcement; `version = 3`; provenance `Club`. Telemetry: `ppInfo` the pass1→pass2 deltas for
  Delivery/Release (the free cross-validation S5 wants).
- `src/Analysis/swing_analysis.h` — `Phase::ShaftParallelBack = 12` (append-only).
- `src/Analysis/wrist_analyzer.cpp` — stage reorder per C.1: fuse → segment (pass 1) → bounded
  pose → bounded shaft → **refine** → `MetricExtractor::extract` with the refined events →
  shaft-lean series → score. Tempo metrics (`tempoBackswing`, `downswingMs`, `tempoRatio`) join
  the series from the final events — free metrics, catalog entries already specified (§ Metrics).
- `src/Export/swing_doc.cpp` — nothing new (phases/segmentation blocks from G2 carry the refined
  values and version).

**Tests:** `phase_segmenter_test` gains a synthetic shaft track (θ/θ̇/L with foreshortening
collapse at Top, wedge-σ growth in the downswing, Coasted gaps): refined P2/P6/P8 within ±25 ms
of ground truth; must-NOT-fire cases (invalid track, low corr, Coasted-only crossing, collapsed-L
Top) leave pass-1 events untouched.

**Acceptance:** on a synthetic full swing the refined Delivery/Release land within tolerance and
carry `Club` provenance; metric extraction demonstrably reads the refined instants
(`impactShaftLean` phase sample moves with a shifted synthetic Delivery).

## G5 — Vision-only fallback ladder (optional, may defer)

Per design C.4: when no hand/forearm IMU is bound and the shaft track is valid, re-instance the
pass-1 detector structure with vision signals (`|θ̇|` envelope, θ̇ reversal Top, pose2d hand
stillness for Address). Conf capped 0.6; export untouched (full window — the exporter cannot wait
for the heavy stages); replay/metric grids do use the late bounds. Smallest stage by code (the
detectors are parameterized over signal sources after G1), but only valuable for camera-only
sessions — **defer if G0–G4 fill the schedule**; nothing else depends on it.

## G6 — Hardware validation (folds into the pending S5 session)

- Hand-label P1/P2/P4/P7/P8/P10 from video frames on the S5 capture set; regression-test the
  inertial ladder and the shaft refinements against the same labels (±1 frame geometric).
- Telemetry checks: tempo-ratio distribution centring near 3:1 (design A.8); pass1→pass2
  Delivery/Release deltas small and unbiased.
- Tune `SegmentationConfig` thresholds (still/onset/hysteresis) against real waggles and regrips.
- Confirm the post-roll growth captures real finishes (1250 ms budget vs observed decay).

---

## Order & sizing

| Stage | Depends on | Size | Notes |
|---|---|---|---|
| G0 signals | — | S | pure helpers + fuser plumbing |
| G1 ladder | G0 | M | the bulk: 7 detectors + synthetic generator |
| G2 plumbing | G1 | M | pre-stage, post-roll, export/replay/metric trim, persistence |
| G3 bounding | G2 | S | **performance payoff lands here** |
| G4 shaft refine | G3 | M | the v3 content; shaft-gated by design |
| G5 vision-only | G4 | S | optional, deferrable |
| G6 hardware | G3 (min) | M | shares the S5 session |

G0–G3 ship as a coherent unit (v2 behaviour + the perf win) even if G4 waits for S5 confidence
in the shaft signal. Segmentation v2's "no ordering dependency" with ShaftTracker still holds in
reverse: G4 is a consumer of the shipped track, not a precondition for anything.

## Risks

- **Pass-1 accuracy gates the truncation.** A late Address or early Finish clips real swing
  frames out of the pose/shaft scan and the export. Mitigations: the ±150 ms heavy-stage pad,
  the 250 ms bound pads, conf-gated fallback to full window, and the G6 labelled goldens.
- **Post-roll growth changes shot-detection timing behaviour** (impact sits later in the ring;
  the processor is busy ~750 ms longer per shot). The arbiter refractory (1.5 s) still exceeds
  the post-roll; verify no re-trigger window opens.
- **Shaft refinement on unproven hardware signal** — bounded by design: validity + corr +
  flag gates, conf rules, monotone chain, and the may-defer split at G4.
- **Pre-stage adds a serial ~ms hop before both workers** — negligible vs the seconds saved,
  but keep it off the GUI thread (it reads ring memory like any worker).

## Out of scope

Learned event detectors, club IMU (I2), per-athlete threshold auto-tuning, DTL-camera events —
all per the v2/v3 addenda deferred lists.
