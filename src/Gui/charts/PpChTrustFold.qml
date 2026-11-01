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
import QtQuick
import PinPointStudio

// PPCHTRUSTFOLD — a Charts section's own "How far can I trust this?", under its profile, CLOSED by default, in the look
// of the panel's own fold (PpTrustSummaryFold): its toggle is the panel's (PpTrustFoldToggle). The panel's fold speaks
// for the traces' readings (TrustPresenter) and carries no section's words, so a section (Posture, Wrist) says its own,
// ONCE: the presenter's TRUST.lines, verbatim —
// for posture which camera each row came from, the ⚠ reasons, the bands' sources, the datum, the rig rule
// (docs/design/posture_chart_design.md §5.6); for the wrist each reading's source and confidence, what was not
// measured and why in full, what is not graded, where the bands come from and which style
// (docs/design/wrist_motion_into_charts.md §8). Nothing in the section's body repeats them: a cell says only its few
// words. No lines: nothing drawn.
// objectNames, from `prefix`: <prefix>Trust, <prefix>TrustToggle, <prefix>TrustLine ("chPosture": PpChPostureTrust;
// "chWrist": the Wrist sections).
Column {
    id: fold
    property string prefix: "ch"
    objectName: prefix + "Trust"
    property QtObject roles: null
    property var lines: []
    property bool open: false
    visible: (lines || []).length > 0
    spacing: roles ? roles.g(4) : Theme.gap(4)

    PpTrustFoldToggle {
        objectName: fold.prefix + "TrustToggle"
        roles: fold.roles; open: fold.open; hitMargin: 2
        onToggled: fold.open = !fold.open
    }
    Repeater {
        model: fold.open ? (fold.lines || []) : []
        delegate: Text {
            required property var modelData
            objectName: fold.prefix + "TrustLine"
            width: fold.roles ? Math.min(fold.width, fold.roles.measureW(fold.width)) : fold.width
            leftPadding: fold.roles ? fold.roles.g(14) : Theme.gap(14)
            wrapMode: Text.WordWrap
            text: String(modelData)
            font: fold.roles ? fold.roles.fSmall : Qt.font({}); color: fold.roles ? fold.roles.ink2 : "grey"
        }
    }
}
