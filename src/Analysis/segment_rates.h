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
#include <cstdint>
#include <vector>

#include "swing_analysis.h"          // PoseTrack2D, ShaftTrack2D, MetricSeries, PhaseEvent, SegmentRole
#include "imu_vision_fuser.h"        // FusedStreams, SegmentStream
#include "kinematic_sequence.h"      // KsNode, KinematicSequence
#include "analysis_tuning.h"         // tuning::apply
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
    // The paired face-on + down-the-line trunk route. `pairTrunkPlacement` is the pair's OWN §9
    // gate — `faceOnTrunkPlacement` governs the span rung and nothing else.
    bool    pairTrunkEnabled   = tuned::sequence::kPairTrunkEnabled;   // sequence.pairTrunk.enabled
    bool    pairTrunkPlacement = tuned::sequence::kPairTrunkPlacement; // sequence.pairTrunk.placement
    double  pairMinCorr        = tuned::sequence::kPairMinCorr;        // sequence.pairMinCorr
    double  pairMinExtentPx    = tuned::sequence::kPairMinExtentPx;    // sequence.pairMinExtentPx
    double  pairMaxGapFrames   = tuned::sequence::kPairMaxGapFrames;   // sequence.pairMaxGapFrames
    double  pairSwapMinFrac    = tuned::sequence::kPairSwapMinFrac;    // sequence.pairSwapMinFrac
    double  pairMaxTurnDps     = tuned::sequence::kPairMaxTurnDps;     // sequence.pairMaxTurnDps
    bool    pairTrunkThoraxPlacement = tuned::sequence::kPairTrunkThoraxPlacement; // sequence.pairTrunk.thoraxPlacement

    static SegmentRatesConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        SegmentRatesConfig c;
        apply(ov, "sequence.enabled",           c.enabled);
        apply(ov, "sequence.derivWindowMs",     c.derivWindowMs);
        apply(ov, "sequence.maxPlaceSigmaMs",   c.maxPlaceSigmaMs);
        apply(ov, "sequence.sigmaK",            c.sigmaK);
        apply(ov, "sequence.confMin",           c.confMin);
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
        apply(ov, "sequence.sightedTurnDeg",    c.sightedTurnDeg);
        apply(ov, "sequence.minAfterReversalMs", c.minAfterReversalMs);
        apply(ov, "sequence.minCredibleClubMph", c.minCredibleClubMph);
        apply(ov, "sequence.pairTrunk.enabled",   c.pairTrunkEnabled);
        apply(ov, "sequence.pairTrunk.placement", c.pairTrunkPlacement);
        apply(ov, "sequence.pairMinCorr",         c.pairMinCorr);
        apply(ov, "sequence.pairMinExtentPx",     c.pairMinExtentPx);
        apply(ov, "sequence.pairMaxGapFrames",    c.pairMaxGapFrames);
        apply(ov, "sequence.pairSwapMinFrac",     c.pairSwapMinFrac);
        apply(ov, "sequence.pairMaxTurnDps",      c.pairMaxTurnDps);
        apply(ov, "sequence.pairTrunk.thoraxPlacement", c.pairTrunkThoraxPlacement);
        return c;
    }
};

// One segment's rate curve plus how it was obtained. `series.key` empty ⇒ not produced.
struct SegmentRateChannel {
    MetricSeries         series;        // °/s on the route's own grid; sigma = median over the domain
    std::vector<double>  sampleSigma;   // per-sample 1σ, parallel to series.t_us (for the peak finder)
    QString              routeId;       // "pelvisImu" | "thoraxImu" | "leadArmImus" | "clubSensorFused" | "faceOn+dtl" | "faceOn" | "faceOnClub"
    bool                 direct = false;
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
    double  invalidFrac = 0.0;  // fraction of the domain's face-on instants with no pair sample
    QString refusal;            // empty ⇒ produced; else why this segment fell through to face-on
};

struct PairDiagnostics {
    bool    attempted  = false;
    double  rVertical  = 0.0;   // (vertical body extent in DTL px) / (the same in FO px), at address
    double  extentFoPx = 0.0, extentDtlPx = 0.0;
    int     addrSamples = 0;
    QString refusal;            // route-level refusal (the scale); empty ⇒ the scale was formed
    PairSegmentDiag pelvis, thorax;
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
    // The same track's clubhead speed at impact (mph), when the kinematics stage produced one;
    // −1 = unknown. Below `minCredibleClubMph` the club node is left unplaced (design §12).
    double                         clubheadSpeedImpactMph = -1.0;
};

struct SegmentRatesResult {
    SegmentRateChannel pelvis, thorax, leadArm, club;
    KinematicSequence  sequence;
    PairDiagnostics    pair;            // what the faceOn+dtl route saw (diagnostic only)
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

// ── Pieces exposed for the unit test and for the pair route's future producer ───────────────────

// De-project an image angle ψ (rad, unwrapped) into the in-plane angle α given the ellipse the
// swing plane images as: major-axis bearing ν (rad) and axis ratio k = minor/major ∈ (0, 1].
// tan(ψ − ν) = k · tan α. Returns α continuous (unwrapped) in the same sheet as ψ − ν.
double deprojectPlaneAngle(double psiRad, double nuRad, double k);

// dα/dψ at ψ — the σ Jacobian and the rate correction factor, in [k, 1/k].
double deprojectGain(double psiRad, double nuRad, double k);

} // namespace pinpoint::analysis
