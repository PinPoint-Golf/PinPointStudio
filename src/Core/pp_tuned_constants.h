/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <cstdint>

// SINGLE SOURCE OF TRUTH for analysis-pipeline parameters that are tuned during
// the validation programme and then FROZEN (docs/validation/pipeline_validation_and_tuning.md
// §2.4). When validation locks a value, edit it HERE and every consumer — live IMU
// path AND offline analyzer — picks it up; the tuned_constants_parity_test guards that
// the indirection stays byte-identical to the historical defaults.
//
// These are the DEFAULTS. SwingLab sweeps them at run time via the dotted-key
// `tuningOverrides` mechanism (analysis_tuning.h) without rebuilding — this header is
// not consulted on the override path, only for the baseline value an override starts from.
//
// LAYERING: src/Core is the lowest common layer (both src/IMU and src/Analysis already
// include from it). To keep `orientation_filter.h` Qt-free and standalone-unit-testable,
// this header carries ONLY numeric literals (no Qt types, no module types). Quaternions
// are expressed as {w, x, y, z} float arrays; imu_calibration.h builds the QQuaternion.
//
// NOT here (deliberately): the wrist axis SIGN conventions and ZXY decomposition in
// src/Analysis/wrist_angles.h — those are sign/axis CHOICES baked into code structure,
// not single numeric literals, and stay at their source of truth. The full reference-band
// lo/hi corridor arrays stay in reference_bands.cpp (a Corpus-2 re-seat, not a freeze).

