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

// Segment angular-speed series and the kinematic sequence — one producer, three routes.
// See docs/design/kinematic_sequence_design.md.
//
// ── The common measure (design §3) ──────────────────────────────────────────
//
// Four signed series, °/s, whichever sensor produced them:
//
//   pelvisAngularSpeed   bearing rate of the hip line in the horizontal plane; + = opening (toward
//   thoraxAngularSpeed   bearing rate of the shoulder line, same axis, same sign        the lead side)
//   leadArmAngularSpeed  angular speed of the lead shoulder→wrist axis about the swing-plane normal
//   clubAngularSpeed     angular speed of the shaft about the swing-plane normal
//
// The arm and club are magnitudes of the long axis's swing (roll about the long axis excluded by
// construction on every route); the pelvis and thorax are signed so the derivative is continuous
// through square — the fold that killed every rate across impact when the LEVEL was unsigned.
//
// ── The routes, resolved PER SEGMENT (design §4–5) ──────────────────────────
//
//   IMU        a bound Pelvis / Thorax / LeadForearm (LeadUpperArm preferred) / Club stream —
//              gyro projected onto the axis, no differentiation. Direct.
//   pair       PELVIS AND THORAX ONLY, routeId "faceOn+dtl". The SIGNED HORIZONTAL SEPARATION of
//              the hip / shoulder keypoints in BOTH views, paired on the window clock:
//              ψ = atan2(d_dtl / r, d_faceOn), with r the pixel-scale ratio between the views.
//              For a line of length W tilted τ out of horizontal and turned ψ about vertical,
//              d_fo = s_F·W·cos τ·cos ψ and d_dtl = s_D·W·cos τ·sin ψ — the TILT and W cancel in
//              the ratio, which a 2-D span distance does not give you, so the route needs no
//              reference width, no square-up inference and no sign unfold, and its sensitivity
//              is uniform through square (there is no blind band). Estimated: r is measured from
//              the body's vertical extent rather than calibrated, and nothing has truthed it.
//              ⚠ THE DOWN-THE-LINE LEG MUST BE SIGNED. An unsigned span has a V at square, and
//              unfolding it at that minimum inserts a step into ψ that the 25 ms derivative reads
//              as an ~800 °/s peak one grid step later — on 21 of 21 measured swings. See
//              docs/research/data/kinematic_sequence/pair_span_turn_20260920.md §3.1, and the
//              G1 case in segment_rates_test §9.
//   skeleton   routeId "faceOn+dtl3d" (ks_skeleton3d_route_design.md §5). PELVIS / THORAX: the hip
//              and shoulder lines' bearing in the horizontal plane, differentiated, BELOW the pair
//              and above the face-on span (skeleton_rate_k0_20261002.md §11).
//              LEAD ARM (live): the shoulder → wrist direction from skeleton3d's two-camera fit,
//              its angle in the arm's OWN downswing plane (the least-variance axis of the
//              directions over the domain), differentiated. Both Estimated: the fit's cameras are
//              assumed until calibrated, a gain error carried on the PEAK VALUE's σ
//              (scaleFrac·|peak|), never on its instant. The trunk sits below the pair because on
//              07-04 it never placed a node the pair did not and carried 3–6× its timing σ (§11).
//   face-on    arm and club: the image angle DE-PROJECTED through the shaft-plane conic's axis
//              ratio k and node bearing ν (tan(ψ−ν) = k·tan α), then differentiated. Estimated,
//              but the correction factor is bounded in [k, 1/k] and the timing barely moves.
//              pelvis and thorax: the span cosine UNFOLDED across the downswing's span maximum
//              (closed positive before square, open after), differentiated, with σ propagated
//              through 1/sin θ and floored. Estimated — and honestly so: near square a single
//              horizontal camera has no sensitivity at all, which the σ says and the verdict
//              respects. Design §2 and §9 are the record of why this rung exists at all.
//
// Everything after "an angle with a σ per sample" is angular_rate.h — one derivative, one peak
// finder, one timing σ — which is what makes a pelvis node from an IMU comparable to a thorax
// node from a camera.
//
// Pure, deterministic, Qt-only (no OpenCV / no Qt-GUI); unit-tested standalone with no fixture.

