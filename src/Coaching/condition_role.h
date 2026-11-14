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

// WHAT A CONDITION IS FOR, in the coaching layer's own words — the ONE projection of the pack's
// condition kind and hold rule (Condition::kind, Condition::reportHold) that every coaching reader
// takes, so no reader re-tests either itself (audit C-06, 14 Nov 2026). Pack-agnostic: the pack side,
// conditionRoleFor(), is in condition_role_pack.h; this header holds only the vocabulary, so a
// pack-agnostic arithmetic header (shot_outcomes.h) can name a role's parts without seeing a pack type.
//
// Who reads which part:
//   workOn     the work-on list (work_ons_pack.h), Home's swing areas (swing_areas_pack.h) and
//              Home → Your swing (swing_themes_pack.h: needs work, the focus, next on your list) —
//              one rule, so "something to work on" means the same habit on every screen (Mark, 14 Nov:
//              Your swing follows Work-ons).
//   held       the session report (session_report.h: never a strength, never "came with it") and the
//              areas' counts. A held condition's workOn is already Excluded.
//   praisable  Your swing's "what you do well" (swing_themes.h doWell).
//   layer      the shot outcome composer's link words (shot_outcomes_pack.h).
//   fact       the session report's kind (session_report.h reportPackOf).

#include "work_ons.h"       // WorkOnClass

namespace pinpoint::analysis {

// Which layer of the model a condition sits in, for the link words (§5.1). Ball = outcomes; Club =
// delivery numbers; Swing = faults; Root = setup, capacity, equipment and intent.
enum class ShotConditionLayer { Ball, Club, Swing, Root };

namespace report {
// The session report's kind of fact (session_report.h).
enum class FactKind { Fault, Setup, Delivery, Outcome, Capacity, Other };
} // namespace report

struct ConditionRole {
    // The work-on list's class: Fault and Setup are movements, Delivery fills a short list, the rest is
    // never listed — and a HELD condition is never listed at all (Excluded, whatever its kind).
    WorkOnClass        workOn    = WorkOnClass::Excluded;
    // Held back (Condition::reportHold): a reading not trusted enough to headline anything.
    bool               held      = false;
    // "What you do well" may name the golfer being clear of it: a Fault, not held. A set-up is read on
    // the panel, not praised on the home screen (the pack validator's golferWell rule says the same).
    bool               praisable = false;
    ShotConditionLayer layer     = ShotConditionLayer::Root;
    report::FactKind   fact      = report::FactKind::Other;

    // "A fault to work on": the one answer the list and Your swing share.
    bool isMovementWorkOn() const { return workOn == WorkOnClass::Movement; }
    // What the ball did — where an explanation ends.
    bool isOutcome() const { return layer == ShotConditionLayer::Ball; }
};

} // namespace pinpoint::analysis
