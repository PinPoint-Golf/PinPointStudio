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

// The fitted skeleton as JSON — schema `pinpoint.skeleton3d/1` — plus its tuning keys and
// a READER for the 3-D swing view. ONE writer for swing.json's `analysis.skeleton3d` and
// SwingLab's result.json alike. Eigen-free: the GUI includes this, not the solver.
//
// Times are relative to `t0Us` (the window start), as club3d's are. Per frame: the rig's
// DoFs (`d`, ybot_rig.h order via `dofNames`; radians, root translation metres), the
// world joint positions (`p`, millimetres, ybot joint order), one tier digit per joint
// (`tier`: 0 absent, 1 inferred, 2 constrained, 3 measured), the lead-hand grip point and
// shaft direction (`g`, `u`), the shaft's tier (`s`: 0 none, 1 model only, 2 one view,
// 3 both) and flags (`f`: 1 face-on label swap, 2 DTL swap, 4 limit held). A NaN is null.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <vector>

#include "../analysis_tuning.h"
#include "skeleton3d_fit.h"
#include "skeleton3d_rig.h"

namespace pinpoint::skeleton3d {

inline FitConfig fitConfigFromOverrides(const QVariantMap &ov)
{
    using namespace pinpoint::analysis::tuning;
    FitConfig c;
    apply(ov, "skeleton3d.enabled",        c.enabled);
    apply(ov, "skeleton3d.useDtl",         c.useDtl);
    apply(ov, "skeleton3d.useLimits",      c.useLimits);
    apply(ov, "skeleton3d.useContact",     c.useContact);
    apply(ov, "skeleton3d.useShaft",       c.useShaft);
    apply(ov, "skeleton3d.useClubhead",    c.useClubhead);
    apply(ov, "skeleton3d.useGrip",        c.useGrip);
    apply(ov, "skeleton3d.useSmooth",      c.useSmooth);
    apply(ov, "skeleton3d.useImu",         c.useImu);
    apply(ov, "skeleton3d.useHm",          c.useHm);
    apply(ov, "skeleton3d.fitCameras",     c.fitCameras);
    apply(ov, "skeleton3d.fitDtlRoll",     c.fitDtlRoll);
    apply(ov, "skeleton3d.fitLengths",     c.fitLengths);
    apply(ov, "skeleton3d.labelSwap",      c.labelSwap);
    apply(ov, "skeleton3d.faceOnDistanceM", c.faceOnDistanceM);
    apply(ov, "skeleton3d.dtlDistanceM",   c.dtlDistanceM);
    apply(ov, "skeleton3d.lengthSigma",    c.lengthSigma);
    apply(ov, "skeleton3d.globalScaleSigma", c.globalScaleSigma);
    apply(ov, "skeleton3d.cauchyC",        c.cauchyC);
    apply(ov, "skeleton3d.smoothAccRad",   c.smoothAccRad);
    apply(ov, "skeleton3d.smoothAccRootM", c.smoothAccRootM);
    apply(ov, "skeleton3d.fastFactor",     c.fastFactor);
    apply(ov, "skeleton3d.stage1Iters",    c.stage1Iters);
    apply(ov, "skeleton3d.stage2Iters",    c.stage2Iters);
    apply(ov, "skeleton3d.hmFlexSign",     c.hmFlexSign);
    apply(ov, "skeleton3d.hmRadSign",      c.hmRadSign);
    return c;
}

namespace json_detail {
inline QJsonValue num(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); }
inline double rnd(double v, double q) { return std::round(v / q) * q; }
inline QJsonArray vec(const V3 &v, double q = 1e-4)
{
    return QJsonArray { rnd(v.x, q), rnd(v.y, q), rnd(v.z, q) };
}
inline QJsonArray arr3(const std::array<double, 3> &v, double q = 1e-4)
{
    return QJsonArray { rnd(v[0], q), rnd(v[1], q), rnd(v[2], q) };
}
inline double at(const QJsonArray &a, int i, double def = 0.0)
{
    return i < a.size() && a[i].isDouble() ? a[i].toDouble() : def;
}
inline V3 v3(const QJsonArray &a) { return { at(a, 0), at(a, 1), at(a, 2) }; }
}

