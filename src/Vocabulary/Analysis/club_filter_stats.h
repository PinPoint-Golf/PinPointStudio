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

// The club rate filter's per-swing diagnostic: a plain value type the shot's analysis record holds
// (ShaftTrack2D::clubFilter, swing_analysis.h), so it sits in the vocabulary; the filter itself
// stays in Club/club_rate_filter.h, which includes this. Pure std. Moved out of club_rate_filter.h
// unchanged (refactor 7.3, A-11).

namespace pinpoint::analysis {

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

} // namespace pinpoint::analysis
