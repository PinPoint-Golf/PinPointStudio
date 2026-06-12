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

#include <QPointF>
#include <QQuaternion>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include "types.h"                 // pinpoint::SourceId
#include "shaft_tracker_math.h"    // ShaftCandidate
#include "swing_analysis.h"        // ShaftSample2D / ShaftTrack2D / flags (canonical shapes)

// Shaft track assembly — turns per-frame anchored-radial candidates into ONE
// smooth, unwrapped shaft-angle track (ShaftTracker stage S2). Pure over plain
// vectors — no SwingWindow access — so the whole pipeline is unit-testable
// standalone (src/Analysis/tests/shaft_track_test.cpp); the window-driving
// orchestration lives in ShaftTracker (shaft_tracker.h).
//
// Three stages, per the design addendum (shot_analyzer_design.md B.5):
//
//  1. ŝ_hand auto-calibration (B.2). Once gripped, the shaft direction is a
//     CONSTANT unit vector in the lead-hand sensor/anatomical-body frame.
//     Over slow, confident frames we fit (ŝ, sign, δ) minimising
//     Σ wrap²(θ_meas − sign·θ_proj(q·ŝ·q⁻¹) − δ): δ absorbs the constant
//     in-plane rotation between the IMU anatomical world and the image
//     (yaw misalignment, camera roll), sign ∈ {±1} absorbs a mirrored
//     chirality (mirroring is not a rotation — no δ can express it). The fit
//     residual becomes the channel's measurement σ; a residual above the
//     acceptance gate disables the channel entirely (degrade, never
//     fabricate). Re-gripping moves ŝ, so the fit is per-shot, never persisted.
//
//  2. Viterbi association. Per-frame top-K candidates + a "missing"
//     hypothesis; node cost from per-frame-normalised scores, transition cost
//     from the deviation of Δθ against the IMU-predicted delta (calibrated)
//     or the path's own velocity trend (vision-only), σ growing with the gap.
//     A strong clutter ridge must pay the jump-out AND jump-back deviation,
//     which is exactly what a real shaft track never does.
//
//  3. Wrap-aware fusion. A constant-acceleration Kalman filter on [θ, θ̇, θ̈]
//     (white-noise jerk) with TWO scalar measurement channels — vision θ
//     (R from the candidate's σ_θ: wedge half-width on blur frames) and the
//     IMU-predicted θ (R from the fit residual) — followed by the
//     Rauch–Tung–Striebel backward smoother over the whole window (offline ⇒
//     zero lag at impact). Each wrapped measurement is lifted into the
//     continuous domain against the filter's own prediction
//     (z = x̂₀ + wrap(θ_z − x̂₀)), so unwrapping is velocity-aware and gaps
//     during the fast downswing cannot alias. Frames with neither channel are
//     predict-only — never a fabricated measurement.
//
// Rotation stays quaternion (QQuaternion) end-to-end; θ here is a genuine 2-D
// image-plane scalar, which the quaternions-only rule explicitly permits.
namespace pinpoint::analysis {

// ---------------------------------------------------------------------------
// Output track: ShaftSample2D / ShaftTrack2D / ShaftSampleFlags live in
// swing_analysis.h (canonical, Qt-only — consumers of SwingAnalysis never see
// the OpenCV-typed inputs below).

// ---------------------------------------------------------------------------
// Assembly inputs

// One frame's detection output + the anchor it was detected about. qHand is
// the lead-hand anatomical quaternion (FusedStreams::qAnat) sampled at t_us;
// qHandValid is false outside IMU coverage or when no lead-hand IMU is bound.
struct ShaftFrameObs {
    int64_t t_us = 0;
    cv::Point2f gripPx;
    std::vector<ShaftCandidate> candidates;   // sorted by descending score; may be empty
    QQuaternion qHand;
    bool qHandValid = false;
};

struct ShaftInHandFit {
    bool      ok = false;
    cv::Vec3f sHand{1.f, 0.f, 0.f};  // unit shaft direction in the hand frame
    double    sign        = 1.0;     // +1 normal, −1 mirrored chirality
    double    offsetRad   = 0.0;     // constant image-angle offset δ
    double    residualRad = 0.0;     // RMS fit residual → IMU channel measurement σ
    int       framesUsed  = 0;
};

struct AssemblyConfig {
    // --- ŝ_hand calibration ---
    int    calibMinFrames     = 8;      // fewer eligible slow frames ⇒ no fit
    double calibMinSpanRad    = 0.26;   // ≥15° of θ variation or the fit is unidentifiable
    double calibSlowRateRadS  = 3.0;    // |θ̇| above this ⇒ frame not "slow"
    double calibAcceptRad     = 0.12;   // ≈7°: residual above this ⇒ channel disabled

