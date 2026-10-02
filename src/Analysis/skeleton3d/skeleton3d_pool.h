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

// The skeleton3d SESSION POOL (swing_3d_viz_design.md §13.2 (C)): what belongs to the golfer and the
// cameras, pooled over a session's swings and then held fixed in each swing's fit.
//
// - The golfer, once per session, as robust medians over the swings' own (pass-1) fits: the club, the
//   grip, the shoulder/hip surface offsets and the bone scales can all be pooled (PoolConfig), but none
//   is by default: graded, each cost (§13.7). Only the cameras are. Pass 1 keeps the scales frozen from the height: freed,
//   they are not identifiable (the spine grew ×2, the club to 1.1 m).
// - The cameras, once per CAMERA EPOCH: a camera that moves mid-session starts a new epoch (the DTL
//   moved between 4 July swings 3 and 4). Swings are taken in order; one whose camera parameters
//   differ from its epoch's median by more than the thresholds starts the next.
//
// Pure (Qt JSON only): the pooled file is `<sessionDir>/skeleton3d_session.json`, schema
// `pinpoint.skeleton3dSession/1`. Pooled at re-analysis and session end only; live shots never use it.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

#include "skeleton3d_fit.h"

namespace pinpoint::skeleton3d {

struct PoolConfig {
    // Tripod moves only. The per-swing camera fit scatters (4 July, v2: focal ±3 %, DTL centre ±8 cm,
    // and face-on pitch trades against DTL roll by up to 11°), so the thresholds sit above that and
    // pitch/roll never split. The 4 July DTL move (swings 3 → 4) is a 5° yaw.
    double epochCentreM = 0.30;     // a DTL camera that moved more than this starts a new epoch
    double epochYawDeg = 4.0;       // …or turned (DTL yaw) more than this
    double epochFocalFrac = 0.25;   // …or zoomed (either camera's focal) more than this fraction: the
                                    // face-on focal alone scatters ±10 % swing to swing on 11 June
    // What is held fixed besides the cameras (§13.7). Each was graded on the corpus and left OFF: a
    // pooled grip costs face-on only 3° (p90 +20°) — the golfer re-grips every swing; the shoulder/hip
    // offsets cost 4°; the club 0.5–1.2° (and reads short: 0.79 m grip-to-head for the wedge); the
    // scales are frozen from the height in pass 1 anyway.
    bool poolClub = false;
    bool poolScale = false;
    bool poolSym = false;
    bool poolGrip = false;
};

struct SessionPool {
    bool valid = false;
    int nSwings = 0;
    // The skeleton3d stage version whose fits were pooled (0 = a pool written before it was
    // stamped). A pool holds CAMERAS, and a fit that changes what the cameras solve to — v6 levels
    // the world on the grounded club — must not be held to an older fit's cameras.
    int stageVersion = 0;
    SkeletonCalib golfer;           // scale, sym, grip, club (no cameras)
    struct Epoch { std::vector<QString> swings; SkeletonCalib cams; };
    std::vector<Epoch> epochs;
    QString reason;
};

namespace pool_detail {

inline double med(std::vector<double> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// A persisted skeleton3d block's camera, in FitInput's order (fF pF cDx cDy cDz psiD pD fD rF rD).
inline std::array<double, 10> camOf(const QJsonObject &sk)
{
    const QJsonObject c = sk.value(QStringLiteral("camera")).toObject();
    const QJsonArray cd = c.value(QStringLiteral("cD")).toArray();
    return { c.value(QStringLiteral("fF")).toDouble(kNaN), c.value(QStringLiteral("pFDeg")).toDouble(kNaN) * kDeg,
             cd.size() == 3 ? cd[0].toDouble() : kNaN, cd.size() == 3 ? cd[1].toDouble() : kNaN,
             cd.size() == 3 ? cd[2].toDouble() : kNaN,
             c.value(QStringLiteral("psiDDeg")).toDouble(kNaN) * kDeg, c.value(QStringLiteral("pDDeg")).toDouble(kNaN) * kDeg,
             c.value(QStringLiteral("fD")).toDouble(kNaN), c.value(QStringLiteral("rFDeg")).toDouble(0.0) * kDeg,
             c.value(QStringLiteral("rDDeg")).toDouble(0.0) * kDeg };
}

inline bool sameEpoch(const std::array<double, 10> &a, const std::array<double, 10> &b, bool dtl, const PoolConfig &cfg)
{
    auto rel = [](double x, double y) { return std::fabs(x - y) / std::max(1e-9, std::fabs(y)); };
    if (rel(a[0], b[0]) > cfg.epochFocalFrac) return false;
    if (!dtl) return true;
    const double dc = std::sqrt((a[2] - b[2]) * (a[2] - b[2]) + (a[3] - b[3]) * (a[3] - b[3]) + (a[4] - b[4]) * (a[4] - b[4]));
    return dc <= cfg.epochCentreM && std::fabs(a[5] - b[5]) <= cfg.epochYawDeg * kDeg && rel(a[7], b[7]) <= cfg.epochFocalFrac;
}

} // namespace pool_detail

// Pool the swings' persisted `analysis.skeleton3d` blocks (id, block), taken in id order.
inline SessionPool poolSkeletons(std::vector<std::pair<QString, QJsonObject>> swings, const PoolConfig &cfg = {})
{
    using namespace pool_detail;
    SessionPool P;
    swings.erase(std::remove_if(swings.begin(), swings.end(),
                                [](const auto &s) { return !s.second.value(QStringLiteral("valid")).toBool(); }),
                 swings.end());
    std::sort(swings.begin(), swings.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    P.nSwings = int(swings.size());
    if (P.nSwings < 2) { P.reason = QStringLiteral("fewer than two fitted swings"); return P; }

    // The golfer.
    const Rig &R = rig();
    (void)R;
    std::array<std::vector<double>, GroupCount> sc;
    std::array<std::vector<double>, 2 * kSymGroups> sy;
    std::array<std::vector<double>, 9> gr;
    std::vector<double> club;
    for (const auto &[id, sk] : swings) {
        const QJsonObject L = sk.value(QStringLiteral("lengths")).toObject();
        for (int g = 0; g < GroupCount; ++g) sc[size_t(g)].push_back(L.value(QString::fromLatin1(groupName(g))).toDouble(kNaN));
        const QJsonObject G = sk.value(QStringLiteral("grip")).toObject();
        const QJsonArray sym = G.value(QStringLiteral("sym")).toArray();
        for (int c = 0; c < 2 * kSymGroups && c < sym.size(); ++c) sy[size_t(c)].push_back(sym[c].toDouble(kNaN));
        const QJsonArray ax = G.value(QStringLiteral("axis")).toArray(), of = G.value(QStringLiteral("offset")).toArray(),
                         tr = G.value(QStringLiteral("trailOffset")).toArray();
        for (int c = 0; c < 3; ++c) {
            gr[size_t(c)].push_back(ax.size() == 3 ? ax[c].toDouble() : kNaN);
            gr[size_t(3 + c)].push_back(of.size() == 3 ? of[c].toDouble() : kNaN);
            gr[size_t(6 + c)].push_back(tr.size() == 3 ? tr[c].toDouble() : kNaN);
        }
        club.push_back(G.value(QStringLiteral("clubLengthM")).toDouble(kNaN) - 0.04);
    }
    P.golfer.hasScale = true;
    for (int g = 0; g < GroupCount; ++g) {
        P.golfer.scale[size_t(g)] = med(sc[size_t(g)]);
        if (!std::isfinite(P.golfer.scale[size_t(g)])) P.golfer.hasScale = false;
    }
    P.golfer.hasSym = !sy[0].empty();
    for (int c = 0; c < 2 * kSymGroups; ++c) {
        P.golfer.sym[size_t(c)] = med(sy[size_t(c)]);
        if (!std::isfinite(P.golfer.sym[size_t(c)])) P.golfer.hasSym = false;
    }
    P.golfer.hasGrip = true;
    for (int c = 0; c < 9; ++c) {
        P.golfer.grip[size_t(c)] = med(gr[size_t(c)]);
        if (!std::isfinite(P.golfer.grip[size_t(c)])) P.golfer.hasGrip = false;
    }
    if (P.golfer.hasGrip) {                                  // the pooled axis is a direction
        const double n = std::sqrt(P.golfer.grip[0] * P.golfer.grip[0] + P.golfer.grip[1] * P.golfer.grip[1] + P.golfer.grip[2] * P.golfer.grip[2]);
        for (int c = 0; c < 3; ++c) P.golfer.grip[size_t(c)] /= std::max(1e-9, n);
    }
    P.golfer.clubToHeadM = cfg.poolClub ? med(club) : kNaN;
    P.golfer.hasScale = P.golfer.hasScale && cfg.poolScale;
    P.golfer.hasSym = P.golfer.hasSym && cfg.poolSym;
    P.golfer.hasGrip = P.golfer.hasGrip && cfg.poolGrip;

    // The cameras, per epoch.
    std::vector<std::array<double, 10>> ep;                  // the current epoch's members' cameras
    auto close = [&](SessionPool::Epoch &e) {
        e.cams.hasCam = !ep.empty();
        for (int c = 0; c < 10; ++c) {
            std::vector<double> v;
            for (const auto &a : ep) v.push_back(a[size_t(c)]);
            e.cams.cam[size_t(c)] = med(v);
        }
    };
    SessionPool::Epoch cur;
    for (const auto &[id, sk] : swings) {
        const auto cam = camOf(sk);
        const bool dtl = sk.value(QStringLiteral("dtl")).toBool();
        if (!cur.swings.empty()) {
            SessionPool::Epoch probe;
            close(probe);
            if (!sameEpoch(cam, probe.cams.cam, dtl, cfg)) {
                close(cur);
                P.epochs.push_back(cur);
                cur = {};
                ep.clear();
            }
        }
        cur.swings.push_back(id);
        ep.push_back(cam);
    }
    close(cur);
    P.epochs.push_back(cur);
    P.valid = true;
    return P;
}

// The calib one swing's fit holds fixed: the golfer's values and its epoch's cameras.
inline bool calibFor(const SessionPool &P, const QString &swingId, SkeletonCalib &out)
{
    if (!P.valid) return false;
    for (const auto &e : P.epochs)
        if (std::find(e.swings.begin(), e.swings.end(), swingId) != e.swings.end()) {
            out = P.golfer;
            out.hasCam = e.cams.hasCam;
            out.cam = e.cams.cam;
            return true;
        }
    return false;
}

inline QJsonObject sessionPoolToJson(const SessionPool &P)
{
    auto arr = [](const auto &a) { QJsonArray o; for (double v : a) o.append(std::isfinite(v) ? QJsonValue(v) : QJsonValue()); return o; };
    QJsonArray eps;
    for (const auto &e : P.epochs) {
        QJsonArray ids;
        for (const QString &s : e.swings) ids.append(s);
        eps.append(QJsonObject { { "swings", ids }, { "cam", arr(e.cams.cam) } });
    }
    return QJsonObject {
        { "schema", QStringLiteral("pinpoint.skeleton3dSession/1") }, { "valid", P.valid }, { "nSwings", P.nSwings },
        { "stageVersion", P.stageVersion },
        { "reason", P.reason },
        { "golfer", QJsonObject { { "scale", P.golfer.hasScale ? QJsonValue(arr(P.golfer.scale)) : QJsonValue() },
                                  { "sym", P.golfer.hasSym ? QJsonValue(arr(P.golfer.sym)) : QJsonValue() },
                                  { "grip", P.golfer.hasGrip ? QJsonValue(arr(P.golfer.grip)) : QJsonValue() },
                                  { "clubToHeadM", std::isfinite(P.golfer.clubToHeadM) ? QJsonValue(P.golfer.clubToHeadM) : QJsonValue() } } },
        { "epochs", eps } };
}

inline SessionPool sessionPoolFromJson(const QJsonObject &o)
{
    SessionPool P;
    if (o.value(QStringLiteral("schema")).toString() != QLatin1String("pinpoint.skeleton3dSession/1")) return P;
    P.valid = o.value(QStringLiteral("valid")).toBool();
    P.nSwings = o.value(QStringLiteral("nSwings")).toInt();
    P.stageVersion = o.value(QStringLiteral("stageVersion")).toInt(0);
    P.reason = o.value(QStringLiteral("reason")).toString();
    const QJsonObject g = o.value(QStringLiteral("golfer")).toObject();
    auto fill = [](const QJsonValue &v, auto &a) {
        const QJsonArray x = v.toArray();
        if (x.size() != int(a.size())) return false;
        for (int i = 0; i < x.size(); ++i) a[size_t(i)] = x[i].toDouble(kNaN);
        return true;
    };
    P.golfer.hasScale = fill(g.value(QStringLiteral("scale")), P.golfer.scale);
    P.golfer.hasSym = fill(g.value(QStringLiteral("sym")), P.golfer.sym);
    P.golfer.hasGrip = fill(g.value(QStringLiteral("grip")), P.golfer.grip);
    P.golfer.clubToHeadM = g.value(QStringLiteral("clubToHeadM")).toDouble(kNaN);
    for (const QJsonValue &ev : o.value(QStringLiteral("epochs")).toArray()) {
        const QJsonObject e = ev.toObject();
        SessionPool::Epoch E;
        for (const QJsonValue &s : e.value(QStringLiteral("swings")).toArray()) E.swings.push_back(s.toString());
        E.cams.hasCam = fill(e.value(QStringLiteral("cam")), E.cams.cam);
        P.epochs.push_back(E);
    }
    return P;
}

} // namespace pinpoint::skeleton3d
