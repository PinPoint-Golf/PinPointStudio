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

// SESSIONS — the rail's Sessions (Main.qml, AppScreen.Sessions): the current golfer's sessions, every
// one, each with its report (docs/design/session_report_design.md §8.0). The one place the list lives;
// Home and the golfer's page no longer carry it. The rail mutes Sessions with no golfer, so the list
// is always the current golfer's (main.cpp points sessionReportList at their library folder).
//
// The list builds its own rows and links (PpSessionReportList); its requests leave through this
// screen's signals, which Main.qml handles.

import QtQuick
import PinPointStudio

Item {
    id: root
    objectName: "sessionsScreen"

    // A row's "Open session"; the links to where a missing detail is entered — Settings → Studio,
    // or the golfer's own page (their address, the photo approval).
    signal reviewSessionRequested(string sessionDir)
    signal studioSettingsRequested()
    signal golferDetailsRequested()

    // Back to the top: Main calls it on the way in.
    function toTop() { flick.contentY = 0 }

    Rectangle { anchors.fill: parent; color: Theme.colorBg }

    Flickable {
        id: flick
        anchors.fill:  parent
        contentWidth:  width
        contentHeight: mainCol.implicitHeight + 80
        clip:          true

        Column {
            id: mainCol
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Theme.contentWidth(parent.width)
            spacing: 0

            Item { width: 1; height: Theme.sp(48) }

            // ── Heading ──────────────────────────────────────────────────────
            Item {
                width:  mainCol.width
                height: Theme.sp(34)
                PpMicro {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: Theme.caps(qsTr("Sessions"))
                }
            }
            Text {
                objectName: "sessionsSubtitle"
                width:   mainCol.width
                visible: text !== ""
                text: {
                    const name = (typeof athleteController !== "undefined" && athleteController)
                                 ? (athleteController.currentName || "") : ""
                    return name !== "" ? qsTr("Every session %1 has recorded, each with its report.").arg(name) : ""
                }
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                font.italic:    true
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
            }
            Item { width: 1; height: Theme.sp(16) }

            PpSessionReportList {
                objectName: "sessionsList"
                width: mainCol.width
                limit: 0
                onReviewSessionRequested:  (dir) => root.reviewSessionRequested(dir)
                onStudioSettingsRequested: root.studioSettingsRequested()
                onGolferDetailsRequested:  root.golferDetailsRequested()
            }
        }
    }
}
