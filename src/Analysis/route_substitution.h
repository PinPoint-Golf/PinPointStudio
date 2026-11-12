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

// A READING ANOTHER ROUTE SUPPLIED — the records the failed route left behind (planned-metrics
// promotion, 12 Nov 2026).
//
// When one route cannot read a metric and a second route supplies it — the face-on upper body
// refuses at address and the two-camera fit's side bend is published instead — the first route's
// record still says the reading was REFUSED (a cascade naming it in `covers`). Every reader that asks
// what speaks for the reading (gate_catalogue.h primaryRecord, the posture chart, Charts → Relations,
// the Trust panel) then shows a refusal beside a graded value. That record is true of the route that
// failed and false of the reading that stands.
//
// THE MARKER. The supplying producer fires a SUBSTITUTION record: Downgraded, subject the reading K,
// evidence `route` (the routeId of the series it published) and `replaces` (the route that could not
// read it). It is the reading's honest record — "this came from the second route" — and it is what
// makes the clean-up route-aware rather than a list of keys.
//
// THE CLEAN-UP (reconcileSubstitutedReadings), after every stage has spliced its records
// (runProfile, analysis_pipeline.cpp). For each substitution record S whose series K is present with
// routeId == S.route, every record R from ANOTHER producer (its id's area — the part before the first
// '.' — differs from S's) that Refused, Bounded or Masked K stops speaking for K:
//   · K in R.covers  → K is removed from R.covers;
//   · K == R.subject → the record's first remaining cover becomes its subject (it still refused
//     those), or, when it covered nothing else, the record is dropped — it said only that the failed
//     route had nothing for K.
// Either way R's id is added to S's evidence `replacedRecords`, so the failed route's reason stays on the
// swing, on the record that is true of the reading. Masked is included beside the two the issue
// named: a mask record counts frames of the failed route's curve, which is not the curve published.
// Downgraded and Withheld records are left alone (a downgrade from another producer is about a
// different assumption; a withhold judges a value, and there is none from that route). Records of
// the substituting producer's own area are never touched.
//
// PRODUCER IDENTITY IS THE GATE-ID AREA. A record carries no stage or producer field, so "another
// producer" is read off the id's area. A substitute must therefore fire under an area DIFFERENT from
// the route it replaces (body3d.fromFit replaces body.* and posture.* records); one fired under the
// failed route's own area would clean nothing.
//
// THE GAP THE SUBSTITUTE DID NOT FILL. The substitute's series need not read everywhere: the fit may
// read the lead knee near address and not through impact. A measure there has no value, and the
// failed route's record — moved off the reading — no longer explains it. substitutedGapGate() hands
// back the first of the substitute's `replacedRecords`, so the reason a reader gives for the gap is
// still the failed route's real one (LiveMeasureSource::gateCause falls back to it), never a
// "phase not segmented" that nothing said. `replacedRecords` is an evidence array: gateRecordsToJson
// writes a QStringList as a JSON array, and a reloaded record's QVariantList reads back through
// toStringList().

#include "gate_record.h"   // only this: the GUI's trust readers include this header without the analysis types

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <vector>

