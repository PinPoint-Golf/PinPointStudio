/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// THE SESSION SUMMARY'S DAY READINGS (refactor 5.5d; DEC-74, the Solution Architect's 23 Nov 2026 "You can grow the
// summary file to support this"; information_model.md FILE-SESS-05 `dayReadings`). What ANOTHER session's Charts and
// report read of this session, so they read it through its summary (the athlete level's SessionIndex summaries), never
// through its files. Only what those readers need:
//   delivery[]  per shot with a swing plane or monitor reading (strike order): the swing number, the ordinal its grid
//               recorded (written only when it is not the swing number), the swing plane at P6, the hands' steepening,
//               the monitor's club path and attack angle. The Charts delivery presenter and the report's Club delivery
//               page pool them into the fits of the golfer's day (session_delivery.h decision 13).
//   address[]   per shot with a reading at address (strike order): the swing number, its data warning (written only
//               when true), and the value of each of the pack's address absolutes (the rig-sensitive, non-composite
//               measures) as the shot's kept rows read it. The Charts posture presenter and the report's Posture page
//               pool them into the "At address" lower box (session_posture.h §7.4.2), from every EARLIER session.
//   cameras     the session's cameras by view (down the line, face-on): present and serial, off its first swing
//               document. An earlier session is kept for a view only on this session's camera.
// Ids and numbers only (R-DER-9). Absent in a summary older than resultsVersion 4: `has` false, and a reader says the
// session is not summed up yet (show with a warning, never hide).
//
// The rules that MAKE them are session_day_readings_rules.h's (the writer and the posture presenter's own session).
//
// Rules ring (src/Diagnostics/derived): values and JSON only.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

struct ShotDeliveryReading {
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    int    shotId  = 0;         // the swing number
    int    ordinal = 0;         // the ordinal its grid recorded, else the swing number
    double planeP6    = kNaN;   // swingPlaneCurve at P6, °
    double steepening = kNaN;   // handPlaneSteepening, °
    double lmPath     = kNaN;   // lm.clubPath, °
    double lmAttack   = kNaN;   // lm.attackAngle, °
};

struct ShotAddressReading {
    int                     shotId = 0;
    bool                    dataWarning = false;
    QHash<QString, double>  values;   // measure id → value; only finite ones
};

struct SessionCameraView {
    bool    present = false;
    QString serial;             // "" not recorded
};

struct SessionCameras {
    bool              known = false;   // a swing document's streams were read
    SessionCameraView dtl, faceOn;
};

struct SessionDayReadings {
    bool                             has = false;   // false: a summary older than resultsVersion 4
    std::vector<ShotDeliveryReading> delivery;      // strike order
    std::vector<ShotAddressReading>  address;       // strike order
    SessionCameras                   cameras;
};

// ── JSON (FILE-SESS-05's `dayReadings`) ──────────────────────────────────────────────────────────────────────────

namespace day_readings_detail {
inline QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }
inline double numOf(const QJsonValue &v) { return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN(); }
inline QJsonObject viewToJson(const SessionCameraView &v)
{
    QJsonObject o{ { QStringLiteral("present"), v.present } };
    if (!v.serial.isEmpty()) o.insert(QStringLiteral("serial"), v.serial);
    return o;
}
inline SessionCameraView viewFromJson(const QJsonObject &o)
{
    return SessionCameraView{ o.value(QStringLiteral("present")).toBool(), o.value(QStringLiteral("serial")).toString() };
}
} // namespace day_readings_detail

inline QJsonObject sessionDayReadingsToJson(const SessionDayReadings &d)
{
    using namespace day_readings_detail;
    QJsonArray delivery, address;
    for (const ShotDeliveryReading &s : d.delivery) {
        QJsonObject o{ { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("planeP6"), num(s.planeP6) },
                       { QStringLiteral("steepening"), num(s.steepening) }, { QStringLiteral("lmPath"), num(s.lmPath) },
                       { QStringLiteral("lmAttack"), num(s.lmAttack) } };
        if (s.ordinal != s.shotId) o.insert(QStringLiteral("ordinal"), s.ordinal);
        delivery.append(o);
    }
    for (const ShotAddressReading &s : d.address) {
        QJsonObject values;
        for (auto it = s.values.constBegin(); it != s.values.constEnd(); ++it)
            if (std::isfinite(it.value())) values.insert(it.key(), it.value());
        QJsonObject o{ { QStringLiteral("shotId"), s.shotId }, { QStringLiteral("values"), values } };
        if (s.dataWarning) o.insert(QStringLiteral("dataWarning"), true);
        address.append(o);
    }
    return QJsonObject{ { QStringLiteral("delivery"), delivery }, { QStringLiteral("address"), address },
                        { QStringLiteral("cameras"), QJsonObject{ { QStringLiteral("known"), d.cameras.known },
                                                                  { QStringLiteral("dtl"), viewToJson(d.cameras.dtl) },
                                                                  { QStringLiteral("faceOn"), viewToJson(d.cameras.faceOn) } } } };
}

// `o` is the `dayReadings` object; an absent one (an older summary) reads as `has` false.
inline SessionDayReadings sessionDayReadingsFromJson(const QJsonValue &v)
{
    using namespace day_readings_detail;
    SessionDayReadings d;
    if (!v.isObject()) return d;
    const QJsonObject o = v.toObject();
    d.has = true;
    for (const QJsonValue &x : o.value(QStringLiteral("delivery")).toArray()) {
        const QJsonObject s = x.toObject();
        ShotDeliveryReading r;
        r.shotId     = s.value(QStringLiteral("shotId")).toInt();
        r.ordinal    = s.value(QStringLiteral("ordinal")).toInt(r.shotId);
        r.planeP6    = numOf(s.value(QStringLiteral("planeP6")));
        r.steepening = numOf(s.value(QStringLiteral("steepening")));
        r.lmPath     = numOf(s.value(QStringLiteral("lmPath")));
        r.lmAttack   = numOf(s.value(QStringLiteral("lmAttack")));
        d.delivery.push_back(r);
    }
    for (const QJsonValue &x : o.value(QStringLiteral("address")).toArray()) {
        const QJsonObject s = x.toObject();
        ShotAddressReading r;
        r.shotId      = s.value(QStringLiteral("shotId")).toInt();
        r.dataWarning = s.value(QStringLiteral("dataWarning")).toBool(false);
        const QJsonObject values = s.value(QStringLiteral("values")).toObject();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
            if (const double n = numOf(it.value()); std::isfinite(n)) r.values.insert(it.key(), n);
        d.address.push_back(std::move(r));
    }
    const QJsonObject c = o.value(QStringLiteral("cameras")).toObject();
    d.cameras.known  = c.value(QStringLiteral("known")).toBool(false);
    d.cameras.dtl    = viewFromJson(c.value(QStringLiteral("dtl")).toObject());
    d.cameras.faceOn = viewFromJson(c.value(QStringLiteral("faceOn")).toObject());
    return d;
}

} // namespace pinpoint::analysis
