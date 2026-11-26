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

// DIRECTION A, "THE INSTRUMENT" — the arithmetic behind the Session view's curves
// (docs/design/charts_panel_traces_relations.md §2.2, §4; ported from variant_a/lib/traces.js and
// TraceModel.qml, whose numbers the golden tests pin).
//
// EVERYTHING HERE RUNS ON THE CANONICAL (P-ALIGNED) AXIS. Curves come in through alignedCurves()
// (session_warp.h's warp applied to the engine's windowed mean); times out are canonical ms from
// impact (`tCanonMs`, or the `t` of a curve or envelope whose axis is CanonicalMs). A time shown to the
// reader goes back through ShotWarp::unwarpMs first (design rule 4). Values are never warped.
//
// ── THE GROUP FIT (§4, D2) — ONE ENGINE ──────────────────────────────────────────────────────
// pooledFit is the only curve-fitting engine in the panel: Session (groups from facets or shapes),
// Kinds (groups = kinds) and This shot ("shots like this one") all call it. At each grid instant x it
// fits a straight line, weighted by the tricube (1 − |d/h|³)³, to the POOLED VALID RAW samples of the
// group's shots within x ± h. h = 20 ms by default: the windowed mean's own 40 ms support, so the fit
// claims no finer detail than a single shot's curve does. Bridged samples are not measurements and
// never enter it. Out: v (the fit), sd (the weighted spread of the pooled samples about it — THE BAND,
// D2), se (the fit's own standard error, available but not drawn), nShots and nSamples in the window.
//
// HONESTY RULES (contract rule 3), enforced here rather than left to the panel:
//   · a group under 3 shots is REFUSED (FitRefusal::GroupTooSmall, no curve at all);
//   · a grid instant with fewer than 3 contributing shots, or fewer than 8 samples, is blank (NaN);
//   · A SHOT CONTRIBUTES AT x ONLY WHERE IT MEASURED x (FitOptions::requireCoverage, the default):
//     x lies inside one of the shot's measured runs — coverageRuns(), the very runs the coverage strip
//     draws — widened at each end by half the shot's median sample spacing (the instant a run's edge
//     sample stands for). Only contributing shots count in nShots, and only THEIR samples enter the fit
//     at x: a non-contributing shot's samples, all on one side of x, would otherwise still tilt the line.
//     Why: a local-linear fit near the end of the data extrapolates the slope it sees. On 4 July
//     clubhead speed is bridged after impact on every shot (every run ends −4.1…−0.3 ms), yet the
//     faster kind's fit read 85.8 mph at +0.6, 91.8 at +6.9 and 103.0 at +13.3 ms with nShots 6 —
//     against shot 8's measured 82 mph at P7. It now ends where fewer than three runs still reach. Why the run
//     and not "samples on both sides within ±h": the run is what the coverage strip shows, so the
//     strip and the fit's n agree hairline for hairline, and a shot's own bridged hole is never
//     "measured" by interpolating across it — the other shots span it, which is the point of pooling.
//     The mocks had no such rule; requireCoverage = false reproduces them (the golden tests).
//   · the result carries its group (ordinals) and n so it is labelled with them — and no shot's number
//     is ever read from it.
// B's adaptive 12–60 ms window (variant_b Stats.loess: the window holding the k nearest samples,
// k = 3 × the group size) is an OPTION of this engine (FitOptions::adaptive), not a second engine.
//
// ── COST, FOR THE FACADE ─────────────────────────────────────────────────────────────────────
// alignedCurves (a windowed mean per shot, O(n²) in the samples) is the expensive step: cache it per
// (key, snapshot revision, alignment). envelope / pooledFit are O(grid × curves) and O(grid × window
// samples) — cheap at 15 shots, still fine at 50; cache them per (key, group, window). shapeSplit and
// typicalShot build their own envelopes and an n × n distance matrix: per (keys, window), not per frame.

#include "session_curves.h"
#include "session_warp.h"

#include <vector>

