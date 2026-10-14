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

// P-ALIGNMENT — every cross-shot analysis runs on P-aligned time
// (docs/design/charts_panel_traces_relations.md §4.1; Mark, 14 Oct: "when working with multiple shots
// they should be aligned to P positions before doing the next level of analysis"; D9: P-aligned time
// only, PCHIP through gated anchors, impact-only falls back to curve landmarks, then a labelled tempo
// scale).
//
// ── WHAT IT PRODUCES ─────────────────────────────────────────────────────────────────────────
// alignSession() reads a SessionSnapshot once and returns a SessionAlignment:
//   · the CANONICAL P-TIMES: for every P-position kept on at least half the shots, the session
//     median of its time from impact (the canonical axis puts that P there), plus q1/q3/lo/hi/n of
//     when it actually happened, on the shots' own clocks — the "middle half of when it happened"
//     annotation the panel writes under each P tag, which is the timing the warp removes;
//   · one ShotWarp per shot: its anchors, EVERY candidate anchor with the gate that kept or dropped
//     it, the ROUTE that aligned it (P-anchors / landmarks / tempo-fitted / impact shift / unaligned,
//     so the panel can say which), and warpMs / unwarpMs between its own clock and the canonical axis.
//
// ── THE GATES (why each threshold) ───────────────────────────────────────────────────────────
// An anchor is a P-position the shot and the canonical ladder share. Impact is ALWAYS the anchor
// (0, 0): it is the origin of "ms from impact" on both axes and the one instant every route agrees on.
// The other candidates are walked OUTWARD FROM IMPACT on each side (P6, P5, P4, P3, P2, P1 before it;
// P8, P9, P10 after), each judged against the nearest anchor already kept on its impact side (its
// "inner neighbour"), so an error is blamed on the instant further from the ball, never on impact:
//
//   1. LOW CONFIDENCE — conf < 0.35. The phase producers' conf is a producer-local quality hint, but
//      three values in it are declared constants (swing_analysis.h TimingClass: 0.35 "proxy", 0.30
//      "fallback", 0.20 "clamp"). Below 0.35 the time was a fallback or a clamp, not a detection, and
//      a warp must not bend every curve of a shot to a guess. A missing (NaN) conf is not a low one.
//      4 July: drops shot 9's P8 (+99.6 ms, conf 0.30) — the only fallback-grade P on the session.
//   2. RUNS BACKWARDS — the candidate is not strictly further from impact than its inner neighbour on
//      the shot's clock, or (pass 2) on the canonical axis. A time map that folds is not a time map.
//   3. OUTLIER SEGMENT — the segment from the inner neighbour to the candidate is more than 2× or less
//      than ½× the session median of the same segment (the median over every shot that kept both
//      ends through gate 1; judged only when at least 3 shots have it). Why a RATIO and why 2: within
//      one session a golfer's segment lengths vary by tempo, and on 4 July the worst real variation of
//      any P-to-P segment is ×0.81…×1.20 of its median. A MAD-based z is useless here because the MAD
//      is tiny (2 ms on impact→P8), so it would cut real ±15 ms variation; a phase-ladder failure, by
//      contrast, is a factor, not a few per cent: shot 9's Finish at +965 ms makes its last segment
//      ×3.5 (impact→P10, once its P8 is gone) / ×4.7 (P8→P10), and 06-11 swing 1's P8 3.8 ms after
//      impact makes impact→P8 ×0.045. A factor of 2 sits an order of magnitude from both.
//      4 July: drops shot 9's Finish (+964.7 ms) and nothing else.
//   4. NOT CANONICAL — the P is kept on fewer than half the shots, so the session has no time for it.
//
// Canonical times are the medians of the anchors that SURVIVED gates 1–3 (pass 1); the walk then
// runs again (pass 2) adding gate 4 and the canonical-axis half of gate 2. A dropped anchor is simply
// absent: its neighbours bridge it.
//
// ── THE WARP (two interpolants through the same anchors) ─────────────────────────────────────
//   · Linear — exactly the mocks' piecewise-linear warp: each segment stretched uniformly, a pure
//     shift outside the first and last anchors. Kept for the golden tests and as an option.
//   · Pchip — THE BUILD DEFAULT: a monotone cubic Hermite through the same anchors (Fritsch–Butland
//     interior slopes, every slope held strictly inside the Fritsch–Carlson box (0, 3δ)), so it lands
//     every anchor exactly and is STRICTLY increasing, which makes unwarp exact (bisection on the
//     bracketing segment to the last bit). The stretch varies smoothly, so curves get no kinks at the
//     P-positions. The end slopes are the usual one-sided three-point estimate (held inside the box,
//     floored at 5 % of the secant so the end segment stays strictly increasing). Outside the end
//     anchors it is a pure shift, as Linear: the one kink left is at P1 / P10, outside every window
//     the panel analyses. (Forcing slope 1 there instead would bend the whole P1→P2 and P8→P10
//     segments by up to ~4/27 · h · the tempo difference — several ms of invented timing — to smooth
//     a join no curve is read across.)
//   Not dynamic time warping, which aligns noise and can hide a real timing fault (design §4.1).
//
// ── WHEN ONLY IMPACT IS KNOWN (in order) ─────────────────────────────────────────────────────
//   1. LANDMARKS found in the shot's own curves, each gated on its data, each a real anchor. Read on
//      the hand-speed curve (the first of WarpOptions::landmarkKeys found on at least half the shots
//      — handSpeed on 4 July; clubheadSpeed is bridged after impact and clubAngularSpeed only starts
//      at the top, so neither can carry all three). On this data club angular speed is an unsigned
//      magnitude, so "the club angular-speed zero crossing at the change of direction" is read as the
//      speed's DIP between the backswing and the downswing; on 4 July it lands within ±10 ms of P4.
//        · SpeedPeak — the windowed mean's maximum in [−200, +40] ms; gated: not at the search
//          window's edge, and a PEAK, not a plateau: the curve 40 ms either side is measured and
//          ≤ 98 % of it.
//        · ChangeOfDirection — the minimum in [peak − 600, peak − 60] ms (impact if no peak); gated:
//          interior, ≤ 25 % of the downswing peak, and a real dip (the backswing maximum before it is
//          at least twice it).
//        · MotionOnset — walking back from the backswing maximum (in [dip − 900, dip − 50] ms), the
//          crossing of 25 % of it, interpolated between samples; gated: found, with a measured sample
//          before it (a curve that starts moving has no onset in it).
//      A landmark's canonical time is the median over the P-anchored shots of its WARPED time when at
//      least 3 have it (so it lands on the canonical axis), else the median of its raw times. Landmark
//      anchors pass gates 2 and 3 against those canonical times exactly as P-anchors do.
//   2. TEMPO — one scale per shot about impact (canonMs = s · shotMs), s in [0.67, 1.5], fitted by
//      least squares to the session's median curve of the same key over [canonical P1 (or −900 ms),
//      +60 ms] — the aligned shots' median when at least 3 are aligned, else every shot's at impact.
//      Needs ≥ 60 % of the window overlapped; refused when the best s sits on the search bound. It is
//      FLAGGED fitted, because a tempo scale can fold a timing difference into a shape difference.
//   3. Otherwise a pure shift at impact (identity in ms from impact): "only impact is known: shots are
//      lined up at impact, tempo differences stay in the curves".
// A shot with no impact (impactUs < 0) is Unaligned and takes part in no cross-shot analysis.
//
// WarpOptions::gates = false reproduces the mocks' ladder exactly (no confidence or segment gate; the
// canonical median over every shot that has the P; the mocks' forward "drop what runs backwards" walk)
// — for the golden tests against variant_a/lib/traces.js. The build always runs with gates on.

