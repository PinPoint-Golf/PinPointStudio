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

inline fusion::Config shaftFusionConfigFromOverrides(const QVariantMap &ov,
                                                     const fusion::Config &seed = fusion::Config{})
{
    using namespace tuning;
    fusion::Config c = seed;
    apply(ov, "shaft.fusion.enabled",           c.enabled);
    apply(ov, "shaft.fusion.dtlYawDeg",         c.dtlYawDeg);
    apply(ov, "shaft.fusion.dtlPitchDeg",       c.dtlPitchDeg);
    apply(ov, "shaft.fusion.dtlRollDeg",        c.dtlRollDeg);
    apply(ov, "shaft.fusion.dtlOffsetX",        c.dtlOffsetM[0]);
    apply(ov, "shaft.fusion.dtlOffsetY",        c.dtlOffsetM[1]);
    apply(ov, "shaft.fusion.dtlOffsetZ",        c.dtlOffsetM[2]);
    apply(ov, "shaft.fusion.calibrated",        c.calibrated);
    apply(ov, "shaft.fusion.minCond",           c.minCond);
    apply(ov, "shaft.fusion.maxGapUs",          c.maxGapUs);
    apply(ov, "shaft.fusion.minPlaneN",         c.minPlaneN);
    apply(ov, "shaft.fusion.planeOopMaxDeg",    c.planeOopMaxDeg);
    apply(ov, "shaft.fusion.offPlaneK",         c.offPlaneK);
    apply(ov, "shaft.fusion.offPlaneFloorDeg",  c.offPlaneFloorDeg);
    apply(ov, "shaft.fusion.backIncoherentDeg", c.backIncoherentDeg);
    apply(ov, "shaft.fusion.minAddressN",       c.minAddressN);
    // dtl_continuous_track_design_update.md §3.2a — (A) η(t), (C) the DTL anchor, (D) the reflected band.
    apply(ov, "shaft.fusion.eta.enabled",       c.eta.enabled);
    apply(ov, "shaft.fusion.eta.knotMs",        c.eta.knotMs);
    apply(ov, "shaft.fusion.eta.lambda",        c.eta.lambda);
    apply(ov, "shaft.fusion.eta.priorReachMs",  c.eta.priorReachMs);
    apply(ov, "shaft.fusion.eta.priorFar",      c.eta.priorFar);
    apply(ov, "shaft.fusion.eta.priorNear",     c.eta.priorNear);
    apply(ov, "shaft.fusion.eta.maxAbsDeg",     c.eta.maxAbsDeg);
    apply(ov, "shaft.fusion.dtlAnchor.enabled", c.dtlAnchor);
    apply(ov, "shaft.fusion.reflectBands.enabled",    c.reflectBands);
    apply(ov, "shaft.fusion.reflectBands.minGainDeg", c.reflectMinGainDeg);
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
        if (s.flags & fusion::DtlAnchored)    flags.append(QStringLiteral("dtlAnchored"));
        if (s.flags & fusion::Reflected)      flags.append(QStringLiteral("reflected"));
        QJsonObject fr {
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
            { "oopDeg",  num(s.oopDeg) } };
        // §3.2a keys are written only where an item produced them, so a run with every
        // item off writes the frames the previous version wrote, key for key.
        if (std::isfinite(s.etaDeg))     fr["etaDeg"] = s.etaDeg;
        if (std::isfinite(s.anchorCond)) fr["anchorCond"] = s.anchorCond;
        if (s.flags & fusion::DtlAnchored) fr["uBridged"] = QJsonArray { s.uBridged.x, s.uBridged.y, s.uBridged.z };
        frames.append(fr);
    }
    QJsonObject eta { { "fitted", t.eta.fitted } };
    if (t.eta.fitted) {
        QJsonArray k, v, nr;
        for (size_t i = 0; i < t.eta.knotsUs.size(); ++i) {
            k.append(qint64(t.eta.knotsUs[i] >= t0Us ? t.eta.knotsUs[i] - t0Us : t.eta.knotsUs[i]));
            v.append(t.eta.values[i]);
            nr.append(int(t.eta.near[i]));
        }
        eta["knotsUs"] = k; eta["values"] = v; eta["near"] = nr;
        eta["n"] = t.eta.n; eta["rmsDeg"] = num(t.eta.rmsDeg);
        eta["knotMs"] = cfg.eta.knotMs; eta["lambda"] = cfg.eta.lambda; eta["priorReachMs"] = cfg.eta.priorReachMs;
        eta["priorFar"] = cfg.eta.priorFar; eta["priorNear"] = cfg.eta.priorNear; eta["maxAbsDeg"] = cfg.eta.maxAbsDeg;
    }
    QJsonArray refl;
    for (int b : t.reflectedBands) refl.append(b);
    return QJsonObject {
        { "schema",       QStringLiteral("pinpoint.club3d/1") },
        { "stageVersion", stageVersion },
        { "valid",        t.valid },
        { "frame",        QStringLiteral("cameras: X face-on image-right, Y face-on view ray, Z up") },
        // The DTL camera the fusion USED. dtlYawDeg / dtlPitchDeg are the original
        // keys (every reader before 2026-10-02); yawDeg / pitchDeg / rollDeg / offset /
        // calibrated are the record dtl_continuous_track_design_update.md §4 names,
        // filled from a measured calibration (camera_pose_sticks.h) when there is one.
        { "camera",       QJsonObject { { "dtlYawDeg", t.dtlYawDeg }, { "dtlPitchDeg", t.dtlPitchDeg },
                                        { "calibrated", t.calibrated },
                                        { "yawDeg", t.dtlYawDeg }, { "pitchDeg", t.dtlPitchDeg },
                                        { "rollDeg", t.dtlRollDeg },
                                        { "offset", QJsonArray { t.dtlOffsetM[0], t.dtlOffsetM[1], t.dtlOffsetM[2] } } } },
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
                                        { "backIncoherent",  t.backIncoherent },
                                        // §3.2a (C) and (D) records
                                        { "nDtlAnchored",       t.nDtlAnchored },
                                        { "nDtlAnchorRefused",  t.nDtlAnchorRefused },
                                        { "reflectedBands",     refl },
                                        { "backRmsBeforeReflectDeg", num(t.backRmsBeforeReflectDeg) } } },
        // §3.2a (A): the out-of-plane curve, and which items ran.
        { "eta",          eta },
        { "items",        QJsonObject { { "eta", cfg.eta.enabled }, { "dtlAnchor", cfg.dtlAnchor },
                                        { "reflectBands", cfg.reflectBands } } },
        { "frames",       frames } };
}

} // namespace pinpoint::analysis
