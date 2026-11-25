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

#include "effective_context.h"

#include "../Vocabulary/Club/club_standards.h"
#include "../Vocabulary/Club/club_vocabulary.h"

namespace pinpoint::analysis {

EffectiveContext effectiveContext(const pinpoint::recording::AnalysisContext &recorded,
                                  const ClubReview &review)
{
    EffectiveContext out;
    out.handedness = recorded.handedness;
    out.heightM    = recorded.heightM;
    out.clubSet    = recorded.clubSet;

    const QString captured = recorded.club.name;
    const QString declared = review.declared.trimmed();
    out.club.captured = captured;
    out.club.declared = declared;

    // THE ONE CLUB (club_vocabulary.h; R-CLUB-2): the correction, else the declared club, else the device club, else the
    // captured club.
    const pinpoint::ClubFacts facts{ captured, review.device.name, review.declared, review.correction };
    const QString one = pinpoint::canonicalClubId(pinpoint::effectiveClub(facts));

    // Nothing moved it (the one club is the captured club, or the shot is undeclared): v1's answer, field for field. The
    // captured name is passed through as recorded (an undeclared club stays empty). The standard length follows the
    // declared club when there is one — today's re-analysis rule (standardClub of review.club, case-insensitive, as
    // stored; invalid → none).
    if (one.isEmpty() || one == pinpoint::canonicalClubId(captured)) {
        out.club.name        = captured;
        out.club.geometry    = recorded.club.geometry;
        out.club.lengthPrior = recorded.club.lengthPrior;
        if (declared.isEmpty()) {
            out.club.standardLengthMm = recorded.club.standardLengthMm;
        } else {
            const pinpoint::ClubStandard st = pinpoint::standardClub(declared, recorded.clubSet, recorded.heightM);
            out.club.standardLengthMm = st.valid ? st.lengthMm : 0.0;
        }
        return out;
    }

    out.club.name = one;
    const pinpoint::ClubStandard st = pinpoint::standardClub(one, recorded.clubSet, recorded.heightM);
    if (st.valid)
        out.club.standardLengthMm = st.lengthMm;

    // The device club, when the rule chose it (no correction, no declared club): its geometry as follow-club stored it,
    // no length prior — the job follow-club's old overwrite of the captured club gave.
    const bool golferSaid = !review.correction.trimmed().isEmpty() || !declared.isEmpty();
    if (!golferSaid) {
        out.club.geometry             = review.device.geometry;
        out.club.geometry.lengthKnown = out.club.geometry.lengthMm > 0.0;
        return out;
    }

    // The golfer named another club (the board's correction, or the popover's club): its geometry from the bag snapshot,
    // and no length prior.
    out.club.corrected = true;
    const auto bagIt = recorded.bag.constFind(one);
    if (bagIt != recorded.bag.constEnd()) {
        out.club.geometry             = bagIt.value();
        out.club.geometry.lengthKnown = out.club.geometry.lengthMm > 0.0;
    }
    return out;
}

} // namespace pinpoint::analysis
