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

// `analysis.dtlRuler` (pinpoint.dtlRuler/1) — the down-the-line ruler DtlPostureStage resolved
// (dtl_posture.h buildDtlPosture; SwingAnalysis::dtlRuler): centimetres per DTL pixel at the
// golfer and where it came from. Kept so a later reader draws centimetres on the DTL frame with
// the SAME scale pelvisThrust was measured in (posture_chart_design.md §5.4: the hip strip at
// impact). ONE builder for swing.ppsw and the replay payload. `cmPerPx` is null when there is no
// ruler — a reader never sees a zero it might multiply by.

#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include <limits>

#include "../Analysis/swing_analysis.h"

namespace pinpoint::analysis {

inline QJsonObject dtlRulerToJson(const DtlRuler &r, int stageVersion)
{
    QJsonObject o {
        { "schema",       QStringLiteral("pinpoint.dtlRuler/1") },
        { "stageVersion", stageVersion },
        { "source",       r.source.isEmpty() ? QStringLiteral("none") : r.source },
        { "cmPerPx",      r.cmPerPx > 0.0 ? QJsonValue(r.cmPerPx) : QJsonValue() } };
    if (r.toward != 0.0)      o["toward"]     = r.toward > 0 ? 1 : -1;
    if (r.scaleRatio > 0.0)   o["scaleRatio"] = r.scaleRatio;
    if (!r.refused.isEmpty()) o["refused"]    = r.refused;
    if (!r.reason.isEmpty())  o["reason"]     = r.reason;
    return o;
}

// The replay payload's `dtl.ruler`: { cmPerPx (NaN when none), source, toward }.
inline QVariantMap dtlRulerPayload(const QJsonObject &o)
{
    const QJsonValue c = o.value(QStringLiteral("cmPerPx"));
    return QVariantMap {
        { QStringLiteral("cmPerPx"), c.isDouble() && c.toDouble() > 0.0 ? c.toDouble()
                                                                         : std::numeric_limits<double>::quiet_NaN() },
        { QStringLiteral("source"),  o.value(QStringLiteral("source")).toString(QStringLiteral("none")) },
        { QStringLiteral("toward"),  o.value(QStringLiteral("toward")).toInt(0) } };
}

} // namespace pinpoint::analysis