inline QJsonObject skeleton3dToJson(const FitResult &r, int64_t t0Us, int stageVersion)
{
    using namespace json_detail;
    const Rig &R = rig();
    QJsonArray dofNames;
    for (const Dof &d : R.dofs) dofNames.append(QString::fromLatin1(d.name));
    QJsonArray jointNames;
    for (int j = 0; j < Rig::N; ++j) jointNames.append(QString::fromLatin1(ybot::kJoints[j].name));
    QJsonObject lengths;
    for (int g = 0; g < GroupCount; ++g) lengths.insert(QString::fromLatin1(groupName(g)), rnd(r.scale[size_t(g)], 1e-4));
    QJsonObject rawCv;
    for (int g = 0; g < GroupCount; ++g)
        if (std::isfinite(r.rawLengthCv[size_t(g)])) rawCv.insert(QString::fromLatin1(groupName(g)), rnd(r.rawLengthCv[size_t(g)], 1e-4));

    QJsonArray frames;
    for (size_t i = 0; i < r.t_us.size() && r.valid; ++i) {
        QJsonArray d, p;
        for (double v : r.theta[i]) d.append(rnd(v, 1e-4));
        for (int j = 0; j < Rig::N; ++j) {
            const V3 &q = r.joints[i][size_t(j)];
            p.append(std::round(q.x * 1000)); p.append(std::round(q.y * 1000)); p.append(std::round(q.z * 1000));
        }
        QString tier;
        tier.reserve(Rig::N);
        for (int j = 0; j < Rig::N; ++j) tier.append(QChar('0' + r.tier[i][size_t(j)]));
        frames.append(QJsonObject {
            // Window-relative whatever domain the fit ran in: subtract t0 only from an ABSOLUTE
            // time (≥ t0). A re-analysed swing is already relative, and subtracting it again wrote
            // −98 s frame times the playhead never reached — the figure sat frozen.
            { "t", qint64(r.t_us[i] >= t0Us ? r.t_us[i] - t0Us : r.t_us[i]) },
            { "d", d }, { "p", p }, { "tier", tier },
            { "g", vec(r.grip[i]) }, { "u", vec(r.shaftDir[i]) },
            { "s", int(r.shaftTier[i]) }, { "f", int(r.flags[i]) } });
    }
    return QJsonObject {
        { "schema",       QStringLiteral("pinpoint.skeleton3d/1") },
        { "stageVersion", stageVersion },
        { "valid",        r.valid },
        { "reason",       QString::fromStdString(r.reason) },
        { "frame",        QStringLiteral("world: X face-on image-right, Y face-on view ray, Z up; rig: Y-bot (ybot_rig.h)") },
        { "dtl",          r.dtlUsed },
        { "foMirrored",   r.foMirrored },
        { "camera",       QJsonObject {
              { "calibrated", false },
              { "fF", rnd(r.cam.fF, 0.1) }, { "pFDeg", rnd(r.cam.pF / kDeg, 0.01) },
              { "cD", vec(r.cam.cD) }, { "psiDDeg", rnd(r.cam.psiD / kDeg, 0.01) },
              { "pDDeg", rnd(r.cam.pD / kDeg, 0.01) }, { "fD", rnd(r.cam.fD, 0.1) },
              { "zG", rnd(r.cam.zG, 1e-4) },
              { "rFDeg", rnd(r.cam.rF / kDeg, 0.01) }, { "rDDeg", rnd(r.cam.rD / kDeg, 0.01) },
              { "gammaDeg", num(std::isfinite(r.gammaDeg) ? rnd(r.gammaDeg, 0.01) : r.gammaDeg) },
              { "rRatio", num(std::isfinite(r.rRatio) ? rnd(r.rRatio, 1e-4) : r.rRatio) } } },
        { "scaleSource",  QString::fromStdString(r.scaleSource) },
        { "scale",        rnd(r.scaleGlobal, 1e-4) },
        { "lengths",      lengths },
        { "grip",         QJsonObject {
              { "axis", arr3(r.gripAxisLocal) }, { "offset", arr3(r.gripOffsetLocal) },
              { "trailOffset", arr3(r.trailGripOffsetLocal) },
              { "clubLengthM", num(std::isfinite(r.clubLengthM) ? rnd(r.clubLengthM, 1e-4) : r.clubLengthM) } } },
        { "display",      QJsonObject {
              { "originWorld", vec(r.displayOrigin) },
              { "stanceYawDeg", rnd(r.stanceYawRad / kDeg, 0.01) },
              { "ball", r.ballValid ? QJsonValue(vec(r.ballWorld)) : QJsonValue() } } },
        { "diagnostics",  QJsonObject {
              { "costInit", rnd(r.costInit, 0.1) }, { "costFinal", rnd(r.costFinal, 0.1) },
              { "iterations", r.iterations }, { "ms", rnd(r.ms, 1) },
              { "reprojMedPxFo", num(r.reprojMedPxFo) }, { "reprojMedPxDtl", num(r.reprojMedPxDtl) },
              { "rawLengthCv", rawCv },
              { "nSwapFo", r.nSwapFo }, { "nSwapDtl", r.nSwapDtl }, { "nLimitHeld", r.nLimitHeld },
              { "footSlipP90Mm", num(r.footSlipP90Mm) } } },
        { "dofNames",     dofNames },
        { "jointNames",   jointNames },
        { "frames",       frames } };
}

// ── the reader: what the 3-D swing view needs, typed ─────────────────────────
struct Skeleton3DFrame {
    int64_t t_us = 0;                         // window-relative
    std::vector<double> dof;                  // rig().dofCount()
    std::array<V3, Rig::N> joint {};          // world, metres
    std::array<uint8_t, Rig::N> tier {};
    V3 grip, shaftDir;
    int shaftTier = 0, flags = 0;
};

