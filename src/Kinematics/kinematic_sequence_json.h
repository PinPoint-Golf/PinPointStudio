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
#include "../Analysis/swing_analysis.h"   // windowTimeUs — the one time rule

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariantMap>

#include <cmath>
#include <functional>
#include <limits>

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
        // The chest's probable peak across the camera's gap (KsNode::probable; design §12.6). Times are ms
        // from impact (+ after), so retimeKinematicSequence leaves them alone. Absent = none.
        if (n.probable) {
            QJsonArray bt, bm, bs;
            for (size_t i = 0; i < n.bridgeMs.size() && i < n.bridgeMean.size() && i < n.bridgeSd.size(); ++i) {
                bt.append(n.bridgeMs[i]);
                bm.append(std::round(n.bridgeMean[i] * 10.0) / 10.0);
                bs.append(std::round(n.bridgeSd[i] * 10.0) / 10.0);
            }
            QJsonObject pr{ { QStringLiteral("peakMs"), n.probableMs }, { QStringLiteral("tSigmaMs"), n.probableSigmaMs },
                            { QStringLiteral("peakDps"), n.probableDps }, { QStringLiteral("peakSigmaDps"), n.probableSigmaDps },
                            { QStringLiteral("how"), n.probableHow },
                            { QStringLiteral("bridge"), QJsonObject{ { QStringLiteral("tMs"), bt }, { QStringLiteral("mean"), bm },
                                                                     { QStringLiteral("sd"), bs } } } };
            if (std::isfinite(n.earlierMs)) {
                pr.insert(QStringLiteral("earlierMs"), n.earlierMs);
                pr.insert(QStringLiteral("earlierDps"), n.earlierDps);
            }
            no.insert(QStringLiteral("probable"), pr);
        }
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

// The typed reader (refactor step 1.3a: each analysis block is a writer and its typed reader,
// side by side). Every field the writer emits comes back; `withheld` and the per-node gate records
// are not part of the object (`analysis.gates` carries the records) and read as their defaults.
// `t0Us` takes the two instants to window time through windowTimeUs (0 = as written).
inline KinematicSequence kinematicSequenceFromJson(const QJsonObject &o, int64_t t0Us = 0)
{
    const auto at = [t0Us](const QJsonValue &v) { return windowTimeUs(int64_t(v.toDouble()), t0Us); };
    const auto finiteOr = [](const QJsonObject &j, const char *k) {
        const QJsonValue v = j.value(QString::fromLatin1(k));
        return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
    };
    KinematicSequence ks;
    if (o.isEmpty()) return ks;
    ks.impactUs = at(o.value(QStringLiteral("impactUs")));
    for (const QJsonValue &nv : o.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject no = nv.toObject();
        KsNode n;
        seqSegmentFromKey(no.value(QStringLiteral("segment")).toString(), n.segment);
        n.placed         = no.value(QStringLiteral("placed")).toBool();
        n.tPeakUs        = at(no.value(QStringLiteral("tPeakUs")));
        n.beforeImpactMs = no.value(QStringLiteral("beforeImpactMs")).toDouble();
        n.peakDps        = no.value(QStringLiteral("peakDps")).toDouble();
        n.tSigmaMs       = no.value(QStringLiteral("tSigmaMs")).toDouble();
        n.peakSigmaDps   = no.value(QStringLiteral("peakSigmaDps")).toDouble();
        n.routeId        = no.value(QStringLiteral("routeId")).toString();
        n.direct         = no.value(QStringLiteral("quality")).toString() == QLatin1String("direct");
        n.peakNoEarlierThanMs = finiteOr(no, "peakNoEarlierThanMs");
        n.peakNoLaterThanMs   = finiteOr(no, "peakNoLaterThanMs");
        const QJsonObject pr = no.value(QStringLiteral("probable")).toObject();
        n.probable = !pr.isEmpty();
        if (n.probable) {
            n.probableMs       = pr.value(QStringLiteral("peakMs")).toDouble();
            n.probableSigmaMs  = pr.value(QStringLiteral("tSigmaMs")).toDouble();
            n.probableDps      = pr.value(QStringLiteral("peakDps")).toDouble();
            n.probableSigmaDps = pr.value(QStringLiteral("peakSigmaDps")).toDouble();
            n.probableHow      = pr.value(QStringLiteral("how")).toString();
            const QJsonObject br = pr.value(QStringLiteral("bridge")).toObject();
            for (const QJsonValue &v : br.value(QStringLiteral("tMs")).toArray())  n.bridgeMs.push_back(v.toDouble());
            for (const QJsonValue &v : br.value(QStringLiteral("mean")).toArray()) n.bridgeMean.push_back(v.toDouble());
            for (const QJsonValue &v : br.value(QStringLiteral("sd")).toArray())   n.bridgeSd.push_back(v.toDouble());
            n.earlierMs  = finiteOr(pr, "earlierMs");
            n.earlierDps = finiteOr(pr, "earlierDps");
        }
        ks.nodes.push_back(std::move(n));
    }
    for (const QJsonValue &v : o.value(QStringLiteral("order")).toArray()) {
        SeqSegment s;
        if (seqSegmentFromKey(v.toString(), s)) ks.order.push_back(s);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("gapsMs")).toArray())   ks.gapsMs.push_back(v.toDouble());
    for (const QJsonValue &v : o.value(QStringLiteral("gainsDps")).toArray()) ks.gainsDps.push_back(v.toDouble());
    ks.orderResolved = o.value(QStringLiteral("orderResolved")).toBool();
    ks.verdict       = o.value(QStringLiteral("verdict")).toString();
    ks.routeSummary  = o.value(QStringLiteral("routeSummary")).toString();
    ks.pelvisDecelerates = o.contains(QStringLiteral("pelvisDecelerates"))
                               ? (o.value(QStringLiteral("pelvisDecelerates")).toBool() ? 1 : 0) : -1;
    ks.valid = !ks.nodes.empty();
    return ks;
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