#include "session_curves.h"
#include "session_series_types.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace pinpoint::charts {

// Phase enum ids (swing_analysis.h Phase) the alignment names.
namespace phase {
constexpr int Address = 0, Top = 2, Impact = 5, Finish = 7, P3 = 8, P6 = 9, P9 = 11, P2 = 12, P5 = 13, P8 = 14;
}

// The P-ladder in swing order: P1 P2 P3 P4 P5 P6 P7 P8 P9 P10 (TimelineLabels' P tags; Takeaway,
// Transition, Downswing, Release and MaxSpeed carry none and are never anchors).
const std::vector<int> &pLadder();

enum class WarpInterp { Linear, Pchip };

// How a shot was put on the canonical axis — the panel says it (design §4.1).
enum class AlignRoute {
    PAnchors,      // at least one P-position besides impact survived the gates
    Landmarks,     // impact only; curve landmarks found in its own data carried it
    TempoFitted,   // impact only, no landmark; one tempo scale about impact, FITTED to the median curve
    ImpactShift,   // impact only and nothing else: lined up at impact, nothing dilated
    Unaligned      // no impact: not on the axis at all
};

// What happened to a candidate anchor.
enum class AnchorGate { Kept, LowConfidence, RunsBackwards, OutlierSegment, NotCanonical };

enum class Landmark { None = -1, MotionOnset = 0, ChangeOfDirection = 1, SpeedPeak = 2 };

struct WarpOptions {
    WarpInterp interp = WarpInterp::Pchip;
    bool   gates = true;                 // false = the mocks' ladder exactly (see the header)
    double minConf = 0.35;               // gate 1
    double maxSegmentRatio = 2.0;        // gate 3: outside [1/r, r] × the session median segment
    int    minSegmentShots = 3;          // gate 3 judges a segment only when this many shots have it
    bool   useLandmarks = true;          // fallback 1
    bool   useTempo = true;              // fallback 2
    QStringList landmarkKeys { QStringLiteral("handSpeed"), QStringLiteral("clubheadSpeed"),
                               QStringLiteral("clubAngularSpeed") };
    double tempoMin = 0.67, tempoMax = 1.5, tempoStep = 0.005;
};

