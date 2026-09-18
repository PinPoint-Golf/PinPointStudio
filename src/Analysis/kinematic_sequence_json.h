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

// The ONE serialisation of KinematicSequence — swing.json's `analysis.kinematicSequence`
// (docs/reference/swing_json_schema.md), the live analysisDetail map (shot_processor.cpp) and the
// reload (disk_replay_source.cpp) all go through here, so a live shot, its document and its
// reloaded self carry byte-identical shapes. Three hand-written copies of a nine-field object is
// how `sigma` and `valid` nearly drifted apart before their comments pinned them to each other.
//
// TIMES. `tPeakUs` and `impactUs` are the only absolute instants; `rel` maps them into the
// document's domain (window-relative for swing.json, identity for the live map). Everything else
// is a duration or a value and needs no re-timing. `retimeKinematicSequence` is the reload's
// inverse: it moves those two fields back with whatever offset the caller's other series got.

#include "kinematic_sequence.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include <cmath>
#include <functional>

namespace pinpoint::analysis {

inline QJsonObject kinematicSequenceToJson(const KinematicSequence &ks,
                                           const std::function<qint64(int64_t)> &rel)
{
    QJsonObject o;
    o.insert(QStringLiteral("impactUs"), rel(ks.impactUs));
    QJsonArray nodes;
    for (const KsNode &n : ks.nodes) {
        QJsonObject no{
            { QStringLiteral("segment"),        QString::fromLatin1(seqSegmentKey(n.segment)) },
            { QStringLiteral("placed"),         n.placed },
            { QStringLiteral("tPeakUs"),        rel(n.tPeakUs) },
            { QStringLiteral("beforeImpactMs"), n.beforeImpactMs },
            { QStringLiteral("peakDps"),        n.peakDps },
            { QStringLiteral("tSigmaMs"),       n.tSigmaMs },
            { QStringLiteral("peakSigmaDps"),   n.peakSigmaDps },
            { QStringLiteral("routeId"),        n.routeId },
            { QStringLiteral("quality"),        n.direct ? QStringLiteral("direct")
                                                         : QStringLiteral("estimated") } };
        // The bound a route emits instead of a node it could not see (KsNode). Absent = none.
        if (std::isfinite(n.peakNoEarlierThanMs))
            no.insert(QStringLiteral("peakNoEarlierThanMs"), n.peakNoEarlierThanMs);
        if (std::isfinite(n.peakNoLaterThanMs))
            no.insert(QStringLiteral("peakNoLaterThanMs"), n.peakNoLaterThanMs);
        nodes.append(no);
    }
    o.insert(QStringLiteral("nodes"), nodes);
    QJsonArray order, gaps, gains;
    for (SeqSegment s : ks.order) order.append(QString::fromLatin1(seqSegmentKey(s)));
    for (double g : ks.gapsMs)    gaps.append(g);
    for (double g : ks.gainsDps)  gains.append(g);
    o.insert(QStringLiteral("order"),         order);
    o.insert(QStringLiteral("gapsMs"),        gaps);
    o.insert(QStringLiteral("gainsDps"),      gains);
    o.insert(QStringLiteral("orderResolved"), ks.orderResolved);
    o.insert(QStringLiteral("verdict"),       ks.verdict);
    o.insert(QStringLiteral("routeSummary"),  ks.routeSummary);
    if (ks.pelvisDecelerates >= 0)
        o.insert(QStringLiteral("pelvisDecelerates"), ks.pelvisDecelerates == 1);
    return o;
}

// The reload: re-time the two absolute instants with the caller's offset function (the same one
// its metric series went through) and hand back the QML-shaped map.
inline QVariantMap retimeKinematicSequence(const QJsonObject &in,
                                           const std::function<qint64(const QJsonValue &)> &rel)
{
    QJsonObject o = in;
    if (o.contains(QStringLiteral("impactUs")))
        o.insert(QStringLiteral("impactUs"), rel(o.value(QStringLiteral("impactUs"))));
    QJsonArray nodes;
    for (const QJsonValue &nv : o.value(QStringLiteral("nodes")).toArray()) {
        QJsonObject n = nv.toObject();
        if (n.contains(QStringLiteral("tPeakUs")))
            n.insert(QStringLiteral("tPeakUs"), rel(n.value(QStringLiteral("tPeakUs"))));
        nodes.append(n);
    }
    o.insert(QStringLiteral("nodes"), nodes);
    return o.toVariantMap();
}

} // namespace pinpoint::analysis
