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

// PPCHPOSTURETRUST — the Posture section's "How far can I trust this?" (docs/design/posture_chart_design.md §5.6),
// under its profile, CLOSED by default, in the look of the panel's own fold (PpTrustSummaryFold). The panel's fold
// speaks for the traces' readings (TrustPresenter) and carries no posture words, so the section says its own,
// ONCE: which camera each row came from, the ⚠ reasons, the bands' sources, the trunk-bend datum and, for the
// absolutes, the rig rule — TRUST.lines, verbatim. Nothing in the body repeats them: a cell says only "⚠" and a
// few words.
// objectNames: chPostureTrust, chPostureTrustToggle, chPostureTrustLine.
Column {
    id: fold
    objectName: "chPostureTrust"
    property QtObject roles: null
    property var lines: []
    property bool open: false
    visible: (lines || []).length > 0
    spacing: roles ? roles.u(4) : 4

    Text {
        objectName: "chPostureTrustToggle"
        text: (fold.open ? "▾" : "▸") + " How far can I trust this?"
        font: fold.roles ? fold.roles.fSmallL : Qt.font({}); color: fold.roles ? fold.roles.link : "grey"
        MouseArea { anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor; onClicked: fold.open = !fold.open }
    }
    Repeater {
        model: fold.open ? (fold.lines || []) : []
        delegate: Text {
            required property var modelData
            objectName: "chPostureTrustLine"
            width: fold.roles ? Math.min(fold.width, fold.roles.measureW(fold.width)) : fold.width
            leftPadding: fold.roles ? fold.roles.u(14) : 14
            wrapMode: Text.WordWrap
            text: String(modelData)
            font: fold.roles ? fold.roles.fSmall : Qt.font({}); color: fold.roles ? fold.roles.ink2 : "grey"
        }
    }
}
