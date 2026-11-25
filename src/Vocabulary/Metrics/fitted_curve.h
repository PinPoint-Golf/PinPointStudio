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

// A CURVE THAT IS ALREADY A FIT IS READ AT THE POSITION, NOT AROUND IT (Mark, 8 Nov 2026).
//
// A position (P3, P6 …) is an instant, and a reading at a position is the value at that instant. The
// windows the other readings go through, ±15 ms (reduceAt) and 40 ms (windowedMeans), are noise
// filters for frame-by-frame measurements. They mean nothing about a swing, and on a curve that is
// already a fit they only smooth what was smoothed: the card and the Club delivery section used to
// differ by up to 1.2° at P3 for that reason alone.
//
// The two plane curves are penalised splines evaluated on a uniform grid (shaft_plane_curve.h). So
// every surface reads them through reduceInstant: the phase grid (and so the conditions and the Club
// delivery section), the Charts card tiles, the headline tiles and the Readings tab. The drawn line
// is the curve itself (MetricCardSpec::windowedMean false). Keyed here because the saved documents
// carry no such flag and these readings must not need a re-analysis.

#include <QLatin1String>
#include <QString>

namespace pinpoint::analysis {

inline bool isFittedCurve(const QString &key)
{
    return key == QLatin1String("swingPlaneCurve") || key == QLatin1String("handPlaneCurve");
}

} // namespace pinpoint::analysis
