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

#include "../Core/club_standards.h"
#include "../Core/club_vocabulary.h"

namespace pinpoint::analysis {

EffectiveContext effectiveContext(const pinpoint::recording::AnalysisContext &recorded,
                                  const ClubReview &review)
{
    EffectiveContext out;
    out.handedness = recorded.handedness;
    out.heightM    = recorded.heightM;
    out.clubSet    = recorded.clubSet;

    const QString captured   = recorded.club.name;
    const QString correction = pinpoint::canonicalClubId(review.correction);
    const QString declared   = review.declared.trimmed();
    out.club.captured = captured;
    out.club.declared = declared;

    // No correction, or a correction that names the captured club: the captured club's geometry and
    // prior. The captured name is passed through as recorded (an undeclared club stays empty). The
    // standard length follows the declared club when there is one — today's re-analysis rule
    // (standardClub of review.club, case-insensitive, as stored; invalid → none).
    if (correction.isEmpty() || correction == pinpoint::canonicalClubId(captured)) {
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

    // The golfer corrected the club on the board: its geometry from the bag snapshot, its standard
    // length for the recorded club set and height, and no length prior. The override wins over the
    // declared club.
    out.club.name      = correction;
    out.club.corrected = true;
    const auto bagIt = recorded.bag.constFind(correction);
    if (bagIt != recorded.bag.constEnd()) {
        out.club.geometry             = bagIt.value();
        out.club.geometry.lengthKnown = out.club.geometry.lengthMm > 0.0;
    }
    const pinpoint::ClubStandard st = pinpoint::standardClub(correction, recorded.clubSet, recorded.heightM);
    if (st.valid)
        out.club.standardLengthMm = st.lengthMm;
    return out;
}

} // namespace pinpoint::analysis