    // --- Viterbi association ---
    double missingPenalty     = 3.0;    // node cost of the missing hypothesis
    double nodeScoreFloor     = 0.05;   // score/bestInFrame clamped here before −log
    double transSigmaBaseRad  = 0.07;   // ≈4°: per-transition deviation floor
    double transAccSlackRadS2 = 800.0;  // vision-only: trend deviation grows 0.5·α·Δt²
    double transAccSlackImu   = 200.0;  // with IMU prediction the slack is much tighter
    double transNoRateExtraRad= 0.35;   // ≈20°: first transition has no velocity trend yet
    double lenSigmaFrac       = 0.30;   // ΔL deviation: σ = frac·max(L) + lenSigmaFloorPx
    double lenSigmaFloorPx    = 15.0;

    // --- Kalman / RTS fusion ---
    double jerkPsd            = 1.0e6;  // white-jerk PSD (rad²/s⁵) — clubhead-grade
    double visionSigmaFloorRad= 0.005;  // ≈0.3°: candidate σ_θ never trusted below this
    double imuSigmaFloorRad   = 0.026;  // ≈1.5°: fit residual never trusted below this
    double confSigmaRefRad    = 0.17;   // ≈10°: conf = 1 − σ_post/ref (clamped)

    // --- Validity gate ---
    double coverageMin        = 0.60;   // Measured|ImuBridged fraction over the span
};

// ---------------------------------------------------------------------------

class ShaftTrackAssembly {
public:
    // Stage 1 — fit the constant shaft-in-hand vector over slow, confident,
    // non-wedge frames (uses each frame's best candidate). ok=false when too
    // few eligible frames, too little angular variation, or the residual
    // fails the acceptance gate.
    static ShaftInHandFit calibrateShaftInHand(const std::vector<ShaftFrameObs> &obs,
                                               const AssemblyConfig &cfg);

    // Wrapped image-angle prediction for one frame from a fit:
    // θ = wrap(sign·atan2(−s_w.z, s_w.x) + δ), s_w = q·ŝ·q⁻¹. The projection
    // assumes the face-on orthographic convention (anatomical X → image +x,
    // Z → image −y, depth along Y); any constant in-plane error is absorbed
    // by δ and chirality by sign, both estimated by the fit itself.
    static double predictTheta(const ShaftInHandFit &fit, const QQuaternion &qHand);

    // Stage 2 — Viterbi over candidates + missing. Returns one selected
    // candidate index per frame (−1 = missing). imuTheta holds the UNWRAPPED
    // per-frame channel (empty or NaN entries when uncalibrated).
    static std::vector<int> associate(const std::vector<ShaftFrameObs> &obs,
                                      const std::vector<double> &imuThetaUnwrapped,
                                      const AssemblyConfig &cfg);

    // Stage 3 — wrap-aware 2-channel KF + RTS over the association.
    // spanStart/spanEnd bound the coverage gate (the swing span when phases
    // are known, else the full obs range).
    static ShaftTrack2D smooth(const std::vector<ShaftFrameObs> &obs,
                               const std::vector<int> &selected,
                               const std::vector<double> &imuThetaUnwrapped,
                               double imuSigmaRad,
                               int64_t spanStartUs, int64_t spanEndUs,
                               const AssemblyConfig &cfg);

    // SwingLab trace: the assembly's internals, filled when a sink is passed
    // (production passes nothing). Lets the offline runner dump WHY a track
    // came out the way it did (fit refusal, association choices).
    struct AssemblyTrace {
        ShaftInHandFit      fit;
        std::vector<int>    selected;            // per obs frame; −1 = missing
        std::vector<double> imuThetaUnwrapped;   // NaN where channel absent
    };

    // The full pipeline: calibrate → build the unwrapped IMU channel →
    // associate → smooth → health metrics. This is what ShaftTracker calls.
    static ShaftTrack2D assemble(const std::vector<ShaftFrameObs> &obs,
                                 int64_t spanStartUs, int64_t spanEndUs,
                                 const AssemblyConfig &cfg = {},
                                 AssemblyTrace *trace = nullptr);

    // Unwrapped per-frame IMU channel from a fit (NaN where qHand is invalid
    // or fit.ok is false). Exposed for tests and for the health metric.
    static std::vector<double> imuThetaChannel(const std::vector<ShaftFrameObs> &obs,
                                               const ShaftInHandFit &fit);
};

} // namespace pinpoint::analysis
