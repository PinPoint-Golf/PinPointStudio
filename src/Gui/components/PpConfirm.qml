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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

// The house confirmation prompt: modal, centred, dimmed, Esc cancels, display-font title, right-aligned
// outlined primary followed by a neutral Cancel. Used by the session-active close interception
// (Main.qml), the Sessions screen's bulk actions and the Diagnostic Model's two prompts — written once
// because hand-drawn copies of one standard drift apart.
//
// `tone` is the meaningful difference between callers, and the difference is meant (`dialogWidth` and
// `buttonHoverScale` exist only so the close prompt keeps the look it has always had). Theme.colorAttention
// is defined as a call-to-action frame — "draws the eye to a row/control that needs the user to act" —
// and the close prompt wears it because it interrupts something live, not because it destroys
// anything. A prompt that discards saved work wears the error family instead, which is how the app
// already draws a destructive action: removing something is a write to the user's pack, and is
// styled as one. Two different events must not read alike.
Popup {
    id: root

    property string title:       ""
    property string body:        ""
    property string confirmText: qsTr("Continue")
    property string cancelText:  qsTr("Cancel")
    // "attention" — a call to act, nothing is lost. "error" — this destroys saved work.
    property string tone:        "attention"
    // The prompt's width before the window clamps it. The close prompt in Main.qml has always been
    // sp(20) narrower than the others and keeps its look.
    property real   dialogWidth: Theme.sp(440)
    // Hover growth on the two buttons. The prompts here sit still (1.0); the close prompt keeps the
    // card/pill default growth it has always had.
    property real   buttonHoverScale: 1.0

    signal confirmed()
    signal cancelled()

    readonly property color _strong: tone === "error" ? Theme.colorError : Theme.colorAttention

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim:   true
    // Esc = cancel, the safe default. Deliberately NOT CloseOnPressOutside: a click beside a prompt
    // that is asking about saved work is not an answer.
    closePolicy: Popup.CloseOnEscape
    padding: Theme.gap(20)
    width: Math.min(root.dialogWidth, (parent ? parent.width : Theme.sp(600)) - Theme.sp(48))

    // Escape, a click on Cancel and a programmatic close all land here, so the cancel path is stated
    // once. `_answered` is what keeps confirming from also reporting a cancel on the way out.
    property bool _answered: false
    onOpened: root._answered = false
    onClosed: if (!root._answered) root.cancelled()

    background: PpPopoverCard { style: "plain"; borderColor: root._strong }

    contentItem: Column {
        spacing: Theme.gap(12)

        Text {
            width: parent.width
            text:  root.title
            font.family:    Theme.fontDisplay
            font.italic:    Theme.fontDisplayItalic
            font.weight:    Theme.fontDisplayWeight
            font.pixelSize: Math.min(Theme.sp(20), Theme.fontSzDisplay)
            color:          root._strong
            wrapMode:       Text.WordWrap
        }

        Text {
            width: parent.width
            text:  root.body
            font.family:    Theme.fontBody
            font.weight:    Theme.fontBodyWeight
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText2
            wrapMode:       Text.WordWrap
            lineHeight:     1.5
        }

        Item { width: 1; height: Theme.sp(4) }

        Row {
            anchors.right: parent.right
            spacing: Theme.gap(8)

            // The primary, outlined in the tone rather than filled: it is the consequential answer,
            // and a filled button here would read as the recommended one.
            PpButton {
                style: "tone"; tone: root.tone === "error" ? "error" : "attention"; size: "compact"
                implicitHeight: Theme.sp(32)
                horizontalPadding: Theme.sp(24)
                hoverScale: root.buttonHoverScale
                label: root.confirmText
                onClicked: {
                    root._answered = true
                    root.close()
                    root.confirmed()
                }
            }

            PpButton {
                style: "subtle"; size: "compact"
                implicitHeight: Theme.sp(32)
                horizontalPadding: Theme.sp(24)
                hoverScale: root.buttonHoverScale
                label: root.cancelText
                onClicked: root.close()
            }
        }
    }
}
