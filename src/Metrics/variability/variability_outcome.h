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

// THE OUTCOME RULE (Mark's rulings R4 and R4b, 7 Nov; docs/design/system/variability.md, "Outcome or movement"), in
// one place, by measure. Until shots record an intent, an OUTCOME reading gets no variability mark at all: no
// consistency grade, no trend and no typicality. Outcomes are ball flight (carry, total, offline, start direction,
// ball speed, launch, spin rate and axis, back and side spin, peak height, descent, distance to the pin, the shot
// shape) and smash factor. Everything else is movement and stays eligible, club delivery included: club speed, path,
// face, face to path, attack, dynamic and spin loft, lie, closure, low point, strike location and height.
//
// Keyed by the metric catalogue's key (MetricDescriptor::key), so the launch monitor's readings ("lm.carryDistance")
// and the camera's own ball readings ("ballSpeed") answer alike. A pack measure is an outcome when any key on its
// ladder is (isOutcomeMeasure, src/Gui/variability/variability_measures.h). The catalogue's `group` is not used: it
// files face to path under "Ball flight" and smash under "Strike", and neither filing is the ruling.

#include <QString>
#include <QStringList>

namespace pinpoint::variability {

inline bool isOutcomeMetric(const QString &metricKey)
{
    static const QStringList kOutcomes{
        // ball flight, as the camera reads it
        QStringLiteral("ballSpeed"), QStringLiteral("launchAngle"), QStringLiteral("launchDirection"),
        QStringLiteral("compoundMiss"),       // the shot shape: pull-hook, push-slice …
        // ball flight, as a launch monitor reports it
        QStringLiteral("lm.ballSpeed"), QStringLiteral("lm.launchAngle"), QStringLiteral("lm.launchDirection"),
        QStringLiteral("lm.spinRate"), QStringLiteral("lm.backSpin"), QStringLiteral("lm.sideSpin"),
        QStringLiteral("lm.spinAxis"), QStringLiteral("lm.carryDistance"), QStringLiteral("lm.totalDistance"),
        QStringLiteral("lm.offline"), QStringLiteral("lm.peakHeight"), QStringLiteral("lm.descentAngle"),
        QStringLiteral("lm.distanceToPin"),
        // smash factor: ball speed over club speed, an outcome (R4b)
        QStringLiteral("lm.smashFactor"),
    };
    return kOutcomes.contains(metricKey);
}

} // namespace pinpoint::variability
