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

// HOW THE DAY READINGS ARE MADE (refactor 5.5d; DEC-74; the values are session_day_readings.h's). Each rule is the one
// the readers used on the other sessions' files before 5.5d, moved here so the session's own SessionDiagnostics makes the
// numbers its summary carries:
//   shotDeliveryReadingOf   a swing's delivery numbers off its phase grid (delivery_grid_numbers.h, the Charts
//                           delivery section's own rule); none when it read neither a plane nor the monitor.
//   isAddressAbsolute       which of the pack's measures the "At address" block reads: rig-sensitive, not composite,
//                           with a series to read (the Charts posture env's selection, session_posture.cpp).
//   addressReadingOf        a measure's graded reading on one ledger shot (the posture presenter's old ledgerReading):
//                           a row driven or read by it that the engine could assess — or, on a shot with a data
//                           warning, a row withheld after it was read (its corridor applied) — else a composite's part.
//   sessionCamerasOf        a swing document's analysis root → the session's cameras by view (setup.perspectiveName,
//                           else the alias) and their serials (source.serial).
//
// Rules ring (src/Diagnostics/derived): values only, no file.

#include "session_day_readings.h"
#include "diagnostic_ledger.h"
#include "../model/characteristic.h"
#include "../model/delivery_grid_numbers.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cmath>
#include <optional>

namespace pinpoint::analysis {

inline std::optional<ShotDeliveryReading> shotDeliveryReadingOf(const DeliveryGridNumbers &n, int shotId, int gridOrdinal)
{
    if (!n.hasPlane() && !n.hasMonitor()) return std::nullopt;
    ShotDeliveryReading r;
    r.shotId     = shotId;
    r.ordinal    = gridOrdinal > 0 ? gridOrdinal : shotId;
    r.planeP6    = n.plane[5];   // P6
    r.steepening = n.steepening;
    r.lmPath     = n.lmPath;
    r.lmAttack   = n.lmAttack;
    return r;
}

inline bool isAddressAbsolute(const Measure &m)
{
    return m.rigSensitive && m.kind != MeasureKind::Composite && !measureKeyLadder(m).isEmpty();
}

inline QStringList addressAbsoluteIds(const CharacteristicPack &pack)
{
    QStringList out;
    for (const Measure &m : pack.measures)
        if (isAddressAbsolute(m)) out << m.id;
    return out;
}

inline double addressReadingOf(const ShotRecord &shot, const QString &measureId)
{
    for (const ConditionRow &r : shot.rows) {
        if (r.state == ShotState::NotAssessable && !(shot.dataWarning && r.corridorHi > r.corridorLo)) continue;
        for (const MeasureRow &m : rowReadings(r))
            if (m.measureId == measureId && std::isfinite(m.value)) return m.value;
    }
    for (const ConditionRow &r : shot.rows) {
        for (const CompositePartRow &p : r.composite.parts)
            if (p.measureId == measureId && std::isfinite(p.value)) return p.value;
        for (const MeasureRow &m : r.readings)
            for (const CompositePartRow &p : m.composite.parts)
                if (p.measureId == measureId && std::isfinite(p.value)) return p.value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// None when the shot reads none of `measureIds`.
inline std::optional<ShotAddressReading> shotAddressReadingOf(const ShotRecord &shot, const QStringList &measureIds)
{
    ShotAddressReading r;
    r.shotId = shot.shotId;
    r.dataWarning = shot.dataWarning;
    for (const QString &id : measureIds)
        if (const double v = addressReadingOf(shot, id); std::isfinite(v)) r.values.insert(id, v);
    if (r.values.isEmpty()) return std::nullopt;
    return r;
}

// An empty root (no document, or one that could not be read): not known.
inline SessionCameras sessionCamerasOf(const QJsonObject &analysisRoot)
{
    SessionCameras c;
    if (analysisRoot.isEmpty()) return c;
    c.known = true;
    for (const QJsonValue &sv : analysisRoot.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject st = sv.toObject();
        if (st.value(QStringLiteral("kind")).toString() != QLatin1String("video")) continue;
        const QString persp = st.value(QStringLiteral("setup")).toObject().value(QStringLiteral("perspectiveName")).toString();
        const QString alias = st.value(QStringLiteral("alias")).toString();
        SessionCameraView *view = nullptr;
        if (persp == QLatin1String("DownTheLine") || (persp.isEmpty() && alias.compare(QLatin1String("DTL"), Qt::CaseInsensitive) == 0))
            view = &c.dtl;
        else if (persp == QLatin1String("FaceOn") || (persp.isEmpty() && alias.startsWith(QLatin1String("Face"), Qt::CaseInsensitive)))
            view = &c.faceOn;
        if (!view) continue;
        view->present = true;
        view->serial = st.value(QStringLiteral("source")).toObject().value(QStringLiteral("serial")).toVariant().toString();
    }
    return c;
}

} // namespace pinpoint::analysis
