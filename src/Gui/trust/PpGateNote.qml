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

// PPGATENOTE — what a surface shows where a gate refused a reading, instead of a bare "-" (trust stage 6
// session 2; docs/design/trust_and_gates_design.md §7.0, §7.1). The gate's short words, quiet, in the
// link colour with " ›"; a click opens the reading in the Trust panel (TrustFocus), or, with the panel
// off, expands the same reading here, under the words (PpTrustInlineFor). In print it is the words alone.
//
//   gate      TrustWords.gateFor(gates, key) — { subject, gate, words, … }; nothing is drawn when empty
//   swingDir  the swing the records are on (the in-place block reads its session through it)
//   prefix    words before the gate's ("Club speed: ") when the surface has no label of its own
//   roles     a PpTypeset (fonts, colours), or null for the theme's small text
//   printed   paper: words only, no link, no fold
// objectNames: gateNote, gateNoteWords.
Column {
    id: gn
    objectName: "gateNote"
    property var gate: ({})
    property string swingDir: ""
    property string prefix: ""
    property QtObject roles: null
    property bool printed: false
    property bool inlineOpen: false
    property font font: roles ? roles.fSmall : Qt.font({ family: Theme.fontBody, pixelSize: Theme.fontSzMicro })
    readonly property bool has: !!gate && !!gate.words
    readonly property color linkColour: roles ? roles.link : Theme.colorAccent
    readonly property color quietColour: roles ? roles.ink3 : Theme.colorText3
    visible: has
    spacing: roles ? roles.u(4) : 4

    Text {
        objectName: "gateNoteWords"
        width: Math.min(implicitWidth, gn.width > 0 ? gn.width : implicitWidth)
        wrapMode: Text.WordWrap
        textFormat: Text.StyledText
        font: gn.font
        color: gn.quietColour
        text: !gn.has ? ""
              : gn.prefix + (gn.printed ? gn.gate.words
                                        : "<font color='" + gn.linkColour + "'>" + gn.gate.words + " ›</font>")
        MouseArea {
            anchors.fill: parent
            anchors.margins: -2
            enabled: gn.has && !gn.printed
            cursorShape: Qt.PointingHandCursor
            onClicked: if (!TrustFocus.open(gn.gate.subject, gn.swingDir)) gn.inlineOpen = !gn.inlineOpen
        }
    }
    PpTrustInlineFor {
        width: gn.width
        roles: gn.roles
        subject: gn.inlineOpen && gn.has ? gn.gate.subject : ""
        swingDir: gn.swingDir
        onClosed: gn.inlineOpen = false
    }
}
