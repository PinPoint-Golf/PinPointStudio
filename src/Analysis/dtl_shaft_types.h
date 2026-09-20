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

namespace pinpoint::analysis {

// ── what a DTL frame earned ──────────────────────────────────────────────────
// Ordered least- to most-evidenced; the three absences come first so `>= Ray`
// reads as "this frame published an angle".
enum class DtlTier : uint8_t { Unseen, EndOn, Occluded, Ray, Seg, Band };

inline const char* dtlTierName(DtlTier t)
{
    switch (t) {
        case DtlTier::Unseen:   return "UNSEEN";
        case DtlTier::EndOn:    return "END_ON";
        case DtlTier::Occluded: return "OCCLUDED";
        case DtlTier::Ray:      return "RAY";
        case DtlTier::Seg:      return "SEG";
        case DtlTier::Band:     return "BAND";
    }
    return "UNSEEN";
}

namespace dtl {
// Absent numerics are NaN, never 0 and never −1: 0 is a legal angle and −1 a
// legal cost, and the whole point of the tier ladder is that "we did not measure
// this" must not be readable as a measurement.
inline constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
} // namespace dtl

// ── where ρ̂_D's face-on input came from ──────────────────────────────────────
// Measured: face-on published a club length on this frame and ρ_F is that ratio.
// Bound: face-on measured an ANGLE but no length (it coasts at address and
// reconstructs at impact), so the schedule substitutes ρ_F := 1, which is the
// CONSERVATIVE side — it maximises |u_x| = |cos θ_F| and therefore MINIMISES
// ρ̂_D, so a near-horizontal face-on shaft of unknown length still reads END-ON
// while a near-vertical one (address, impact) reads sighted. It is a bound on the
// SCHEDULE only: the corridor (D5) and the half-plane (D4) still require a
// measured face-on tier, because those read the face-on geometry itself rather
// than "could anything be seen here at all".
// None: no witness on this frame at all.
enum class DtlRhoSrc : uint8_t { None, Measured, Bound };

inline const char* dtlRhoSrcName(DtlRhoSrc s)
{
    switch (s) {
        case DtlRhoSrc::None:     return "none";
        case DtlRhoSrc::Measured: return "measured";
        case DtlRhoSrc::Bound:    return "bound";
    }
    return "none";
}

// ── where the published lenPx was measured ───────────────────────────────────
// The visible run is what the minimum-length rule is asked about, so HOW it was
// measured is part of the claim. Rend is the DP's own terminus radius, read off
// a ray cast from the POSE grip — and the pose grip sits tens of px off the shaft
// axis, so that ray leaves the thin shaft after ~100 px however long the club
// really is. MEASURED on the dev six: rEnd was EXACTLY 98 px (ridgeSweep's floor,
// rLo 8 + minLenPx 90) on 51–101 of the refused frames per swing — the ridge
// score's argmax sitting at its own floor, which is not a length at all. The two
// other sources measure the run AFTER the line has been re-registered:
//   SnapLine — along the SNAPPED line from the snapped grip point;
//   LatBand  — along the solved θ from the pose grip, the best of a few lateral
//              origin offsets, for frames the snap did not or could not move.
// Face-on made this same correction ("the head search never looked at the club")
// by measuring off the re-registered line, and this is that sentence in this view.
enum class DtlLenSrc : uint8_t { Rend, SnapLine, LatBand };

inline const char* dtlLenSrcName(DtlLenSrc s)
{
    switch (s) {
        case DtlLenSrc::Rend:     return "rend";
        case DtlLenSrc::SnapLine: return "snapLine";
        case DtlLenSrc::LatBand:  return "latBand";
    }
    return "rend";
}

// ── where the RAY tier's evidence gate was satisfied ─────────────────────────
// Ev is the normalised E2 evidence read along a ray FROM THE POSE GRIP, and that
// ray is the same off-axis ray DtlLenSrc is a record of: the grip anchor sits
// tens of px off the shaft axis, so on a frame where the shaft is a plainly
// visible bright streak the ray reads 0.37–0.40 against a 0.45 gate. LineConf is
// the alternative the snap already earned — the support under the RE-REGISTERED
// line, which is a measurement of the line the frame would publish rather than of
// a ray nobody draws. It is admissible only where the snap was ACCEPTED (it had
// to beat its own origin line, clear the limb veto and stay inside the ball gate
// to be), and it replaces the EV gate alone: SUP, the limb veto, the minimum
// length, the ball gate and the schedule all still apply.
enum class DtlEvSrc : uint8_t { Ev, LineConf };

inline const char* dtlEvSrcName(DtlEvSrc s)
{
    switch (s) {
        case DtlEvSrc::Ev:       return "ev";
        case DtlEvSrc::LineConf: return "lineConf";
    }
    return "ev";
}

// ── why a reverse ray was not held against a frame (D1, DTL) ─────────────────
// None means the test stood as it is written. Arm means the reverse direction
// runs up the golfer's own arm; BallGate that the direction was already decided
// by DTL's own ball. Both are recorded rather than inferred, because "the test
// passed" and "the test was waived" are different statements about a frame.
enum class DtlRevWaiver : uint8_t { None, Arm, BallGate };

inline const char* dtlRevWaiverName(DtlRevWaiver w)
{
    switch (w) {
        case DtlRevWaiver::None:     return "";
        case DtlRevWaiver::Arm:      return "arm";
        case DtlRevWaiver::BallGate: return "ballGate";
    }
    return "";
}

// ── the limb veto's joint set (D2, generalised) ──────────────────────────────
// Face-on's C2 free-space schedule does not port (§5.6), and at address and
// impact the DTL shaft runs down the trouser line to the feet: a long,
// high-contrast leg edge out of the hands that ties the shaft on evidence alone.
// The discriminator is the one the research record found the counterfeit fails —
// the ray passes CLOSE TO A JOINT as well as along its direction — so the veto is
// asked of every limb joint below the shoulders, not only the elbows.
// Deliberately NOT the shoulders or the head: at P3 the true shaft passes near
// them, and a veto there would refuse a real frame.
enum class DtlLimb : int8_t {
    None = -1, LeadElbow = 0, TrailElbow = 1,
    LeftHip = 2, RightHip = 3, LeftKnee = 4, RightKnee = 5, LeftAnkle = 6, RightAnkle = 7
};

inline const char* dtlLimbName(int j)
{
    switch (j) {
        case 0: return "leadElbow";
        case 1: return "trailElbow";
        case 2: return "leftHip";
        case 3: return "rightHip";
        case 4: return "leftKnee";
        case 5: return "rightKnee";
        case 6: return "leftAnkle";
        case 7: return "rightAnkle";
    }
    return "";
}

// One DTL frame. Emitted for EVERY frame in the swing span, including the
// absences — `tier` and `reason` say what happened, the numerics are NaN when
// nothing was measured.
struct DtlSample {
    int64_t t_us        = 0;
    QPointF gripPx;                      // DTL pose anchor (image px)
    QPointF headPx;                      // measured terminus, or absent (see tier)
    double  thetaRad    = dtl::kNan;     // image angle, atan2 convention (NOT unwrapped)
    double  lenPx       = dtl::kNan;     // evidenced run length along θ
    DtlLenSrc lenSrc    = DtlLenSrc::Rend;   // … and where that number came from
    // … and which gate let the RAY tier through: the ray's own EV, or the support
    // under the snapped line. Ev on an absence, because a frame that published
    // nothing satisfied neither.
    DtlEvSrc  evSrc     = DtlEvSrc::Ev;
    float   conf        = 0.f;           // 0..1
    DtlTier tier        = DtlTier::Unseen;
    // The visibility schedule (§5.3/§5.7), inherited: ρ̂_D = √(1 − u_x²) with
    // u_x = ρ_F·cos θ_F from the face-on witness. This is what makes a forearm
    // lock at the top impossible rather than merely out-scored.
    double  rhoPred     = dtl::kNan;
    // … and WHERE its face-on input came from. A ρ̂_D built on the ρ_F := 1 bound
    // is a different claim from one built on a measured length, and a report that
    // cannot tell them apart cannot say which of the two best-seen moments of the
    // swing the tracker is standing on.
    DtlRhoSrc rhoSrc    = DtlRhoSrc::None;
    // The face-on corridor as it stood on this frame. Two centres because the
    // depth sign is unknown until the sign table settles the phase; corrHalfDeg
    // is the measured half-width, NOT an assumed one.
    double  corrCentreDeg[2] = { dtl::kNan, dtl::kNan };
    double  corrHalfDeg = dtl::kNan;
    bool    corridorOn     = false;      // false ⇒ gated off (tier PRED/RECON, ρ_F > max, finish)
    bool    corridorEscape = false;      // clean evidence won OUTSIDE the corridor — always worth a frame number
    int     band        = -1;            // index into DtlShaftTrack2D::bands; −1 = in a gap
    QString reason;                      // why an absence is an absence; empty when published
    double  bandS       = dtl::kNan;     // E1 lock scale (px/mm) when tier == Band
    double  bandR0      = dtl::kNan;     // E1 lock butt→anchor offset (mm) when tier == Band
};

// A maximal run of solvable frames (§5.8). Bands are solved INDEPENDENTLY;
// nothing connects them and no sample is published in a gap.
struct DtlBand {
    int     lo = -1, hi = -1;            // inclusive frame indices
    int64_t loUs = 0, hiUs = 0;
    QString name;                        // e.g. "address", "P2.5-P3.5" — for the report, never gated on
};

// ── the DTL ball (§4.3, §5.5) ────────────────────────────────────────────────
// A direct DTL measurement: at address and impact the head is AT the ball, so
// grip→ball is an angle prior that owes face-on nothing — and it covers exactly
// the two bands where the corridor of §4.1 (c) is degenerate. `found` false is
// the normal answer, not a failure: `reason` says which gate refused, and D6 is
// then simply absent (never a fabricated centre).
// ONE detector, TWO cues, ONE verdict. The bright cue is the ball imaged as a
// bright compact blob; the shadow cue is the ball's own contact shadow on a mat
// blown so white the ball has no edge on it (06-11: 0 of 9 by brightness, and
// the address hold is the wrong time to look at all, because the clubhead and
// its shadow sit on top of the ball there). The shadow cue is only CONSULTED
// where the bright one found nothing, but it is always MEASURED, so a trace can
// put the two beside each other on a scene where both exist.
struct DtlBall {
    bool    found = false;
    double  x = dtl::kNan, y = dtl::kNan;   // image px
    QString reason;
    // Which cue answered. "none" when there is no ball — a first-class outcome.
    QString source = QStringLiteral("none");        // "bright" | "shadow" | "none"
    // The shadow cue's own numbers, recorded whether or not it supplied the
    // verdict: the crescent's centroid (the ball centre is radiusPx ABOVE it),
    // how much brightness that patch regained once the ball had gone, the radius
    // the scene scale implied, and how many candidates survived the shape gates.
    double  shadowX = dtl::kNan, shadowY = dtl::kNan;
    double  launchRise = dtl::kNan;
    double  radiusPx   = dtl::kNan;
    int     nCandidates = 0;
};

// Band-lock truth, generated WITHOUT any face-on input (§6) — so the instrument
// that grades the coupling does not depend on it.
struct DtlTruthSample {
    int64_t t_us = 0;
    double  thetaRad = dtl::kNan;
    double  s        = dtl::kNan;        // px/mm
    double  r0       = dtl::kNan;        // mm
    QPointF gripPx, headPx;
};

// The product. SwingLab-only: written to analysis.clubDtl by swinglab_run,
// read by nothing in the app (§5.11).
struct DtlShaftTrack2D {
    bool  valid  = false;
    pinpoint::SourceId camera = pinpoint::kInvalidSourceId;
    int   frameWidth = 0, frameHeight = 0;
    int64_t clockOffsetUs = 0;           // DTL t_us − face-on t_us, 0 on a shared clock
    std::vector<DtlSample>      samples;
    std::vector<DtlBand>        bands;
    std::vector<DtlTruthSample> truth;
    // The second number §5.9 insists on: what fraction of the swing span was
    // sighted at all. Coverage over sighted-band frames alone is not honest by
    // itself — this is the denominator that stops a 0.95 hiding the gaps.
    double sightedFrac = 0.0;
    // A pin: how many samples were published in a frame whose ρ̂_D put it END-ON.
    // Should be 0. Anything else means the schedule was overruled.
    int    publishedInEndOn = 0;
    // The DTL ball and the DTL full club length L̂_D that D3/D6 were built on —
    // carried on the product because the report has to say which SOURCE the
    // length came from (§5.6 D3: address grip→ball / ρ̂_D, else the cross-view
    // row scale, else none). A length with no stated provenance is a number.
    DtlBall ball;
    double  lFullPx = dtl::kNan;
    QString lFullSource;          // "ball" | "faceOnRowScale" | "none"
    // The fitted cross-view row scale/offset (§5.2), carried out of the solve so
    // a report can say what the quarantine's verdicts were measured against.
    double  rowFitA = dtl::kNan, rowFitB = dtl::kNan;
};

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
