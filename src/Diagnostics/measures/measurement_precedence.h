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

// THE ONE RULE FOR WHICH SOURCE WINS FOR A METRIC (refactor 6.1a; information_model.md R-INS-3).
//
//   measurementLadder(metric, authored)   the stored keys a reader takes `metric` from, best source first.
//                                         FOR A MEASURE (`authored` given: the pack's statement, Measure::preferKeys —
//                                         the Coaching Lead's per-quantity judgement, rule data, which the model
//                                         browser edits): its rungs, then `metric`. An empty statement means the
//                                         measure reads its own key only (the editor's "drop a rung" keeps working).
//                                         FOR ANY OTHER READER (`authored` none): the default order — the instruments'
//                                         READINGS (HackMotion, then the launch monitor), then what PinPoint DERIVES
//                                         from a reading (the LM-derived keys), then PinPoint's OWN estimate; each
//                                         rung only when the vocabulary declares that key (measurement.h), so a
//                                         quantity no instrument reads has one rung, its own. A key already naming its
//                                         source ("lm.faceAngle") is its own one rung.
//   pickByPrecedence(metric, offered)     of the candidates a reader holds (stored key -> value or none), the one the
//                                         ladder ranks first among those that ANSWER (a value), with its key; none
//                                         when nothing answers. A reader filters by source by what it offers; it never
//                                         orders them itself.
//   findByPrecedence(series, metric)      the series a list holds for `metric`: the first rung that exists.
//
// Every reader walks this: the analysis layer (what was swing_analysis.h's findSeriesByLadder: HackMotion first), the
// diagnostics (measureKeyLadder: the measure's ladder), the launch monitor board's low point, Charts Delivery's attack
// and low point (session_delivery.cpp) and the ranking's clubhead-speed tie-break (fire_findings.cpp). THE FIRST RUNG THAT
// ANSWERS WINS, not the first that exists: a device that reports a shot while omitting one column falls through.
//
// Every authored ladder in the shipped pack (core.json, 27 measures) equals the default order.
//
// Rules ring (src/Diagnostics): pure.

#include "../../Metrics/measurement.h"
#include "../../Vocabulary/Analysis/swing_analysis.h"   // MetricSeries

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pinpoint::analysis {

static_assert(kStoredImpactPhase == int(Phase::Impact), "measurement.h reads a stored Impact sample by this number");

inline QStringList measurementLadder(const QString &metric, const std::optional<QStringList> &authored = std::nullopt)
{
    if (authored) {
        QStringList keys = *authored;
        if (!metric.isEmpty())
            keys << metric;
        return keys;
    }
    if (metric.isEmpty())
        return {};
    // A key that already names its source (a reading, or an LM-derived key) is asked for as itself.
    if (isDeclaredInstrumentKey(metric) || isLaunchMonitorReading(metric) || isHackMotionReading(metric))
        return { metric };
    QStringList keys;
    for (const Instrument in : { Instrument::HackMotion, Instrument::LaunchMonitor }) {
        const QString k = storedKey(metric, in);
        if (isDeclaredInstrumentKey(k))
            keys << k;
    }
    for (const QString &k : lmDerivedKeys())
        if (k != metric && lmDerivedMetric(k) == metric)
            keys << k;
    keys << metric;
    return keys;
}

struct PickedMeasurement {
    QString key;     // the stored key that won
    double  value = 0.0;
};

inline std::optional<PickedMeasurement> pickByPrecedence(const QString &metric,
                                                         const QHash<QString, std::optional<double>> &offered,
                                                         const std::optional<QStringList> &authored = std::nullopt)
{
    for (const QString &k : measurementLadder(metric, authored)) {
        const auto it = offered.constFind(k);
        if (it != offered.constEnd() && it->has_value())
            return PickedMeasurement{ k, **it };
    }
    return std::nullopt;
}

// The series a list holds for `metric`, best source first: the first rung that EXISTS (a series is there or not; what
// was swing_analysis.h's findSeriesByLadder, HackMotion before our own wrist maths). Pass the metric; a key that already
// names its source is asked for as itself.
inline const MetricSeries *findByPrecedence(const std::vector<MetricSeries> &v, const QString &metric)
{
    for (const QString &k : measurementLadder(metric))
        for (const MetricSeries &m : v)
            if (m.key == k) return &m;
    return nullptr;
}

} // namespace pinpoint::analysis
