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

// THE GRADER'S `at` READING OF A SERIES, for a reading derived after the run from other readings
// (plane stack D6, 13 Nov 2026: moved here from shoulder_tilt_vs_bend.h, unchanged, when it came to
// serve two of them). Used by:
//   * shoulder_tilt_vs_bend.h — the tilt at the Top and the forward bend at Address (W7);
//   * club_slot_top.h — the swing plane at P1 and P3 and the shoulder plane at P1 (plane stack D3).
//
// A derived reading must take each part EXACTLY as the grader would (measure_sample.cpp buildPhaseGrid +
// reduceOverGrid's `at`), so that the value graded is the arithmetic of the parts' graded values on the
// same swing: the median of the VALID samples within ±sampler::kWindowHalfUs of the ladder instant
// (series_reduce.h reduceAt, one sample enough), or the instant itself on a fitted curve; else the phase
// sample the series labels at that phase. A part missing at its phase reads nothing — no zero.
//
// σ, as the grader stamps it (stampSigma → SigmaSource): the phase sample's own σ (Reading), else the
// series σ — a full budget when it carries a SigmaKind (Series), the frame-to-frame noise floor when it
// carries none (Noise). No σ anywhere: None (not characterised, never zero).

#include "../Analysis/swing_analysis.h"
#include "../Core/pp_tuned_constants.h"
#include "../Metrics/fitted_curve.h"
#include "../Metrics/line_angle.h"
#include "../Metrics/series_reduce.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

namespace grader {

// Where the grader takes a reading's σ from (measure_sample.cpp stampSigma → SigmaSource).
enum class SigmaFrom : uint8_t { None, Reading, Series, Noise };

struct GradedReading {
    bool      ok = false;
    int64_t   tUs = 0;
    double    value = std::numeric_limits<double>::quiet_NaN();
    double    sigma = std::numeric_limits<double>::quiet_NaN();
    SigmaFrom sigmaFrom = SigmaFrom::None;
    uint8_t   sigmaKind = 0;      // the SigmaKind behind it (the phase sample's, or the series')
};

// The reading the grader takes of `m` at phase `ph` (the `at` reducer), with its σ. The ladder's FIRST
// event for a phase wins and an empty ladder reads nothing, as buildPhaseGrid's ladder does.
inline GradedReading graderReadingAt(const MetricSeries &m, Phase ph, const std::vector<PhaseEvent> &ladder)
{
    GradedReading out;
    if (ladder.empty()) return out;
    const auto at = std::find_if(ladder.begin(), ladder.end(), [&](const PhaseEvent &e) { return e.phase == ph; });
    const auto lab = std::find_if(m.phaseSamples.begin(), m.phaseSamples.end(),
                                  [&](const PhaseSample &p) { return p.phase == ph; });
    const bool labelled = lab != m.phaseSamples.end();
    const auto fold = [&](double x) { return m.lineAngle ? wrapLeanDeg(x) : x; };
    // The instant: the ladder's, else — a phase the ladder lacks but the series labels — the label's own
    // (buildPhaseGrid's candidates). The curve is read there first either way.
    std::optional<int64_t> instant;
    if (at != ladder.end()) instant = at->t_us;
    else if (labelled)      instant = lab->t_us;
    if (instant) {
        const size_t n = std::min(m.t_us.size(), m.value.size());
        if (n > 0) {
            SeriesView view;
            view.t = m.t_us.data(); view.v = m.value.data(); view.n = n;
            view.valid = m.valid.size() >= n ? m.valid.data() : nullptr;
            ReduceConfig rc;
            rc.atHalfWindowUs = tuned::sampler::kWindowHalfUs;   // PhaseGridConfig::windowHalfUs
            rc.minAtSamples   = 1;                               // PhaseGridConfig::minValidSamples
            const Reduced r = isFittedCurve(m.key) ? reduceInstant(view, *instant) : reduceAt(view, *instant, rc);
            if (r.ok) { out.ok = true; out.tUs = *instant; out.value = fold(r.value); }
        }
    }
    if (!out.ok && labelled) { out.ok = true; out.tUs = lab->t_us; out.value = fold(lab->value); }
    if (!out.ok || !std::isfinite(out.value)) return GradedReading{};
    if (labelled && lab->sigma && std::isfinite(*lab->sigma) && *lab->sigma > 0.0) {
        out.sigma = *lab->sigma; out.sigmaFrom = SigmaFrom::Reading; out.sigmaKind = lab->sigmaKind;
    } else if (m.sigma && std::isfinite(*m.sigma) && *m.sigma > 0.0) {
        out.sigma = *m.sigma; out.sigmaKind = m.sigmaKind;
        out.sigmaFrom = m.sigmaKind > 0 ? SigmaFrom::Series : SigmaFrom::Noise;
    }
    return out;
}

} // namespace grader

} // namespace pinpoint::analysis
