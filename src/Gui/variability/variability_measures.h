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

// WHAT A MEASURE IS, FOR A VARIABILITY MARK: its scale type, whether it is an outcome (R4), and its norm's corridor
// half-width. One lookup each, shared by every surface that grades a pack measure or a catalogue reading (Home's
// work-ons, the report's work-on pages, the diagnostics panel, the launch monitor, the Charts scale provider), so no
// two of them can disagree about what kind of number a measure is or which scale it is graded against.
//
//   metricScaleType(key)       MetricDescriptor::scaleType from the metric manifest; Interval for a key it does not
//                              know (the descriptor default).
//   measureScaleType(m)        the descriptor of the first key on the measure's ladder that has one (measureKeyLadder,
//                              best first); Interval otherwise.
//   isOutcomeMeasure(m)        any key on its ladder is an outcome (isOutcomeMetric, variability_outcome.h).
//   corridorHalfWidthOf(lo,hi) (σLo + σHi)/2, the norm's in-range band (|z| ≤ 1; spec §8.2 step 4.1, Mark 7 Nov), or
//                              NaN when it is not a positive number. Whatever the norm's shape: a floor or a ceiling is
//                              how a value is judged, not how widely the golfer may spread (R12).
//   normCorridorHalfWidth(…)   the same for the norm that resolves for a measure in a context; NaN with none.
//
// The manifest is installed once, alone (no process-wide labeller), and never changed after, so readers on any
// thread take no lock.

#include "../../Diagnostics/measures/measure_vocabulary.h"         // Measure, measureKeyLadder
#include "../../Diagnostics/norms/norm_provider.h"                 // INormProvider
#include "../../Metrics/metric_catalogue.h"
#include "../../Metrics/variability/variability_outcome.h"         // isOutcomeMetric
#include "../../Metrics/variability/variability_scale_type.h"

#include <QString>

#include <cmath>
#include <limits>

namespace pinpoint::variability {

inline const analysis::MetricCatalogue &variabilityManifest()
{
    static const analysis::MetricCatalogue catalogue = [] {
        analysis::MetricCatalogue c;
        analysis::installMetricManifest(c);
        return c;
    }();
    return catalogue;
}

inline ScaleType metricScaleType(const QString &key)
{
    const analysis::MetricDescriptor *d = key.isEmpty() ? nullptr : variabilityManifest().descriptor(key);
    return d ? d->scaleType : ScaleType::Interval;
}

inline ScaleType measureScaleType(const analysis::Measure *m)
{
    if (!m) return ScaleType::Interval;
    for (const QString &key : analysis::measureKeyLadder(*m))
        if (const analysis::MetricDescriptor *d = variabilityManifest().descriptor(key)) return d->scaleType;
    return ScaleType::Interval;
}

inline bool isOutcomeMeasure(const analysis::Measure *m)
{
    if (!m) return false;
    for (const QString &key : analysis::measureKeyLadder(*m))
        if (isOutcomeMetric(key)) return true;
    return false;
}

inline double corridorHalfWidthOf(double sigmaLo, double sigmaHi)
{
    const double half = 0.5 * (sigmaLo + sigmaHi);
    return std::isfinite(half) && half > 0.0 ? half : std::numeric_limits<double>::quiet_NaN();
}

inline double normCorridorHalfWidth(const analysis::INormProvider *norms, const QString &measureId, const QString &contextId)
{
    if (!norms || measureId.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    const analysis::NormResolution res = norms->resolve(measureId, contextId);
    return res.found() ? corridorHalfWidthOf(res.norm->sigmaLo, res.norm->sigmaHi) : std::numeric_limits<double>::quiet_NaN();
}

} // namespace pinpoint::variability
