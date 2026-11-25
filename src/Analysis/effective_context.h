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

// THE EFFECTIVE CONTEXT — the recorded analysis context with the shot's one club applied: what one analysis run uses
// (governance/artefacts/information_model.md §2; IM-3, Mark 14 Nov 2026). DERIVED, never stored; a copy becomes the analysis'
// input stamp (step 1.6).
//
// THE RULE, effectiveContext() v2 (refactor 6.2; R-CLUB-2, R-REC-6, DEC-133; closes DEC-72's interim):
//
//   name   = THE ONE CLUB (club_vocabulary.h effectiveClub): the board's correction (review.clubOverride), else the
//            popover's club (review.club), else the device club follow-club stored (capture.deviceClub), else the
//            captured club (context.club.name); empty = undeclared, never a stub. The board's evidence suggestion is
//            never one of these: the golfer did not say it.
//   name is the captured club (compared as canonical ids), or undeclared — nothing moved it: EXACTLY v1's answer, so a
//            shot whose club nobody changed keeps its input stamp: name, geometry and length prior are the RECORDED
//            (captured) ones; standard length = standardClub(declared, club set, height) when declared is set (0 when
//            it names no standard club), else the recorded one.
//   name is the device club (no correction, no declared club):
//            geometry = the device club's as follow-club stored it (lengthKnown when its lengthMm > 0); standard length
//            = standardClub(device, the recorded club set, height); length prior = none. The same job as follow-club's
//            old overwrite of the captured club gave (DEC-133: the same analysis result as DEC-82).
//   otherwise (the golfer's correction or popover club names another club than the captured one): corrected;
//            geometry = the bag snapshot's entry for it, if present (lengthKnown when its lengthMm > 0), else none;
//            standard length = standardClub(name, the recorded club set, height); length prior = none (the recorded
//            one belongs to the captured club).
//
// v1 (until step 6) took only the board's correction as a club; the popover's club set only the standard length, and
// the device club came in by overwriting the captured club. A stored "DRIVER" stub in review.club is read as stored: a
// declared driver (J-82: no migration).
//
// The caller passes the review facts in (the repository's load returns them beside the Recording): analysis never
// reads the shot's review block.
//
// Rules ring: pure, no I/O.

#include <QString>

#include "../Vocabulary/Recording/analysis_context.h"

namespace pinpoint::analysis {

inline constexpr int kEffectiveContextRuleVersion = 2;   // 2: the one club (refactor 6.2)

struct EffectiveClub {
    QString                          name;        // the club the analysis uses; empty = undeclared
    QString                          captured;    // the club capture recorded (provenance)
    QString                          declared;    // review.club as passed in (provenance)
    bool                             corrected = false;   // name is the golfer's club (correction or popover), not the captured one
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

// The shot's club facts beyond the captured club, as stored (R-CLUB-1): the golfer's two club edits on its review block
// and the device club follow-club stored beside the captured club. Empty = not set.
struct ClubReview {
    QString correction;   // review.clubOverride (any spelling the club vocabulary reads)
    QString declared;     // review.club, used as today's re-analysis uses it (trimmed, not canonicalised)
    pinpoint::recording::RecordedClub device;   // capture.deviceClub: name and geometry (no prior, no standard length)
};

EffectiveContext effectiveContext(const pinpoint::recording::AnalysisContext &recorded,
                                  const ClubReview &review);

} // namespace pinpoint::analysis
