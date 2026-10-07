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

// The words for a shot's data-integrity warning, from its facts (ShotListModel's
// dataWarningDetail: swing_doc.h dataWarningDetailFrom plus the shot processor's
// clubRefused / dtlTopOutOfView). ONE wording for every place the ⚠ is explained —
// the film-strip card's tooltip and the swing panel's warning block — so the two
// can never tell a reader different things about the same shot.

pragma Singleton

import QtQuick

QtObject {
    function text(detail) {
        const d = detail || {}
        const parts = []
        if (d.capture) {
            const where = d.preImpact ? qsTr("during the swing")
                                      : qsTr("after impact, so the follow-through positions are unreliable")
            parts.push(qsTr("Frames were lost during capture (%1 frames in %2 hole%3, worst %4 ms) %5.")
                       .arg(d.framesLost).arg(d.holes).arg(d.holes === 1 ? "" : "s")
                       .arg(Math.round(d.worstHoleMs)).arg(where))
        }
        if (d.imu)
            parts.push(qsTr("IMU data integrity check failed — the recorded motion data is "
                            + "inconsistent (orientation re-fusion mismatch), so this shot "
                            + "cannot be re-analysed."))
        if (d.capture || d.imu)
            parts.push(qsTr("This shot is not included in the session assessment."))
        // A refused club track (analysis.club.refused): the tracker's own witnesses
        // contradicted what it saw, so nothing from the face-on club is shown or
        // measured on this shot; body and wrist measurements are unaffected.
        if (d.clubRefused) {
            const why = d.clubRefused === "p1BallConflict"
                          ? qsTr("the shaft at address did not point at the ball")
                          : d.clubRefused === "phaseSuspect"
                          ? qsTr("the takeaway could not be found from the hands")
                          : d.clubRefused === "lengthConflict"
                          ? qsTr("the club length at address disagreed with the ball distance")
                          : d.clubRefused === "handsUnusable"
                          ? qsTr("the pose could not place the hands on most frames")
                          : d.clubRefused
            parts.push(qsTr("The club track was refused (%1), so the club is not drawn and "
                            + "club measurements show \"-\" on this shot. Body and wrist "
                            + "measurements are unaffected.").arg(why))
        }
        // The down-the-line camera did not have the top of the swing in frame
        // (analysis.clubDtl.summary.topOutOfView): a framing fact, so it says how to
        // reframe and does not exclude the shot.
        if (d.dtlTopOutOfView)
            parts.push(qsTr("The down-the-line camera could not see the top of this swing "
                            + "(hands %1 px from the top edge, a club is %2 px here), so the "
                            + "club is unmeasured from mid-backswing to delivery in that view. "
                            + "Frame that camera with the headroom check: at the top, the hands "
                            + "at least a club length below the top edge.")
                       .arg(d.dtlHandsFromTopPx).arg(d.dtlClubPx))
        return parts.join(" ")
    }
}
