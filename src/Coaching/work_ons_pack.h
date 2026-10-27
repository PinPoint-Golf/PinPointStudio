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

// THE PACK'S FACTS FOR THE WORK-ON LIST, marshalled once into work_ons.h's pack-agnostic inputs, and
// the list reduced from them. ONE function, so every reader of "which fault comes first" — Session
// diagnostics' list and the session report (SessionDiagnosticsModel::sessionWorkOns), and the Charts
// Posture section's head line and Findings (SessionSeriesModel → SessionSnapshot::workOnOrder) —
// orders by the same rule: roots before symptoms, still happening before resolving, sharp first, then
// where in the swing, then the bigger miss (reduceSessionWorkOns, rule v4).
//
// Moved verbatim out of SessionDiagnosticsModel::sessionWorkOns() (22 Oct 2026); that member now calls
// this. `opt` is the caller's: the list caps at five, a reader that wants the ORDER of every session
// pattern passes a larger maxPerSession (the order of the first five is unchanged by the cap — the sort
// runs before the cut).

#include "work_ons.h"
#include "../Diagnostics/engine/characteristic.h"
#include "../Diagnostics/engine/condition_reading.h"
#include "../Diagnostics/measures/measure_facets.h"   // phaseLabel

#include <vector>

namespace pinpoint::analysis {

// WHAT A CONDITION IS, for the list: Fault and Setup are movements, Delivery fills a short list, the
// rest is never listed. One function, so the session's list and Home's areas (swing_areas_pack.h)
// classify by the same rule.
//
// HELD BACK (Condition::reportHold): a reading not trusted enough to headline anything is never a
// work-on — not here, so not on the home screen and not in the report, and "work-on 1" stays one
// habit everywhere. The row still grades and still shows on the panel; only the list leaves it out.
inline WorkOnClass workOnClassFor(const Condition &c)
{
    if (!c.reportHold.isEmpty()) return WorkOnClass::Excluded;
    switch (c.kind) {
    case ConditionKind::Fault:
    case ConditionKind::Setup:    return WorkOnClass::Movement;
    case ConditionKind::Delivery: return WorkOnClass::Delivery;
    default: break;
    }
    return WorkOnClass::Excluded;
}

inline SessionWorkOns sessionWorkOnsForPack(const CharacteristicPack *pack, const std::vector<ShotRecord> &shots,
                                            const std::vector<ConditionLedger> &ledgers,
                                            const WorkOnOptions &opt = WorkOnOptions())
{
    QHash<QString, WorkOnClass> classOf;
    QHash<QString, double>      swingPosOf;
    WorkOnCauses causes;
    WorkOnSharedEvidence shared;
    if (pack) {
        for (const Condition &c : pack->conditions) classOf.insert(c.id, workOnClassFor(c));
        for (const Edge &e : pack->edges)
            if (e.type == EdgeType::Causes) {
                causes.emplace_back(e.from, e.to);
                // the panel's cap, carried into the list: measured from the same readings, never tested
                if (e.sharedEvidence) shared.insert(workOnEdgeKey(e.from, e.to));
            }

        // WHERE IN THE SWING each condition is read, for rule v4's swing order (work_ons.h, above
        // workOnSwingPosOfPhase): where the ledger's driving measure's reduction lands — the same
        // reading, and so the same P-position, the card's phase chip names (measurePhaseOf). A
        // session-scope condition is read over the session's swings and sorts after all of them.
        for (const ConditionLedger &l : ledgers) {
            const ConditionReading r = conditionReadingFor(*pack, l.id, l.drivingMeasureId);
            const Measure *m = r.found ? pack->measure(r.measureId) : nullptr;
            const bool sessionLevel = l.sessionScope || (m && m->scope == MeasureScope::Session);
            swingPosOf.insert(l.id, (sessionLevel || !r.found) ? kWorkOnSwingPosSession
                                                               : workOnSwingPosOfPhase(phaseLabel(Phase(r.phase))));
        }
    }
    return reduceSessionWorkOns(shots, ledgers, classOf, causes, swingPosOf, opt, shared);
}

} // namespace pinpoint::analysis
