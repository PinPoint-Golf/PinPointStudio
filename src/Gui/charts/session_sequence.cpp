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
#include "../../Metrics/gate_catalogue.h"     // the gates' words (trust_and_gates_design.md §5)

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
        // the analysis' probable peak across the camera's gap (KsNode::probable; gap_bridge.h): ms from impact
        const QVariantMap pr = n.value(QStringLiteral("probable")).toMap();
        if (!pr.isEmpty() && !p.placed) {
            p.probable = true;
            p.probableMs = pr.value(QStringLiteral("peakMs"), kNaN).toDouble();
            p.probableSigmaMs = pr.value(QStringLiteral("tSigmaMs"), kNaN).toDouble();
            p.probableDps = pr.value(QStringLiteral("peakDps"), kNaN).toDouble();
            p.probableSigmaDps = pr.value(QStringLiteral("peakSigmaDps"), kNaN).toDouble();
            p.probableHow = pr.value(QStringLiteral("how"), QStringLiteral("bridge")).toString();
        }
    }
    // THE GATE RECORDS against each node (analysis.gates, subject "ks.<segment>"). The refusal shown is
    // the one that says the most: a bound first (out of sight, in a gap, still rising), then the gates
    // that leave nothing (whole domain blind, spike, route switch, club floor), the σ ceiling last —
    // a pelvis out of sight whose σ is also wide is out of sight (§3.3), and both records are kept.
    if (shot.gatesVersion > 0) {
        static const QStringList rank{ QStringLiteral("sequence.sightedBand"), QStringLiteral("sequence.validityHole"),
                                       QStringLiteral("sequence.risingAtEnd"), QStringLiteral("sequence.reversalSpike"),
                                       QStringLiteral("sequence.routePlacement"), QStringLiteral("sequence.credibleClub"),
                                       QStringLiteral("sequence.maxPlaceSigma") };
        const auto score = [&](const QVariantMap &r) {
            const QString eff = r.value(QStringLiteral("effect")).toString();
            if (eff != QLatin1String("bounded") && eff != QLatin1String("refused")) return 1000;
            // the chest's curve runs backwards (#103): says the most — no bound from it can be trusted
            if (r.value(QStringLiteral("gate")).toString() == QLatin1String("sequence.chestUnreadable")) return -1;
            const int k = int(rank.indexOf(r.value(QStringLiteral("gate")).toString()));
            return (eff == QLatin1String("bounded") ? 0 : 100) + (k < 0 ? 99 : k);
        };
        for (int i = 0; i < kSequenceSegments; ++i) out.segments[size_t(i)].reasonsRecorded = true;
        for (const QVariant &rv : shot.gates) {
            const QVariantMap r = rv.toMap();
            const QString subj = r.value(QStringLiteral("subject")).toString();
            if (!subj.startsWith(QLatin1String("ks."))) continue;
            const auto it = std::find(ids.begin(), ids.end(), subj.mid(3));
            if (it == ids.end()) continue;
            SegmentPeak &p = out.segments[size_t(it - ids.begin())];
            p.records << r;
            const QString gate = r.value(QStringLiteral("gate")).toString();
            if (gate == QLatin1String("sequence.sightedBand") || gate == QLatin1String("sequence.validityHole")) {
                const QVariantMap ev = r.value(QStringLiteral("evidence")).toMap();
                if (ev.contains(QStringLiteral("sightLostMs"))) {
                    p.blindFromMs = ev.value(QStringLiteral("sightLostMs")).toDouble();
                    p.blindToMs = ev.contains(QStringLiteral("sightBackMs")) ? ev.value(QStringLiteral("sightBackMs")).toDouble() : kNaN;
                    p.blindGate = gate;
                }
            }
            if (score(r) < 1000 && (p.refusal.isEmpty() || score(r) < score(p.refusal))) p.refusal = r;
        }
        // A blind band that opened after impact bounds the peak "no earlier than" a NEGATIVE time before
        // impact. Without records that read as still rising at the end (stage 0); the record says it was
        // the band, so it is a bound after impact, not the rising-at-the-end gate.
        for (SegmentPeak &p : out.segments) {
            const QString g = p.refusal.value(QStringLiteral("gate")).toString();
            const QVariantMap b = p.refusal.value(QStringLiteral("bound")).toMap();
            if ((g == QLatin1String("sequence.sightedBand") || g == QLatin1String("sequence.validityHole"))
                && b.contains(QStringLiteral("noEarlierThanMs"))) {
                p.risingAtImpact = false;
                p.afterMs = -b.value(QStringLiteral("noEarlierThanMs")).toDouble();
            }
        }
    }
    rankSequence(out);
    return out;
}

