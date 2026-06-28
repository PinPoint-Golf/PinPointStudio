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

// Score measurement-uncertainty interval (design §B.7) — the SEPARATE track from the
// band σ (which is coaching tolerance, not noise). The per-cell lead-wrist FE error
// budget
//
//     σ_x = √( σ_sensor² + σ_crosstalk² + (dθ/dt · σ_timing)² )
//
// is propagated through R_p of the WINNING archetype to an interval on the resemblance
// score. The phase-timing term is inflated by low phase confidence, so a poorly-seated
// or badly-segmented swing reads as LESS CERTAIN (a wider interval) — it never changes
// the central score. This is the cure for "precise, confident, wrong" (A3/B2/C5): the
// old severity×confidence central term is gone (wrist_assessment_engine), and confidence
// now only widens the band.

#include "swing_analysis.h"

#include <QVariantMap>
#include <vector>

namespace pinpoint::analysis {

class ScoreUncertainty {
public:
    // Interval on a resemblance ScoreBreakdown. The central score (sb.overall) is NOT changed.
    // Returns an invalid interval (halfWidth -1) when the score is not a resemblance score or
    // the FE series / Top+Impact phases needed for the budget are unavailable.
    static ScoreInterval wristInterval(const ScoreBreakdown &sb,
                                       const std::vector<MetricSeries> &series,
                                       const std::vector<PhaseEvent> &phases,
                                       const QVariantMap &overrides = {});
};

} // namespace pinpoint::analysis
