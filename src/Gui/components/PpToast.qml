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

// Transient snackbar — elevated surface fill with a strong border so it
// reads as a floating notice on any theme. Auto-hides on a Timer; the UNDO
// action is the host's to wire (it restores via the model invokable). Soft
// delete is reversible, so this replaces a modal confirm. Hosts can also set
// copyText to expose a copy-to-clipboard action.

import QtQuick
import PinPointStudio

Rectangle {
    id: root

    property string message: ""
    property string glyph:   "🗑"
    // Hide the UNDO action for purely informational notices.
    property bool   showUndo: true
    // When non-empty, a copy icon copies this text to the clipboard on tap.
    property string copyText: ""
    // "info" (neutral, default), "warn", or "error" — tints the border and
    // glyph so the notice reads at the right urgency without shouting (the
    // fill stays the neutral elevated surface).
    property string severity: "info"

    readonly property color _severityColor:
        severity === "error" ? Theme.colorError
      : severity === "warn"  ? Theme.colorWarn
      :                        Theme.colorBorderStrong

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
    color:  Theme.colorBg3
    border.width: 1
    border.color: root._severityColor

    Timer {
        id: hideTimer
        interval: Theme.durationSlow * 21
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
                color:          root.severity === "info" ? Theme.colorText2
                                                         : root._severityColor
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:           root.message
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorText
            }
        }

        Rectangle {   // hairline separator before the copy action
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(18)
            color: Theme.colorBorderStrong
            visible: root.copyText.length > 0
        }

        Text {        // copy-to-clipboard action; flashes a check as feedback
            anchors.verticalCenter: parent.verticalCenter
            visible:        root.copyText.length > 0
            text:           copyConfirm.running ? "✓" : "⧉"
            font.family:    Theme.fontSymbol
            font.pixelSize: Theme.fontSzBody
            color:          copyConfirm.running ? Theme.colorGood : Theme.colorAccent

            Timer { id: copyConfirm; interval: 1200 }

            PpPressable {
                anchors.margins: -Theme.sp(6)
                onClicked: {
                    clipboard.setText(root.copyText)
                    copyConfirm.restart()
                    hideTimer.restart()   // give the user time to see the confirmation
                }
            }
        }

        Rectangle {   // hairline separator before the action
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(18)
            color: Theme.colorBorderStrong
            visible: root.showUndo
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible:        root.showUndo
            text:           qsTr("UNDO")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorAccent

            PpPressable {
                anchors.margins: -Theme.sp(6)
                onClicked: {
                    root.visible = false
                    root.undoClicked()
                }
            }
        }
    }
}