namespace pinpoint::tuned {

// --- Orientation filter (src/IMU/orientation_filter.h) -----------------------------
namespace filter {
// Madgwick gradient-descent gain: higher trusts the accelerometer more. ~0.03–0.1
// typical for consumer MEMS. The phase-adaptive schedule (validation §5.3.1) starts
// from this as its static-phase value.
inline constexpr float kBeta = 0.05f;
} // namespace filter

// --- Stillness-gated seed tolerances (src/IMU/imu_base.h) --------------------------
namespace seed {
inline constexpr float kInitAccelTolG      = 0.15f;   // |‖a‖ − 1g| within this ⇒ "still"
inline constexpr float kInitGyroMaxRadps   = 0.5f;    // ~28 °/s gyro magnitude ceiling
inline constexpr int   kInitMaxSeedAttempts = 200;    // ~1 s @200 Hz, ~2 s @100 Hz fallback
} // namespace seed

// --- Anatomical calibration (src/IMU/imu_calibration.h) ----------------------------
namespace calib {
// Functional-axis orthogonality gate: the two solved joint axes are anatomically ~90°;
// outside this band the calibration capture was poor and is rejected.
inline constexpr float kAxisAngleMinDeg = 60.0f;
inline constexpr float kAxisAngleMaxDeg = 120.0f;

// Nominal mount quaternions {w, x, y, z} (anatomical-segment-body -> sensor-body, M).
// Arm strap convention (gravity-pinned signs, 2026-05-31); all three arm segments share it.
inline constexpr float kNominalArmMount[4]  = { 0.5f, -0.5f, -0.5f, -0.5f };
// Hand (dorsal) mount, solved numerically from a characterization capture.
inline constexpr float kNominalHandMount[4] = { 0.4388f, 0.6054f, 0.4965f, -0.4409f };
} // namespace calib

// --- Swing scoring bands + deadbands (src/Analysis/swing_scorer.cpp) ----------------
namespace scoring {
// Deadband + bounded falloff (design §B.1): |z| ≤ kZIn ⇒ 100; ramps to ~0 at kZOut.
inline constexpr double kZIn       = 1.0;
inline constexpr double kZOut      = 3.0;
inline constexpr double kFalloffPow = 2.0;

// PROVISIONAL Wrist (session type 1) reference bands — μ, σ, oneSidedDir, weight.
// See docs/reference/wristmetrics.md; locked against HackMotion in Corpus 2.
namespace bands {
inline constexpr double kFlexExtMu = 15.0, kFlexExtSigma = 12.0, kFlexExtWeight = 0.45;
inline constexpr int    kFlexExtOneSided = +1;   // penalise BELOW μ (cupping)
inline constexpr double kRadUlnMu  = 0.0,  kRadUlnSigma  = 12.0, kRadUlnWeight  = 0.15;
inline constexpr int    kRadUlnOneSided = 0;     // two-sided
inline constexpr double kPronationMu = 0.0, kPronationSigma = 25.0, kPronationWeight = 0.20;
inline constexpr int    kPronationOneSided = 0;  // two-sided
inline constexpr double kArmFlexionMu = 5.0, kArmFlexionSigma = 12.0, kArmFlexionWeight = 0.20;
inline constexpr int    kArmFlexionOneSided = -1; // penalise ABOVE μ (bent lead arm)
} // namespace bands

// PROVISIONAL per-archetype lead-wrist FE resemblance centres (design §B.0a; validation
// §5.6/§6.3). Flexion-positive, neutral-relative degrees, sampled at Top and Impact. v1
// scores FE only. σ_p is the pattern's COACHING TOLERANCE (its natural spread), NOT sensor
// noise (§B.7). Externally anchored to HackMotion published tour ranges (top −30/+5, impact
// −15/−40, extension-positive → ×−1 to flexion-positive here): the bowed centre IS the tour
// reference; neutral and cupped are extrapolated away from it. NOT FINAL — re-seated against
// the corpus + HackMotion concurrent capture at Corpus 2; frozen (not swept) until then.
namespace resemblance {
inline constexpr double kBlendedDeltaPts = 10.0;   // top-two within this ⇒ "blended"
inline constexpr double kBowedMuTop   =  13.0, kBowedMuImpact   =  27.0, kBowedSigma   = 18.0;
inline constexpr double kNeutralMuTop =  -8.0, kNeutralMuImpact =   5.0, kNeutralSigma = 18.0;
inline constexpr double kCuppedMuTop  = -30.0, kCuppedMuImpact  = -18.0, kCuppedSigma  = 18.0;
} // namespace resemblance

// Score measurement-uncertainty budget (design §B.7) — the per-cell angle error that
// PROPAGATES INTO a score interval, kept strictly separate from the band σ (which is
// coaching tolerance). σ_sensor + σ_crosstalk are the FE error floor; the timing term is
// dθ/dt × phase-timing jitter, inflated by low phase confidence (so low confidence WIDENS
// the interval, never moves the central score). σ_crosstalk is the ~10–15° systematic
// FE↔RUD leak carried conservatively as uncertainty until Corpus 2 localises it (C4).
namespace uncertainty {
inline constexpr double       kSensorSigmaDeg    = 6.0;     // IMU FE noise (~5–8°)
inline constexpr double       kCrosstalkSigmaDeg = 12.0;    // FE↔RUD leak (~10–15°), until Corpus 2
inline constexpr std::int64_t kTimingSigmaUs     = 10000;   // phase-timing jitter (~10 ms) × dθ/dt
inline constexpr double       kConfInflate       = 1.5;     // σ_x ×= 1 + (1−conf)·this — low conf widens
inline constexpr double       kIntervalSigmas    = 1.0;     // coverage factor on σ(d²)
} // namespace uncertainty
} // namespace scoring

// --- Wrist-angle windowed-median sampler (src/Analysis/wrist_angle_sampler.h) -------
namespace sampler {
inline constexpr std::int64_t kWindowHalfUs       = 15000;  // ±15 ms about Pn
inline constexpr double       kGimbalThresholdDeg = 75.0;   // pitch-proxy ≥ this ⇒ Indeterminate
inline constexpr int          kMinValidSamples    = 1;      // fewer in window ⇒ Gap
} // namespace sampler

// --- Assessment rule engine (src/Analysis/assessment_rule.h) ------------------------
namespace rules {
inline constexpr float  kConfidenceFloor              = 0.45f; // below ⇒ lowConfidence (demoted)
inline constexpr double kScoreScale                   = 18.0;  // score v2 penalty scale
inline constexpr double kSeverityWeightFault          = 1.0;
inline constexpr double kSeverityWeightWatch          = 0.5;
inline constexpr double kCorroborationBoost           = 0.30;  // confidence add when corroborated
inline constexpr bool   kStrengthsRequireAdjacentFault = true;
// Discrimination thresholds (validation C1 / A.5 #15) — the most behaviourally-decisive cut
// points, lifted out of the .cpp so they are one source of truth + parity-guarded + sweepable.
// FROZEN until labels exist (supervised fault-rule calibration; SwingLab refuses score.*/rules.*).
inline constexpr double kFlipFaultDeg          = -8.0;  // F3: P6→P7 FE drop ≤ this ⇒ Fault
inline constexpr double kFlipWatchDeg          = -5.0;  // F3: ≤ this ⇒ Watch
inline constexpr double kTrailFlattenDeg       = -8.0;  // flip corroboration: trail-wrist P6→P7 drop
inline constexpr double kArchetypeTopDeltaDeg  = 10.0;  // detectArchetype: |FE Δ@Top| ⇒ bowed/cupped
inline constexpr double kArchetypeFaceOffsetDeg = 10.0; // archetype face-corridor shift (±)
} // namespace rules

// --- Offline pose accuracy: person crop + DARK decode (wholebody_pose_design.md §3) ---
// WB1 upgrades the OFFLINE ViTPose pass only (PoseRunner); the live 60 Hz MoveNet
// path is untouched. Both upgrades default ON; setting crop.kEnabled AND decode.kDark
// false via "pose.*" overrides reproduces the pre-WB1 full-frame + argmax pipeline
// byte-for-byte (the WB1 parity gate). Consumed by PoseAccuracyConfig::fromOverrides
// (src/Analysis/pose_crop.h) and PoseRunner.
namespace pose {
// Offline ViTPose ORT intra-op thread count (PoseRunner → PoseEstimatorViTPose::load).
// The offline pose pass is 70%+ of analysis wall-time on CPU hosts, so pool sizing
// matters. THREE-WAY resolution via the "pose.intraOpThreads" dotted key:
//   > 0   → pin exactly this many intra-op threads (manual override)
//   == -1 → topology auto: clamp(pinpoint::physicalCoreCount(), 1, 16) from
//           src/Core/cpu_topology.h — OPT-IN, deliberately NOT yet the default (a
//           determinism A/B on the affected hardware — no-SMT, hybrid P/E-core,
//           >16-logical — is owed before it can become the default)
//    0    → (DEFAULT) today's proxy heuristic clamp(hardware_concurrency()/2, 1, 8),
//           left UNCHANGED so the default path stays byte/thread-count-identical to
//           the historical behaviour
// The live 60 Hz MoveNet path is untouched (pinned at 1 in its own estimator).
inline constexpr int kIntraOpThreads = 0;   // pose.intraOpThreads (0 = legacy auto)

namespace crop {
inline constexpr bool   kEnabled       = true;
inline constexpr double kMarginFrac    = 0.15;   // bbox expansion each side (arms/club headroom)
inline constexpr double kMaxAreaFrac   = 0.90;   // crop ≥ this fraction of frame area ⇒ no gain ⇒ full-frame
inline constexpr int    kMinBboxFrames = 3;      // < this many contributing bbox frames ⇒ full-frame fallback
} // namespace crop
namespace decode {
inline constexpr bool kDark = true;              // DARK sub-pixel decode (else argmax + ±0.25)
} // namespace decode
// Per-group confidence-threshold scales (design §3.4). REGISTERED here so a
// "pose.confScale.*" sweep resolves to a value; WB1 wires NO consumer (the
// wholebody-group consumers land in WB2/WB3). Body thresholds stay frozen (×1.0).
namespace confScale {
inline constexpr double kFeet  = 1.0;
inline constexpr double kFace  = 1.0;
inline constexpr double kHands = 1.0;
} // namespace confScale
// WB4 hand consumers (wholebody_pose_design.md §2.2). Both ship DARK: with the
// defaults below the pipeline output is byte-identical to the pre-WB4 tree.
namespace grip {
// Recompute leadHand/trailHand/handConf from the SMOOTHED hand keypoints after
// the RTS smoother, so the grip anchor ShaftTracker consumes inherits the
// smoother's honesty tiers. false ⇒ smoothed hands copied through unchanged.
inline constexpr bool kFromSmoothedHands = false;   // pose.gripFromSmoothedHands
} // namespace grip
namespace wristAngles {
inline constexpr bool   kEnabled         = false;   // pose.wristAngles.enabled — IMU-less pose source
inline constexpr double kConfMin         = 0.30;    // per-keypoint conf gate (codebase-wide)
inline constexpr double kApparentPenalty = 0.5;     // camera-plane apparent-angle confidence factor
                                                    //   (× min endpoint conf) — these are PROJECTED,
                                                    //   not anatomical, angles so trust is halved
} // namespace wristAngles
} // namespace pose

// --- Head tracking (WB2 — src/Analysis/head_track.h) --------------------------
// Head position/tilt from the COCO-WholeBody head keypoints (nose/eyes/ears +
// optional chin). Consumed by HeadTrackConfig::fromOverrides via "head.*" dotted
// keys. FROZEN defaults; SwingLab sweeps them without rebuild.
namespace head {
inline constexpr double  kConfMin        = 0.30;    // per-keypoint conf gate (codebase-wide)
inline constexpr double  kEarIpdFactor   = 1.8;     // inter-ear ≈ 1.8× inter-eye (anatomical bi-tragion
                                                    //   vs inter-pupillary ratio) — head-scale fallback
inline constexpr double  kEarWidthMm     = 145.0;   // nominal inter-ear (bi-tragion) breadth, mm — the
                                                    //   head-plane px→mm ruler for head sway/lift until
                                                    //   2D camera calibration lands (head.earWidthMm)
inline constexpr double  kChinConfWeight = 0.0;     // chin (kp 31) centroid weight when confident; 0 ⇒
                                                    //   OFF (body 0–4 only — face channels may be noisy)
inline constexpr int     kMinContribPts  = 2;       // min confident head kps for a valid head centre
inline constexpr int     kAddrMinFrames  = 5;       // fallback address ref = first N valid frames
inline constexpr std::int64_t kAddrWindowUs = 250000; // ±window about the Address event for the robust ref
} // namespace head

// --- Ball stance corridor v2 (WB3 — src/Analysis/ball_runner.cpp) -------------
// Toe/heel span + ground line replace the ankle-based v1 corridor when foot
// keypoint coverage is sufficient (wholebody_pose_design.md §2.1/§5). Consumed
// by BallCorridorConfig::fromOverrides via "ball.corridor.*" dotted keys.
// kUseFeet=false, or too few feet-confident frames, falls back VERBATIM to the
// v1 ankle path — byte-identical on legacy 17-kp tracks (conf[17..] == 0).
namespace ball {
namespace corridor {
inline constexpr bool   kUseFeet     = true;   // false ⇒ ankle path only (pre-WB3 behaviour)
inline constexpr double kFootConfMin = 0.30;   // per-keypoint conf gate (codebase-wide convention)
inline constexpr int    kMinFrames   = 5;      // < this many feet-confident frames ⇒ ankle fallback
} // namespace corridor
} // namespace ball

// --- Setup + footwork metrics (WB3 — src/Analysis/foot_metrics.h) ------------
// Stance width / per-foot flare / toe-line angle (address) + the lead-heel-lift
// trace, from the COCO-WholeBody foot keypoints (bigtoe/heel). Consumed by
// FootMetricsConfig::fromOverrides via "foot.*" dotted keys. FROZEN defaults;
// SwingLab sweeps them without rebuild. Mirrors head:: exactly (same defaults
// for the shared conf-gate / address-reference-window shape).
namespace foot {
inline constexpr double       kConfMin       = 0.30;    // per-keypoint conf gate (heel + bigtoe)
inline constexpr int          kAddrMinFrames = 5;       // fallback address ref = first N valid frames
inline constexpr std::int64_t kAddrWindowUs  = 250000;  // ±window about the Address event for the robust ref
} // namespace foot

} // namespace pinpoint::tuned
