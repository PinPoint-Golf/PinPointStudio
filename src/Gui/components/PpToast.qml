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

// Transient snackbar — the notice card (PpNotificationCard, toast style): elevated surface fill
// with a strong border so it reads as a floating notice on any theme. Auto-hides on a Timer; the
// UNDO action is the host's to wire (it restores via the model invokable). Soft delete is
// reversible, so this replaces a modal confirm. Hosts can also set copyText to expose a
// copy-to-clipboard action.
//
// It places itself, centred across its parent:
//   bottom  `margin` above the parent's bottom edge — or above `clearOf`'s top while that is
//           shown (a selection bar the toast must not cover)
//   above   `margin` above the parent's top edge, outside it (a bar's toast, over what is above it)

import QtQuick
import PinPointStudio

PpNotificationCard {
    id: root

    property string message: ""
    // Hide the UNDO action for purely informational notices.
    property bool   showUndo: true
    // Suppresses the auto-hide timer — for notices whose lifetime is owned by
    // the caller (e.g. a "…in progress" toast that must stay up for the whole
    // duration of an operation of unknown length). The caller is responsible
    // for calling show() again with sticky=false once the operation ends, so
    // the normal auto-hide resumes for the outcome message.
    property bool   sticky: false
    // "info" (neutral, default), "warn", or "error" — tints the border and
    // glyph so the notice reads at the right urgency without shouting (the
    // fill stays the neutral elevated surface).
    property string severity: "info"

    property string placement: "bottom"
    property real   margin:    Theme.gap(24)
    property Item   clearOf:   null

    readonly property color _severityColor: Theme.toneColor(severity, Theme.colorBorderStrong)

    signal undoClicked()

    function show(msg) {
        message = msg
        visible = true
        if (sticky)
            hideTimer.stop()    // stop, not just skip-restart: a prior toast's timer may still be ticking
        else
            hideTimer.restart()
    }

    style:      "toast"
    glyph:      "🗑"
    visible:    false
    title:      root.message
    tone:       root._severityColor
    glyphColor: root.severity === "info" ? Theme.colorText2 : root._severityColor
    actionText: root.showUndo ? Theme.caps(qsTr("Undo")) : ""

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: !parent                              ? undefined
                  : root.placement === "above"           ? parent.top
                  : root.clearOf && root.clearOf.visible ? root.clearOf.top
                  :                                        parent.bottom
    anchors.bottomMargin: root.margin

    onCopied: hideTimer.restart()   // give the user time to see the confirmation
    onActionClicked: {
        root.visible = false
        root.undoClicked()
    }

    Timer {
        id: hideTimer
        interval: Theme.durationSlow * 21
        onTriggered: root.visible = false
    }
}
