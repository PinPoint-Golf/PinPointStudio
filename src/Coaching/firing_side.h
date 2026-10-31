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

// WHICH END OF A MEASURE'S CORRIDOR A CONDITION FIRES ON (#16) — the one derivation, for every holder
// that grades a reading on a condition's behalf: the session panel's ledger rows
// (ConditionRow::firingSide, session_diagnostics_model.cpp), Home's work-on line
// (work_ons_controller.cpp) and Home's swing areas (swing_areas_pack.h, AreaConditionInfo::tailByMeasure).
// Pure pack logic, header-only, so the three cannot drift apart.
//
// From the CONDITION's own signals on that measure, never from the measure: a band can be two-sided
// while each condition on it watches one end ("Ball too far forward" the low end, "Ball too far back"
// the high end of m_ballPosition). Two test kinds take part, the two that read the measure itself
// against a line on one side of it:
//   corridor   its direction is the tail of the norm it fires past;
//   threshold  its direction is the side of its authored number it fires past.
// A signal with no direction is read as the HIGH tail, exactly as characteristic_engine.cpp evaluates
// it (direction.value_or(High)) — the pack validator refuses such a signal (`signalDirection`), so this
// only decides what an unvalidated pack means, and it means what the engine does.
//
// NOT a ratio, and not an order test. A ratio fires on a QUOTIENT of two measures against an authored
// number: neither measure's corridor has a side in it (a small enough denominator sends the quotient
// high however the numerator sits), so a ratio gives its measures no side and they read Either. An
// order test compares two event times and has no direction at all.
//
// Either when the condition watches both ends of the measure, or has no corridor or threshold signal
// on it.

#include "diagnostic_ledger.h"                       // FiringSide
#include "../Diagnostics/engine/characteristic.h"   // CharacteristicPack, Condition, Signal

#include <QString>

namespace pinpoint::analysis {

inline FiringSide conditionFiringSide(const CharacteristicPack &pack, const Condition &c, const QString &measureId)
{
    if (measureId.isEmpty()) return FiringSide::Either;
    bool high = false, low = false;
    for (const QString &sid : c.detectedBy) {
        const Signal *s = pack.signal(sid);
        if (!s || (s->test != SignalTest::OutsideCorridor && s->test != SignalTest::Threshold)
            || !s->measures.contains(measureId))
            continue;
        (s->direction.value_or(Direction::High) == Direction::High ? high : low) = true;
    }
    return high == low ? FiringSide::Either : high ? FiringSide::High : FiringSide::Low;
}

inline FiringSide conditionFiringSide(const CharacteristicPack &pack, const QString &conditionId,
                                      const QString &measureId)
{
    const Condition *c = pack.condition(conditionId);
    return c ? conditionFiringSide(pack, *c, measureId) : FiringSide::Either;
}

} // namespace pinpoint::analysis
