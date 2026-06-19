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

#include <opencv2/core.hpp>

#include <vector>

// Per-frame club-shaft detection — the anchored radial transform (ShaftTracker
// stage S1). Pure math: no Qt, no SwingWindow access, core OpenCV imgproc only
// (no ximgproc/contrib). Unit-tested standalone in
// src/Analysis/tests/shaft_tracker_test.cpp; the temporal track assembly and
// app hook live in ShaftTracker (stage S2).
//
// A club shaft is a thin straight ridge with one endpoint pinned to the hands,
// so the 4-DOF line-segment search collapses to a 1-DOF angle search about a
// known anchor (the pose-estimated grip point). Morphological top-hat and
// black-hat maps give isotropic thin-structure responses for bright (steel)
// and dark (graphite) shafts; a polar ray scan about the grip (sampled
// straight off the Cartesian response maps — no warpPolar/remap pass, see the
// .cpp) turns a shaft into one θ column. Per-θ columns are scored by the
// longest supra-threshold run that starts near the anchor — mean ridge
// strength × coverage, with run length rewarding long support — which
// inherently rejects the classic golf false positives: the shaft's ground
// shadow (a strong line, but through the clubhead, not the grip), alignment
// sticks and mat edges (not radial through the grip), and Hough's 180°
// direction ambiguity (ρ is integrated on one side of the grip only).
//
// Motion blur near impact fans the shaft into a wedge centred on the grip — a
// *plateau* in S(θ), read as signal, not noise: the plateau centroid is the
// mid-exposure shaft angle, its half-width a direct measurement of ω·t_exp
// (reported as the candidate's σ_θ). The forearm IS a radial ridge into the
// anchor and is removed by a hard angular mask; every other prior (inter-hand
// direction, temporal/IMU prediction) weights S(θ) softly and never gates a
// measurement. Anchor error (the true grip sits below the wrist midpoint and
// pose jitters) is absorbed by rescoring over a 3×3 grid of anchor
// perturbations plus a final total-least-squares line refit over the winning
// candidate's proximal ridge pixels.
//
// See docs/design/shot_analyzer_design.md addendum B.2–B.4 (algorithm) and
// B.9 (validation tolerances).

namespace pinpoint::analysis {

struct ShaftDetectConfig {
    // --- Search geometry ---
    float maxRadiusPx    = 400.f;  // search radius ≈ 1.25 · clubLen · px/m (caller computes)
    float rhoMinPx       = 30.f;   // skip grip/glove clutter (≈ 1.5 hand-widths)
    int   thetaBins      = 720;    // angular resolution, 0.5°/bin
    int   ridgeKernelPx  = 9;      // top/black-hat ellipse kernel. Must be strictly wider
                                   // than the widest shaft image (2–6 px core + AA/defocus
                                   // fringe) or opening retains the proximal shaft and the
                                   // ridge response dies exactly where the run must start.

    // --- Candidate selection ---
    int   maxCandidates    = 5;    // top-K peaks handed to temporal association (S2)
    float nmsSeparationDeg = 5.f;  // min angular separation between candidates
    float clutterMaskDeg   = 12.f; // hard-mask half-width around g→elbow directions
    float minVisibleLenPx  = 30.f; // runs shorter than this are noise, never candidates
    float minScoreFrac     = 0.10f;// drop candidates below this fraction of the best peak

    // --- Run extraction (per-θ column scan) ---
    float runStartGapPx = 15.f;    // run must begin within this of rhoMinPx (anchored!)
    float runMaxGapPx   = 20.f;    // internal sub-threshold gap bridged (torso crossing)

    // --- Adaptive ridge threshold ---
    float noiseSigmaK    = 4.f;    // threshold = median + K · 1.4826 · MAD of the response
    float thresholdFloor = 8.f;    // absolute minimum threshold (8-bit response units)

    // --- Soft priors (multiplicative weights — never hard gates) ---
    float interHandSigmaDeg = 25.f;// width of the inter-hand-direction bump (worth ±15–20°)
    float priorFloor        = 0.3f;// soft priors never scale a column below this factor

