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

// What the trunk stand-tall solve left on a binding (trunk_imu_sequence_grf_design.md §6.8,
// "Persistence: per session, snapshotted per shot into the binding": the δ tier and σ_δ and the
// gate residuals; alignA, mountM, calibratedAt and the serial are the binding's own fields).
//
// Qt Core only, and deliberately free of imu_calibration.h: it rides on ImuSegmentBinding,
// BindingRecord and SwingImuDeviceInfo, which half the tree includes. The one place a
// TrunkSolveRecord becomes one of these is trunk_stand_tall.h (provenanceFrom).
//
// swing.json (additive; swing-json-backward-compat — readers accept the absence):
//   "calibrationSource": "trunkStandTall"            present ONLY when A/M came from the trunk
//                                                     solve; absent = the arm routines (or none)
//   "trunkSolve": { "deltaTier": "δ0", "sigmaDeltaDeg": 10,
//                   "sampleFraction", "maxAbsGyroDps", "gravityErrPct",
//                   "longAxisFromUpDeg", "faceFromHorizontalDeg" }
// The residual key names match swinglab --trunk-calib-from's per-sensor "residuals" block.
// Only a VALID solve is ever applied, so every gate passed by construction and the gate bools
// are not repeated here; the residuals say by how much.

#include <QJsonObject>
#include <QString>

namespace pinpoint {

inline constexpr const char *kTrunkStandTallCalibSource = "trunkStandTall";

struct TrunkCalibProvenance {
    bool    present = false;               // A/M on this binding came from solveTrunkSegment
    QString deltaTier;                     // "δ0" | "δ1" | "δ2" (design §6.5)
    double  sigmaDeltaDeg         = 0.0;   // the mount-yaw σ that tier carries
    double  sampleFraction        = 0.0;   // received / expected
    double  maxAbsGyroDps         = 0.0;   // the stillness residual (and the gyro bias)
    double  gravityErrPct         = 0.0;   // | |f̄| − 1 g | × 100
    double  longAxisFromUpDeg     = 0.0;   // sensor +Y (away from the USB end) from up
    double  faceFromHorizontalDeg = 0.0;   // the face normal from horizontal
};

// Add the two keys to a binding (or a stream's device block) — nothing when not present.
inline void writeTrunkCalibProvenance(QJsonObject &o, const TrunkCalibProvenance &p)
{
    if (!p.present)
        return;
    o[QStringLiteral("calibrationSource")] = QString::fromLatin1(kTrunkStandTallCalibSource);
    o[QStringLiteral("trunkSolve")] = QJsonObject{
        { QStringLiteral("deltaTier"),             p.deltaTier },
        { QStringLiteral("sigmaDeltaDeg"),         p.sigmaDeltaDeg },
        { QStringLiteral("sampleFraction"),        p.sampleFraction },
        { QStringLiteral("maxAbsGyroDps"),         p.maxAbsGyroDps },
        { QStringLiteral("gravityErrPct"),         p.gravityErrPct },
        { QStringLiteral("longAxisFromUpDeg"),     p.longAxisFromUpDeg },
        { QStringLiteral("faceFromHorizontalDeg"), p.faceFromHorizontalDeg } };
}

// The inverse. Absent keys (every swing before W2, and every arm binding) → present = false.
inline TrunkCalibProvenance readTrunkCalibProvenance(const QJsonObject &o)
{
    TrunkCalibProvenance p;
    if (o[QStringLiteral("calibrationSource")].toString() != QLatin1String(kTrunkStandTallCalibSource))
        return p;
    const QJsonObject t = o[QStringLiteral("trunkSolve")].toObject();
    p.present               = true;
    p.deltaTier             = t[QStringLiteral("deltaTier")].toString();
    p.sigmaDeltaDeg         = t[QStringLiteral("sigmaDeltaDeg")].toDouble();
    p.sampleFraction        = t[QStringLiteral("sampleFraction")].toDouble();
    p.maxAbsGyroDps         = t[QStringLiteral("maxAbsGyroDps")].toDouble();
    p.gravityErrPct         = t[QStringLiteral("gravityErrPct")].toDouble();
    p.longAxisFromUpDeg     = t[QStringLiteral("longAxisFromUpDeg")].toDouble();
    p.faceFromHorizontalDeg = t[QStringLiteral("faceFromHorizontalDeg")].toDouble();
    return p;
}

} // namespace pinpoint
