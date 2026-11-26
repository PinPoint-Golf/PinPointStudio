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

// A CONDITION, WORDED FOR THE GOLFER'S HAND (refactor 6.3b fix round, B-2). Rules ring, header-only.
//
// The one handedness gloss (handedness_gloss.h) on a pack condition: its name, its golfer phrase and its consequence,
// all from the same glossed condition, so a surface can never show one condition's name over another's sentence (a
// left-hander's "Hook" over the slice sentence). The diagnostics model's cards and details, Home's work-on details
// and its Swing areas all word a condition through this, once, from its ROW id.

#include "handedness_gloss.h"
#include "../engine/characteristic.h"

#include <QString>

namespace pinpoint::analysis {

struct ConditionInHand {
    const Condition *condition = nullptr;   // the glossed condition, else the row's own; null when the pack has neither
    QString displayId;                      // the id it reads as for this golfer ("" when the pack has neither)
    QString label;                          // its name ("" when it has none: the caller's fallback)
    QString golfer;                         // its golfer phrase
    QString consequence;                    // its consequence
};

// `rowId` is a ROW id (never one already glossed); `handedness` the golfer's (golferHand). The sentences are the
// glossed condition's own, mirrored only when the condition names a side (conditionWordsInHand, B-1).
inline ConditionInHand conditionInGolfersHand(const CharacteristicPack &pack, const QString &rowId,
                                              const QString &handedness)
{
    ConditionInHand out;
    const QString display = glossedConditionId(rowId, handedness);
    const Condition *c = pack.condition(display);
    // A pack without the mirror condition: the row's own words, unmirrored (they name the side the ball went).
    const QString hand = c ? handedness : QString();
    if (!c) c = pack.condition(rowId);
    if (!c) return out;
    out.condition   = c;
    out.displayId   = c->id;
    out.label       = c->label;
    out.golfer      = conditionWordsInHand(rowId, c->golfer, hand);
    out.consequence = conditionWordsInHand(rowId, c->consequence.text(), hand);
    return out;
}

} // namespace pinpoint::analysis
