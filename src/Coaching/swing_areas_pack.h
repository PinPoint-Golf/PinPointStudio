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

// THE PACK'S FACTS FOR THE SWING AREAS, marshalled once into swing_areas.h's pack-agnostic
// AreaConditionInfo — as work_ons_pack.h does for the list, and with the list's own role
// (conditionRoleFor, condition_role_pack.h), so a condition that can never be a work-on is never
// counted toward an area either.
//
// Display-only: nothing here goes into the pack, so nothing here moves the content stamp
// (SessionDiagnosticsModel::computeContentStamp hashes savePack) — a relabel re-detects no swing.

#include "swing_areas.h"
#include "condition_role_pack.h"                    // conditionRoleFor
#include "firing_side.h"                            // conditionFiringSide
#include "../Diagnostics/engine/characteristic.h"
#include "../Diagnostics/engine/condition_reading.h"  // conditionReadings

#include <QHash>
#include <QSet>
#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

namespace pinpoint::analysis {

// The launch-monitor prefix on a catalogue key. A measure is LM-only when every rung of its key ladder
// (measureKeyLadder: the preferred instruments, then its own key) carries it; a COMPOSED measure has
// no key at all and is built from the camera's facets, so it is never LM-only.
inline bool areaMeasureIsLmOnly(const Measure &m)
{
    const QStringList keys = measureKeyLadder(m);
    if (keys.isEmpty()) return false;
    for (const QString &k : keys)
        if (!k.startsWith(QLatin1String("lm."))) return false;
    return true;
}

// Every condition the pack authors, keyed by id. A detecting signal or measure the pack does not hold
// leaves the condition not LM-only — the safe direction: ball flight stays hidden rather than shown on
// evidence nobody can name.
inline QHash<QString, AreaConditionInfo> areaConditionInfoForPack(const CharacteristicPack &pack)
{
    QHash<QString, AreaConditionInfo> out;
    out.reserve(int(pack.conditions.size()));
    for (const Condition &c : pack.conditions) {
        AreaConditionInfo i;
        i.group  = conditionGroupName(c.group);
        i.label  = c.label;
        i.golfer = c.golfer;
        const ConditionRole role = conditionRoleFor(c);
        i.counts = areaConditionCounts(i.group, role.workOn, role.isOutcome(), role.held, c.id);
        bool anyMeasure = false, allLm = true;
        bool allBinary = !c.detectedBy.isEmpty();
        for (const QString &sigId : c.detectedBy) {
            const Signal *sg = pack.signal(sigId);
            if (!sg) { allLm = false; allBinary = false; continue; }
            if (sg->test != SignalTest::Order && sg->test != SignalTest::Threshold) allBinary = false;
            for (const QString &mid : sg->measures) {
                // The tail it watches here: the one derivation the session panel uses (firing_side.h).
                // A ratio's measures get none — the quotient fires, not either measure (#16).
                const FiringSide side = conditionFiringSide(pack, c, mid);
                if (side != FiringSide::Either)
                    i.tailByMeasure.insert(mid, side == FiringSide::High ? QStringLiteral("high") : QStringLiteral("low"));
                const Measure *m = pack.measure(mid);
                if (!m) { allLm = false; continue; }
                anyMeasure = true;
                if (i.unit.isEmpty()) i.unit = m->unit;
                if (!i.unitByMeasure.contains(mid)) i.unitByMeasure.insert(mid, m->unit);
                if (!areaMeasureIsLmOnly(*m)) allLm = false;
            }
        }
        i.lmOnly = anyMeasure && allLm;
        i.binary = allBinary;
        out.insert(c.id, std::move(i));
    }
    return out;
}

// THE AREA EACH CATALOGUE KEY IS READ UNDER, from the conditions that detect on it (Consistency,
// consistency_design.md Appendix B). Walks every condition the pack authors, so a key has its area
// whether or not anything fired on this golfer, which ConditionInfo.group cannot say.
//
// One use is one condition reading a measure whose OWN metricKey is the key. Not the wider key ladder:
// a better instrument's rung reads the same quantity, but it is not what the condition was authored on.
// conditionReadings counts a measure once per condition, however many of its signals read it.
//
// The group used most wins. On a tie the group met FIRST wins, walking pack.conditions in order, then
// each condition's detectedBy, then each signal's measures. That is deliberately not areaGroupOrder():
// it is a most-common count with first-met ties (as Python's Counter.most_common(1) answers, which the
// retired consistency tally cross-checked it with).
//
// Ball flight is left out: it has no spans to read. `golfer` is left out because it is not on
// areaGroupOrder(): those conditions are latent, and they are never an area. A key no body condition
// detects on is absent.
inline QHash<QString, QString> metricAreasForPack(const CharacteristicPack &pack)
{
    using GroupUses = std::vector<std::pair<QString, int>>;     // first met first
    QHash<QString, GroupUses> uses;
    for (const Condition &c : pack.conditions) {
        const QString group = conditionGroupName(c.group);
        if (group == kAreaBallFlight() || !areaGroupOrder().contains(group)) continue;
        for (const ConditionReading &r : conditionReadings(pack, c.id)) {
            const Measure *m = pack.measure(r.measureId);
            if (!m || m->metricKey.isEmpty()) continue;
            GroupUses &u = uses[m->metricKey];
            const auto it = std::find_if(u.begin(), u.end(), [&](const std::pair<QString, int> &e) {
                return e.first == group;
            });
            if (it == u.end()) u.emplace_back(group, 1);
            else ++it->second;
        }
    }

    QHash<QString, QString> out;
    out.reserve(uses.size());
    for (auto it = uses.constBegin(); it != uses.constEnd(); ++it) {
        const GroupUses &u = it.value();
        const auto most = std::max_element(u.begin(), u.end(),   // the first of equals, as the tally's
                                           [](const std::pair<QString, int> &a, const std::pair<QString, int> &b) {
                                               return a.second < b.second;
                                           });
        out.insert(it.key(), most->first);
    }
    return out;
}

// THE READINGS GRADED AFTER THE BALL HAS GONE (Consistency's P7-P8 column, Mark, 8 Nov). A reading is graded
// there when some condition reads its measure at the `finish` anchor: the pack says that reading means
// something once the ball has gone. Every other reading of the column is shown but does not count. This is
// the one derivation; the grades and the table's lighter cells both take it from ConsistencyReading::afterBall.
//
// A measure's OWN key, as metricAreasForPack reads it, and only an `at` reducer at Phase::Finish: an
// extremum whose window ends there says where the search stopped, not that the measure is read at the finish.
inline QSet<QString> afterBallKeysForPack(const CharacteristicPack &pack)
{
    QSet<QString> out;
    for (const Condition &c : pack.conditions)
        for (const ConditionReading &r : conditionReadings(pack, c.id)) {
            const Measure *m = pack.measure(r.measureId);
            if (m && !m->metricKey.isEmpty() && m->reducer.kind == ReducerKind::At
                && m->reducer.anchor == Phase::Finish)
                out.insert(m->metricKey);
        }
    return out;
}

// The areas for a golfer, straight from the pack. A null pack holds no conditions: every body area
// comes back unmeasurable and ball flight not at all.
inline std::vector<AreaSummary> summariseAreasForPack(const CharacteristicPack *pack,
                                                      const std::vector<SessionWorkOns> &sessions,
                                                      const AreaOptions &opt = AreaOptions(),
                                                      const WorkOnOptions &wopt = WorkOnOptions())
{
    const QHash<QString, AreaConditionInfo> info = pack ? areaConditionInfoForPack(*pack)
                                                        : QHash<QString, AreaConditionInfo>();
    return summariseAreas(sessions, info, opt, wopt);
}

} // namespace pinpoint::analysis