void rankSequence(ShotSequence &out)
{
    out.placedCount = 0;
    out.timedCount = 0;
    out.anyLate = false;
    for (int i = 0; i < kSequenceSegments; ++i) {
        SegmentPeak &a = out.segments[size_t(i)];
        a.rank = 0;
        a.late = false;
        if (!a.timed()) continue;
        if (a.placed) ++out.placedCount;
        ++out.timedCount;
        a.rank = 1;
        for (int j = 0; j < kSequenceSegments; ++j) {
            const SegmentPeak &b = out.segments[size_t(j)];
            if (j != i && b.timed() && b.timeMs() < a.timeMs()) ++a.rank;
            if (j > i && b.timed() && b.timeMs() < a.timeMs()) a.late = true;
        }
        out.anyLate = out.anyLate || a.late;
    }
}

UnplacedWhy unplacedWhy(const SegmentPeak &p)
{
    namespace W = words;
    const auto bare = [](double ms) { QString s = W::signedMs(ms); return s.endsWith(QStringLiteral("ms")) ? s.chopped(3) : s; };
    // THE RECORD, when there is one: the catalogue's short words, the record's own bound.
    if (!p.refusal.isEmpty()) {
        const gates::GateEntry *e = gates::gateEntry(p.refusal.value(QStringLiteral("gate")).toString());
        if (e) {
            const QVariantMap b = p.refusal.value(QStringLiteral("bound")).toMap();
            const QString lower = e->title.left(1).toLower() + e->title.mid(1);
            // what follows a bound: where the route could not see
            const QString tail = e->id == QLatin1String("sequence.sightedBand")   ? QStringLiteral("out of the camera's sight")
                               : e->id == QLatin1String("sequence.validityHole") ? QStringLiteral("at a gap in the track")
                                                                                 : lower;
            if (b.contains(QStringLiteral("noEarlierThanMs"))) {
                const double ms = -b.value(QStringLiteral("noEarlierThanMs")).toDouble();   // + after impact
                if (e->id == QLatin1String("sequence.risingAtEnd"))
                    return { QStringLiteral("rising"), QString(), e->shortText, QStringLiteral("still speeding up at ") + W::signedMs(ms) };
                return { QStringLiteral("after ") + bare(ms), QStringLiteral("ms"), e->shortText,
                         QStringLiteral("peaked after ") + W::signedMs(ms) + QStringLiteral(", ") + tail };
            }
            if (b.contains(QStringLiteral("noLaterThanMs"))) {
                const double ms = -b.value(QStringLiteral("noLaterThanMs")).toDouble();
                return { QStringLiteral("before ") + bare(ms), QStringLiteral("ms"), e->shortText,
                         QStringLiteral("peaked before ") + W::signedMs(ms) + QStringLiteral(", ") + tail };
            }
            const QVariantMap ev = p.refusal.value(QStringLiteral("evidence")).toMap();
            QString sentence = lower;
            if (e->id == QLatin1String("sequence.maxPlaceSigma") && ev.contains(QStringLiteral("tSigmaMs")))
                sentence = QStringLiteral("timing too uncertain to place (σ ") + W::figMs(ev.value(QStringLiteral("tSigmaMs")).toDouble()) + QStringLiteral(")");
            else if (e->id == QLatin1String("sequence.sightedBand") || e->id == QLatin1String("sequence.validityHole"))
                sentence = e->id == QLatin1String("sequence.sightedBand") ? QStringLiteral("out of the camera's sight throughout")
                                                                           : QStringLiteral("in a gap in the track");
            return { QStringLiteral("-"), QString(), e->shortText, sentence };
        }
    }
    if (p.risingAtImpact)
        return { QStringLiteral("rising"), QString(), QStringLiteral("still rising"), QStringLiteral("still speeding up at impact") };
    if (std::isfinite(p.afterMs))
        return { QStringLiteral("after ") + bare(p.afterMs), QStringLiteral("ms"), QStringLiteral("out of sight"),
                 QStringLiteral("peaked after ") + W::signedMs(p.afterMs) + QStringLiteral(", out of the camera's sight") };
    if (std::isfinite(p.beforeMs))
        return { QStringLiteral("before ") + bare(p.beforeMs), QStringLiteral("ms"), QStringLiteral("out of sight"),
                 QStringLiteral("peaked before ") + W::signedMs(p.beforeMs) + QStringLiteral(", out of the camera's sight") };
    // Records exist and none refused this node: the producer had no peak to offer.
    if (p.reasonsRecorded)
        return { QStringLiteral("-"), QString(), QStringLiteral("not placed"), QStringLiteral("not placed from this view") };
    // Analysed before records existed (§6): the σ is a guess at the gate, so say only what is known.
    if (std::isfinite(p.tSigmaMs) && p.tSigmaMs > pinpoint::tuned::sequence::kMaxPlaceSigmaMs)
        return { QStringLiteral("-"), QString(), QStringLiteral("timing uncertain"),
                 QStringLiteral("timing too uncertain to place (σ ") + W::figMs(p.tSigmaMs) + QStringLiteral("; reason not recorded, analysed before records existed)") };
    return { QStringLiteral("-"), QString(), QStringLiteral("not placed"),
             QStringLiteral("not placed (reason not recorded, analysed before records existed)") };
}

