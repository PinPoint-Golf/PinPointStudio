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

#include <vector>

#include "swing_analysis.h"   // MetricSeries, ScoreBreakdown

namespace pinpoint::analysis {

// Transparent, non-compensatory swing scorer (design: shot_analyzer_design.md
// "B) Scoring model"). Each metric is read at its scoring phase and mapped to a
// 0..100 sub-score against a reference band (deadband + bounded falloff, one-sided
// where a fault is directional); sub-scores aggregate by a WEIGHTED GEOMETRIC mean
// (weakest-link — one severe fault can't be averaged away) into per-region, per-phase,
// and overall scores.
//
// Reference bands are a versioned table per session type. The wrist bands are
// PROVISIONAL pending the FE/RUD/pronation sign-lock on the "check your sensors"
// wizard page (see wrist_angles.h) — the band centres/directions, not the math.
class SwingScorer {
public:
    static ScoreBreakdown score(const std::vector<MetricSeries> &series, int sessionType);
};

} // namespace pinpoint::analysis