#include <QString>
#include <QVariantMap>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Analysis/swing_analysis.h" // PoseTrack2D, ShaftTrack2D, MetricSeries, PhaseEvent, SegmentRole
#include "../IMU/imu_vision_fuser.h" // FusedStreams, SegmentStream
#include "kinematic_sequence.h"      // KsNode, KinematicSequence
#include "../Analysis/analysis_tuning.h" // tuning::apply
#include "../Core/pp_tuned_constants.h"   // tuned::sequence::

namespace pinpoint::analysis {

// Sequence knobs. Defaults track the frozen constants (pp_tuned_constants.h sequence::);
// SwingLab sweeps them via "sequence.*" dotted keys.
struct SegmentRatesConfig {
    bool    enabled          = tuned::sequence::kEnabled;           // sequence.enabled
    double  derivWindowMs    = tuned::sequence::kDerivWindowMs;     // sequence.derivWindowMs — SG window, in TIME
    double  maxPlaceSigmaMs  = tuned::sequence::kMaxPlaceSigmaMs;   // sequence.maxPlaceSigmaMs — wider ⇒ unresolved
    double  sigmaK           = tuned::sequence::kSigmaK;            // sequence.sigmaK — gap must exceed k·σ to order
    double  confMin          = tuned::sequence::kConfMin;           // sequence.confMin — keypoint admission
    // THE FACE-ON ARM IS NOT READ WHILE IT IS FOLDED OR HIDDEN (2026-10-19). In the follow-through the
    // lead arm folds across the body: its image length falls to 40–65 % of impact's and the wrist
    // half-hides, the de-projected angle stops meaning anything, and the rate ran to 1,250–2,600 °/s
    // on the 22 library swings whose "peak" landed after impact. A sample is unseen when either
    // keypoint is below minConf, or — after impact only — the arm is shorter than minLengthFrac of
    // its length at impact (before impact a short image arm is foreshortening, not folding).
    double  faceOnArmMinConf       = 0.5;    // sequence.faceOnArm.minConf
    double  faceOnArmMinLengthFrac = 0.75;   // sequence.faceOnArm.minLengthFrac (0 = off)
    double  faceOnArmRisingIntoMinFrac = 0.9;   // sequence.faceOnArm.risingIntoMinFrac — PlacementGate::risingIntoMinFrac
    int     addrMinFrames    = tuned::sequence::kAddrMinFrames;     // sequence.addrMinFrames
    int64_t addrWindowUs     = tuned::sequence::kAddrWindowUs;      // sequence.addrWindowUs
    double  spanNoisePx      = tuned::sequence::kSpanNoisePx;       // sequence.spanNoisePx — 1σ of a span
    double  sinFloor         = tuned::sequence::kSinFloor;          // sequence.sinFloor — floors 1/sin θ
    double  minSpanPx        = tuned::sequence::kMinSpanPx;         // sequence.minSpanPx — address span floor
    double  planeRatioFloor  = tuned::sequence::kPlaneRatioFloor;   // sequence.planeRatioFloor — below ⇒ no de-projection
    double  noPlaneRelSigma  = tuned::sequence::kNoPlaneRelSigma;   // sequence.noPlaneRelSigma — ±15 % when k unknown
    double  shaftThetaSigmaRad = tuned::sequence::kShaftThetaSigmaRad; // sequence.shaftThetaSigmaRad
    double  kpSigmaPx        = tuned::sequence::kKpSigmaPx;         // sequence.kpSigmaPx — when no smoother σ
    double  gyroNoiseDps     = tuned::sequence::kGyroNoiseDps;      // sequence.gyroNoiseDps — per-sample IMU σ
    bool    faceOnTrunkPlacement = tuned::sequence::kFaceOnTrunkPlacement; // sequence.faceOnTrunkPlacement — §9 gate
    double  sightedTurnDeg   = tuned::sequence::kSightedTurnDeg;    // sequence.sightedTurnDeg — |turn| below ⇒ blind band
    double  minAfterReversalMs = tuned::sequence::kMinAfterReversalMs; // sequence.minAfterReversalMs — a sighted trunk peak closer to a sign change is a spike
    double  minCredibleClubMph = tuned::sequence::kMinCredibleClubMph; // sequence.minCredibleClubMph
    bool    throughP8          = tuned::sequence::kThroughP8;          // sequence.throughP8 — domain Transition→P8, not →Impact
    // The paired face-on + down-the-line trunk route. `pairTrunkPlacement` is the pair's OWN §9
    // gate — `faceOnTrunkPlacement` governs the span rung and nothing else.
    bool    pairTrunkEnabled   = tuned::sequence::kPairTrunkEnabled;   // sequence.pairTrunk.enabled
    bool    pairTrunkPlacement = tuned::sequence::kPairTrunkPlacement; // sequence.pairTrunk.placement
    double  pairMinCorr        = tuned::sequence::kPairMinCorr;        // sequence.pairMinCorr
    double  pairMinExtentPx    = tuned::sequence::kPairMinExtentPx;    // sequence.pairMinExtentPx
    double  pairMaxGapFrames   = tuned::sequence::kPairMaxGapFrames;   // sequence.pairMaxGapFrames
    double  pairSwapMinFrac    = tuned::sequence::kPairSwapMinFrac;    // sequence.pairSwapMinFrac
    double  pairMaxTurnDps     = tuned::sequence::kPairMaxTurnDps;     // sequence.pairMaxTurnDps
    // The DTL leg's two gap rules (the swap carry, the pair bracket) judged against the DTL track's
    // LOCAL spacing rather than its global median, never tighter than today — so a thinned DTL pass
    // (pose.dtlSchedule: 13 ms backswing, 26 ms address/finish) is not read as holes. pose_schedule.h.
    bool    pairDtlLocalGap    = tuned::pose::kDtlLocalGap;            // pose.dtlLocalGap
    bool    pairTrunkThoraxPlacement = tuned::sequence::kPairTrunkThoraxPlacement; // sequence.pairTrunk.thoraxPlacement
    // The two-camera skeleton's lead-arm rung, and the camera-scale fraction its peak σ carries
    // (the rotation route's constant: one assumed camera, one assumed error).
    bool    skel3dLeadArm       = tuned::sequence::kSkel3dLeadArm;       // sequence.skel3d.leadArm
    bool    skel3dLeadArmFirst  = false;   // sequence.skel3d.leadArmFirst — the skeleton above the face-on arm (pre-19 Oct)
    double  skel3dMinUsableFrac = tuned::sequence::kSkel3dMinUsableFrac; // sequence.skel3d.minUsableFrac
    double  skel3dScaleFrac     = tuned::bodyRotation::kTriScaleFrac;    // sequence.skel3d.scaleFrac
    double  skel3dScaleFracCalibrated = tuned::bodyRotation::kTriScaleFracCalibrated;
    // The two-camera skeleton's TRUNK rung (pelvis, thorax), below the pair, above the span.
    bool    skel3dTrunk            = tuned::sequence::kSkel3dTrunk;            // sequence.skel3d.trunk
    bool    skel3dPlacement        = tuned::sequence::kSkel3dPlacement;        // sequence.skel3d.placement
    bool    skel3dThoraxPlacement  = tuned::sequence::kSkel3dThoraxPlacement;  // sequence.skel3d.thoraxPlacement
    bool    peakTimes              = tuned::sequence::kPeakTimes;              // sequence.peakTimes
    bool    nodeSeries             = tuned::sequence::kNodeSeries;             // sequence.nodeSeries
    // The chest's probable peak across the camera's gap (gap_bridge.h; tuned::sequence::kGapBridge).
    bool    gapBridge              = tuned::sequence::kGapBridge;              // sequence.gapBridge
    double  gapBridgeEllMs         = tuned::sequence::kGapBridgeEllMs;         // sequence.gapBridge.ellMs
    double  gapBridgeSfDps         = tuned::sequence::kGapBridgeSfDps;         // sequence.gapBridge.sfDps
    double  gapBridgeNoiseDps      = tuned::sequence::kGapBridgeNoiseDps;      // sequence.gapBridge.noiseDps
    double  gapBridgeAgreeSigma    = tuned::sequence::kGapBridgeAgreeSigma;    // sequence.gapBridge.agreeSigma
    double  chestBackwardsWindowMs = tuned::sequence::kChestBackwardsWindowMs; // sequence.chestBackwardsWindowMs
    // The lead arm and club keep their DIRECTION (towards the target = +, the trunk's convention) instead of
    // being read as a speed (|ω|) — pp_tuned_constants.h kSignedArmClub. Every route, the arm IMU included.
    bool    signedArmClub          = tuned::sequence::kSignedArmClub;          // sequence.signedArmClub
    // A camera arm node's σ_t widened by the span over which its curve stays within this many σ_peak of its
    // peak (σ² + span²/12) — pp_tuned_constants.h kCameraArmTopWidthSigmas. 0 = off.
    double  cameraArmTopWidthSigmas = tuned::sequence::kCameraArmTopWidthSigmas; // sequence.cameraArm.topWidthSigmas
    // The club angle's per-sample σ from the shaft uncertainty pass (sigmaThetaDeg) in place of
    // shaftThetaSigmaRad / conf. A VALUE change, not a σ-only one: the wider σ un-places the club
    // node on some swings (the 1 Oct sigma sweep), so it has its own switch, off until Mark rules.
    bool    calibratedClubSigma = tuned::uncertainty::kSequenceSigma;  // uncertainty.sequenceSigma
    // The camera club node judged per swing (KB-18) instead of the blanket withhold; judged mode
    // reads the synth's per-tick σθ like calibratedClubSigma. tuned::sequence has the rule.
    bool    cameraClubJudged    = tuned::sequence::kCameraClubJudged;  // sequence.cameraClubJudged
    double  judgeMaxSigmaMs     = tuned::sequence::kJudgeMaxSigmaMs;   // sequence.cameraClubJudgeMaxSigmaMs
    double  judgeWindowMs       = tuned::sequence::kJudgeWindowMs;     // sequence.cameraClubJudgeWindowMs
    int     judgeMinMeasured    = tuned::sequence::kJudgeMinMeasured;  // sequence.cameraClubJudgeMinMeasured

