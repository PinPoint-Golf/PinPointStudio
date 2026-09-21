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

#include "dtl_overlay_payload.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace pinpoint {

namespace {

// ShaftSample2D::flags bit the face-on painter reads as "a measurement" (ShaftMeasured).
// Every published DTL frame is one; none is coasted/predicted/synthesised, so no other
// bit is ever set and the painter's _restsOnMeasurement() admits them all.
constexpr int kFlagMeasured = 0x01;

// Used only when the track has fewer than two frames to measure its own interval.
constexpr qint64 kFallbackFrameIntervalUs = 8333;   // 120 fps

// A clubDtl frame published an angle: tier RAY or better (dtl_shaft_types.h DtlTier).
bool isPublished(const QJsonObject &f)
{
    const QString tier = f.value(QStringLiteral("tier")).toString();
    return tier == QLatin1String("RAY") || tier == QLatin1String("SEG")
        || tier == QLatin1String("BAND");
}

// A normalised [x, y] pair from clubDtl, or false when the point is absent (null).
bool point2(const QJsonValue &v, QVariantList &out)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2 || !a.at(0).isDouble() || !a.at(1).isDouble())
        return false;
    out = QVariantList{ a.at(0).toDouble(), a.at(1).toDouble() };
    return true;
}

double numOr(const QJsonValue &v, double fallback)
{
    return v.isDouble() ? v.toDouble() : fallback;
}

} // namespace

