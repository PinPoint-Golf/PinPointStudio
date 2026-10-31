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
// AreaConditionInfo — as work_ons_pack.h does for the list, and with the list's own class rule
// (workOnClassFor), so a condition that can never be a work-on is never counted toward an area either.
//
// Display-only: nothing here goes into the pack, so nothing here moves the content stamp
// (SessionDiagnosticsModel::computeContentStamp hashes savePack) — a relabel re-detects no swing.

#include "swing_areas.h"
#include "work_ons_pack.h"                          // workOnClassFor
#include "firing_side.h"                            // conditionFiringSide
#include "../Diagnostics/engine/characteristic.h"

#include <QHash>
#include <QSet>
#include <QString>

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
        i.counts = areaConditionCounts(i.group, workOnClassFor(c), c.kind == ConditionKind::Outcome,
                                       !c.reportHold.isEmpty());
        bool anyMeasure = false, allLm = true;
        for (const QString &sigId : c.detectedBy) {
            const Signal *sg = pack.signal(sigId);
            if (!sg) { allLm = false; continue; }
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
        out.insert(c.id, std::move(i));
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
