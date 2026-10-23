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

// `analysis.addressMarks` (pinpoint.addressMarks/2) — the body's outer edges at hip height on
// each camera's address frame (address_marks.h), and on the DTL view TPI's spine angle line
// (tailbone, back of the neck; stage version 2 — absent keys on older records read as NaN).
// Schema /2 (stage version 3) adds `dtlPhases`: the DTL butt line and back line at every P-frame
// P1…P7, each { phase, eventTUs, found, refused, <the view's keys> }, and `phaseNeckFrac`, where
// their back line's upper point is read (behind the ears; the coach line in `dtl` reads the collar,
// 0.5 — P1's entry is the same frame and butt as `dtl` with the back line re-read). /1 is the same object
// without `dtlPhases`; every reader below takes both. ONE builder for swing.ppsw and the replay
// payload: the document holds the whole block; the tile reads one view's object
// (`addressMarks` for face-on, `dtl.addressMarks` for down the line). Times relative to `t0Us`,
// as every analysis block's are. A NaN is written as null, never as a number.
// Stage version 4 adds, on each address view, the HEAD OUTLINE (address_marks.h measureHead): `headFound`,
// and either the circle `headCx` (÷ W), `headCy`, `headR` (÷ H) with `crownY`, `headRowY`, `headLeftX`,
// `headRightX`, or `headRefused` (why not). Additive within schema /2: an older record has none of them
// and reads as no head outline (the readers draw the face-point circle).

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <limits>

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
    // TPI's spine angle line (DTL, stage version 2): null when not measured
    o["tailboneX"] = num(v.tailboneX);
    o["tailboneY"] = num(v.tailboneY);
    o["neckX"]     = num(v.neckX);
    o["neckY"]     = num(v.neckY);
    o["backWalks"] = v.backWalks;
    // the head outline (stage version 4; the address frames only — a P-frame never reads one): absent
    // keys on an older record read as not found
    if (v.headFound || !v.headRefused.empty()) o["headFound"] = v.headFound;
    if (v.headFound) {
        o["headCx"]     = num(v.headCx);
        o["headCy"]     = num(v.headCy);
        o["headR"]      = num(v.headR);
        o["crownY"]     = num(v.crownY);
        o["headRowY"]   = num(v.headRowY);
        o["headLeftX"]  = num(v.headLeftX);
        o["headRightX"] = num(v.headRightX);
    } else if (!v.headRefused.empty()) {
        o["headRefused"] = QString::fromStdString(v.headRefused);
    }
    return o;
}

// One DTL P-frame (address_marks.h DtlPhaseMarks): the view's keys, plus which position and why not.
inline QJsonObject dtlPhaseMarksToJson(const addressmarks::DtlPhaseMarks &p, int64_t t0Us)
{
    QJsonObject o = viewMarksToJson(p.v, t0Us);
    o["phase"]    = p.phase;
    o["eventTUs"] = qint64(p.eventTUs >= t0Us ? p.eventTUs - t0Us : p.eventTUs);
    if (!p.v.found || !p.refused.empty()) o["refused"] = QString::fromStdString(p.refused);
    if (p.v.side != 0) o["side"] = p.v.side;   // the frozen rear side, also on a refused frame
    return o;
}

inline QJsonObject addressMarksToJson(const addressmarks::AddressMarks &m, int64_t t0Us, int stageVersion)
{
    using namespace addressmarks_json_detail;
    QJsonObject o {
        { "schema",       QStringLiteral("pinpoint.addressMarks/2") },
        { "stageVersion", stageVersion },
        { "source",       QStringLiteral("person mask (u2netp) on the frame nearest Address, read on the hip rows") },
        { "faceOn",       viewMarksToJson(m.faceOn, t0Us) },
        { "dtl",          viewMarksToJson(m.dtl, t0Us) } };
    if (!m.dtlPhases.empty()) {
        o["phaseNeckFrac"] = num(m.phaseNeckFrac);   // where the P-frames' back line is read (not the coach line's 0.5)
        QJsonArray a;
        for (const addressmarks::DtlPhaseMarks &p : m.dtlPhases) a.append(dtlPhaseMarksToJson(p, t0Us));
        o["dtlPhases"] = a;
    }
    return o;
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
    for (const char *k : { "rowY", "hipLeftX", "hipRightX", "leftX", "rightX", "buttX",
                           "tailboneX", "tailboneY", "neckX", "neckY",
                           "headCx", "headCy", "headR", "crownY", "headRowY", "headLeftX", "headRightX" })
        out.insert(QString::fromLatin1(k), numOr(v.value(QLatin1String(k)), std::numeric_limits<double>::quiet_NaN()));
    // the head outline (stage version 4): found only with its circle; an older record has neither
    out.insert(QStringLiteral("headFound"), v.value(QStringLiteral("headFound")).toBool(false)
                                                && v.value(QStringLiteral("headR")).isDouble());
    if (v.contains(QStringLiteral("headRefused")))
        out.insert(QStringLiteral("headRefused"), v.value(QStringLiteral("headRefused")).toString());
    out.insert(QStringLiteral("side"), v.value(QStringLiteral("side")).toInt(0));
    out.insert(QStringLiteral("rows"), v.value(QStringLiteral("rows")).toInt(0));
    out.insert(QStringLiteral("backWalks"), v.value(QStringLiteral("backWalks")).toInt(0));
    out.insert(QStringLiteral("rowSource"), v.value(QStringLiteral("rowSource")).toString(QStringLiteral("hips")));
    return out;
}

// The DTL P-frames of an `analysis.addressMarks` object (schema /2) as the replay payload carries
// them: one map per position, the view's keys (viewMarksPayload) plus `phase`, `eventTUs` (re-timed)
// and `refused`. A /1 object has no `dtlPhases` and gives an empty list.
template <typename Retime>
inline QVariantList dtlPhaseMarksPayload(const QJsonObject &am, Retime retime)
{
    QVariantList out;
    for (const QJsonValue &pv : am.value(QStringLiteral("dtlPhases")).toArray()) {
        const QJsonObject p = pv.toObject();
        QVariantMap m = viewMarksPayload(p, retime);
        m.insert(QStringLiteral("phase"),    p.value(QStringLiteral("phase")).toInt(-1));
        m.insert(QStringLiteral("eventTUs"), retime(p.value(QStringLiteral("eventTUs"))));
        m.insert(QStringLiteral("side"),     p.value(QStringLiteral("side")).toInt(0));
        if (p.contains(QStringLiteral("refused")))
            m.insert(QStringLiteral("refused"), p.value(QStringLiteral("refused")).toString());
        out.append(m);
    }
    return out;
}

} // namespace pinpoint::analysis