struct Skeleton3DTrack {
    bool valid = false;
    QString reason;
    bool dtl = false;
    std::array<double, GroupCount> lengths {};
    double clubLengthM = 0.95;
    V3 displayOrigin;
    double stanceYawDeg = 0;
    bool ballValid = false;
    V3 ball;
    double floorZ = 0;
    std::vector<Skeleton3DFrame> frames;
};

// `clockT0Us`: the document's clock.t0_us. Frame times at or above it are absolute and are made
// window-relative here — the same idempotent rule every other reader applies — so a document
// written before the writer applied it still plays.
inline Skeleton3DTrack skeleton3dFromJson(const QJsonObject &o, int64_t clockT0Us = 0)
{
    using namespace json_detail;
    Skeleton3DTrack t;
    if (o.value(QStringLiteral("schema")).toString() != QLatin1String("pinpoint.skeleton3d/1")) {
        t.reason = QStringLiteral("no skeleton3d on this shot");
        return t;
    }
    t.valid = o.value(QStringLiteral("valid")).toBool();
    t.reason = o.value(QStringLiteral("reason")).toString();
    t.dtl = o.value(QStringLiteral("dtl")).toBool();
    const QJsonObject L = o.value(QStringLiteral("lengths")).toObject();
    for (int g = 0; g < GroupCount; ++g) t.lengths[size_t(g)] = L.value(QString::fromLatin1(groupName(g))).toDouble(1.0);
    t.clubLengthM = o.value(QStringLiteral("grip")).toObject().value(QStringLiteral("clubLengthM")).toDouble(0.95);
    const QJsonObject D = o.value(QStringLiteral("display")).toObject();
    t.displayOrigin = v3(D.value(QStringLiteral("originWorld")).toArray());
    t.stanceYawDeg = D.value(QStringLiteral("stanceYawDeg")).toDouble();
    if (D.value(QStringLiteral("ball")).isArray()) {
        t.ballValid = true;
        t.ball = v3(D.value(QStringLiteral("ball")).toArray());
    }
    t.floorZ = o.value(QStringLiteral("camera")).toObject().value(QStringLiteral("zG")).toDouble();

    // DoFs by NAME, so a document written by a different DoF layout reads as absent, not wrong.
    const Rig &R = rig();
    const QJsonArray names = o.value(QStringLiteral("dofNames")).toArray();
    std::vector<int> map(size_t(names.size()), -1);
    for (int i = 0; i < names.size(); ++i)
        for (int k = 0; k < R.dofCount(); ++k)
            if (names[i].toString() == QLatin1String(R.dofs[size_t(k)].name)) map[size_t(i)] = k;
    const QJsonArray jn = o.value(QStringLiteral("jointNames")).toArray();
    const bool jointsMatch = jn.size() == Rig::N;
    for (const QJsonValue &fv : o.value(QStringLiteral("frames")).toArray()) {
        const QJsonObject f = fv.toObject();
        Skeleton3DFrame fr;
        fr.t_us = f.value(QStringLiteral("t")).toInteger();
        if (clockT0Us > 0 && fr.t_us >= clockT0Us) fr.t_us -= clockT0Us;
        fr.dof.assign(size_t(R.dofCount()), 0.0);
        const QJsonArray d = f.value(QStringLiteral("d")).toArray();
        for (int i = 0; i < d.size() && i < int(map.size()); ++i)
            if (map[size_t(i)] >= 0) fr.dof[size_t(map[size_t(i)])] = d[i].toDouble();
        const QJsonArray p = f.value(QStringLiteral("p")).toArray();
        if (jointsMatch && p.size() == 3 * Rig::N)
            for (int j = 0; j < Rig::N; ++j)
                fr.joint[size_t(j)] = { p[3 * j].toDouble() / 1000.0, p[3 * j + 1].toDouble() / 1000.0,
                                        p[3 * j + 2].toDouble() / 1000.0 };
        const QString tier = f.value(QStringLiteral("tier")).toString();
        for (int j = 0; j < Rig::N && j < tier.size(); ++j) fr.tier[size_t(j)] = uint8_t(tier[j].unicode() - '0');
        fr.grip = v3(f.value(QStringLiteral("g")).toArray());
        fr.shaftDir = v3(f.value(QStringLiteral("u")).toArray());
        fr.shaftTier = f.value(QStringLiteral("s")).toInt();
        fr.flags = f.value(QStringLiteral("f")).toInt();
        t.frames.push_back(std::move(fr));
    }
    if (t.valid && t.frames.size() < 2) {
        t.valid = false;
        t.reason = QStringLiteral("skeleton3d has fewer than two frames");
    }
    return t;
}

} // namespace pinpoint::skeleton3d
