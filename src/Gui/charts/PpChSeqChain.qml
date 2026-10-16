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

// PPCHSEQCHAIN — one shot's kinematic sequence as ONE typeset chain (design §2.1: "Lead arm — 58 ms →
// Club — 69 ms → Pelvis, late"), from ChartsTracesPresenter.thisShot.sequence: the placed peaks in the order
// they happened, the gap on each arrow, and the segment that peaked late in warn, underlined, with its word
// (D4: only that one). Then one quiet line: the order it should be, and what was not placed and why
// (sequence.note), and — wherever the pelvis is said late — the neutral note (thisShot.note, rule 5).
// objectNames: chSeqChain; each link chSeqLink:<segment>; the late one also chSeqLate:<segment>.
Column {
    id: sc
    objectName: "chSeqChain"
    property QtObject roles: null
    property var sequence: null                  // thisShot.sequence
    property var lateNote: null                  // thisShot.note (Sentence)
    property bool showNote: true
    property bool showOrderNote: true
    property bool atom: true
    readonly property var keyKinds: []
    readonly property var links: sequence && sequence.links ? sequence.links : []
    visible: links.length > 0
    spacing: roles ? roles.u(3) : 3
    Flow {
        width: sc.width
        spacing: 0
        Repeater {
            model: sc.roles ? sc.links : []
            delegate: Row {
                required property var modelData
                required property int index
                objectName: modelData.late ? "chSeqLate:" + modelData.segment : "chSeqLink:" + modelData.segment
                spacing: sc.roles.u(5)
                rightPadding: sc.roles.u(5)
                Text { id: w; text: modelData.word; font: sc.roles.fBody; color: modelData.late ? sc.roles.warn : sc.roles.ink
                       Rectangle { visible: modelData.late; anchors.top: parent.baseline; anchors.topMargin: sc.roles.u(2); width: parent.width; height: 1; color: sc.roles.warn } }
                Text { visible: modelData.late && !!modelData.lateWord; anchors.baseline: w.baseline; text: modelData.lateWord || ""; font: sc.roles.fBody; color: sc.roles.warn }
                // the arrow to the next placed peak, the gap on it
                Text { visible: !!modelData.gapText; anchors.baseline: w.baseline; text: "—"; font: sc.roles.fBody; color: sc.roles.ink3 }
                Text { visible: !!modelData.gapText; anchors.baseline: w.baseline; text: modelData.gapText || ""; font: sc.roles.fFig; color: sc.roles.ink2 }
                Text { visible: index < sc.links.length - 1; anchors.baseline: w.baseline; text: "→"; font: sc.roles.fBody; color: sc.roles.ink3 }
            }
        }
    }
    PpChText { visible: sc.showNote && sc.showOrderNote && present; roles: sc.roles; sentence: sc.sequence ? sc.sequence.note : null; tone: sc.roles ? sc.roles.ink3 : "grey"; measure: false }
    PpChText { visible: sc.showNote && present; roles: sc.roles; sentence: sc.lateNote; tone: sc.roles ? sc.roles.ink3 : "grey"; measure: false }
    // WHY, ON DEMAND (trust_and_gates_design.md §7.0): the note says each gate in short; this opens the
    // readings a gate touched — in the Trust panel when it is on, else in place under the strip.
    signal trustClicked(string subject)
    Text {
        objectName: "chSeqWhy"
        visible: sc.showNote && sc.showOrderNote && !!sc.roles && !!sc.sequence && !!sc.sequence.whyText
        text: (sc.sequence ? (sc.sequence.whyText || "") : "") + "  ›"
        font: sc.roles ? sc.roles.fSmall : Qt.font({}); color: sc.roles ? sc.roles.link : "grey"
        MouseArea { anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor; onClicked: sc.trustClicked("") }
    }
    Text {
        objectName: "chSeqUnrecorded"
        visible: sc.showNote && sc.showOrderNote && !!sc.roles && !!sc.sequence && !!sc.sequence.unrecordedText
        width: sc.roles ? sc.roles.measureW(sc.width) : sc.width; wrapMode: Text.WordWrap
        text: sc.sequence ? (sc.sequence.unrecordedText || "") : ""
        font: sc.roles ? sc.roles.fSmallI : Qt.font({}); color: sc.roles ? sc.roles.ink3 : "grey"
    }
}
