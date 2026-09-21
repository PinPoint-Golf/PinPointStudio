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

// Down-the-line shaft tracker — the value types
// (docs/design/dtl_shaft_tracker_design.md §5.9, §5.11). Types only: no
// SwingWindow, no decode, no engine, so the decide/post halves stay unit-testable
// over plain vectors the way the face-on ones are.
//
// Two things here are the design, not bookkeeping, and should be read before the
// tracker is written:
//
//  · DtlTier has no PRED. "A frame is never measured on face-on's word" (§5.9) —
//    the corridor shapes the search, the tier is earned from DTL pixels. UNSEEN,
//    END_ON and OCCLUDED are all ABSENCES, and they are different absences: END_ON
//    means the geometry says nothing could be seen, OCCLUDED that the anchor was
//    not trustworthy, UNSEEN that we looked and found nothing. Publishing them as
//    one "no sample" is how a 0.95 coverage hides a 40 % end-on swing.
//
//  · FaceOnWitness is READ-ONLY and one-directional (§5.10). It carries face-on's
//    SCHEDULE — when the club is visible down the line and how long it should look
//    — into the DTL search. Nothing in DTL may flow back the other way.
//
//  · ρ_F IS ALLOWED TO BE NaN, and NaN means unknown. Only a face-on sample whose
//    visible length is a club length (ShaftMeasured) has a ρ_F at all; on every
//    other sample the field carries a frame-edge clamp, and the builder writes NaN
//    rather than a ratio. The schedule ρ̂_D = √(1 − u_x²) with u_x = ρ_F·cos θ_F is
//    therefore NaN too on those frames, and a NaN ρ̂_D MUST be read as END-ON /
//    unknown — never as sighted. Two rules follow, and both have to be obeyed at
//    every site that evaluates the schedule:
//      – clamp 1 − u_x² at 0 before the square root (a ρ_F that rounds above 1
//        makes it negative, and √negative is a NaN that looks like the first kind
//        but is not);
//      – gate on std::isfinite(ρ̂_D) before comparing it to rhoSolveMin, because
//        every comparison against NaN is false and `!(rho < min)` reads as "solve
//        this frame".

#include <QPointF>
#include <QString>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "shaft_tracker_math.h"   // BandMatch (the E1 lock the DTL solve carries per frame)
#include "types.h"                // pinpoint::SourceId

#include "dtl_shaft_track.h"   // the product types: DtlTier … DtlShaftTrack2D (OpenCV-free)

namespace pinpoint::analysis {

// ── the face-on witness ──────────────────────────────────────────────────────
// Tier ints as the face-on trace writes them (0..5), so a trace line and this
// enum cannot drift apart.
enum class FoTier : uint8_t { Pred, Ray, Band, Recon, Wedge, Seg };

// Read-only, interpolated to DTL frame times. Built by the caller from the
// face-on ShaftTrack2D + ShaftDecideTrace of the SAME swing.
struct FaceOnWitness {
    std::vector<int64_t> tUs;               // ascending, face-on emitted frames
    std::vector<double>  thetaUnwrapRad;    // unwrapped so interpolation cannot cross the branch cut
    std::vector<double>  thetaDotRadS;
    // min(1, visibleLenPx / fullLenPx) on a MEASURED face-on sample, NaN on every
    // other one — see the header note. NaN is not a gap in the data, it is the
    // datum: face-on did not see a club length on that frame.
    std::vector<double>  rhoF;
    // The face-on grip's IMAGE ROW (px), same indexing as tUs. Both cameras see
    // vertical, so y_D ≈ a·y_F + b per swing (§5.2) and a DTL anchor that
    // disagrees is quarantined — which is how the post-impact invented hands are
    // caught without trusting hand confidence. OPTIONAL, like tier/phase: a
    // witness built without it is still ok, and `at()` returns NaN here rather
    // than voiding the sample.
    std::vector<double>  gripYPx;
    std::vector<FoTier>  tier;
    std::vector<int>     phase;             // SwingPhase as int
    std::vector<std::pair<int, int64_t>> ladder;   // (p, t_us), P1..P10
    int64_t impactUs  = -1;
    int     chir      = 0;                  // face-on chirality (+1/−1)
    double  fullLenPx = 0.0;                // the ρ_F denominator (p95 of visibleLenPx over measured samples)
    // Median inter-sample interval (µs). Filled by the builder; `at()` needs a
    // frame interval to apply the 3-interval bracket rule and a mean over a
    // gappy track is not one. 0 ⇒ at() falls back to the mean interval.
    int64_t frameIntervalUs = 0;

