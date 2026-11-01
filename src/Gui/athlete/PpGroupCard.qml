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

// One group of the athlete form (ScreenAthleteForm's Required / Recommended / Optional / Reports,
// and AthleteClubsSection): a radiusLg box, a 36 px colorBg2 header strip with the group's title on
// the left and a quiet hint on the right over a hairline, then a colorSurface body. The children
// declared inside go into the body, a Column inset 18 px; `footer` takes anything that sits below
// the body at full width (the Optional group's note strip).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: root

    property string title: ""
    property string hint:  ""
    // The hint fills the space beside the title, right-aligned, and elides when it runs out
    // (Reports' long sentence). Off: the hint keeps its natural width.
    property bool   elideHint: false
    // Gap between the body's rows.
    property real   spacing: Theme.gap(12)

    default property alias content: bodyCol.data
    property alias footer: footerCol.data

    width:        parent ? parent.width : implicitWidth
    height:       cardCol.implicitHeight
    radius:       Theme.radiusLg
    border.width: 1
    border.color: Theme.colorBorderMid
    clip:         true
    color:        "transparent"

    Column {
        id: cardCol
        width: parent.width
        spacing: 0

        // Header strip
        Rectangle {
            width:  parent.width
            height: Theme.sp(36)
            color:  Theme.colorBg2

            RowLayout {
                anchors { fill: parent; leftMargin: Theme.gap(14); rightMargin: Theme.gap(14) }
                PpMicro {
                    text:             root.title
                    color:            Theme.colorText2
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }
                Text {
                    text:           root.hint
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                    elide:          root.elideHint ? Text.ElideRight : Text.ElideNone
                    Layout.fillWidth: root.elideHint
                    horizontalAlignment: root.elideHint ? Text.AlignRight : Text.AlignLeft
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // Bottom border
            Rectangle {
                anchors.bottom: parent.bottom
                width:  parent.width
                height: 1
                color:  Theme.colorBorderMid
            }
        }

        // Body
        Rectangle {
            width:  parent.width
            height: bodyCol.implicitHeight + 36
            color:  Theme.colorSurface

            Column {
                id: bodyCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(18) }
                spacing: root.spacing
            }
        }

        // Footer — empty unless the group has something under its body.
        Column {
            id: footerCol
            width: parent.width
        }
    }
}
