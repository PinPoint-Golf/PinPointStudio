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

// Segmented "pill" selector: one recessed capsule track, equal-width segments,
// the active one filled. Used for the View popup's layout presets. Width-driven
// (fills its parent); set `options` (string list) and `selected`.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: seg

    property var    options:  []
    property string selected: ""
    property bool   solid:    true     // solid accent fill vs recessed/raised
    signal activated(string value)

    implicitHeight: Theme.sp(30)
    radius: height / 2
    color: Theme.colorBg               // recessed below the popup surface
    border.width: 1
    border.color: Theme.colorBorderMid

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(3)
        spacing: Theme.sp(2)

        Repeater {
            model: seg.options
            delegate: Rectangle {
                required property string modelData
                readonly property bool active: modelData === seg.selected

                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: height / 2
                color: active ? (seg.solid ? Theme.colorAccent : Theme.colorBg3)
                              : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight: parent.active ? Font.Medium : Theme.fontBodyWeight
                    color: parent.active
                           ? (seg.solid ? (Theme.dark ? Theme.colorBg : "#FFFFFF")
                                        : Theme.colorAccent)
                           : Theme.colorText2
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.activated(modelData)
                }
            }
        }
    }
}