    // --- Motion-blur wedge (B.4) ---
    float wedgeMinSpanDeg = 3.f;   // supra-half-peak span beyond this ⇒ blur fan, not a line
    float wedgePairMaxSepDeg = 12.f; // twin edge-ridge peaks within this merge into one wedge
                                   // (a fan whose solid interior exceeds the ridge kernel
                                   // responds at its two edges, not as one plateau)

    // --- Anchor-error tolerance ---
    float anchorPerturbPx  = 3.f;  // 3×3 grid step for anchor rescoring (0 disables)
    float headSeedRadiusPx = 10.f; // blob-centroid disc at the ridge terminus (clubhead seed)

    // --- Skeleton-aware enhancement (R5/R6/R8) — caller-set per frame; defaults
    //     reproduce current behaviour, and the consuming code lands in later
    //     phases (K1/K2/K4). See docs/implementation/shaft_detection_skeleton_impl.md.
    float minShaftLenPx   = 30.f;  // R5: arm-relative length floor (= max(minVisibleLenPx, minLenFracOfArm·armPx))
    bool  hasEnvelope     = false; // R6: kinematic angle guardrail active this frame
    float envelopeKSigma  = 3.0f;  //     soft-penalty arc half-width = k·σ_β
    float envelopeHardK   = 4.0f;  //     hard-reject only beyond this AND wrong side
    bool  blurMode        = false; // R8: high-ω fan-integration mode this frame
    float predFanHalfRad  = 0.f;   //     predicted wedge half-width = 0.5·ω̂·t_exp
    float blurThreshScale = 0.5f;  //     threshold multiplier inside the envelope in blur mode
    float blurSnrMargin   = 1.6f;  //     in-envelope fan must beat the out-of-envelope noise peak ×this
};

struct AnchorPrior {
    cv::Point2f gripPx;                 // pose-estimated grip anchor (image px)
    bool  hasInterHandDir = false; float interHandDirRad = 0.f;  // soft weight toward this θ
    bool  hasPredictedTheta = false; float predictedThetaRad = 0.f, predictedSigmaRad = 0.f;
    int   numElbowDirs = 0; float elbowDirRad[2] = {0.f, 0.f};   // hard clutter mask
    // R6 kinematic direction (lead-arm → club, wrist-cock model): the predicted
    // club angle φ_club_pred ± σ_β. When set, supersedes the inter-hand bump as
    // the directional prior and (with cfg.hasEnvelope) bounds the angle guardrail.
    // Consumed from the K2 phase; defaults leave current behaviour unchanged.
    bool  hasKinematicDir = false; float kinematicDirRad = 0.f, kinematicSigmaRad = 0.f;
    int   armSide = 0;                  // +1 club expected trail-side of arm, −1 lead-side, 0 unknown
};

struct ShaftCandidate {
    float thetaRad     = 0.f;   // ray direction from grip, image convention atan2(dy,dx), sub-bin refined
    float sigmaThetaRad= 0.f;   // measurement σ (wedge half-width when wedge=true, else bin-level)
    float visibleLenPx = 0.f;   // supra-threshold ridge extent along the ray
    float score        = 0.f;   // mean ridge strength × coverage, prior-weighted
    cv::Point2f headPx;         // ridge-terminus blob centroid (clubhead seed), image px
    bool  wedge        = false; // plateau (motion-blur fan), θ = plateau centroid
    bool  darkPolarity = false; // black-hat (graphite) vs top-hat (steel)
};

// Detect shaft candidates in one luma frame (CV_8UC1) about the grip anchor.
// Returns up to cfg.maxCandidates candidates sorted by descending score
// (empty when nothing plausible is found — never fabricates a measurement).
std::vector<ShaftCandidate> detectShaft(const cv::Mat &luma,
                                        const ShaftDetectConfig &cfg,
                                        const AnchorPrior &prior);

} // namespace pinpoint::analysis
