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

// `analysis.addressMarks` (pinpoint.addressMarks/1) — the body's outer edges at hip height on
// each camera's address frame (address_marks.h). ONE builder for swing.ppsw and the replay
// payload: the document holds the whole block; the tile reads one view's object
// (`addressMarks` for face-on, `dtl.addressMarks` for down the line). Times relative to `t0Us`,
// as every analysis block's are. A NaN is written as null, never as a number.

#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include <cmath>

#include "address_marks.h"

namespace pinpoint::analysis {

namespace addressmarks_json_detail {
inline QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }
inline double numOr(const QJsonValue &v, double d) { return v.isDouble() ? v.toDouble() : d; }
} // namespace addressmarks_json_detail

inline QJsonObject viewMarksToJson(const addressmarks::ViewMarks &v, int64_t t0Us)
{
    using namespace addressmarks_json_detail;
    QJsonObject o { { "found", v.found } };
    if (!v.found) return o;
    o["t_us"]      = qint64(v.frameTUs >= t0Us ? v.frameTUs - t0Us : v.frameTUs);
    o["rowY"]      = num(v.rowY);
    o["hipLeftX"]  = num(v.hipLeftX);
    o["hipRightX"] = num(v.hipRightX);
    o["leftX"]     = num(v.leftX);
    o["rightX"]    = num(v.rightX);
    o["buttX"]     = num(v.buttX);
    o["side"]      = v.side;
    o["rows"]      = v.rows;
    o["rowSource"] = v.rowSource == 1 ? QStringLiteral("wrists") : QStringLiteral("hips");
    return o;
}

inline QJsonObject addressMarksToJson(const addressmarks::AddressMarks &m, int64_t t0Us, int stageVersion)
{
    return QJsonObject {
        { "schema",       QStringLiteral("pinpoint.addressMarks/1") },
        { "stageVersion", stageVersion },
        { "source",       QStringLiteral("person mask (u2netp) on the frame nearest Address, read on the hip rows") },
        { "faceOn",       viewMarksToJson(m.faceOn, t0Us) },
        { "dtl",          viewMarksToJson(m.dtl, t0Us) } };
}

// One view's object as the replay payload carries it, re-timed by `retime` (identity on the
// live path; window-relative → absolute on the disk path).
template <typename Retime>
inline QVariantMap viewMarksPayload(const QJsonObject &v, Retime retime)
{
    using namespace addressmarks_json_detail;
    QVariantMap out { { QStringLiteral("found"), v.value(QStringLiteral("found")).toBool(false) } };
    if (!out.value(QStringLiteral("found")).toBool()) return out;
    out.insert(QStringLiteral("t_us"),      retime(v.value(QStringLiteral("t_us"))));
    for (const char *k : { "rowY", "hipLeftX", "hipRightX", "leftX", "rightX", "buttX" })
        out.insert(QString::fromLatin1(k), numOr(v.value(QLatin1String(k)), std::numeric_limits<double>::quiet_NaN()));
    out.insert(QStringLiteral("side"), v.value(QStringLiteral("side")).toInt(0));
    out.insert(QStringLiteral("rows"), v.value(QStringLiteral("rows")).toInt(0));
    out.insert(QStringLiteral("rowSource"), v.value(QStringLiteral("rowSource")).toString(QStringLiteral("hips")));
    return out;
}

} // namespace pinpoint::analysis
