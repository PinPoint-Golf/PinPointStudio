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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QPointF>

#include "swing_analysis.h"   // ShaftSample2D / ShaftPosition / ShaftSynthesized flag

// Position-first shaft measurement — Layer C "synthesis between anchors"
// (docs/design/shaft_position_first_design.md §2 Layer C). Header-only, unit-tested
// per the detector-math convention (shaft_positions.h / impact_detector.h).
//
// Given the located coaching P-anchors (P1..P8, whatever subset exists) and a
// timestamp grid, emit ONE synthesized shaft state per grid tick STRICTLY between
// consecutive anchors — a smooth-scrub VISUALIZATION tier flagged
// ShaftSynthesized. The production caller drives the grid at SynthConfig::rateHz
// (default 240 Hz), NOT the source frame rate, so ¼× replay and the shaft fan read
// a dense series that fills the inter-frame gaps (the interpolation math is
// time-continuous — see synthSampleAt). It REPLACES nothing: samples[] keeps the
// real per-frame track and this series rides alongside.
//
// ⚠ METRICS DO READ IT, and the "display channel only" framing below is no longer the whole truth.
// Scoring, the estimands, the fusion plane fits and the wrist channel all still filter this tier out
// by flag (§2 Layer C). But the metrics that measure the club's PATH take it on purpose:
//   * `clubheadSpeed`, `handSpeed`, `lagAngle`, `clubheadPeakLead` (kinematic_series.cpp) PREFER
//     synth over samples and fall back to samples — a C¹ curve differentiates better than a gappy one.
//   * `clubAngularSpeed` (segment_rates.cpp, the kinematic sequence) likewise prefers it.
//   * `attackAngle` (club_delivery.cpp) prefers the synth arc where it is continuous through impact
//     and falls back to measured heads.
//   * `lowPointAhead` (club_delivery.h) is DEFINED on it, with no fallback: the clubhead detector
//     does not hold a measured lock through impact, so the interpolated arc is the best statement
//     about the club's path there that exists. It ships as an ESTIMATE with a published ±2 in.
//   * the face-on conic plane (shaft_plane.h) has a synth CHANNEL, used when the measured one fails,
//     and shaft fusion (shaft_fusion.h) uses it as the BRIDGE where face-on did not measure (bridged
//     frames are checked against the planes but never fitted).
// The consequence to know: `enabled=false` below no longer changes nothing — the speeds, lag and
// attack angle drop back to the measured samples, and `lowPointAhead` disappears entirely.
//
// SYNTHESIS MODEL (per bracket [a,b] of consecutive anchors, τ = (t−t_a)/(t_b−t_a)):
//   θ(t)   C¹ monotone-safe cubic Hermite through the two anchors' (θ, θ̇). The
//          target θ_b is UNWRAPPED onto θ_a along the track's direction of rotation
//          (expected Δθ = mean anchor slope × Δt) so a pair straddling a large
//          rotation (e.g. 200°) takes the track-consistent branch, not the shortest
//          wrap. Between P anchors the rotation is monotone (that is why the P-times
//          are located as monotone-elevation crossings), so the Fritsch–Carlson
//          slope limiter is a no-op in the common case and C¹ is exact at anchors;
//          it engages only to veto overshoot at a reversal anchor (θ̇≈0 at the top).
//   grip(t) the per-frame HAND track (gripFromHands below), never an interpolation
//          between anchors (2026-09-17). A plain cubic Hermite per axis through the
//          anchor grips is only the fallback for a tick the hand track cannot bracket.
//   L(t)   linear interpolation of the anchor drawn-lengths (lenPx).
//   conf   min(anchor confs) · decay — 1.0 at anchors, midConfFrac at the midpoint
//          (the parabola 4τ(1−τ) peaks 1 in the middle), so a synthesized run reads
//          as least-trustworthy where it is furthest from a real measurement.
//   headPx grip + L·(cos θ, sin θ) — the same image-plane convention as samples[].
//   θ̇      linear interpolation of the anchor rates (legacy viz field), or with
//          SynthConfig::curveRate the ANALYTIC dθ/dt of the Hermite above — the rate
//          the emitted θ(t) actually has, which is what `clubheadSpeed` composes from.
//
// IMPACT IS A BOUNDARY, NOT A KNOT (Sept 2026, clubhead-speed timing). The club's
// angular rate is discontinuous at contact (an iron loses ~20–30 % of its speed in
// two frames), and a Hermite that shares ONE slope at the P7 anchor cannot say so:
// pinned to an under-estimated end slope but forced to cover the true Δθ, the cubic
// bulges and its rate peaks at the bracket MIDPOINT (−18 ms on the 08-18 pairs) then
// decays ~1 mph/ms into P7. The six-argument overload of synthesizeBetweenAnchors
// therefore takes an IN and an OUT rate per anchor: bracket [a,b] uses θ̇_out(a) and
// θ̇_in(b). The five-argument form (in == out) is the legacy behaviour, bit for bit.
//
// C⁰ is exact at anchors by construction (Hermite interpolates its endpoints
// regardless of the slopes), so a synthesized state evaluated AT an anchor equals
// that anchor's geometry. Nothing is emitted outside [first anchor, last anchor].

