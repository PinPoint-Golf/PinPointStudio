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

// THE PACK'S FACTS FOR THE COACHING PLAN, marshalled once into coaching_plan.h's pack-agnostic
// CoachingConditionInfo + authored causal edges — as swing_areas_pack.h does for Your swing.
// Uses conditionRoleFor (Fault/Setup, not held) and conditionInGolfersHand for left/right glossing.

#include "coaching_plan.h"
#include "condition_in_hand.h"
#include "condition_role_pack.h"
#include "swing_themes_pack.h"          // themes::whenOf
#include "../content/drill_pack.h"
#include "../model/characteristic.h"

#include <QHash>
#include <QString>

#include <vector>

namespace pinpoint::analysis {

inline QHash<QString, CoachingConditionInfo> coachingConditionInfoForPack(const CharacteristicPack &pack,
                                                                          const DrillSet &drills,
                                                                          const QString &handedness = QString())
{
    QHash<QString, int> measureWhen;
    measureWhen.reserve(int(pack.measures.size()));
    for (const Measure &m : pack.measures)
        measureWhen.insert(m.id, themes::whenOf(landingMeasureOf(pack, m)->reducer));

    QHash<QString, const Drill *> drillById;
    drillById.reserve(int(drills.drills.size()));
    for (const Drill &d : drills.drills)
        if (!drillById.contains(d.id)) drillById.insert(d.id, &d);

    QHash<QString, CoachingConditionInfo> out;
    out.reserve(int(pack.conditions.size()));
    for (const Condition &c : pack.conditions) {
        CoachingConditionInfo i;
        i.group  = conditionGroupName(c.group);
        i.counts = conditionRoleFor(c).isMovementPriority();   // Fault or Setup, not held
        for (const QString &sid : c.detectedBy) {
            const Signal *s = pack.signal(sid);
            if (!s || s->measures.isEmpty()) continue;
            const int w = measureWhen.value(s->measures.front());
            if (w > 0 && (i.when == 0 || w < i.when)) i.when = w;
        }
        i.stage = coachingStageOf(i.when, i.group);

        const ConditionInHand words = conditionInGolfersHand(pack, c.id, handedness);
        const Condition *src = words.condition ? words.condition : &c;
        const QString hand = words.condition ? handedness : QString();
        i.golfer     = !words.golfer.isEmpty() ? words.golfer : (!words.label.isEmpty() ? words.label : c.label);
        i.golferWell = conditionWordsInHand(c.id, src->golferWell, hand);
        i.golferWhy  = conditionWordsInHand(c.id, src->golferWhy, hand);

        for (const QString &did : src->drills) {
            if (const Drill *d = drillById.value(did, nullptr)) {
                i.drillId          = d->id;
                i.drillLabel       = d->label;
                i.drillInstruction = d->instruction;
                break;
            }
        }
        out.insert(c.id, std::move(i));
    }
    return out;
}

// Authored causal edges that are NOT shared-evidence tautologies.
inline PriorityCauses coachingCausesForPack(const CharacteristicPack &pack)
{
    PriorityCauses out;
    for (const Edge &e : pack.edges)
        if (e.type == EdgeType::Causes && !e.sharedEvidence && e.from != e.to)
            out.emplace_back(e.from, e.to);
    return out;
}

inline CoachingPlan buildCoachingPlanForPack(const CharacteristicPack *pack,
                                             const DrillSet &drills,
                                             const std::vector<SessionPriorities> &sessions,
                                             const QString &handedness = QString(),
                                             const CoachingPlanOptions &opt = CoachingPlanOptions())
{
    if (!pack) return CoachingPlan{};
    return buildCoachingPlan(sessions, coachingConditionInfoForPack(*pack, drills, handedness),
                             coachingCausesForPack(*pack), opt);
}

} // namespace pinpoint::analysis