    static SegmentRatesConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        SegmentRatesConfig c;
        apply(ov, "sequence.enabled",           c.enabled);
        apply(ov, "sequence.derivWindowMs",     c.derivWindowMs);
        apply(ov, "sequence.maxPlaceSigmaMs",   c.maxPlaceSigmaMs);
        apply(ov, "sequence.sigmaK",            c.sigmaK);
        apply(ov, "sequence.confMin",           c.confMin);
        apply(ov, "sequence.faceOnArm.minConf",        c.faceOnArmMinConf);
        apply(ov, "sequence.faceOnArm.minLengthFrac",  c.faceOnArmMinLengthFrac);
        apply(ov, "sequence.faceOnArm.risingIntoMinFrac", c.faceOnArmRisingIntoMinFrac);
        apply(ov, "sequence.addrMinFrames",     c.addrMinFrames);
        apply(ov, "sequence.addrWindowUs",      c.addrWindowUs);
        apply(ov, "sequence.spanNoisePx",       c.spanNoisePx);
        apply(ov, "sequence.sinFloor",          c.sinFloor);
        apply(ov, "sequence.minSpanPx",         c.minSpanPx);
        apply(ov, "sequence.planeRatioFloor",   c.planeRatioFloor);
        apply(ov, "sequence.noPlaneRelSigma",   c.noPlaneRelSigma);
        apply(ov, "sequence.shaftThetaSigmaRad", c.shaftThetaSigmaRad);
        apply(ov, "sequence.kpSigmaPx",         c.kpSigmaPx);
        apply(ov, "sequence.gyroNoiseDps",      c.gyroNoiseDps);
        apply(ov, "sequence.faceOnTrunkPlacement", c.faceOnTrunkPlacement);
        apply(ov, "uncertainty.sequenceSigma",  c.calibratedClubSigma);
        apply(ov, "sequence.cameraClubJudged",            c.cameraClubJudged);
        apply(ov, "sequence.cameraClubJudgeMaxSigmaMs",   c.judgeMaxSigmaMs);
        apply(ov, "sequence.cameraClubJudgeWindowMs",     c.judgeWindowMs);
        apply(ov, "sequence.cameraClubJudgeMinMeasured",  c.judgeMinMeasured);
        apply(ov, "sequence.sightedTurnDeg",    c.sightedTurnDeg);
        apply(ov, "sequence.minAfterReversalMs", c.minAfterReversalMs);
        apply(ov, "sequence.minCredibleClubMph", c.minCredibleClubMph);
        apply(ov, "sequence.throughP8",          c.throughP8);
        apply(ov, "sequence.pairTrunk.enabled",   c.pairTrunkEnabled);
        apply(ov, "sequence.pairTrunk.placement", c.pairTrunkPlacement);
        apply(ov, "sequence.pairMinCorr",         c.pairMinCorr);
        apply(ov, "sequence.pairMinExtentPx",     c.pairMinExtentPx);
        apply(ov, "sequence.pairMaxGapFrames",    c.pairMaxGapFrames);
        apply(ov, "sequence.pairSwapMinFrac",     c.pairSwapMinFrac);
        apply(ov, "sequence.pairMaxTurnDps",      c.pairMaxTurnDps);
        apply(ov, "pose.dtlLocalGap",             c.pairDtlLocalGap);
        apply(ov, "sequence.pairTrunk.thoraxPlacement", c.pairTrunkThoraxPlacement);
        apply(ov, "sequence.skel3d.leadArm",       c.skel3dLeadArm);
        apply(ov, "sequence.skel3d.leadArmFirst",  c.skel3dLeadArmFirst);
        apply(ov, "sequence.skel3d.minUsableFrac", c.skel3dMinUsableFrac);
        apply(ov, "sequence.skel3d.scaleFrac",     c.skel3dScaleFrac);
        apply(ov, "sequence.skel3d.trunk",           c.skel3dTrunk);
        apply(ov, "sequence.skel3d.placement",       c.skel3dPlacement);
        apply(ov, "sequence.skel3d.thoraxPlacement", c.skel3dThoraxPlacement);
        apply(ov, "sequence.peakTimes",              c.peakTimes);
        apply(ov, "sequence.nodeSeries",             c.nodeSeries);
        apply(ov, "sequence.gapBridge",              c.gapBridge);
        apply(ov, "sequence.gapBridge.ellMs",        c.gapBridgeEllMs);
        apply(ov, "sequence.gapBridge.sfDps",        c.gapBridgeSfDps);
        apply(ov, "sequence.gapBridge.noiseDps",     c.gapBridgeNoiseDps);
        apply(ov, "sequence.gapBridge.agreeSigma",     c.gapBridgeAgreeSigma);
        apply(ov, "sequence.chestBackwardsWindowMs", c.chestBackwardsWindowMs);
        apply(ov, "sequence.signedArmClub",          c.signedArmClub);
        apply(ov, "sequence.cameraArm.topWidthSigmas", c.cameraArmTopWidthSigmas);
        return c;
    }
};