// A canonical P-time. tMs is ON THE CANONICAL AXIS (it is where the axis puts the P); q1/q3/lo/hi are
// when the P happened ON THE SHOTS' OWN CLOCKS (ms from impact) — the timing the warp removes.
struct CanonicalPhase {
    int    phase = -1;
    double tMs = kNaN;
    double q1Ms = kNaN, q3Ms = kNaN, loMs = kNaN, hiMs = kNaN;
    int    n = 0;                        // shots whose anchor for this P survived the gates
};

// A landmark's canonical time (canonical axis) and the key it was read on.
struct CanonicalLandmark {
    Landmark which = Landmark::None;
    QString  key;
    double   tMs = kNaN;
    int      nFound = 0;                 // shots it was found on
    bool     fromAligned = false;        // median of P-anchored shots' warped times (else raw times)
};

// One anchor: shot clock ms ↔ canonical ms. A P-anchor has phase ≥ 0; a landmark has phase −1.
struct WarpAnchor {
    int      phase = -1;
    Landmark landmark = Landmark::None;
    double   shotMs = kNaN;
    double   canonMs = kNaN;
};

// Every candidate considered for a shot, kept or not, and why.
struct GatedAnchor {
    int        phase = -1;
    Landmark   landmark = Landmark::None;
    double     shotMs = kNaN;            // shot clock
    double     conf = kNaN;
    AnchorGate gate = AnchorGate::Kept;
    double     segmentMs = kNaN;         // the segment gate 3 judged (shot clock), NaN when not judged
    double     sessionSegmentMs = kNaN;  // the session median it was judged against
};

// One shot's map between its own clock and the canonical axis.
struct ShotWarp {
    int        ordinal = 0;
    qint64     impactUs = -1;
    AlignRoute route = AlignRoute::Unaligned;
    WarpInterp interp = WarpInterp::Pchip;
    double     tempoScale = kNaN;        // TempoFitted only: canonMs = tempoScale · shotMs
    std::vector<WarpAnchor>  anchors;    // ascending in both shotMs and canonMs; impact included
    std::vector<GatedAnchor> considered;
    std::vector<double>      slopes;     // Pchip: d canon / d shot at each anchor

    bool aligned() const { return route != AlignRoute::Unaligned; }
    int  droppedCount() const;

    double warpMs(double shotMs) const;      // shot clock → canonical axis
    double unwarpMs(double canonMs) const;   // canonical axis → shot clock (exact inverse)

    // The conversions to and from the shot's window-relative µs (the clock shotReplay seeks on).
    double shotMsAt(qint64 tUs) const { return double(tUs - impactUs) / 1000.0; }
    qint64 usAtShotMs(double shotMs) const;
    double canonMsAt(qint64 tUs) const { return warpMs(shotMsAt(tUs)); }
    qint64 usAtCanonMs(double canonMs) const { return usAtShotMs(unwarpMs(canonMs)); }
};

// A warp through explicit anchors (ascending, strictly, in both coordinates — the caller's duty; the
// gates guarantee it). One anchor = a pure shift; none = identity.
ShotWarp makeWarp(std::vector<WarpAnchor> anchors, WarpInterp interp);

struct SessionAlignment {
    WarpOptions options;
    std::vector<CanonicalPhase>    canonical;   // ladder order; only P kept on ≥ half the shots
    std::vector<CanonicalLandmark> landmarks;   // those found on enough shots to have a canonical time
    std::vector<ShotWarp>          shots;       // parallel to SessionSnapshot::shots

    const ShotWarp       *warpFor(int ordinal) const;
    const CanonicalPhase *canonicalOf(int phase) const;
    int countRoute(AlignRoute r) const;
};

SessionAlignment alignSession(const SessionSnapshot &session, const WarpOptions &options = {});

// The landmarks found on one shot's curve (shot clock), gated as the header says. found = false
// with a NaN time when a landmark's gate refused it. Exposed for the tests and the panel's notes.
struct LandmarkFind {
    Landmark which = Landmark::None;
    bool     found = false;
    double   shotMs = kNaN;
};
std::vector<LandmarkFind> findLandmarks(const TraceCurve &shotClockCurve);

// A shot's curve moved onto the canonical axis by its warp (values untouched). Empty when the curve
// is empty or the shot is unaligned.
TraceCurve alignedCurve(const TraceCurve &shotClockCurve, const ShotWarp &warp);

} // namespace pinpoint::charts
