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

// club_rate_filter.h — ONE estimate of the face-on shaft angle θ(t) and its rate θ̇(t) through the
// downswing (docs/design/club_rate_filter_design.md).
//
// Why: clubhead speed read the synth's Hermite anchor-curve rate while the KS club angular speed
// differentiated the synth θ fitted to the frames — two rates of one shaft, which disagreed (24 Oct
// 2026: the KS club ω had two or more humps in the last 250 ms on 91 % of 261 swings, while
// clubhead speed held a flat P6→P7 mean). Here a robust Kalman filter with an RTS pass, state
// [θ, θ̇, θ̈] under white jerk, reads every INDEPENDENT observation the tracker made — the measured
// frames at their own σθ, the blur-wedge edges at their own sub-frame times, the ball line at P7 —
// and its smoothed θ and θ̇ replace the synth's over the downswing, so every consumer reads one ω.
//
// The P-position anchors are NOT observations: they are located on the same frames, and reading
// them again would count those frames twice.
//
// Robust: the frames the tracker spent on the legs or a wrong branch through impact (KB-18 / #22)
// are what made the rate wobble. Pass 1 gates innovations at gateSigma; each later pass inflates a
// frame's R by its Huber weight on the previous pass's smoothed residual. What it threw out or
// down-weighted is counted, and that count is the developer diagnostic (ClubFilterStats).

#include "../Core/kf3_smoother.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace pinpoint::analysis {

struct ClubRateFilterConfig {
    bool    enabled         = false;
    double  jerkDps3        = 12000.0;
    double  sigmaFloorDeg   = 1.0;
    double  sigmaUnsetDeg   = 3.0;
    double  gateSigma       = 3.0;
    double  robustK         = 2.0;
    int     robustIters     = 2;
    double  ballSigmaDeg    = 8.0;
    int64_t leadInUs        = 40000;
    double  initRateDps     = 600.0;
    double  initRatePostDps = 2500.0;
};

// One reading of the shaft's image angle. σ in radians; θ on any sheet (the filter brings each
// reading onto the sheet of its own prediction).
struct ClubFilterObs {
    enum Kind : uint8_t { Frame = 0, Wedge = 1, Ball = 2 };
    int64_t t_us     = 0;
    double  thetaRad = 0.0;
    double  sigmaRad = 0.0;
    Kind    kind     = Frame;
};

// The per-swing diagnostic (developer only, persisted as analysis.club.filter). How far the frames
// sat off the filtered track IS the wobble — the measurement error the tracker made.
struct ClubFilterStats {
    bool   ran            = false;
    int    version        = 1;
    int    nEvidence      = 0;      // observations read, both segments
    int    nGated         = 0;      // thrown out by pass 1's gate
    int    nDownweighted  = 0;      // R inflated on the final pass (|residual| > robustK·σ)
    double nisMean        = -1.0;   // mean normalised innovation on the final pass (≈ 1 when σθ is honest)
    double residualRmsDeg = -1.0;   // RMS of frame − filtered θ over the downswing
    double rateWobble     = -1.0;   // RMS of (raw dθ/dt − filtered θ̇) / max(|θ̇|, floor), downswing
    int    humpsRaw       = -1;     // countHumps of the raw |dθ/dt|, last 250 ms before impact
    int    humpsFiltered  = -1;     // the same on the filtered |θ̇|
};

// One filtered segment, kept so the uncertainty draws can sample it (forward-filter backward-sample).
struct ClubFilterSegment {
    kf3::Kf3             kf { 0.0, 0.0, 0.0, 0.0, 0.0 };
    std::vector<size_t>  tick;      // synth tick index …
    std::vector<int>     step;      // … and its step in kf's history
};

struct ClubRateFilterResult {
    std::vector<char>   covered;        // per tick: the filter set it
    std::vector<double> thetaRad;       // per tick (unwrapped; the caller puts it on the tick's sheet)
    std::vector<double> thetaDotRadS;
    std::vector<double> sigmaThetaRad;
    std::vector<double> sigmaThetaDotRadS;
    ClubFilterStats     stats;
    std::vector<std::shared_ptr<const ClubFilterSegment>> segments;
};

// Filter the shaft angle over [tTop − leadIn, tImpact] (output on ticks in [tTop, tImpact]) and
// (tImpact, tEnd] (output on ticks after impact), as two segments, because the collision steps the
// rate. tickT is the synth grid. obs need not be sorted. Ticks outside both segments are left
// uncovered. tImpact ≤ tTop or too few observations in a segment ⇒ that segment does not run.
ClubRateFilterResult runClubRateFilter(const std::vector<int64_t> &tickT,
                                       const std::vector<ClubFilterObs> &obs,
                                       int64_t tTop, int64_t tImpact, int64_t tEnd,
                                       const ClubRateFilterConfig &cfg);

// Humps in a rate: maxima above `frac` of the window's top that rise at least `prom` of the top
// above the trough between them and a taller neighbour (the window's ends count). A club that
// speeds up once has one. club_filter_gate.py counts them the same way.
int countHumps(const std::vector<double> &v, double frac = 0.4, double prom = 0.1);

} // namespace pinpoint::analysis