// Where a SIGNED trunk rate crossed into the downswing: the zero crossing that opens the positive
// lobe holding the node's peak (findRateReversal below). `sigmaMs` 0 = the route stated no rate σ.
struct RateReversal {
    bool    found   = false;
    int64_t tUs     = 0;        // absolute, shared timeline
    double  sigmaMs = 0.0;      // 1σ on the instant: the rate σ at the crossing over |slope|
};

// One segment's rate curve plus how it was obtained. `series.key` empty ⇒ not produced.
struct SegmentRateChannel {
    MetricSeries         series;        // °/s on the route's own grid; sigma = median over the domain
    std::vector<double>  sampleSigma;   // per-sample 1σ, parallel to series.t_us (for the peak finder)
    // A route with a blind band only (the face-on span): 1 where the camera could see the rate, parallel
    // to series.t_us; empty = no band. The series keeps the blind samples for the chart; the peak search
    // and the chest's gap bridge treat them as unmeasured.
    std::vector<uint8_t> sighted;
    std::vector<double>  turnDeg;       // …and |turn| from square per sample (°), parallel too; empty = no band
    QString              routeId;       // "pelvisImu" | "thoraxImu" | "leadArmImus" | "clubSensorFused" | "faceOn+dtl" | "faceOn+dtl3d" | "faceOn" | "faceOnClub"
    bool                 direct = false;
    // PELVIS AND THORAX ONLY: the reversal into the downswing, found on the route's own rate BEFORE
    // the curve was cut to P4 and masked to the domain — the hips reverse up to 250 ms before the
    // club's top (phase_segmenter.h transBeforeTopUs), which is outside both. Set whenever the peak
    // finder found a peak; the series is emitted only for a PLACED node (sequenceNodeSeries).
    RateReversal         reversal;
    bool produced() const { return !series.key.isEmpty(); }
};

