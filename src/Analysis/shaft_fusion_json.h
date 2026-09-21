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

// The fused 3-D shaft as JSON — schema `pinpoint.club3d/1` — and the Qt side of its
// config (shaft_fusion.h is a pure std header). ONE builder, for swing.json's
// `analysis.club3d` and SwingLab's result.json alike.
//
// Times are written relative to `t0Us` (swing.json's window start), as clubDtl's are.
// Directions are in the CAMERAS' frame (shaft_fusion.h): X face-on image-right, Y the
// face-on view ray, Z up. A NaN is written as null, never as a number.

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

#include <cmath>

#include "analysis_tuning.h"
#include "shaft_fusion.h"

namespace pinpoint::analysis {

inline fusion::Config shaftFusionConfigFromOverrides(const QVariantMap &ov)
{
    using namespace tuning;
    fusion::Config c;
    apply(ov, "shaft.fusion.enabled",           c.enabled);
    apply(ov, "shaft.fusion.dtlYawDeg",         c.dtlYawDeg);
    apply(ov, "shaft.fusion.dtlPitchDeg",       c.dtlPitchDeg);
    apply(ov, "shaft.fusion.minCond",           c.minCond);
    apply(ov, "shaft.fusion.maxGapUs",          c.maxGapUs);
    apply(ov, "shaft.fusion.minPlaneN",         c.minPlaneN);
    apply(ov, "shaft.fusion.planeOopMaxDeg",    c.planeOopMaxDeg);
    apply(ov, "shaft.fusion.offPlaneK",         c.offPlaneK);
    apply(ov, "shaft.fusion.offPlaneFloorDeg",  c.offPlaneFloorDeg);
    apply(ov, "shaft.fusion.backIncoherentDeg", c.backIncoherentDeg);
    apply(ov, "shaft.fusion.minAddressN",       c.minAddressN);
    return c;
}

namespace fusion_json_detail {
inline QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }
inline QJsonObject planeJson(const fusion::PlaneFit &p, bool offered)
{
    QJsonObject o { { "fitted", p.fitted }, { "n", p.n } };
    if (!p.fitted) return o;
    o["offered"]   = offered;
    o["normal"]    = QJsonArray { p.normal.x, p.normal.y, p.normal.z };
    o["inclDeg"]   = num(p.inclDeg);
    o["azimDeg"]   = num(p.azimDeg);
    o["oopRmsDeg"] = num(p.oopRmsDeg);
    o["oopP90Deg"] = num(p.oopP90Deg);
    o["foRatio"]   = num(p.foRatio);
    o["foNodeDeg"] = num(p.foNodeDeg);
    return o;
}
} // namespace fusion_json_detail

inline QJsonObject shaftTrack3dToJson(const fusion::Track3D &t, const fusion::Config &cfg,
                                      int64_t t0Us, int stageVersion)
{
    using namespace fusion_json_detail;
    QJsonArray frames;
    for (const fusion::Sample3D &s : t.samples) {
        QJsonArray flags;
        if (s.flags & fusion::SignDisagree)   flags.append(QStringLiteral("signDisagree"));
        if (s.flags & fusion::IllConditioned) flags.append(QStringLiteral("illConditioned"));
        if (s.flags & fusion::OffPlane)       flags.append(QStringLiteral("offPlane"));
        frames.append(QJsonObject {
            { "t_us",    qint64(s.t_us >= t0Us ? s.t_us - t0Us : s.t_us) },
            { "u",       QJsonArray { s.u.x, s.u.y, s.u.z } },
            { "cond",    s.cond },
            { "foSrc",   s.foSrc == fusion::FoSource::Measured ? QStringLiteral("measured")
                                                               : QStringLiteral("bridged") },
            { "flags",   flags },
            { "dtlBand", s.dtlBand },
            { "thetaF",  s.thetaF },
            { "thetaD",  s.thetaD },
            { "rhoF",    num(s.rhoF) },
            { "rhoD",    num(s.rhoD) },
            { "oopDeg",  num(s.oopDeg) } });
    }
    return QJsonObject {
        { "schema",       QStringLiteral("pinpoint.club3d/1") },
        { "stageVersion", stageVersion },
        { "valid",        t.valid },
        { "frame",        QStringLiteral("cameras: X face-on image-right, Y face-on view ray, Z up") },
        { "camera",       QJsonObject { { "dtlYawDeg", t.dtlYawDeg }, { "dtlPitchDeg", t.dtlPitchDeg },
                                        { "calibrated", false } } },
        { "address",      QJsonObject { { "inclDeg", num(t.addressInclDeg) }, { "n", t.addressN } } },
        { "deliveryVsAddressDeg", num(t.deliveryVsAddressDeg) },
        { "planes",       QJsonObject { { "back", planeJson(t.back, t.back.offered(cfg)) },
                                        { "down", planeJson(t.down, t.down.offered(cfg)) } } },
        { "summary",      QJsonObject { { "nDtlPublished",   t.nDtlPublished },
                                        { "nFused",          int(t.samples.size()) },
                                        { "nNoFaceOn",       t.nNoFaceOn },
                                        { "nBridged",        t.nBridged },
                                        { "nSignDisagree",   t.nSignDisagree },
                                        { "nIllConditioned", t.nIllConditioned },
                                        { "nOffPlane",       t.nOffPlane },
                                        { "backIncoherent",  t.backIncoherent } } },
        { "frames",       frames } };
}

} // namespace pinpoint::analysis
