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

// THE EFFECTIVE CONTEXT — the recorded analysis context with the golfer's club correction applied:
// what one analysis run uses (docs/developer/information_model.md §2; IM-3, Mark 14 Nov 2026).
// DERIVED, never stored; a copy becomes the analysis' input stamp (step 1.6).
//
// THE RULE, effectiveContext() v1:
//
//   correction = the LM board's override (review.clubOverride) — until step 6 the only correction.
//                The board's evidence suggestion is never a correction: the golfer did not say it.
//   declared   = the swing-edit popover's club (review.club). Not a correction: it changes only the
//                STANDARD length, exactly as today's re-analysis does (Mark, 14 Nov 2026: the
//                popover's club keeps setting the standard length, as today).
//   effective  = the correction if set, else the captured club (context.club.name)
//   effective != captured (compared as canonical ids) — the club was corrected:
//                geometry        = the bag snapshot's entry for the effective club, if present
//                                  (lengthKnown when its lengthMm > 0), else none
//                standard length = standardClub(effective, the recorded club set, height)
//                length prior    = none (the recorded one belongs to the captured club)
//   otherwise:   geometry and length prior are the RECORDED (captured) ones;
//                standard length = standardClub(declared, club set, height) when declared is set
//                                  (0 when it names no standard club), else the recorded one
//   undeclared stays undeclared: an empty club is never the "DRIVER" stub
//
// THE DECLARED CLUB IS TAKEN AS STORED. Capture seeds review.club (the session club, else the
// athlete's primary, else the "DRIVER" stub); a seed is not told apart from a golfer's pick, so a
// seeded stub sets the driver's standard length — as today's re-analysis does. That stays until
// step 6 removes the seed.
//
// The caller passes the review facts in (the repository's load returns them beside the Recording):
// analysis never reads the shot's review block.
//
// Rules ring: pure, no I/O.

#include <QString>

#include "../Recording/analysis_context.h"

namespace pinpoint::analysis {

inline constexpr int kEffectiveContextRuleVersion = 1;

struct EffectiveClub {
    QString                          name;        // the club the analysis uses; empty = undeclared
    QString                          captured;    // the club capture recorded (provenance)
    QString                          declared;    // review.club as passed in (provenance)
    bool                             corrected = false;   // name came from the golfer's correction
    pinpoint::recording::ClubGeometry geometry;
    pinpoint::recording::LengthPrior  lengthPrior;
    double                           standardLengthMm = 0.0;   // 0 = none
};

struct EffectiveContext {
    pinpoint::recording::Handedness handedness = pinpoint::recording::Handedness::Unknown;
    double                          heightM = 0.0;
    pinpoint::ClubSet               clubSet = pinpoint::ClubSet::Men;
    EffectiveClub                   club;
};

// The golfer's two club edits on the shot's review block, as stored. Empty = not set.
struct ClubReview {
    QString correction;   // review.clubOverride (any spelling the club vocabulary reads)
    QString declared;     // review.club, used as today's re-analysis uses it (trimmed, not canonicalised)
};

EffectiveContext effectiveContext(const pinpoint::recording::AnalysisContext &recorded,
                                  const ClubReview &review);

} // namespace pinpoint::analysis