// What the pair route measured about itself — logged per swing and read by the corpus pass; it
// is NOT serialised and nothing downstream gates on it. Every field is 0 / empty when the route
// was not attempted.
struct PairSegmentDiag {
    bool    produced   = false;
    double  rEllipse   = 0.0;   // p90|d_dtl| / p90|d_fo| over takeaway→impact — the alternative to
                                //   the vertical-extent ratio, reported so the two can be compared
    double  corrAbs    = 0.0;   // |Pearson| of |d_dtl|/r against sqrt(max(0, W² − d_fo²)), domain
    double  corrSigned = 0.0;   // the same on the SIGNED d_dtl (see the note in the .cpp)
    double  closureP50 = 0.0;   // |(d_fo/W)² + (d_dtl/(r·W))² − 1| over the domain
    double  closureP90 = 0.0;
    int     nPaired    = 0;     // face-on samples that found a down-the-line bracket in the domain
    int     signFo = 0, signDtl = 0;   // the two measured orientation bits (see the .cpp)
    // Left/right keypoint RELABELS undone in each view, and the frames thrown away because the
    // labels were alternating too fast to trust either way (see deswapInPlace in the .cpp).
    int     nSwapsFo = 0, nSwapsDtl = 0, nSwapFramesDropped = 0;
    // Samples the rigid-body rate limit refused, and which tier each leg was finally read from:
    // "smoothed" where nothing had to be undone, "rawDeswapped" where something did.
    int     nRateLimitedFo = 0, nRateLimitedDtl = 0;
    QString srcFo, srcDtl;
    // The two face-on read-outs the 14 Oct 2026 fixes are judged by (thorax_pair_20261014.md
    // §3.1, §3.2, §12). `wFoPx` is the face-on leg's own raw p95 |d| over takeaway→impact — the
    // line's image width. `foConsumedMaxPx` is the largest |d| the route actually CONSUMED over the
    // domain (P4 → P8), after the guards: a smoothed leg that bridged a crossing's flutter reads
    // wider than the line itself here. `foAtImpactPx` is the oriented consumed face-on separation
    // at impact (median over impact ± one derivative window): the trunk is near square to that
    // camera at the ball, so a negative value means the labels came out 180° wrong. NaN when not
    // measured. `nCrossingsFo` counts the genuine zero crossings of the de-swapped, rate-limited
    // RAW face-on track inside the domain (each one forces the raw source); `nRevokedFo` the
    // relabel calls the impact anchor overturned (deswapInPlace in the .cpp).
    double  wFoPx = 0.0;
    double  foConsumedMaxPx = std::nan("");
    double  foAtImpactPx    = std::nan("");
    int     nCrossingsFo = 0, nRevokedFo = 0;
    double  invalidFrac = 0.0;  // fraction of the domain's face-on instants with no pair sample
    QString refusal;            // empty ⇒ produced; else why this segment fell through to face-on
    QVariantMap refusalEvidence; // the same, as gate-record evidence ({ code, numbers }; gate_catalogue.h)
};

