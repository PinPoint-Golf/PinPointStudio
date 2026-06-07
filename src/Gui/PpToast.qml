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

// Transient undo snackbar — inverted fill (colorText) with light text so it
// reads as a floating notice on any theme. Auto-hides on a Timer; the UNDO
// action is the host's to wire (it restores via the model invokable). Soft
// delete is reversible, so this replaces a modal confirm.

import QtQuick
import PinPointStudio

Rectangle {
    id: root

    property string message: ""
    property string glyph:   "🗑"
    // Hide the UNDO action for purely informational notices.
    property bool   showUndo: true

    signal undoClicked()

    function show(msg) {
        message = msg
        visible = true
        hideTimer.restart()
    }

    visible: false
    implicitWidth:  toastRow.implicitWidth + Theme.sp(30)
    implicitHeight: Theme.sp(40)
    radius: Theme.radiusLg
    color:  Theme.colorText

    // UNDO must stay legible on the inverted (dark-on-light-theme) fill — the
    // raw accent is too dark there, so lighten it; dark themes invert the fill
    // to a light surface where the plain accent already reads.
    readonly property color undoColor: Theme.dark ? Theme.colorAccent
                                                  : Qt.lighter(Theme.colorAccent, 1.9)

    Timer {
        id: hideTimer
        interval: Theme.durationSlow * 14
        onTriggered: root.visible = false
    }

    Row {
        id: toastRow
        anchors.centerIn: parent
        spacing: Theme.sp(14)

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.sp(9)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           root.glyph
                font.family:    Theme.fontSymbol
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorBg3
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           root.message
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorBg
            }
        }

        Rectangle {   // hairline separator before the action
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(18)
            color: Theme.colorBg
            opacity: 0.25
            visible: root.showUndo
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible:        root.showUndo
            text:           qsTr("UNDO")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            font.letterSpacing: Theme.trackingLabel
            color:          root.undoColor

            MouseArea {
                anchors.fill:    parent
                anchors.margins: -Theme.sp(6)
                cursorShape:     Qt.PointingHandCursor
                onClicked: {
                    root.visible = false
                    root.undoClicked()
                }
            }
        }
    }
}
