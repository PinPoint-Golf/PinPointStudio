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

// v3.4 — the ball as the club's far-end anchor (docs/design/club_tracking_v3_design.md
// §9). An ADDITIVE post-hoc pass applied AFTER decideTrack() returns — reads the
// frozen DP output, fills in individual samples that were NOT already a real
// vision measurement, never re-solves the DP/transition-band machinery. A
// missing or disagreeing ball can only leave the track unchanged, never
// degrade it (design §9.6).
//
// Three exploits (design §9.2-9.4), all gated the same way — "not already
// ShaftMeasured" — so a confirmed measurement is never second-guessed:
//   1. tk0 — the frame the club's measured/tracked theta first departs
//      theta_ball beyond a threshold (the clubhead rotating away from the
//      ball). Computed and logged only (trace.ballTk0Frame) — NOT fed into
//      the phase model/DP; that relabel is v3.0-internal, deferred to the
//      v3.0-r1 corpus re-gate (impl doc's "ACTION" section).
//   3. Impact anchor — the last pre-launch frame's theta, read directly off
//      grip->ball, bridging the impact blur with a measurement instead of a
//      pure kinematic reconstruction.
//   4. Address discrimination + scale floor — publishes ball-anchored theta
//      across the address hold (tk0's domain) where nothing was measured,
//      and derives out.measuredClubLenPx from the grip-to-ball distance.

#include <cstdint>
#include <vector>

#include "swing_analysis.h"       // ShaftTrack2D, BallTrack2D
#include "shaft_track_assembly.h" // ShaftDecideTrace

struct ShotAnalysisJob;

namespace pinpoint::analysis {

void applyBallAnchor(ShaftTrack2D &out, const BallTrack2D &ball,
                     const std::vector<double> &gx, const std::vector<double> &gy,
                     const std::vector<int64_t> &tUs, int frameW, int frameH,
                     int impf, const ShotAnalysisJob &job, ShaftDecideTrace *trace);

} // namespace pinpoint::analysis