struct PairDiagnostics {
    bool    attempted  = false;
    double  rVertical  = 0.0;   // (vertical body extent in DTL px) / (the same in FO px), at address
    double  extentFoPx = 0.0, extentDtlPx = 0.0;
    int     addrSamples = 0;
    QString refusal;            // route-level refusal (the scale); empty ⇒ the scale was formed
    QVariantMap refusalEvidence; // the same, as gate-record evidence ({ code, numbers })
    PairSegmentDiag pelvis, thorax;
};

// The LEAD arm as skeleton3d's two-camera fit placed it (the stage picks the side): per fitted
// frame, the unit shoulder → wrist direction in world coordinates, its 1σ (rad, the two joints'
// posterior position σ over the arm's length), and whether the frame is usable — both joints
// measured or constrained by the fit, never merely inferred. Plain arrays, so this producer never
// includes a skeleton3d header.
struct SkeletonLeadArm {
    std::vector<int64_t>               t_us;
    std::vector<std::array<double, 3>> dir;
    std::vector<double>                sigmaRad;
    std::vector<uint8_t>               usable;
    bool camerasCalibrated = false;    // skeleton3d solved from a MEASURED calibration
};

// The hip and shoulder lines as skeleton3d's two-camera fit placed them — the rotation route's
// own bearings (rad, world XY, atan2 of left → right; NaN = unusable frame) and their 1σ.
struct SkeletonTrunk {
    std::vector<int64_t> t_us;
    std::vector<double>  pelvisBearing, pelvisSigma;
    std::vector<double>  thoraxBearing, thoraxSigma;
    bool camerasCalibrated = false;
};