namespace pinpoint::charts {

// Every shot's curve of `key` on the canonical axis, parallel to session.shots (an EMPTY curve, with
// its ordinal set, for a shot without the curve or without alignment).
std::vector<TraceCurve> alignedCurves(const SessionSnapshot &session, const SessionAlignment &alignment,
                                      const QString &key);

// Pointers to the non-empty curves of the given ordinals (all of them when `ordinals` is empty).
std::vector<const TraceCurve *> curvesOf(const std::vector<TraceCurve> &curves,
                                         const std::vector<int> &ordinals = {});

// ── the envelope, and where the curves part ──────────────────────────────────────────────────

// The session's spread at each grid instant (canonical ms): type-7 quartiles and median, min / max,
// and the count measured there; NaN wherever fewer than `minShots` curves were measured.
struct SessionEnvelope {
    std::vector<double> t;              // canonical ms (the grid)
    std::vector<double> q1, med, q3, lo, hi;
    std::vector<int>    n;
    int count = 0;                      // curves that went in
};
SessionEnvelope envelope(const std::vector<const TraceCurve *> &curves, const std::vector<double> &grid,
                         int minShots = 3);

// Where the curves part: the instant of the widest middle half, among instants where at least 80 % of
// the shots were measured (so a ragged end, where three curves remain, cannot win), the window's first
// and last tenth left out (curves start and stop there; a ragged start is not a difference in the swing).
struct PartingPoint {
    bool   found = false;
    double tCanonMs = kNaN, iqr = kNaN, q1 = kNaN, q3 = kNaN, med = kNaN;
    int    n = 0;
};
PartingPoint partAt(const SessionEnvelope &env, int nShots);

// ── the group fit ────────────────────────────────────────────────────────────────────────────

struct PooledSample {
    double tCanonMs = kNaN;
    double value = kNaN;
    int    ordinal = 0;
};
// The valid, finite raw samples of the given shots' curves (canonical axis), pooled.
std::vector<PooledSample> pooledSamples(const std::vector<TraceCurve> &curves, const std::vector<int> &ordinals);

struct FitOptions {
    double halfWidthMs = 20.0;          // the windowed mean's 40 ms support
    int    minShots = 3;                // per grid instant, and the smallest group fitted at all
    int    minSamples = 8;              // per grid instant
    bool   adaptive = false;            // B's window: the k nearest samples, held to [hMin, hMax]
    double adaptiveMinMs = 12.0, adaptiveMaxMs = 60.0;
    int    adaptiveK = 0;               // 0 = 3 × the group size (B's choice)
    bool   requireCoverage = true;      // a shot contributes only inside its measured runs (header);
                                        // false = the mocks (golden tests only)
};

struct TimeSpan { double t0 = kNaN, t1 = kNaN; };

// Where one shot measured a curve, as the fit judges contribution: its runs (coverageRuns) and the
// half sample spacing allowed past each run's ends.
struct ShotCoverage {
    int    ordinal = 0;
    std::vector<TimeSpan> runs;
    double edgeMs = 0.0;
    bool covers(double x) const;
};
ShotCoverage shotCoverage(const TraceCurve &curve);

enum class FitRefusal { None, GroupTooSmall, NoSamples };

struct GroupFit {
    FitRefusal refusal = FitRefusal::None;
    std::vector<int> ordinals;          // the group the curve belongs to (label it with these and n)
    int groupSize = 0;                  // shots in the group
    int samplesPooled = 0;
    std::vector<double> t;              // canonical ms (the grid)
    std::vector<double> v, sd, se;      // NaN where blank
    std::vector<int>    nShots, nSamples;

    bool refused() const { return refusal != FitRefusal::None; }
    int  drawnPoints() const;           // grid instants with a value
};

// THE ENGINE, on pooled samples (sorted internally, stably by time). `coverage` says where each shot
// measured; when requireCoverage is on and it is empty, it is derived from the samples themselves (runs
// broken at a gap wider than three median spacings — a bridged hole shorter than that is invisible to
// it, so pass the curves' coverage when you have it, as the overload below does).
GroupFit pooledFit(std::vector<PooledSample> samples, const std::vector<double> &grid, int groupSize,
                   const FitOptions &options = {}, const std::vector<ShotCoverage> &coverage = {});
// The usual call: the group's curves (from alignedCurves) → pooled → fitted on `grid`.
GroupFit pooledFit(const std::vector<TraceCurve> &curves, const std::vector<int> &ordinals,
                   const std::vector<double> &grid, const FitOptions &options = {});

// ── coverage ─────────────────────────────────────────────────────────────────────────────────

// A shot's own coverage of a curve, on the curve's axis: the runs of valid samples, broken at a bridged
// sample or a hole wider than three median sample spacings.
std::vector<TimeSpan> coverageRuns(const TraceCurve &curve);
// The holes in a coverage inside [t0, t1].
std::vector<TimeSpan> coverageHoles(const std::vector<TimeSpan> &runs, double t0, double t1);

// ── shapes and typical shots ─────────────────────────────────────────────────────────────────

// Two kinds of curve: the pair of medoids that minimises every shot's distance to its nearer one, each
// side at least `minEach` shots. Distance between two shots = the mean |difference| of their aligned
// curves over a grid of [t0, t1] (step max(2, span/120)), each key scaled by its session middle-half
// width so units do not decide. `curvesPerKey[k]` is alignedCurves() of key k. Shots with no curve
// for any key take no part. found = false with fewer than 6 shots or no split with minEach a side.
struct ShapeSplit {
    bool   found = false;
    int    medoidA = 0, medoidB = 0;    // ordinals ("shaped like shot 3")
    std::vector<int> groupA, groupB;    // ordinals, ascending
    double cost = kNaN;
};
ShapeSplit shapeSplit(const std::vector<std::vector<TraceCurve>> &curvesPerKey, double t0, double t1,
                      int minEach = 3);

// The shot whose curves sit closest to (furthest from) the session median over [t0, t1], averaged
// over the keys as a share of each key's median middle-half width; 0 when none qualifies.
struct ShotScore { int ordinal = 0; double score = kNaN; };
ShotScore typicalShot(const std::vector<std::vector<TraceCurve>> &curvesPerKey, double t0, double t1,
                      int excludeOrdinal = 0);
ShotScore leastTypicalShot(const std::vector<std::vector<TraceCurve>> &curvesPerKey, double t0, double t1,
                           int excludeOrdinal = 0);

// ── against a reference ──────────────────────────────────────────────────────────────────────

// Where a curve and a reference (refT, refV on the same axis) part most: the instant of the largest
// |curve − ref| within [t0, t1].
struct TraceGap {
    bool   found = false;
    double t = kNaN;                    // on the curve's axis
    double diff = kNaN, value = kNaN, ref = kNaN;
};
TraceGap gapAt(const TraceCurve &curve, const std::vector<double> &refT, const std::vector<double> &refV,
               double t0, double t1);
// curve − ref on the reference's grid (NaN where either is missing).
std::vector<double> diffFrom(const TraceCurve &curve, const std::vector<double> &refT,
                             const std::vector<double> &refV);
// The mean |curve − ref| over [t0, t1]; NaN when nothing overlaps.
double meanGap(const TraceCurve &curve, const std::vector<double> &refT, const std::vector<double> &refV,
               double t0, double t1);

} // namespace pinpoint::charts
