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

#include "session_sequence_words.h"

#include "charts_words.h"
#include "../../Vocabulary/Common/pp_tuned_constants.h"   // sequence::kMaxPlaceSigmaMs
#include "../../Vocabulary/Metrics/gate_catalogue.h"     // the gates' words (trust_and_gates_design.md §5)

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>

namespace pinpoint::charts {

words::Sentence peakTimesSentence(const SegmentPeak &club, const SegmentPeak &pelvis)
{
    words::Sentence s;
    if (club.placed) {
        const words::ImpactTime t = words::impactTime(club.shotMs);
        s.append(QStringLiteral("The club peaked ")).figure(t.figure).append(t.words);
    }
    if (pelvis.placed) {
        const words::ImpactTime t = words::impactTime(pelvis.shotMs);
        s.append(club.placed ? QStringLiteral(", the pelvis ") : QStringLiteral("The pelvis peaked ")).figure(t.figure).append(t.words);
    }
    if (!s.isEmpty()) s.append(QStringLiteral("."));
    return s;
}

void appendProbableWhen(words::Sentence &s, double probableMs, double sigmaMs)
{
    const words::ImpactTime t = words::impactTime(probableMs);
    const QString sigma = words::pm(sigmaMs);
    if (t.atImpact()) {                                       // "at impact (± 8 ms)"
        s.append(t.words);
        if (!sigma.isEmpty()) s.append(QStringLiteral(" (")).figure(sigma + QStringLiteral("\u00A0ms")).append(QStringLiteral(")"));
    } else {
        s.figure(words::fig(std::abs(probableMs)) + (sigma.isEmpty() ? QString() : QStringLiteral(" ") + sigma) + QStringLiteral("\u00A0ms"))
            .append(t.words);
    }
}

void appendPeaksWhen(words::Sentence &s, const QString &label, double shotMs)
{
    const words::ImpactTime t = words::impactTime(shotMs);
    s.append(label + QStringLiteral(" peaks ")).figure(t.figure).append(t.words);
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
        for (const gates::GateTuning &t : e->tuning)
            if (t.unit != QLatin1String("switch")) tun << gates::tuningValue(t);   // the catalogue's one threshold wording
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

} // namespace pinpoint::charts