QVariantList segmentGates(const SegmentPeak &p, int segment)
{
    QVariantList out;
    const QString subject = QStringLiteral("ks.") + (segment >= 0 && segment < kSequenceSegments ? sequenceSegmentIds()[size_t(segment)] : QString());
    QStringList tried;         // routes this swing already ran for the segment: not offered as a lift
    for (const QVariant &r : p.records) {
        const QString route = r.toMap().value(QStringLiteral("evidence")).toMap().value(QStringLiteral("route")).toString();
        if (!route.isEmpty() && !tried.contains(route)) tried << route;
    }
    QVariantList ordered;
    if (!p.refusal.isEmpty()) ordered << p.refusal;
    for (const QVariant &r : p.records) if (r.toMap() != p.refusal) ordered << r;
    for (const QVariant &rv : ordered) {
        const QVariantMap r = rv.toMap();
        const QString eff = r.value(QStringLiteral("effect")).toString();
        // a band that only masked samples is drawn (shaded), not listed — it refused nothing
        if (eff == QLatin1String("masked")) continue;
        const gates::GateEntry *e = gates::gateEntry(r.value(QStringLiteral("gate")).toString());
        if (!e) continue;
        QStringList tun;
        for (const gates::GateTuning &t : e->tuning) {
            if (t.unit == QLatin1String("switch")) continue;
            const QString v = words::fig(t.value, std::fabs(t.value - std::round(t.value)) < 1e-9 ? 0 : 2);
            tun << (t.unit == QStringLiteral("°") ? v + t.unit : t.unit.isEmpty() ? v : v + QChar(0x00A0) + t.unit);
        }
        out << QVariantMap{ { QStringLiteral("gate"), e->id },
                            { QStringLiteral("effect"), eff },
                            { QStringLiteral("title"), e->title },
                            { QStringLiteral("evidence"), gates::evidenceText(*e, r.value(QStringLiteral("evidence")).toMap()) },
                            { QStringLiteral("explain"), e->explain },
                            { QStringLiteral("tuning"), tun.isEmpty() ? QString() : QStringLiteral("Threshold ") + tun.join(QStringLiteral(", ")) },
                            { QStringLiteral("lift"), gates::liftText(*e, subject, tried) } };
    }
    return out;
}

SessionSequence sessionSequence(const SessionSnapshot &s)
{
    SessionSequence out;
    for (const auto &sh : s.shots) out.shots.push_back(shotSequence(*sh));
    summariseSequence(out);
    return out;
}

void summariseSequence(SessionSequence &out)
{
    out.segments = {};
    out.commonOrder.clear();
    out.commonOrderCount = 0;
    std::array<std::vector<double>, kSequenceSegments> ms;
    QStringList orderSeen;
    QHash<QString, int> orderCount;
    for (const ShotSequence &q : out.shots) {
        for (int i = 0; i < kSequenceSegments; ++i) {
            const SegmentPeak &p = q.segments[size_t(i)];
            if (!p.timed()) continue;
            ms[size_t(i)].push_back(p.timeMs());
            SegmentSummary &sm = out.segments[size_t(i)];
            if (p.placed) ++sm.placed;
            if (p.probable) ++sm.probable;
            if (p.timeMs() > 0) ++sm.afterImpact;
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
}

} // namespace pinpoint::charts
