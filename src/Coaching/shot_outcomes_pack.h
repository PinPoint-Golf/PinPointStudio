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

// THE PACK'S FACTS FOR THE SHOT OUTCOME COMPOSER, marshalled once into shot_outcomes.h's pack-agnostic
// inputs (the pattern of work_ons_pack.h). The composer's own walk needs nothing but this: the words of
// each condition, its causes edges, and the physics edges (LeadsTo plus the causes edges that already
// run between two outcomes, the strike_toe/heel -> smash_deficit pair).

#include "shot_outcomes.h"
#include "condition_role_pack.h"        // conditionRoleFor
#include "../Diagnostics/engine/characteristic.h"

namespace pinpoint::analysis {

inline ShotOutcomePack shotOutcomePackFor(const CharacteristicPack *pack)
{
    ShotOutcomePack facts;
    if (!pack) return facts;

    int order = 0;
    QSet<QString> outcomes;
    for (const Condition &c : pack->conditions) {
        const ConditionRole role = conditionRoleFor(c);     // the layer, from the one projection
        ShotConditionFacts f;
        f.id          = c.id;
        f.family      = c.family;
        f.label       = c.label;
        f.golfer      = c.golfer;
        f.golferShort = c.golferShort;
        f.golferAbbr  = c.golferAbbr;
        f.golferStory = c.golferStory;
        f.playTo      = c.playTo;
        f.layer       = role.layer;
        f.packOrder   = order++;
        if (role.isOutcome()) outcomes.insert(c.id);
        facts.conditions.insert(c.id, f);
    }

    for (const Edge &e : pack->edges) {
        if (e.type == EdgeType::Causes) {
            facts.causes[e.from] << e.to;
            if (outcomes.contains(e.from) && outcomes.contains(e.to)) facts.physics[e.from] << e.to;
        } else if (e.type == EdgeType::LeadsTo) {
            facts.physics[e.from] << e.to;
        } else if (e.type == EdgeType::Excludes) {
            facts.excludes[e.from] << e.to;
            facts.excludes[e.to] << e.from;
        }
    }
    return facts;
}

} // namespace pinpoint::analysis