namespace pinpoint::analysis {

// "synth.*" tuning namespace (design §4). Nested in ShaftV3Config as `synth` (lives
// here beside the Layer-C code, mirroring PositionsConfig in shaft_positions.h).
// enabled defaults ON: the synthesized tier is a visualization channel AND an input
// to the club-path metrics listed in the ⚠ above; the real per-frame track stays in
// samples[]. Set enabled=false to go dark again (ShaftTrack2D.synth stays empty ⇒
// swing.json omits the club.synth block) — which now ALSO moves those metrics onto
// the measured samples and suppresses `lowPointAhead`. Keys
// "synth.enabled" / "synth.midConfFrac" / "synth.rateHz" via ShaftV3Config::fromOverrides.
struct SynthConfig {
    bool   enabled     = true;    // master gate — VIZ tier is live, and the club-path metrics read it (⚠ above)
    double midConfFrac = 0.6;     // conf multiplier at a span midpoint (1.0 at the anchors)
    double rateHz      = 240.0;   // dense visualization cadence (Hz). The series is sampled on
                                  // this FIXED grid, not the source frame rate, so a low-fps or
                                  // gappy capture still yields a smooth ¼×-replay/fan trail.
                                  // <= 0 ⇒ fall back to the source per-frame timestamps.
    bool   curveRate   = true;    // synth.curveRate — thetaDotRadS = analytic dθ/dt of the emitted
                                  // Hermite (slope-limited, in/out-slope aware); what clubheadSpeed
                                  // composes from (ON 2026-09-06). false = the legacy linear
                                  // interpolation of the anchor rates (pre-Sept swing.json, bit for bit).
    // ── The synth may not carry on where the club did not (2026-09-17) ─────────
    // A pitch shot stops short of P10. The tracker coasts, the kinematic model keeps
    // the shaft turning, the ladder's Finish lands on a coasted sample, and a P10
    // anchor is built with an angle the club never reached — then the Hermite from P8
    // sweeps 165° in 80 ms and the fan draws it as the club. Two rules, both in
    // synthesizeLayerC (shaft_track_assembly.cpp):
    //   measuredEndAfterImpact — a bracket that STARTS at or after P7 is bridged only
    //     when its END anchor rests on a measurement (ShaftPosition::timing != Proxy).
    //   envelope* — inside any bracket, a tick's θ may not leave the envelope of the
    //     measured samples within ±envelopeWindowUs by more than envelopeTolDeg; where
    //     the club has stopped the synth stops with it, and the Hermite bridges only
    //     the genuine inter-frame gaps. Clamped ticks get a finite-difference θ̇.
    //   maxFollowThroughRateDps — a bracket that STARTS at or after P8 is bridged only when
    //     the mean rate its anchors imply is one a club past P8 can have. The club is
    //     decelerating from impact by P8; a 190° sweep in 80 ms (2400 °/s) is the tracker
    //     having captured the lead arm, with head confidence to match — which is why the
    //     timing gate above cannot see it and the rate can.
    bool    measuredEndAfterImpact = true;    // synth.measuredEndAfterImpact
    int64_t envelopeWindowUs       = 25000;   // synth.envelopeWindowUs
    double  envelopeTolDeg         = 10.0;    // synth.envelopeTolDeg (<= 0 disables the clamp)
    double  maxFollowThroughRateDps = 1500.0; // synth.maxFollowThroughRateDps (<= 0 disables)
    // ── The curve between the anchors follows the shaft evidence (2026-09-29) ──
    // The Hermite above is set by the anchors alone: every measured frame between two P-positions
    // reached it only through the anchors' smoothed rates and the 10° envelope clamp below — a
    // replay-tier curve that clubhead speed, hand speed, lag and low point then read as a
    // measurement. With fitEvidence the synth's θ is instead the curve that best balances
    //   · FIT — every shaft reading in the bracket (measured/IMU-bridged frames at
    //     evidenceSigmaMeasuredDeg, and each blurred frame's timed trail/mid/lead edges at their
    //     own σ, ShaftWedgeObs), each weighted by 1/σ², against
    //   · PLAUSIBILITY — ∫ (θ̈ / evidenceAccelSigmaDps2)² dt: the club does not change its rate
    //     arbitrarily fast. The larger the scale, the more the curve trusts the evidence.
    // The P-anchors stay HARD (they are the positions' definitions); IMPACT is a break (no
    // plausibility term across contact, where the club loses speed in two frames). Grip, length
    // and head placement are unchanged — only θ, θ̇ and the head derived from them. A stretch with no
    // evidence keeps exactly the Hermite. Both σ's are tuned against the hand-marked frames, which
    // the fit never sees.
    //
    // TUNED 2026-09-29 (32 corpus swings, studio Release, synth θ vs 283 hand-marked frames the fit
    // never sees; |median| error by stretch, and the curve's median peak |θ̈| within ±150 ms of
    // impact):
    //     σ_a (°/s²)   P1–P4   P7–P8 (med / |med|)   peak |θ̈|
    //     anchors only  2.7°   +7.6° / 9.5°            6k   (the Hermite: smooth, and wrong)
    //     1 000         2.1°   +3.9° / 7.8°            8k
    //     2 000         2.1°   +2.8° / 7.3°           12k
    //     5 000         2.0°   +2.3° / 6.5°           18k   ← chosen
    //     20 000        2.0°   +3.7° / 7.0°           36k   (starts chasing reading noise)
    //     80 000        2.1°   +4.7° / 7.0°           71k
    // P4–P7 barely moves at any setting (it was already within ~3°). Clubhead speed moves
    // |0.5| mph, lag |0.2|°; lowPointAhead is produced on the same 27 swings.
    bool    fitEvidence              = true;      // synth.fitEvidence
    double  evidenceAccelSigmaDps2   = 5000.0;    // synth.evidenceAccelSigmaDps2 (°/s²)
    double  evidenceSigmaMeasuredDeg = 3.0;       // synth.evidenceSigmaMeasuredDeg
    // THE BALL AS EVIDENCE (impact_anchor.h). When the address ball was found, the line from the
    // hands to it at the P7 instant is one more reading at ballAnchorSigmaDeg (its error against the
    // 32 hand-marked contacts: sd ~3° without the four wrong-ball swings), and P7's own tracker angle
    // softens to impactAnchorSigmaDeg so the fit can weigh the two. Pinning P7 to the ball outright
    // bent the curve where clubhead speed is read (typical 22 mph, 2026-09-29); as evidence it is
    // balanced against every other reading and the plausibility term instead. Without a ball, P7
    // stays hard and nothing changes.
    //
    // DARK (0) since 2026-09-29, on the corpus evidence: at σ 3° the synth after impact gets closer
    // to the hand marks (P7–P8 |median| 5.9 → 4.9°, p90 13.5 → 8.5°) but its P6–P7 tail worsens
    // (p90 8.2 → 12.5°), and clubhead speed at P7 moves a typical 9 mph (p90 29) both ways with no
    // launch-monitor speed on those swings to say which is right. Switch on (3.0) once a well-lit
    // session with launch-monitor speed shows it helps.
    double  ballAnchorSigmaDeg       = 0.0;       // synth.ballAnchorSigmaDeg (≤ 0 ⇒ the ball is not used)
    double  impactAnchorSigmaDeg     = 8.0;       // synth.impactAnchorSigmaDeg — P7's angle, only when the ball is in
};

namespace synth_detail {

constexpr double kSynthPi = 3.14159265358979323846;

// Unwrap target angle b (rad) onto reference a (rad) along the track's rotation:
// pick the full-turn count so (wrap(b−a) + 2πk) is closest to `expectedDelta` (the
// signed rotation the anchor slopes predict). Returns a continuous θ_b = a + Δ that
// survives arbitrarily large inter-anchor rotations.
inline double unwrapTowards(double a, double b, double expectedDelta)
{
    const double dRaw  = std::remainder(b - a, 2.0 * kSynthPi);            // ∈ (−π, π]
    const double turns = std::round((expectedDelta - dRaw) / (2.0 * kSynthPi));
    return a + dRaw + turns * 2.0 * kSynthPi;
}

// Fritsch–Carlson limiting of the endpoint derivatives (θ path): derivatives opposite
// in sign to the secant are zeroed and the pair is scaled back inside the α²+β²≤9
// monotone region — so the curve never bulges past [p0,p1]. Split out so the value
// AND the derivative below are evaluated with the SAME limited slopes.
inline void limitMonotone(double p0, double p1, double& m0, double& m1)
{
    const double delta = p1 - p0;
    if (delta == 0.0) { m0 = 0.0; m1 = 0.0; return; }
    double a = m0 / delta, b = m1 / delta;
    if (a < 0.0) { m0 = 0.0; a = 0.0; }
    if (b < 0.0) { m1 = 0.0; b = 0.0; }
    const double s = a * a + b * b;
    if (s > 9.0) {
        const double tau = 3.0 / std::sqrt(s);
        m0 = tau * a * delta;
        m1 = tau * b * delta;
    }
}

// Cubic Hermite value at unit parameter τ∈[0,1] with the slopes AS GIVEN (already
// limited, or deliberately unlimited). The value at τ=0/1 is p0/p1 EXACTLY.
inline double hermiteValue(double p0, double p1, double m0, double m1, double t)
{
    const double t2 = t * t, t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * p0 + (t3 - 2.0 * t2 + t) * m0
         + (-2.0 * t3 + 3.0 * t2) * p1 + (t3 - t2) * m1;
}

// dP/dτ of the same cubic — equals m0 at τ=0 and m1 at τ=1 exactly.
inline double hermiteDeriv(double p0, double p1, double m0, double m1, double t)
{
    const double t2 = t * t;
    return (6.0 * t2 - 6.0 * t) * p0 + (3.0 * t2 - 4.0 * t + 1.0) * m0
         + (-6.0 * t2 + 6.0 * t) * p1 + (3.0 * t2 - 2.0 * t) * m1;
}

// Cubic Hermite value at unit parameter τ∈[0,1]. p0/p1 = endpoint values; m0/m1 =
// endpoint derivatives w.r.t. τ (i.e. dP/dτ). `monotone` applies the Fritsch–Carlson
// limiter above. The value at τ=0/1 is p0/p1 EXACTLY regardless of limiting.
inline double hermite(double p0, double p1, double m0, double m1, double t, bool monotone)
{
    if (monotone) limitMonotone(p0, p1, m0, m1);
    return hermiteValue(p0, p1, m0, m1, t);
}

} // namespace synth_detail

// The synthesized shaft state at time `t` within the bracket of consecutive anchors
// [a, b] given each anchor's θ̇ (rad/s) and grip velocity (px/s). `t` may equal an
// endpoint ⇒ that anchor's geometry EXACTLY (C⁰ contract; public so the continuity
// gate is directly unit-testable). Emits a ShaftSample2D flagged ShaftSynthesized.
inline ShaftSample2D synthSampleAt(const ShaftPosition& a, double thetaDotA, const QPointF& gripVelA,
                                   const ShaftPosition& b, double thetaDotB, const QPointF& gripVelB,
                                   int64_t t, const SynthConfig& cfg)
{
    using namespace synth_detail;
    ShaftSample2D s;

    const double dtUs = double(b.t_us - a.t_us);
    const double hSec = dtUs * 1e-6;                                  // bracket duration (s)
    const double tau  = dtUs > 0.0 ? double(t - a.t_us) / dtUs : 0.0; // ∈ [0,1] for in-bracket t

    // θ — unwrap b onto a along the track direction, then monotone-safe Hermite. The
    // limited slopes are kept so the analytic rate below is the rate of THIS curve.
    const double expected = 0.5 * (thetaDotA + thetaDotB) * hSec;     // signed expected Δθ (rad)
    const double thB      = unwrapTowards(a.thetaRad, b.thetaRad, expected);
    double mA = thetaDotA * hSec, mB = thetaDotB * hSec;
    limitMonotone(a.thetaRad, thB, mA, mB);
    const double theta    = hermiteValue(a.thetaRad, thB, mA, mB, tau);

    // grip — plain cubic Hermite per axis (grip path is an arc, not monotone).
    const double gxp = hermite(a.gripPx.x(), b.gripPx.x(), gripVelA.x() * hSec, gripVelB.x() * hSec, tau, false);
    const double gyp = hermite(a.gripPx.y(), b.gripPx.y(), gripVelA.y() * hSec, gripVelB.y() * hSec, tau, false);

    // L — linear interpolation of the anchor extents (fall back to whichever anchor
    // has a valid length when the other abstains, lenPx < 0).
    const double la = a.lenPx, lb = b.lenPx;
    double len;
    if (la > 0.0 && lb > 0.0)  len = la + (lb - la) * tau;
    else if (la > 0.0)         len = la;
    else if (lb > 0.0)         len = lb;
    else                       len = 0.0;

    // conf — min(anchor confs) decayed toward the midpoint.
    const double base  = std::min(double(a.conf), double(b.conf));
    const double decay = 1.0 - (1.0 - cfg.midConfFrac) * 4.0 * tau * (1.0 - tau);

    s.t_us         = t;
    s.gripPx       = QPointF{ gxp, gyp };
    s.thetaRad     = theta;
    // θ̇ — the analytic rate of the emitted curve (curveRate), or the legacy linear
    // interpolation of the anchor rates. With curveRate (ON) this is what
    // clubheadSpeed composes from.
    s.thetaDotRadS = (cfg.curveRate && hSec > 0.0)
                   ? hermiteDeriv(a.thetaRad, thB, mA, mB, tau) / hSec
                   : thetaDotA + (thetaDotB - thetaDotA) * tau;
    s.visibleLenPx = len;
    s.conf         = float(base * decay);
    s.flags        = ShaftSynthesized;
    s.headPx       = QPointF{ gxp + len * std::cos(theta), gyp + len * std::sin(theta) };
    return s;
}

// ── The grip is the HANDS, never an interpolation (2026-09-17) ───────────────────
//
// The measured samples honour the design's hard rule — every sample's grip is the
// per-frame hand-axis point from pose. The synth tier used to Hermite the grip
// between the two bracketing anchors' grips instead, so wherever an anchor was
// missing or mis-placed (a lost P2 on a dark clip) the grip path swung free of the
// hands by hundreds of pixels while the fan drew it as if it were the club. The
// hands are tracked on every frame; there is never a reason to invent a grip.
//
// `HandGripTrack` is the per-frame hand-axis grip (px) on the pose timebase — the
// same gx/gy the samples were built from — and gripFromHands() reads it at any
// instant by linear interpolation between the bracketing FINITE frames. Frames the
// pose could not resolve are NaN; a tick whose nearest finite neighbours are further
// apart than `maxGapUs` is treated as unbracketed and the caller falls back to the
// anchor Hermite for that tick only. θ and length keep their Hermite; the head is
// re-derived from the hand grip so the drawn shaft stays a rigid line.
struct HandGripTrack {
    const std::vector<int64_t>* tUs = nullptr;   // ascending
    const std::vector<double>*  x   = nullptr;   // px; NaN = unresolved frame
    const std::vector<double>*  y   = nullptr;
    int64_t maxGapUs = 40000;                    // widest bracket the interpolation may span
    bool available() const
    {
        return tUs && x && y && !tUs->empty() && x->size() == tUs->size() && y->size() == tUs->size();
    }
};

inline bool gripFromHands(const HandGripTrack& h, int64_t t, QPointF& out)
{
    if (!h.available()) return false;
    const std::vector<int64_t>& T = *h.tUs;
    const std::vector<double>&  X = *h.x;
    const std::vector<double>&  Y = *h.y;
    const size_t n = T.size();
    // First frame at or after t (ascending grid).
    size_t hi = size_t(std::lower_bound(T.begin(), T.end(), t) - T.begin());
    // Walk to the nearest FINITE frames either side.
    long a = long(hi) - 1, b = long(hi);
    while (a >= 0 && (!std::isfinite(X[size_t(a)]) || !std::isfinite(Y[size_t(a)]))) --a;
    while (b < long(n) && (!std::isfinite(X[size_t(b)]) || !std::isfinite(Y[size_t(b)]))) ++b;
    const bool haveA = a >= 0, haveB = b < long(n);
    if (haveA && haveB) {
        if (T[size_t(b)] - T[size_t(a)] > h.maxGapUs) return false;
        const double den  = double(T[size_t(b)] - T[size_t(a)]);
        const double frac = den > 0.0 ? double(t - T[size_t(a)]) / den : 0.0;
        out = QPointF{ X[size_t(a)] + (X[size_t(b)] - X[size_t(a)]) * frac,
                       Y[size_t(a)] + (Y[size_t(b)] - Y[size_t(a)]) * frac };
        return true;
    }
    // One-sided: hold the nearest finite frame only when it is within the gap.
    if (haveA && t - T[size_t(a)] <= h.maxGapUs) { out = QPointF{ X[size_t(a)], Y[size_t(a)] }; return true; }
    if (haveB && T[size_t(b)] - t <= h.maxGapUs) { out = QPointF{ X[size_t(b)], Y[size_t(b)] }; return true; }
    return false;
}

// Emit synthesized samples between the located P-anchors. `anchors` MUST be sorted
// strictly ascending by t_us (the caller passes ShaftTrack2D.positions, which is
// ordered by p ⇒ by time); `thetaDotRadS` / `gripVelPxS` are the smoothed-track θ̇
// (rad/s) and grip velocity (px/s) sampled at each anchor instant (parallel to
// `anchors`). `frameTUs` are the per-frame camera timestamps (ascending). Produces
// one sample per frame STRICTLY between each consecutive-anchor pair — so the result
// is ascending in t_us, all flagged ShaftSynthesized, and empty outside
// [first anchor, last anchor] or when < 2 anchors are given.
// Two rates per anchor: bracket [a_k, a_k+1] leaves a_k at thetaDotOutRadS[k] and
// arrives at a_k+1 at thetaDotInRadS[k+1]. An anchor whose in and out rates differ
// (P7: the impact step) is a C⁰ knot with a rate discontinuity — deliberately.
// `hands` — when available — is where every tick's grip comes from (see the note
// above); the anchor Hermite is the fallback for a tick the hand track cannot
// bracket. Pass a default-constructed HandGripTrack for the legacy behaviour.
inline std::vector<ShaftSample2D> synthesizeBetweenAnchors(
    const std::vector<ShaftPosition>& anchors,
    const std::vector<double>&        thetaDotInRadS,
    const std::vector<double>&        thetaDotOutRadS,
    const std::vector<QPointF>&       gripVelPxS,
    const std::vector<int64_t>&       frameTUs,
    const SynthConfig&                cfg,
    const HandGripTrack&              hands)
{
    std::vector<ShaftSample2D> out;
    const size_t n = anchors.size();
    if (n < 2 || thetaDotInRadS.size() != n || thetaDotOutRadS.size() != n
        || gripVelPxS.size() != n) return out;

    for (size_t k = 0; k + 1 < n; ++k) {
        const ShaftPosition& a = anchors[k];
        const ShaftPosition& b = anchors[k + 1];
        if (b.t_us <= a.t_us) continue;                     // defensive: strictly increasing
        for (int64_t t : frameTUs) {
            if (t <= a.t_us || t >= b.t_us) continue;       // STRICTLY between the anchors
            ShaftSample2D s = synthSampleAt(a, thetaDotOutRadS[k],    gripVelPxS[k],
                                            b, thetaDotInRadS[k + 1], gripVelPxS[k + 1], t, cfg);
            QPointF g;
            if (gripFromHands(hands, t, g)) {
                s.gripPx = g;
                s.headPx = QPointF{ g.x() + s.visibleLenPx * std::cos(s.thetaRad),
                                    g.y() + s.visibleLenPx * std::sin(s.thetaRad) };
            }
            out.push_back(s);
        }
    }
    return out;
}

inline std::vector<ShaftSample2D> synthesizeBetweenAnchors(
    const std::vector<ShaftPosition>& anchors,
    const std::vector<double>&        thetaDotInRadS,
    const std::vector<double>&        thetaDotOutRadS,
    const std::vector<QPointF>&       gripVelPxS,
    const std::vector<int64_t>&       frameTUs,
    const SynthConfig&                cfg)
{
    return synthesizeBetweenAnchors(anchors, thetaDotInRadS, thetaDotOutRadS, gripVelPxS, frameTUs,
                                    cfg, HandGripTrack{});
}

// One shaft reading the synthetic track is fitted to (SynthConfig::fitEvidence).
struct SynthEvidence {
    int64_t t_us     = 0;
    double  thetaRad = 0.0;
    double  sigmaRad = 0.0;
};

// Refit the θ of `synth` (sorted by time, as synthesizeLayerC emits it) to `evidence` — see
// SynthConfig::fitEvidence. Works stretch by stretch: a stretch is a run of consecutive brackets that
// all carry synth ticks, with its anchors as fixed nodes and its ticks as the unknowns. Minimises
//   Σ_evidence ((θ(t_e) − y_e)/σ_e)²  +  Σ_nodes (θ̈_j / σ_a)² · Δt_j
// with θ(t) linear between nodes and θ̈ the second divided difference, skipped at a P7 node. The
// synth ticks' θ̇ become the central difference of the fitted nodes and the head is re-derived.
// Returns the number of evidence readings used.
inline int fitSynthToEvidence(std::vector<ShaftSample2D>&        synth,
                              const std::vector<ShaftPosition>&  anchors,
                              const std::vector<SynthEvidence>&  evidence,
                              const SynthConfig&                 cfg,
                              bool                               softImpact = false)
{
    using namespace synth_detail;
    if (!cfg.fitEvidence || synth.empty() || anchors.size() < 2 || evidence.empty()
        || !(cfg.evidenceAccelSigmaDps2 > 0.0)) return 0;
    const size_t nA = anchors.size();
    // Bracket of each tick.
    std::vector<int> brk(synth.size(), -1);
    for (size_t j = 0; j < synth.size(); ++j)
        for (size_t k = 0; k + 1 < nA; ++k)
            if (synth[j].t_us > anchors[k].t_us && synth[j].t_us < anchors[k + 1].t_us) { brk[j] = int(k); break; }
    const double sigA = cfg.evidenceAccelSigmaDps2 * kSynthPi / 180.0;   // rad/s²
    int usedTotal = 0;
    size_t j0 = 0;
    while (j0 < synth.size()) {
        if (brk[j0] < 0) { ++j0; continue; }
        // A stretch: ticks whose brackets are consecutive with no bracket skipped.
        size_t j1 = j0;
        while (j1 + 1 < synth.size() && brk[j1 + 1] >= 0
               && (brk[j1 + 1] == brk[j1] || brk[j1 + 1] == brk[j1] + 1)) ++j1;
        const int k0 = brk[j0], k1 = brk[j1];
        // Nodes in time order: anchor k0, ticks, anchor k0+1, … anchor k1+1.
        struct Node { int64_t t; double th; bool fixed; bool impact; long tick; };
        std::vector<Node> nd;
        size_t j = j0;
        for (int k = k0; k <= k1 + 1; ++k) {
            const bool softP7 = anchors[size_t(k)].p == 7 && cfg.impactAnchorSigmaDeg > 0.0 && softImpact;
            nd.push_back({ anchors[size_t(k)].t_us, anchors[size_t(k)].thetaRad, !softP7,
                           anchors[size_t(k)].p == 7, softP7 ? -2L : -1L });
            while (j <= j1 && brk[j] == k) { nd.push_back({ synth[j].t_us, synth[j].thetaRad, false, false, long(j) }); ++j; }
        }
        for (size_t q = 1; q < nd.size(); ++q)                     // one continuous sheet
            nd[q].th = nd[q - 1].th + std::remainder(nd[q].th - nd[q - 1].th, 2.0 * kSynthPi);
        const size_t N = nd.size();
        std::vector<int> ui(N, -1);
        int nu = 0;
        for (size_t q = 0; q < N; ++q) if (!nd[q].fixed) ui[q] = nu++;
        // Normal equations  M x = r  over the free nodes.
        std::vector<double> M(size_t(nu) * size_t(nu), 0.0), r(size_t(nu), 0.0);
        const auto addRow = [&](const size_t* idx, const double* c, int m, double y, double w) {
            double rhs = y;                                          // move fixed nodes to the rhs
            for (int a = 0; a < m; ++a) if (ui[idx[a]] < 0) rhs -= c[a] * nd[idx[a]].th;
            for (int a = 0; a < m; ++a) {
                const int ia = ui[idx[a]];
                if (ia < 0) continue;
                r[size_t(ia)] += w * c[a] * rhs;
                for (int b = 0; b < m; ++b) {
                    const int ib = ui[idx[b]];
                    if (ib >= 0) M[size_t(ia) * size_t(nu) + size_t(ib)] += w * c[a] * c[b];
                }
            }
        };
        int used = 0;
        for (const SynthEvidence& e : evidence) {
            if (e.t_us <= nd.front().t || e.t_us >= nd.back().t || !(e.sigmaRad > 0.0)) continue;
            size_t q = 1;
            while (q < N && nd[q].t < e.t_us) ++q;
            const size_t idx[2] = { q - 1, q };
            const double u = double(e.t_us - nd[q - 1].t) / double(std::max<int64_t>(1, nd[q].t - nd[q - 1].t));
            const double c[2] = { 1.0 - u, u };
            const double cur = c[0] * nd[q - 1].th + c[1] * nd[q].th;
            const double y = cur + std::remainder(e.thetaRad - cur, 2.0 * kSynthPi);
            addRow(idx, c, 2, y, 1.0 / (e.sigmaRad * e.sigmaRad));
            ++used;
        }
        if (used > 0 && nu > 0) {
            // A soft P7 (tick == -2): its tracker angle is one more reading, at impactAnchorSigmaDeg.
            for (size_t q = 0; q < N; ++q) {
                if (nd[q].tick != -2) continue;
                const size_t idx[1] = { q };
                const double c[1] = { 1.0 };
                const double sP = cfg.impactAnchorSigmaDeg * kSynthPi / 180.0;
                addRow(idx, c, 1, nd[q].th, 1.0 / (sP * sP));
            }
            for (size_t q = 1; q + 1 < N; ++q) {
                if (nd[q].impact) continue;                          // contact is a break
                const double h0 = double(nd[q].t - nd[q - 1].t) * 1e-6, h1 = double(nd[q + 1].t - nd[q].t) * 1e-6;
                if (!(h0 > 0.0 && h1 > 0.0)) continue;
                const double hm = 0.5 * (h0 + h1);
                const size_t idx[3] = { q - 1, q, q + 1 };
                const double c[3] = { 1.0 / (h0 * hm), -(1.0 / h0 + 1.0 / h1) / hm, 1.0 / (h1 * hm) };
                addRow(idx, c, 3, 0.0, hm / (sigA * sigA));
            }
            // Cholesky (M is SPD: every free node is tied to its neighbours by the penalty).
            bool ok = true;
            std::vector<double> L = M;
            for (int a = 0; a < nu && ok; ++a) {
                for (int b = 0; b <= a; ++b) {
                    double sum = L[size_t(a) * size_t(nu) + size_t(b)];
                    for (int k = 0; k < b; ++k) sum -= L[size_t(a) * size_t(nu) + size_t(k)] * L[size_t(b) * size_t(nu) + size_t(k)];
                    if (a == b) {
                        if (!(sum > 0.0)) { ok = false; break; }
                        L[size_t(a) * size_t(nu) + size_t(a)] = std::sqrt(sum);
                    } else {
                        L[size_t(a) * size_t(nu) + size_t(b)] = sum / L[size_t(b) * size_t(nu) + size_t(b)];
                    }
                }
            }
            if (ok) {
                std::vector<double> x(size_t(nu), 0.0);
                for (int a = 0; a < nu; ++a) {
                    double sum = r[size_t(a)];
                    for (int k = 0; k < a; ++k) sum -= L[size_t(a) * size_t(nu) + size_t(k)] * x[size_t(k)];
                    x[size_t(a)] = sum / L[size_t(a) * size_t(nu) + size_t(a)];
                }
                for (int a = nu - 1; a >= 0; --a) {
                    double sum = x[size_t(a)];
                    for (int k = a + 1; k < nu; ++k) sum -= L[size_t(k) * size_t(nu) + size_t(a)] * x[size_t(k)];
                    x[size_t(a)] = sum / L[size_t(a) * size_t(nu) + size_t(a)];
                }
                for (size_t q = 0; q < N; ++q) if (ui[q] >= 0) nd[q].th = x[size_t(ui[q])];
                for (size_t q = 0; q < N; ++q) {
                    if (nd[q].tick < 0) continue;
                    ShaftSample2D& s = synth[size_t(nd[q].tick)];
                    s.thetaRad = nd[q].th;
                    const size_t a = q > 0 ? q - 1 : q, b = q + 1 < N ? q + 1 : q;
                    const double dt = double(nd[b].t - nd[a].t) * 1e-6;
                    if (dt > 0.0) s.thetaDotRadS = (nd[b].th - nd[a].th) / dt;
                    s.headPx = QPointF{ s.gripPx.x() + s.visibleLenPx * std::cos(s.thetaRad),
                                        s.gripPx.y() + s.visibleLenPx * std::sin(s.thetaRad) };
                }
                usedTotal += used;
            }
        }
        j0 = j1 + 1;
    }
    return usedTotal;
}

// Legacy single-rate form: in == out at every anchor (C¹ everywhere).
inline std::vector<ShaftSample2D> synthesizeBetweenAnchors(
    const std::vector<ShaftPosition>& anchors,
    const std::vector<double>&        thetaDotRadS,
    const std::vector<QPointF>&       gripVelPxS,
    const std::vector<int64_t>&       frameTUs,
    const SynthConfig&                cfg)
{
    return synthesizeBetweenAnchors(anchors, thetaDotRadS, thetaDotRadS, gripVelPxS, frameTUs, cfg);
}

} // namespace pinpoint::analysis
