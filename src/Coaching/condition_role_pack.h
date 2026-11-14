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

// A CONDITION'S ROLE, read off the pack ONCE (condition_role.h says who reads which part). This is the
// only place in the coaching layer that switches on Condition::kind or tests Condition::reportHold
// for a role: the work-on list (work_ons_pack.h), Home's areas (swing_areas_pack.h), the shot outcome
// composer (shot_outcomes_pack.h), the session report (session_report.h) and Your swing
// (swing_themes_pack.h) all take conditionRoleFor(). Iterates nothing and looks nothing up, so it links
// nothing beyond what holding a Condition already needs.

#include "condition_role.h"
#include "../Diagnostics/engine/characteristic.h"

namespace pinpoint::analysis {

inline ConditionRole conditionRoleFor(const Condition &c)
{
    ConditionRole r;
    r.held = !c.reportHold.isEmpty();
    switch (c.kind) {
    case ConditionKind::Fault:
        r.workOn = WorkOnClass::Movement;
        r.layer  = ShotConditionLayer::Swing;
        r.fact   = report::FactKind::Fault;
        r.praisable = true;
        break;
    case ConditionKind::Setup:
        r.workOn = WorkOnClass::Movement;
        r.layer  = ShotConditionLayer::Root;
        r.fact   = report::FactKind::Setup;
        // Mark, 14 Nov 2026: "yes you can praise setup" — a set-up the golfer holds in the ideal range
        // is praised by the same rule as a fault they are clear of (swing_themes.h doWell, rule v5).
        r.praisable = true;
        break;
    case ConditionKind::Delivery:
        r.workOn = WorkOnClass::Delivery;
        r.layer  = ShotConditionLayer::Club;
        r.fact   = report::FactKind::Delivery;
        break;
    case ConditionKind::Outcome:
        r.layer  = ShotConditionLayer::Ball;
        r.fact   = report::FactKind::Outcome;
        break;
    case ConditionKind::Capacity:
        r.fact   = report::FactKind::Capacity;
        break;
    default:                    // Intent, Equipment: Root, never listed, Other to the report
        break;
    }
    // HELD BACK (Condition::reportHold): a reading not trusted enough to headline anything is never a
    // work-on and never praised — not on the list, so not on the home screen (areas, Your swing) and not
    // in the report, and "work-on 1" stays one habit everywhere. The row still grades and still shows on
    // the panel; its layer and report kind are unchanged.
    if (r.held) {
        r.workOn    = WorkOnClass::Excluded;
        r.praisable = false;
    }
    return r;
}

// WHAT A CONDITION IS, for the list: Fault and Setup are movements, Delivery fills a short list, the
// rest is never listed, and a held condition never is. The role's workOn, kept as the list's name for it.
inline WorkOnClass workOnClassFor(const Condition &c)
{
    return conditionRoleFor(c).workOn;
}

} // namespace pinpoint::analysis
