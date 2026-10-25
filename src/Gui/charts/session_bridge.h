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

// THE CHEST'S PROBABLE PEAK — a short gap in the chest curve bridged for the SESSION view
// (docs/design/charts_panel_traces_relations.md §2.2.1; Mark, 25 Oct 2026: "the area of the chart that
// is missing … is relatively short and I would argue cannot plausibly spike during the swing … add a
// spline to bridge the gap and use that as an estimator (with a warning to the user)").
//
// WHY THE CHEST, AND WHY A GAP. The face-on chest route stops measuring for 3–6 frames (20–40 ms) at or
// just before impact, where the shoulder line squares to that camera (sequence.sightedBand), and the
// analysis never places the chest's node (pp_tuned_constants.h kPairTrunkThoraxPlacement). On 07-04 and
// 10-20 the curve rises into the hole and falls out of it on 24 of 32 shots: the peak is IN the hole. The
// hole sits at the same instant on every shot, so pooling shots cannot fill it; each shot is bridged on its
// own, which is also what lets the session see the peak move from shot to shot.
//
// THE ESTIMATOR: a Gaussian process (Matérn 5/2 kernel, constant mean, white noise) through the measured
// samples either side of the hole. Mark asked for the best evidence there is ("if we are calling out a
// golfer … we need the best evidence we can provide"), and on the two sessions it beat a cubic bridge and a
// parabola (prototype, 25 Oct): the same peak times to 1–4 ms, but an honest ± (the spline's swung with its
// two-point edge slopes, the parabola's collapsed onto the edge sample) and it alone said "still rising"
// where the curve dips into the hole. The kernel's length and height are fitted ONCE PER SESSION to every
// bridged shot's curve together (maximum marginal likelihood, Nelder–Mead in log space) — the session's own
// evidence of how fast the chest's speed can change — and then each shot is conditioned on its own samples.
//
// THE NOISE IS ASSUMED, NOT FITTED. The stored curve is already smoothed, so a fitted noise comes out at
// ~1 °/s and the ± at 1–3 ms, which is false precision. 20 °/s is assumed for every sample (kNoiseDps):
// the ± is "given that noise", and the warning says the estimate is a fit across a gap. Calibrate it
// against the worn trunk IMU when that session exists (trunk_imu_design.md).
//
// THE PEAK: 600 draws from the posterior over [hole − 40 ms, hole + 40 ms] (clipped at P8 — the KS rule
// is "the max before P8"), each draw's maximum; the probable peak is the median time and speed of those
// maxima, ± half their 16–84 % spread. When most draws peak at the search window's right edge the curve
// is still rising and there is NO probable peak; at the left edge it fell into the hole and the top is
// outside it — nothing is said then either. Option A (Mark, 25 Oct): the probable peak is the hump the
// hole hides. A taller MEASURED hump earlier in the downswing (10-20: −150…−220 ms, where the face-on
// shoulder keypoints jump) is recorded on the bridge (`earlier*`) so the trust fold can say it was read as
// the measurement, never taken as the peak.
//
// DETERMINISTIC: the draws come from mt19937_64 seeded by the shot's ordinal, normals by Box–Muller on its
// raw output (std::normal_distribution differs between standard libraries), so a session draws the same
// estimate on the Mac and on the cabin PC.
//
// THE ANALYSIS DOES THIS TOO NOW (25 Oct 2026, the same day: src/Kinematics/gap_bridge.h, one implementation,
// the corpus's kernel). A swing analysed with it carries its probable peak and bridge on the chest node, and
// this file READS that (storedBridge) so Session and This shot say the same number. It bridges itself, with a
// kernel fitted to the session, only the swings analysed before the analysis did.

#include "session_curves.h"
#include "session_sequence.h"
#include "session_series_types.h"

#include <QString>

#include <vector>