QVariantMap dtlOverlayDetail(const QVariantMap &pose2d,
                             const QJsonObject &clubDtl,
                             const QVariantList &foPositions,
                             qint64 impactUs,
                             const std::function<qint64(const QJsonValue &)> &retime)
{
    QVariantMap dtl;
    if (!pose2d.isEmpty())
        dtl.insert(QStringLiteral("pose2d"), pose2d);

    const QJsonArray frames = clubDtl.value(QStringLiteral("frames")).toArray();
    const int fw = clubDtl.value(QStringLiteral("frameWidth")).toInt();
    const int fh = clubDtl.value(QStringLiteral("frameHeight")).toInt();
    if (frames.isEmpty() || fw <= 0 || fh <= 0)
        return dtl;

    // ── Club samples: published frames only. grip/head in clubDtl are already
    //    normalised to the DTL frame (x / frameWidth, y / frameHeight), which is the
    //    face-on payload's convention, so they pass through as numbers.
    QVariantList samples;
    std::vector<qint64> sampleT;
    std::vector<qint64> allT;
    allT.reserve(size_t(frames.size()));
    for (const QJsonValue &fv : frames) {
        const QJsonObject f = fv.toObject();
        const qint64 t = retime(f.value(QStringLiteral("t_us")));
        allT.push_back(t);
        if (!isPublished(f))
            continue;
        QVariantList grip, head;
        if (!point2(f.value(QStringLiteral("grip")), grip)
            || !point2(f.value(QStringLiteral("head")), head))
            continue;
        samples.append(QVariantMap{
            { QStringLiteral("t_us"),  t },
            { QStringLiteral("grip"),  grip },
            { QStringLiteral("head"),  head },
            { QStringLiteral("theta"), numOr(f.value(QStringLiteral("theta")), 0.0) },
            { QStringLiteral("lenPx"), numOr(f.value(QStringLiteral("lenPx")), -1.0) },
            { QStringLiteral("conf"),  numOr(f.value(QStringLiteral("conf")), 0.0) },
            { QStringLiteral("flags"), kFlagMeasured } });
        sampleT.push_back(t);
    }

    // The track's own frame interval (median spacing of ALL its frames, published or
    // not) — the tolerance within which a face-on P-position may borrow a DTL sample.
    qint64 intervalUs = kFallbackFrameIntervalUs;
    if (allT.size() >= 2) {
        std::vector<qint64> dts;
        dts.reserve(allT.size() - 1);
        for (size_t i = 1; i < allT.size(); ++i)
            if (allT[i] > allT[i - 1]) dts.push_back(allT[i] - allT[i - 1]);
        if (!dts.empty()) {
            std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
            intervalUs = dts[dts.size() / 2];
        }
    }

    if (!samples.isEmpty()) {
        // ── P-positions: each face-on P moment, drawn on the DTL tile ONLY where the
        //    DTL track published a sample within one frame of it. clockOffsetUs is
        //    DTL − face-on (0 on a shared clock), so a face-on instant lands on the
        //    DTL clock by adding it. Absent where DTL did not see the club — that is
        //    the finding, not a gap to fill.
        const qint64 clockOffsetUs =
            static_cast<qint64>(clubDtl.value(QStringLiteral("clockOffsetUs")).toDouble());
        struct Pos { qint64 t; QVariantMap m; };
        std::vector<Pos> positions;
        for (const QVariant &pv : foPositions) {
            const QVariantMap fo = pv.toMap();
            if (!fo.contains(QStringLiteral("t_us")))
                continue;
            const qint64 want = fo.value(QStringLiteral("t_us")).toLongLong() + clockOffsetUs;
            const auto it = std::lower_bound(sampleT.begin(), sampleT.end(), want);
            int best = -1;
            qint64 bestDt = 0;
            for (auto c : { it, it == sampleT.begin() ? sampleT.end() : it - 1 }) {
                if (c == sampleT.end()) continue;
                const qint64 dt = std::llabs(*c - want);
                if (best < 0 || dt < bestDt) { best = int(c - sampleT.begin()); bestDt = dt; }
            }
            if (best < 0 || bestDt > intervalUs)
                continue;
            const QVariantMap s = samples.at(best).toMap();
            positions.push_back(Pos{ sampleT[size_t(best)], QVariantMap{
                { QStringLiteral("p"),      fo.value(QStringLiteral("p")).toInt() },
                { QStringLiteral("t_us"),   sampleT[size_t(best)] },
                { QStringLiteral("grip"),   s.value(QStringLiteral("grip")) },
                { QStringLiteral("head"),   s.value(QStringLiteral("head")) },
                { QStringLiteral("theta"),  s.value(QStringLiteral("theta")) },
                { QStringLiteral("lenPx"),  s.value(QStringLiteral("lenPx")) },
                { QStringLiteral("conf"),   s.value(QStringLiteral("conf")) },
                { QStringLiteral("source"), 0 } } });   // PositionSource::TrackSample
        }
        // The painter bisects positions by t_us.
        std::stable_sort(positions.begin(), positions.end(),
                         [](const Pos &a, const Pos &b) { return a.t < b.t; });
        QVariantList posList;
        for (const Pos &p : positions) posList.append(p.m);

        QVariantMap club{
            { QStringLiteral("valid"),       true },
            { QStringLiteral("frameWidth"),  fw },
            { QStringLiteral("frameHeight"), fh },
            { QStringLiteral("samples"),     samples } };
        if (!posList.isEmpty())
            club.insert(QStringLiteral("positions"), posList);
        dtl.insert(QStringLiteral("club"), club);
    }

    // ── Ball: one static position (the tracker finds it once, at address), shown
    //    from the track's first frame and withdrawn at impact — the face-on block's
    //    found=true … found=false convention. x/y/radiusPx are image px in clubDtl;
    //    normalised here like face-on (x, y by the frame; r by the frame WIDTH).
    //    An unmeasured radius is 0, which the painter floors to its minimum ring.
    const QJsonObject ball = clubDtl.value(QStringLiteral("summary")).toObject()
                                    .value(QStringLiteral("ball")).toObject();
    const QJsonValue bx = ball.value(QStringLiteral("x")), by = ball.value(QStringLiteral("y"));
    if (ball.value(QStringLiteral("found")).toBool() && bx.isDouble() && by.isDouble()) {
        const qint64 firstT = allT.front();
        const double rPx = numOr(ball.value(QStringLiteral("radiusPx")), 0.0);
        QVariantList bs{ QVariantMap{
            { QStringLiteral("t_us"),  firstT },
            { QStringLiteral("x"),     bx.toDouble() / fw },
            { QStringLiteral("y"),     by.toDouble() / fh },
            { QStringLiteral("r"),     (std::isfinite(rPx) && rPx > 0.0) ? rPx / fw : 0.0 },
            { QStringLiteral("conf"),  1.0 },
            { QStringLiteral("found"), true } } };
        if (impactUs > firstT)
            bs.append(QVariantMap{
                { QStringLiteral("t_us"),  impactUs },
                { QStringLiteral("found"), false } });
        dtl.insert(QStringLiteral("ball"), QVariantMap{ { QStringLiteral("samples"), bs } });
    }
    return dtl;
}

} // namespace pinpoint
