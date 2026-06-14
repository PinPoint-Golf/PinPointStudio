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

// Segmented playback-speed selector — all speeds visible, the active one
// highlighted with an accent-tinted capsule; click any segment to select it
// directly. Used by the Replay transport (PpReplayTransport) to drive the
// session-stage replay speed.
Rectangle {
    id: root

    // Capture-time multipliers offered, slowest → full speed.
    readonly property var speeds: [0.1, 0.25, 0.5, 1.0]
    // Bind to shotReplay.speed; segments highlight by proximity so float
    // representation never breaks the match.
    property real current: 0.25
    signal selected(real speed)

    implicitWidth:  segRow.implicitWidth + Theme.sp(8)
    implicitHeight: Theme.sp(26)
    radius: height / 2
    color: "transparent"
    border.width: 1
    border.color: Theme.colorBorderMid

    Row {
        id: segRow
        anchors.centerIn: parent
        spacing: Theme.sp(2)

        Repeater {
            model: root.speeds

            Rectangle {
                required property var modelData
                readonly property bool active: Math.abs(root.current - modelData) < 0.01

                width:  segText.implicitWidth + Theme.sp(12)
                height: Theme.sp(20)
                anchors.verticalCenter: parent.verticalCenter
                radius: height / 2
                color: active ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g,
                                        Theme.colorAccent.b, 0.16)
                              : segMa.containsMouse ? Theme.colorBg3 : "transparent"

                Text {
                    id: segText
                    anchors.centerIn: parent
                    text: (modelData >= 1 ? "1" : Number(modelData).toString()) + "×"
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color: parent.active ? Theme.colorAccent : Theme.colorText3
                }

                MouseArea {
                    id: segMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(parent.modelData)
                }
            }
        }
    }
}