namespace pinpoint::charts {

struct BridgeOptions {
    double noiseDps       = 20.0;     // the assumed per-sample noise (see the header)
    double beforeMs       = 110.0;    // measured samples this far before the hole train the fit…
    double afterMs        = 80.0;     // …and this far after it
    double searchMarginMs = 40.0;     // the peak is looked for this far either side of the hole
    double gridStepMs     = 1.0;
    int    draws          = 600;
    double maxHoleMs      = 80.0;     // a longer hole is not bridged (Mark's premise: a SHORT gap)
    int    minEachSide    = 3;        // measured samples needed on each side of the hole
    double nearFromMs     = -80.0;    // the hole must reach into [nearFromMs, nearToMs] about impact
    double nearToMs       = 40.0;
    double edgeShare      = 0.5;      // draws at a search edge above this: no probable peak
    double ellMinMs = 10.0, ellMaxMs = 200.0, sfMinDps = 20.0, sfMaxDps = 2000.0;
    double ellStartMs = 45.0, sfStartDps = 190.0;   // the start, and the kernel when < 3 shots can fit it
};

// The session's kernel: fitted when at least three shots had a hole to bridge, else the start values.
struct BridgeKernel {
    double ellMs = kNaN, sfDps = kNaN, noiseDps = kNaN;
    int    shots = 0;                 // shots the fit was made on
    bool   fitted = false;
};

enum class BridgeOutcome {
    NoHole,          // nothing to bridge near impact (or the node is placed)
    HoleTooLong,     // longer than maxHoleMs
    TooLittleCurve,  // fewer than minEachSide measured samples on a side
    StillRising,     // most draws peak at the search window's right edge
    FellIntoHole,    // most draws peak at its left edge: the top is before the hole
    Bridged          // a probable peak
};

struct ChestBridge {
    int    ordinal = 0;
    BridgeOutcome outcome = BridgeOutcome::NoHole;
    double holeFromMs = kNaN, holeToMs = kNaN;   // the last measured sample before, the first after (shot clock)
    double searchFromMs = kNaN, searchToMs = kNaN;
    // the fit across the hole, on the shot's clock: posterior mean and sd at gridStepMs, holeFrom…holeTo
    std::vector<double> t, mean, sd;
    // the probable peak (Bridged), and where the draws put it at the edges (any outcome past TooLittleCurve)
    double peakMs = kNaN, peakLoMs = kNaN, peakHiMs = kNaN, peakSigmaMs = kNaN;
    double peakDps = kNaN, peakSigmaDps = kNaN;
    double edgeShare = kNaN;
    // a taller measured hump earlier in the downswing (from Top to the search window), NaN when none
    double earlierMs = kNaN, earlierDps = kNaN;

    bool stored = false;              // the analysis' own probable peak (KsNode::probable), read, not recomputed

    bool bridged() const { return outcome == BridgeOutcome::Bridged; }
};

struct SessionBridges {
    BridgeKernel kernel;
    std::vector<ChestBridge> shots;   // parallel to SessionSnapshot::shots
    int    bridgedCount() const;
    int    attemptedCount() const;    // shots with a hole near impact (bridged or not)
    int    earlierCount() const;      // bridged shots with a taller measured hump earlier
    double holeMinMs() const;         // the shortest and longest hole bridged
    double holeMaxMs() const;
    const ChestBridge *forOrdinal(int ordinal) const;
};

// The thorax segment's curve key and index (session_sequence.h order).
constexpr int kChestSegment = 1;

SessionBridges bridgeChest(const SessionSnapshot &session, const SessionSequence &sequence,
                           const BridgeOptions &options = {});

// The probable peaks into the sequence: each bridged shot whose chest the analysis did not place gets
// probable = true and its time, then is ranked again (rankSequence) with its order rebuilt from the timed
// segments; then the session is summarised again (summariseSequence).
void applyBridges(SessionSequence &sequence, const SessionBridges &bridges);

} // namespace pinpoint::charts
