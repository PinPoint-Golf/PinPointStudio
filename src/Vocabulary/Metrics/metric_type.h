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

#include <QString>

#include <optional>

// The metric-shape taxonomy for the Metric Catalogue (design §3.1). Every catalogue consumer
// branches on MetricType, never on the producer. There is no existing metric-kind enum in the tree
// (MetricSeries carries no discriminator), so this is the canonical home.

namespace pinpoint::analysis {

// The four first-class metric shapes:
//   Summary     — one derived value for the whole shot (a score, a tempo ratio).
//   PointInTime — a value read at a single phase; in the tree this is a MetricSeries with an empty
//                 t_us/value curve and a single PhaseSample (foot_metrics' address scalars).
//   TimeSeries  — a continuous curve, reduced to peak / @impact / Δ / rate by the chart layer.
//   Sequence    — an ordered set of timed peak events (the kinematic sequence). No v1 producer yet;
//                 the shape is declared here so the directory and future stages share one vocabulary.
enum class MetricType { Summary, PointInTime, TimeSeries, Sequence };

// Stable lower-camel string names — the QML-boundary spelling used in query filters and row shapes.
inline QString metricTypeName(MetricType t)
{
    switch (t) {
    case MetricType::Summary:     return QStringLiteral("summary");
    case MetricType::PointInTime: return QStringLiteral("pointInTime");
    case MetricType::TimeSeries:  return QStringLiteral("timeSeries");
    case MetricType::Sequence:    return QStringLiteral("sequence");
    }
    return QStringLiteral("summary");
}

// Inverse of metricTypeName() for parsing a QML filter string. Returns nullopt for an unknown name.
inline std::optional<MetricType> metricTypeFromName(const QString &name)
{
    if (name == QLatin1String("summary"))     return MetricType::Summary;
    if (name == QLatin1String("pointInTime")) return MetricType::PointInTime;
    if (name == QLatin1String("timeSeries"))  return MetricType::TimeSeries;
    if (name == QLatin1String("sequence"))    return MetricType::Sequence;
    return std::nullopt;
}

} // namespace pinpoint::analysis