namespace pinpoint::analysis {

inline bool isRouteSubstitution(const GateRecord &r)
{
    return r.effect == GateEffect::Downgraded && r.evidence.contains(QStringLiteral("route"))
        && r.evidence.contains(QStringLiteral("replaces")) && !r.subject.isEmpty();
}

// The failed route's record behind a gap in a substituted reading `key`: the first id the clean-up
// moved onto key's substitution record. Empty when there is none.
inline QString substitutedGapGate(const std::vector<GateRecord> &gates, const QString &key)
{
    for (const GateRecord &r : gates)
        if (isRouteSubstitution(r) && r.subject == key) {
            const QStringList ids = r.evidence.value(QStringLiteral("replacedRecords")).toStringList();
            if (!ids.isEmpty()) return ids.front();
        }
    return {};
}

// Returns how many (record, reading) pairs it cleaned. `Series` is anything with `key` and `routeId`
// (MetricSeries in the pipeline) — a template so this header needs no analysis types.
template <class Series>
inline int reconcileSubstitutedReadings(std::vector<GateRecord> &gates, const std::vector<Series> &series)
{
    const auto area = [](const QString &id) { return id.section(QLatin1Char('.'), 0, 0); };
    const auto stale = [](GateEffect e) {
        return e == GateEffect::Refused || e == GateEffect::Bounded || e == GateEffect::Masked;
    };
    int cleaned = 0;
    std::vector<char> drop(gates.size(), 0);
    for (size_t si = 0; si < gates.size(); ++si) {
        if (!isRouteSubstitution(gates[si])) continue;
        const QString K = gates[si].subject;
        const QString route = gates[si].evidence.value(QStringLiteral("route")).toString();
        const bool present = std::any_of(series.begin(), series.end(), [&](const Series &m) {
            return m.key == K && m.routeId == route;
        });
        if (!present) continue;
        const QString mine = area(gates[si].gate);
        QStringList replacedRecords = gates[si].evidence.value(QStringLiteral("replacedRecords")).toStringList();
        for (size_t ri = 0; ri < gates.size(); ++ri) {
            GateRecord &r = gates[ri];
            if (ri == si || drop[ri] || !stale(r.effect) || area(r.gate) == mine) continue;
            const bool inCovers = r.covers.contains(K);
            const bool isSubject = r.subject == K;
            if (!inCovers && !isSubject) continue;
            r.covers.removeAll(K);
            if (isSubject) {
                if (!r.covers.isEmpty()) r.subject = r.covers.takeFirst();
                else drop[ri] = 1;
            }
            if (!replacedRecords.contains(r.gate)) replacedRecords << r.gate;
            ++cleaned;
        }
        if (!replacedRecords.isEmpty()) gates[si].evidence.insert(QStringLiteral("replacedRecords"), replacedRecords);
    }
    std::vector<GateRecord> kept;
    kept.reserve(gates.size());
    for (size_t i = 0; i < gates.size(); ++i)
        if (!drop[i]) kept.push_back(std::move(gates[i]));
    gates = std::move(kept);
    return cleaned;
}

// THE BETTER RUNG WINNING (trunk sensors, planned-metrics promotion W3, 12 Nov 2026). The reverse of
// the substitution above: route `route` is the ladder's BEST rung for `key`, and when its series is
// present it is the answer whatever else read the key. supersedeReadings makes that hold whatever the
// stage order:
//   · every other series under `key` (another routeId) is ERASED — one reading per key;
//   · every record that spoke for the erased readings stops speaking for `key`: a Refused, Bounded or
//     Masked record (it counted the other route's frames or refused the other route's reference), and
//     a substitution record (above) for a route other than `route` (it says a weaker rung stood in,
//     which is no longer true). The same removal as the clean-up above: `key` leaves `covers`; a
//     record whose subject is `key` takes its next cover as subject, or is dropped.
// NO RECORD IS FIRED for the winner: the better rung winning is not a fallback (W2's rule), so the
// reading carries no ⚠ for it. Downgraded (non-substitution) and Withheld records are left alone —
// the winner's own assumptions are Downgraded records (body.thoraxFaceIn covers the trunk sensors'
// bends), and no other producer fires either effect on the keys this is used for today. Producer
// identity is NOT the area here, deliberately: the trunk sensors' records share the `body` area with
// the face-on upper body whose refusals must go, so the rule is by effect, and it relies on the
// winning route firing no Refused / Bounded / Masked record on `key` (true of body_rotation.cpp).
struct Supersession {
    int seriesDropped  = 0;   // other routes' series erased
    int recordsCleaned = 0;   // (record, key) pairs taken off the key
};
template <class Series>
inline Supersession supersedeReadings(std::vector<Series> &series, std::vector<GateRecord> &gates,
                                      const QString &key, const QString &route)
{
    Supersession out;
    const bool present = std::any_of(series.begin(), series.end(), [&](const Series &m) {
        return m.key == key && m.routeId == route;
    });
    if (!present) return out;
    const size_t before = series.size();
    series.erase(std::remove_if(series.begin(), series.end(),
                                [&](const Series &m) { return m.key == key && m.routeId != route; }),
                 series.end());
    out.seriesDropped = int(before - series.size());
    std::vector<char> drop(gates.size(), 0);
    for (size_t i = 0; i < gates.size(); ++i) {
        GateRecord &r = gates[i];
        const bool stale = r.effect == GateEffect::Refused || r.effect == GateEffect::Bounded
                        || r.effect == GateEffect::Masked;
        const bool weakerStandIn = isRouteSubstitution(r)
                                && r.evidence.value(QStringLiteral("route")).toString() != route;
        if (!stale && !weakerStandIn) continue;
        const bool inCovers = r.covers.contains(key);
        const bool isSubject = r.subject == key;
        if (!inCovers && !isSubject) continue;
        r.covers.removeAll(key);
        if (isSubject) {
            if (!r.covers.isEmpty()) r.subject = r.covers.takeFirst();
            else drop[i] = 1;
        }
        ++out.recordsCleaned;
    }
    std::vector<GateRecord> kept;
    kept.reserve(gates.size());
    for (size_t i = 0; i < gates.size(); ++i)
        if (!drop[i]) kept.push_back(std::move(gates[i]));
    gates = std::move(kept);
    return out;
}

} // namespace pinpoint::analysis
