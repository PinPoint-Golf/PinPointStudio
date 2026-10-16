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

#include "session_sequence.h"

#include "session_curves.h"   // quantile7
#include "charts_words.h"
#include "../../Core/pp_tuned_constants.h"   // sequence::kMaxPlaceSigmaMs

#include <QHash>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace pinpoint::charts {

const std::array<QString, kSequenceSegments> &sequenceSegmentIds()
{
    static const std::array<QString, kSequenceSegments> ids { QStringLiteral("pelvis"), QStringLiteral("thorax"),
                                                              QStringLiteral("leadArm"), QStringLiteral("club") };
    return ids;
}

QString sequenceSeriesKey(int segment)
{
    static const std::array<QString, kSequenceSegments> keys { QStringLiteral("pelvisAngularSpeed"),
                                                               QStringLiteral("thoraxAngularSpeed"),
                                                               QStringLiteral("leadArmAngularSpeed"),
                                                               QStringLiteral("clubAngularSpeed") };
    return segment >= 0 && segment < kSequenceSegments ? keys[size_t(segment)] : QString();
}

ShotSequence shotSequence(const ShotSeries &shot)
{
    ShotSequence out;
    out.ordinal = shot.ordinal;
    const QVariantMap &ks = shot.kinematicSequence;
    for (const QVariant &v : ks.value(QStringLiteral("order")).toList()) out.order << v.toString();
    const QVariantList nodes = ks.value(QStringLiteral("nodes")).toList();
    const auto &ids = sequenceSegmentIds();
    for (const QVariant &nv : nodes) {
        const QVariantMap n = nv.toMap();
        const QString seg = n.value(QStringLiteral("segment")).toString();
        const auto it = std::find(ids.begin(), ids.end(), seg);
        if (it == ids.end()) continue;
        if (n.value(QStringLiteral("tPeakUs")).toLongLong() == 0) continue;       // no peak (sequenceTable's rule)
        SegmentPeak &p = out.segments[size_t(it - ids.begin())];
        p.present = true;
        p.placed = n.value(QStringLiteral("placed")).toBool() || out.order.contains(seg);
        p.shotMs = -n.value(QStringLiteral("beforeImpactMs")).toDouble();
        p.tSigmaMs = n.value(QStringLiteral("tSigmaMs"), kNaN).toDouble();
        p.peakDps = n.value(QStringLiteral("peakDps"), kNaN).toDouble();
        p.peakSigmaDps = n.value(QStringLiteral("peakSigmaDps"), kNaN).toDouble();
        if (n.contains(QStringLiteral("peakNoEarlierThanMs"))) {
            const double b = n.value(QStringLiteral("peakNoEarlierThanMs")).toDouble();
            p.risingAtImpact = std::isfinite(b) && b <= 0.0;
            if (std::isfinite(b) && b > 0.0) p.afterMs = -b;          // "no earlier than b ms before impact"
        }
        if (n.contains(QStringLiteral("peakNoLaterThanMs"))) {
            const double b = n.value(QStringLiteral("peakNoLaterThanMs")).toDouble();
            if (std::isfinite(b)) p.beforeMs = -b;
        }
    }
    for (int i = 0; i < kSequenceSegments; ++i) {
        SegmentPeak &a = out.segments[size_t(i)];
        if (!a.placed) continue;
        ++out.placedCount;
        a.rank = 1;
        for (int j = 0; j < kSequenceSegments; ++j) {
            const SegmentPeak &b = out.segments[size_t(j)];
            if (j != i && b.placed && b.shotMs < a.shotMs) ++a.rank;
            if (j > i && b.placed && b.shotMs < a.shotMs) a.late = true;
        }
        out.anyLate = out.anyLate || a.late;
    }
    return out;
}

UnplacedWhy unplacedWhy(const SegmentPeak &p)
{
    namespace W = words;
    const auto bare = [](double ms) { QString s = W::signedMs(ms); return s.endsWith(QStringLiteral("ms")) ? s.chopped(3) : s; };
    if (p.risingAtImpact)
        return { QStringLiteral("rising"), QString(), QStringLiteral("still rising"), QStringLiteral("still speeding up at impact") };
    if (std::isfinite(p.afterMs))
        return { QStringLiteral("after ") + bare(p.afterMs), QStringLiteral("ms"), QStringLiteral("out of sight"),
                 QStringLiteral("peaked after ") + W::signedMs(p.afterMs) + QStringLiteral(", out of the camera's sight") };
    if (std::isfinite(p.beforeMs))
        return { QStringLiteral("before ") + bare(p.beforeMs), QStringLiteral("ms"), QStringLiteral("out of sight"),
                 QStringLiteral("peaked before ") + W::signedMs(p.beforeMs) + QStringLiteral(", out of the camera's sight") };
    if (std::isfinite(p.tSigmaMs) && p.tSigmaMs > pinpoint::tuned::sequence::kMaxPlaceSigmaMs)
        return { QStringLiteral("-"), QString(), QStringLiteral("timing uncertain"),
                 QStringLiteral("timing too uncertain to place (σ ") + W::figMs(p.tSigmaMs) + QStringLiteral(")") };
    return { QStringLiteral("-"), QString(), QStringLiteral("not placed"), QStringLiteral("not placed from this view") };
}

SessionSequence sessionSequence(const SessionSnapshot &s)
{
    SessionSequence out;
    std::array<std::vector<double>, kSequenceSegments> ms;
    QStringList orderSeen;
    QHash<QString, int> orderCount;
    for (const auto &sh : s.shots) {
        out.shots.push_back(shotSequence(*sh));
        const ShotSequence &q = out.shots.back();
        for (int i = 0; i < kSequenceSegments; ++i) {
            const SegmentPeak &p = q.segments[size_t(i)];
            if (!p.placed) continue;
            ms[size_t(i)].push_back(p.shotMs);
            SegmentSummary &sm = out.segments[size_t(i)];
            ++sm.placed;
            if (p.shotMs > 0) ++sm.afterImpact;
            if (p.late) ++sm.late;
        }
        const QString key = q.order.join(QLatin1Char(','));
        if (!orderCount.contains(key)) orderSeen << key;
        orderCount[key] += 1;
    }
    for (int i = 0; i < kSequenceSegments; ++i) {
        out.segments[size_t(i)].medianMs = quantile7(ms[size_t(i)], 0.5);
        out.segments[size_t(i)].q1Ms     = quantile7(ms[size_t(i)], 0.25);
        out.segments[size_t(i)].q3Ms     = quantile7(ms[size_t(i)], 0.75);
    }
    for (const QString &k : orderSeen)
        if (orderCount.value(k) > out.commonOrderCount) { out.commonOrderCount = orderCount.value(k); out.commonOrder = k; }
    return out;
}

} // namespace pinpoint::charts