    struct At {
        bool   ok           = false;
        double thetaRad     = dtl::kNan;    // unwrapped
        double thetaDotRadS = dtl::kNan;
        double rhoF         = dtl::kNan;
        double gripYPx      = dtl::kNan;    // NaN when the witness carries no grip-row column
        FoTier tier         = FoTier::Pred;
        int    phase        = -1;
    };

    // Linear interpolation of θ (unwrapped) and ρ_F between the bracketing
    // samples; tier and phase from the NEARER one — they are labels, and a label
    // does not interpolate. ok = false outside the range, and when the bracketing
    // gap exceeds 3 frame intervals: across a face-on coverage hole the witness
    // has nothing to say, and saying it anyway is what §2 warns about.
    //
    // ok means "in range AND every numeric column is present" — tUs,
    // thetaUnwrapRad, thetaDotRadS and rhoF must all be n long. A half-built
    // witness used to come back ok with silent NaNs in whichever column was
    // short, which is indistinguishable at the call site from a frame the
    // instrument genuinely could not measure. tier and phase are labels and stay
    // optional: missing ones default to Pred / −1 rather than voiding the sample.
    //
    // ok = true does NOT mean rhoF is finite. ρ_F is legitimately NaN on an
    // unmeasured face-on sample (see the header note), and the interpolation
    // propagates that: if EITHER bracketing sample's ρ_F is NaN the returned rhoF
    // is NaN with ok still true. That is the honest answer — in range, and face-on
    // saw no club length here — and callers must test std::isfinite(rhoF) rather
    // than reading ok as a promise of a number.
    At at(int64_t t_us) const
    {
        At r;
        const size_t n = tUs.size();
        if (n < 2 || t_us < tUs.front() || t_us > tUs.back()) return r;
        if (thetaUnwrapRad.size() != n || thetaDotRadS.size() != n || rhoF.size() != n)
            return r;
        // last index with tUs[i] <= t_us
        size_t hi = size_t(std::upper_bound(tUs.begin(), tUs.end(), t_us) - tUs.begin());
        if (hi == 0) return r;
        if (hi >= n) hi = n - 1;
        const size_t lo = hi - 1;
        const int64_t gap = tUs[hi] - tUs[lo];
        const int64_t step = frameIntervalUs > 0
                                 ? frameIntervalUs
                                 : (tUs.back() - tUs.front()) / int64_t(n - 1);
        if (step > 0 && gap > 3 * step) return r;
        const double f = (gap > 0) ? double(t_us - tUs[lo]) / double(gap) : 0.0;
        // Sizes are guaranteed n by the guard above; NaN in, NaN out (ρ_F).
        const auto lerp = [&](const std::vector<double>& v) {
            return v[lo] + (v[hi] - v[lo]) * f;
        };
        r.thetaRad     = lerp(thetaUnwrapRad);
        r.thetaDotRadS = lerp(thetaDotRadS);
        r.rhoF         = lerp(rhoF);
        // Optional column: interpolated when present, NaN when the builder never
        // filled it. NaN in ⇒ NaN out, and the quarantine reads that as "no
        // cross-view opinion on this frame" rather than as a disagreement.
        if (gripYPx.size() == n) r.gripYPx = lerp(gripYPx);
        const size_t nearer = (f < 0.5) ? lo : hi;
        if (tier.size()  == n) r.tier  = tier[nearer];
        if (phase.size() == n) r.phase = phase[nearer];
        r.ok = true;
        return r;
    }
};

// ── DTL pose anchors (§5.2) ──────────────────────────────────────────────────
// BOTH forearms from day one: D2 vetoes a candidate near EITHER forearm's
// direction whose ray also passes close to that elbow (§5.6).
struct DtlAnchors {
    std::vector<double> gx, gy;                       // grip anchor (px), per frame
    std::vector<cv::Point2d> leadElbow, trailElbow;
    std::vector<cv::Point2d> leadWrist,  trailWrist;
    // Set by the cross-view row check: a DTL anchor whose image row disagrees
    // with the face-on fit by more than its p95 residual. Its frame is left
    // unsolved and tiered OCCLUDED — this is how the post-impact invented hands
    // are caught without trusting hand confidence.
    std::vector<char> quarantined;
    std::vector<std::vector<cv::Point2d>> joints;     // the 8-joint body skeleton, as face-on
};

// ── the reverse ray's ARM excuse (D1, and only in this view) ─────────────────
// Face-on's C1 says strong evidence out of the grip the OTHER way is a scene
// line, because face-on has FREE SPACE behind the butt. Down the line it does
// not: the lead arm is near-collinear with the shaft at address and at impact,
// and the forearms are at P3/P5, on the opposite side of the grip. The reverse
// ray of a CORRECT direction therefore runs up the golfer's own arm, and "the
// reverse ray is as strong" is the NORMAL condition of a right frame here rather
// than the sign of a counterfeit. MEASURED on the dev six at the four adjudicated
// tiles: |reverse − grip→elbow| is 1.0°, 6.9°, 18.0° and 19.1°.
//
// Fills `out` with the grip→joint directions (deg, atan2 convention) that may
// excuse a reverse ray — BOTH elbows and BOTH shoulders — and returns how many
// were usable. A joint nearer the grip than minPx is not a limb the ray can run
// along: grip→J is pose jitter at that range and an excuse built on it would
// waive the test for no reason. This is the SAME sentence D2's minJointPx makes
// about the veto, in the opposite direction.
inline int dtlArmDirsDeg(const DtlAnchors& an, int i, double gx, double gy,
                         double minPx, double out[4])
{
    int n = 0;
    const auto add = [&](const cv::Point2d& J) {
        if (n >= 4) return;
        if (!std::isfinite(J.x) || !std::isfinite(J.y)) return;
        if (!std::isfinite(gx) || !std::isfinite(gy)) return;
        const double dx = J.x - gx, dy = J.y - gy;
        if (std::hypot(dx, dy) <= minPx) return;
        out[n++] = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
    };
    if (i < 0) return 0;
    if (i < int(an.leadElbow.size()))  add(an.leadElbow[size_t(i)]);
    if (i < int(an.trailElbow.size())) add(an.trailElbow[size_t(i)]);
    // 0 and 1 of the shared 8-joint skeleton are the shoulders. They are here and
    // NOT in D2's veto set on purpose: at P3 the true shaft passes near a
    // shoulder, so a shoulder may excuse a REVERSE ray without being allowed to
    // refuse a forward one.
    if (i < int(an.joints.size()) && an.joints[size_t(i)].size() >= 2) {
        add(an.joints[size_t(i)][0]);
        add(an.joints[size_t(i)][1]);
    }
    return n;
}

// ── the solve's working state ────────────────────────────────────────────────
// Handed from dtlSolve to dtlPostSolve so the post-solve probes read the SAME
// tables the DP saw, rather than re-deriving them.
struct DtlSolveState {
    int nf = 0;                                       // frames
    int NS = 0;                                       // θ grid states (360 / grid)
    std::vector<std::vector<float>> EV;               // per-frame normalised evidence row
    std::vector<std::vector<float>> SUP;              // per-frame absolute ridge support row
    // Per-frame per-θ accepted terminus radius (px) of the channel that WON that
    // θ. D3's over-length test and the published lenPx are both read off it, so
    // it has to be the winning channel's run and not an average of three.
    std::vector<std::vector<float>> REND;
    // Per-frame per-θ D2 forearm-veto mask. Kept rather than recomputed because
    // the tier ladder asks "was the SOLVED direction vetoed?" and a second
    // evaluation of the same geometry is a second chance to disagree with itself.
    std::vector<std::vector<char>>  ARMVETO;
    // WHICH limb fired the veto at each θ (DtlLimb, −1 = none). Kept beside the
    // mask because "vetoed" with no joint named is a verdict nobody can argue
    // with: the montage review has to be able to go and look at the knee.
    std::vector<std::vector<signed char>> ARMJOINT;
    std::vector<BandMatch> band;                      // E1 lock per frame
    std::vector<char>      bandOk;
    std::vector<double>    rhoPred;                   // the visibility schedule
    std::vector<DtlRhoSrc> rhoSrc;                    // measured ρ_F, the ρ_F := 1 bound, or no witness
    // ── the ball gate at the still club (§4.3, and the as-built face-on rule
    // "probe address toward the ball, not along a clamp") ────────────────────
    // Frames where the club IS at the ball: the address hold, and ±20 ms of the
    // inherited impact instant. Only the TIMING is inherited; θ_ball is DTL's own
    // measurement. With no DTL ball these frames are left UNSOLVED rather than
    // solved blind — the trouser line to the feet is a longer, higher-contrast
    // ray out of the hands than the shaft, and evidence alone ties them.
    std::vector<char>      ballGate;
    std::vector<double>    thetaBallDeg;              // grip→ball (deg), NaN where no ball / not gated
    std::vector<DtlBand>   bands;
    std::vector<double>    thetaDeg;                  // NaN where unsolved (end-on / occluded / no band)
    std::vector<char>      solved;
    std::vector<float>     gridDeg;                   // the θ grid, in degrees
    // ── the per-frame decide record (§5.2, §5.7) ─────────────────────────────
    // These live on the STATE, not only on the trace, because the post-solve half
    // and the tiering must read the SAME tables the DP saw. A trace is a sink a
    // caller may not have asked for; a tier decided off a re-derivation is a
    // second opinion nobody asked for either.
    std::vector<char>      quarantined;               // cross-view row check refused the anchor
    std::vector<char>      sighted;                   // inside a solved band
    // Inside the INHERITED swing span. Kept per frame, not just counted, because
    // the tier ladder must not read "we never looked here" as END-ON: END-ON is a
    // claim about the GEOMETRY (the club is pointing at the lens), and a frame a
    // second before the takeaway makes no such claim. Three absences that collapse
    // into one are the thing §5.9 exists to prevent.
    std::vector<char>      inSpan;
    // Frames inside the INHERITED swing span — the denominator §5.9 insists on.
    // Coverage over sighted-band frames alone is not honest by itself; this is
    // what stops a 0.95 hiding that 40 % of the swing was end-on.
    int                    spanFrames = 0;
    std::vector<double>    rowResid;                  // y_D − (a·y_F + b), px; NaN where unfittable
    std::vector<double>    corrCentreADeg, corrCentreBDeg, corrHalfDeg;
    std::vector<char>      corridorOn, corridorEscape;
    std::vector<int>       corrSignTaken;             // −1 / 0 (off or tied) / +1 — which centre the solve landed nearer
    std::vector<QString>   reason;                    // why an absence is an absence; empty when solved
    double                 rowFitA = dtl::kNan, rowFitB = dtl::kNan;   // the fitted cross-view row scale/offset
    DtlBall                ball;                      // the DTL ball, or why there is none
    double                 lFullPx = dtl::kNan;       // L̂_D, the DTL full club length (px)
    QString                lFullSource;               // "ball" | "faceOnRowScale" | "none"
    // Which rule placed the club-away window (§5.4) — see DtlShaftTrack2D. It
    // lives on the STATE because the window is chosen in the solve and the two
    // things built from it (the clean plate's low region, the shadow ball cue)
    // must be built from the SAME one; the post half only carries it out.
    QString                clubAwayWindow;
    // The phase-aware clean plate (§5.4) as built, kept for the report: which
    // region came from which frames is the one place face-on touches EVIDENCE,
    // and it is the place the permanence-snapshot error gets made again.
    cv::Mat platesNote;
};

// Per-frame decide diagnostics — one trace_dtl.jsonl line per entry. Every
// vector is nf long when filled and empty when the sink was not asked for.
struct DtlDecideTrace {
    std::vector<double> rhoPred;
    std::vector<double> corrCentreADeg, corrCentreBDeg, corrHalfDeg;
    std::vector<char>   corridorOn, corridorEscape;
    std::vector<double> evAtTheta, supAtTheta;        // evidence at the SOLVED θ, not at the best bin
    std::vector<int>    bandN;
    std::vector<double> bandTheta, bandS, bandR0;
    std::vector<char>   d2ArmVeto;                    // a candidate was vetoed by a forearm
    std::vector<double> d3LenCost;                    // the one-sided over-length cost applied
    std::vector<char>   quarantine;
    std::vector<QString> reason;
    // Which corridor centre the solve landed nearer to (−1 / 0 = corridor off or
    // exactly tied / +1). §4.2: the mid-band depth SIGN is not established, so
    // both centres are offered and the evidence is asked which one it wants. If
    // golf is right this column will be boring, and then it can be hardcoded.
    std::vector<int>    corrSignTaken;
    // The cross-view grip-row residual (px) behind the quarantine verdict — the
    // number, beside the verdict, so a leaked post-impact anchor has a margin
    // rather than only a flag.
    std::vector<double> rowResid;
    // ── the post-solve half's columns ────────────────────────────────────────
    // θ as the DP left it and θ as the track PUBLISHES it are two different
    // numbers once the snap can move the line, and a trace that carries only one
    // of them cannot say whether a bad frame was mis-solved or mis-registered.
    std::vector<double>  thetaSolvedDeg, thetaOutDeg;
    std::vector<double>  rEnd;                        // the winning channel's run at the solved θ (px)
    // The evidenced run the minimum-length rule actually applied, and where it was
    // measured. Beside rEnd rather than instead of it: "the DP's argmax sat at its
    // floor" and "the club really is short here" are different findings and a
    // trace with one column cannot tell them apart.
    std::vector<double>    lenPx;
    std::vector<DtlLenSrc> lenSrc;
    std::vector<double>    lenLatOffsetPx;            // the lateral origin offset the LatBand measurement used
    // Which gate the RAY tier came through, and — where the reverse-ray test was
    // not held against the frame — why. "the test passed" and "the test was
    // waived" are different statements and a trace that carries one column cannot
    // tell a reviewer which of them a published frame is.
    std::vector<DtlEvSrc>     evSrc;
    std::vector<DtlRevWaiver> revWaived;
    // The snap's own two support numbers, on every frame the snap RAN on — not
    // only the ones it moved. cfg.lineConfRay was set from the distribution of
    // the first of them, so a trace that did not carry it could not be used to
    // argue about the threshold.
    std::vector<double>    snapBestConf, snapOriginConf;
    // … and the extent the snap's objective was averaged over. The address
    // offset was this number: the DP's rEnd is the near half of the club (98–216
    // px against a 316 px projection), and a mean over the near half prefers the
    // bright near ridge to the club.
    std::vector<double>    snapDrawnPx;
    std::vector<DtlRhoSrc> rhoSrc;
    std::vector<char>    ballGate;                    // this frame was gated toward the DTL ball
    std::vector<double>  thetaBall;                   // grip→ball (deg) where it was
    std::vector<int>     limbVetoJoint;               // DtlLimb at the SOLVED θ, −1 = none
    std::vector<double>  snapOffsetPx, snapDThetaDeg; // the snap's search result …
    std::vector<char>    snapAccepted;                // … and whether it was taken
    std::vector<int>     tier;                        // DtlTier as int, the published verdict
    std::vector<int>     band;                        // band index, −1 in a gap
    // Run-level, for the trace's summary line: the fitted cross-view row scale.
    double rowFitA = dtl::kNan, rowFitB = dtl::kNan;
};

} // namespace pinpoint::analysis