struct SegmentRatesInputs {
    const PoseTrack2D             *pose    = nullptr;   // face-on pose (smoothed preferred)
    int                            frameW  = 0, frameH = 0;
    // The DOWN-THE-LINE pose of the same swing on the same window clock (smoothed preferred), and
    // ITS frame dimensions — the second leg of the pair route. Null ⇒ the trunk falls through to
    // the face-on span rung exactly as before.
    const PoseTrack2D             *poseDtl = nullptr;
    int                            dtlFrameW = 0, dtlFrameH = 0;
    bool                           leadIsLeft = true;   // right-hander seen face-on: lead is image-left? (handedness != 2)
    const FusedStreams            *streams = nullptr;   // bound IMU streams (may be null)
    const ShaftTrack2D            *shaft   = nullptr;   // face-on club track (check ->valid)
    const std::vector<PhaseEvent> *phases  = nullptr;
    int64_t                        impactUs = -1;
    // The same track's PEAK clubhead speed over the downswing (mph), when the kinematics stage
    // produced one; −1 = unknown. Below `minCredibleClubMph` the club node is left unplaced
    // (design §12). Was the at-impact sample until 2026-10-02, which an early-peaking club had
    // already let fall below the floor.
    double                         clubheadSpeedPeakMph = -1.0;
    // The DOWNSWING plane as the two-camera shaft fusion MEASURED it (shaft_fusion.h PlaneFit,
    // only when it was `offered`), in the face-on de-projection's own terms: minor/major ratio and
    // node bearing (rad, image atan2). When present the CLUB's face-on angle is de-projected
    // through it and the route reads `faceOn+dtl`; the ellipse plane inferred from the face-on
    // track alone is the fallback, exactly as before. The lead arm is NOT moved onto it — the arm
    // does not swing on the shaft's plane, and the ellipse was never a measurement of the arm's
    // either; one thing changes at a time.
    struct FusedPlane { bool have = false; double ratio = 1.0, nodeRad = 0.0; };
    FusedPlane                     fusedClubPlane;
    // The two-camera skeleton's lead arm (null ⇒ the arm reads off the face-on image as before).
    const SkeletonLeadArm         *skelArm = nullptr;
    // The two-camera skeleton's hip and shoulder lines (null ⇒ the trunk is IMU → pair → span).
    const SkeletonTrunk           *skelTrunk = nullptr;
};

struct SegmentRatesResult {
    SegmentRateChannel pelvis, thorax, leadArm, club;
    KinematicSequence  sequence;
    PairDiagnostics    pair;            // what the faceOn+dtl route saw (diagnostic only)
    // Every gate record the sequence fired (trust_and_gates_design.md §6): the nodes' own, in node
    // order, then the route-level ones (a two-camera route that refused or withdrew). The stage
    // appends them to `analysis.gates`.
    std::vector<GateRecord> gates;
    bool valid = false;                 // at least one channel produced

