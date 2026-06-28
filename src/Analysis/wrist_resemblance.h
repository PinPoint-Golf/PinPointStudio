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

// Wrist resemblance scorer (design §B.0a) — the Wrist-session estimand.
//
// The Wrist score is NOT a quality grade: bowed/neutral/cupped are all workable
// lead-wrist patterns (§B.0). For each pattern p it computes an INDEPENDENT
// absolute resemblance
//
//     R_p = 100 · exp(−½ · d_p²),   d_p² = Σ_phase ((x − μ_p)/σ_p)²
//
// over the scored phases (Top, Impact). v1 scores lead-wrist flex/extension (FE)
// only — RUD and forearm pronation are secondary and added later. The three R_p
// are NOT normalised to sum to 100, so a clean bowed swing reads e.g.
// bowed 86 / neutral 40 / cupped 8. `overall` is the maximum R_p, `patternLabel`
// its argmax, and `blended` is set when the top two are within blendedDeltaPts.
//
// μ_p/σ_p are PROVISIONAL (pp_tuned_constants.h, externally anchored to HackMotion
// tour ranges) and re-seated at Corpus 2. σ_p is coaching tolerance, never sensor
// noise; measurement uncertainty is a separate interval filled by the uncertainty
// pass (§B.7), left unset here. Replaces SwingScorer for the Wrist session;
// SwingScorer remains the Swing/GRF adherence model.

#include "swing_analysis.h"

#include <QVariantMap>
#include <vector>

namespace pinpoint::analysis {

class WristResemblanceScorer {
public:
    // `overrides` applies the frozen dotted keys score.resemblance.<pattern>.{muTop,muImpact,
    // sigma} and score.resemblance.blendedDeltaPts onto a copy of the defaults (empty ⇒ frozen
    // defaults). Returns a ScoreBreakdown with kind = Resemblance.
    static ScoreBreakdown score(const std::vector<MetricSeries> &series,
                                const QVariantMap &overrides = {});
};

} // namespace pinpoint::analysis
