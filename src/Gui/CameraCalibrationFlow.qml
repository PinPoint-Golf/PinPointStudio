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

// Compact in-panel STUB for camera (stereo / ChArUco) calibration. The real
// pipeline and its QML-exposed validity property don't exist yet, so this is a
// friendly placeholder with a disabled Start — the slot the real flow drops into
// later. Mirrors ImuCalibrationFlow's public shape (layoutMode / completed() /
// cancelled()) so PpCameraPanel hosts it the same way. It NEVER navigates.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: flow

    property string layoutMode: "compact"   // shape-compatible with ImuCalibrationFlow
    property bool   showHeader:  false

    signal completed()
    signal cancelled()

    // No-op API entry points (kept for shape compatibility with the real flow).
    function begin() {}
    function reset() {}
    function showCompleted() {}

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: col
            width: parent.width
            spacing: Theme.sp(16)

            Item { Layout.preferredHeight: Theme.sp(8) }   // top breathing room

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "◫"
                font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(40)
                color: Theme.colorText3
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.sp(20); Layout.rightMargin: Theme.sp(20)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Stereo camera calibration runs here")
                font.family: Theme.fontDisplay
                font.italic: Theme.fontDisplayItalic
                font.weight: Theme.fontDisplayWeight
                font.pixelSize: Math.min(Theme.sp(18), Theme.fontSzDisplay)
                color: Theme.colorText
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.sp(20); Layout.rightMargin: Theme.sp(20)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.5
                text: qsTr("Capture a ChArUco target from both cameras to solve the stereo extrinsics. This step isn't available yet — it'll appear here once the calibration pipeline lands.")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText2
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: Theme.sp(12)
                spacing: Theme.sp(8)
                PpButton {
                    label:   qsTr("Start")
                    primary: true
                    enabled: false                       // disabled — not implemented
                }
                PpButton {
                    label:   qsTr("Cancel")
                    primary: false
                    onClicked: flow.cancelled()
                }
            }
        }
    }
}