    const SegmentRateChannel &channel(SeqSegment s) const
    {
        switch (s) {
        case SeqSegment::Pelvis:  return pelvis;
        case SeqSegment::Thorax:  return thorax;
        case SeqSegment::LeadArm: return leadArm;
        case SeqSegment::Club:    return club;
        }
        return pelvis;
    }
};

// Produce every channel the inputs support, place the nodes, resolve the sequence.
SegmentRatesResult buildSegmentRates(const SegmentRatesInputs &in, const SegmentRatesConfig &cfg);

// The MetricSeries to append to the swing's series (produced channels only).
std::vector<MetricSeries> segmentRateSeries(const SegmentRatesResult &res);

// The PLACED pelvis and thorax nodes as point-in-time metrics — `pelvisPeakTime`, `thoraxPeakTime`:
// one Impact phase sample whose value is the peak instant relative to impact in ms (NEGATIVE =
// before the ball), σ = the node's σ_t (propagated). The diagnostics' Order test compares event
// times, so this is the quantity `sequence_order` must read. Unplaced (or bounded) ⇒ nothing.
std::vector<MetricSeries> sequencePeakTimeSeries(const KinematicSequence &ks);

// The rest of the PLACED nodes as point-in-time metrics, one Impact phase sample each, σ Propagated,
// every series carrying its node's routeId / direct (MetricSeries::routeId — the diagnostics'
// same-scale rule for ratios; trunk_imu_sequence_grf_design.md §10.4, §10.7):
//   leadArmPeakTime, clubPeakTime          the peak instant relative to impact in ms, NEGATIVE before
//                                          the ball — pelvisPeakTime's convention (NOT beforeImpactMs,
//                                          which is the opposite sign; §10.9 P1); σ = σ_t
//   pelvisPeakSpeed, thoraxPeakSpeed,      |peakDps| in °/s — a magnitude, so a ratio of two of them
//   leadArmPeakSpeed, clubPeakSpeed        never carries a sign convention (§10.9 P3); σ = peak σ
//   pelvisReversalTime, thoraxReversalTime the channel's RateReversal, ms relative to impact like the
//                                          peak times; absent when no crossing was found
// An unplaced (or bounded) node emits nothing, so a check that reads one is not assessable.
std::vector<MetricSeries> sequenceNodeSeries(const SegmentRatesResult &res);

// ── Pieces exposed for the unit test and for the pair route's future producer ───────────────────

// De-project an image angle ψ (rad, unwrapped) into the in-plane angle α given the ellipse the
// swing plane images as: major-axis bearing ν (rad) and axis ratio k = minor/major ∈ (0, 1].
// tan(ψ − ν) = k · tan α. Returns α continuous (unwrapped) in the same sheet as ψ − ν.
double deprojectPlaneAngle(double psiRad, double nuRad, double k);

// dα/dψ at ψ — the σ Jacobian and the rate correction factor, in [k, 1/k].
double deprojectGain(double psiRad, double nuRad, double k);

// THE REVERSAL OF A SIGNED RATE INTO THE DOWNSWING. Walk back from the peak instant `tPeakUs` over
// the admissible samples (`valid` non-zero, or empty ⇒ all; value finite) while the rate is
// positive; the first sample at or below zero and the positive sample after it bracket the
// crossing, which is linearly interpolated. σ_t = (the rate σ interpolated at the crossing) /
// |slope|. Not found when the walk reaches the start of the track without a non-positive sample,
// or when the bracket straddles an inadmissible sample (the rate is not known across the hole).
//
// ⚠ THE CROSSING THAT OPENS THE PEAK'S LOBE, NOT "THE FIRST AFTER THE TOP". The design (§10.4) says
// "after the top", but the top on the ladder is the CLUB's, and the hips reverse before it — the
// segmenter's own Transition is the pelvis reversal searched up to 250 ms BEFORE Top. A search
// that starts at the top misses exactly the reversal it is there to time, and then lands on
// whatever dither comes next. The lobe definition needs no top: it is the instant after which the
// segment turned toward the target without stopping until its peak.
RateReversal findRateReversal(const std::vector<int64_t> &t, const std::vector<double> &dps,
                              const std::vector<double> &sigmaDps, const std::vector<uint8_t> &valid,
                              int64_t tPeakUs);

} // namespace pinpoint::analysis
