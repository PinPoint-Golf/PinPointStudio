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

// PPCHWRISTSESSION — THE WRIST SECTION OF SESSION (docs/design/wrist_motion_into_charts.md §4; PpChartsSessionView
// draws it under its own section head "Wrist", after the standard session content): "what is my wrist style, and
// how repeatable is it?" The head and standfirst (the style each shot was graded as, the score's median, the most
// and least repeatable checkpoint, the resemblance index against what Auto chose, the session's size said);
// the checkpoint profile as one box per checkpoint across the shots over the picked shot's band, this shot
// ringed, the median and how wide the middle half is under each, its caption, and under it the section's own
// "How far can I trust this?" (PpChTrustFold, closed: session.trust.lines verbatim — the sources and confidence, the
// shots with no reading / refused / not graded, the bands' sources and the style rule, no resemblance index, a small
// session); then — in a right rail at wide — the score shot
// by shot with its box (PpChRunBox), the style table, and what keeps firing as a mark per shot that picks that
// swing. Every word and number is ChartsWristPresenter.session's.
// objectNames: chWristSession, chWristTrust (+ chWristTrustToggle, chWristTrustLine).
Item {
    id: ws
    objectName: "chWristSession"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property bool interactive: true
    readonly property var se: body ? body.wSession : ({})
    readonly property bool ok: !!roles
    readonly property bool available: !!se && se.available === true
    readonly property bool rail: ok && roles.atLeast(width, "wide")
    readonly property real railW: ok ? Math.min(roles.u(600), width * 0.3) : 0
    readonly property real mainW: rail ? width - railW - roles.u(48) : width
    height: rail ? Math.max(mainCol.height, railCol.height) : mainCol.height

    Column {
        id: mainCol
        width: ws.mainW
        spacing: ws.ok ? ws.roles.u(18) : 18
        Column {
            objectName: "chWristSessionHead"
            width: parent.width; spacing: ws.ok ? ws.roles.u(6) : 6
            PpChText { roles: ws.roles; sentence: ws.se ? ws.se.head : null
                       font: ws.ok ? ws.roles.fStand : Qt.font({}); tone: ws.ok ? ws.roles.ink : "black" }
            PpChText { roles: ws.roles; sentence: ws.se ? ws.se.standfirst : null
                       font: ws.ok ? ws.roles.fBody : Qt.font({}) }
        }
        Column {
            visible: ws.available
            width: parent.width; spacing: ws.ok ? ws.roles.u(8) : 8
            PpSectionHead { width: parent.width; roles: ws.roles; title: "Checkpoints across the session"; sub: ws.se.sub || "" }
            PpChWristProfile {
                width: parent.width
                roles: ws.roles; brush: ws.brush; interactive: ws.interactive
                ticker: ws.body ? ws.body.relationsP : null
                rows: ws.available ? (ws.se.rows || []) : []
                positions: ws.available ? (ws.se.positions || []) : []
                selected: ws.body ? ws.body.wSelected : -1
                mode: "session"
            }
            Text { width: parent.width; wrapMode: Text.WordWrap; text: ws.se.caption || ""
                   font: ws.ok ? ws.roles.fSmall : Qt.font({}); color: ws.ok ? ws.roles.ink3 : "grey" }
            // how far the session's wrist read can be trusted, said once, under the profile (closed)
            PpChTrustFold { prefix: "chWrist"; width: parent.width; roles: ws.roles
                            lines: ws.available && ws.se.trust ? (ws.se.trust.lines || []) : [] }
        }
        Loader { active: !ws.rail && ws.available; visible: active; width: parent.width; sourceComponent: sideC }
    }
    Column {
        id: railCol
        visible: ws.rail && ws.available
        x: ws.mainW + (ws.ok ? ws.roles.u(48) : 48)
        width: ws.railW
        Loader { active: ws.rail && ws.available; width: parent.width; sourceComponent: sideC }
    }

    // ── the score run, the style table, what keeps firing ──
    Component { id: sideC
    Column {
        id: side
        spacing: ws.roles.u(26)
        Column {
            width: side.width; spacing: ws.roles.u(6)
            PpSectionHead { width: parent.width; roles: ws.roles; title: "Score"; sub: "a mark per shot; the box is the session" }
            PpChRunBox { width: parent.width; roles: ws.roles; brush: ws.brush; ticker: ws.body ? ws.body.relationsP : null
                         run: ws.se.scoreRun || null; interactive: ws.interactive
                         onShotActivated: (o) => { if (ws.body) ws.body.pickOrdinal(o) } }
        }
        // the style table: how much each shot resembles each style, the style it is graded as in bold
        Column {
            id: sty
            objectName: "chWristStyles"
            width: side.width; spacing: ws.roles.u(4)
            readonly property var st: ws.se.styles || ({})
            readonly property var cols: st.columns || []
            readonly property real lw: ws.roles.u(80)
            readonly property real cw: Math.min(ws.roles.u(64), (width - lw) / Math.max(1, cols.length))
            readonly property bool bare: cw < ws.roles.u(52)       // "Shot 12" no longer fits: the bare ordinal
            PpSectionHead { width: parent.width; roles: ws.roles; title: "Wrist style"; sub: "how much each shot resembles each style, %; the style it is graded as in bold" }
            // no shot carries a resemblance index: the presenter's one quiet line instead of a grid of "-"
            Text { visible: !!sty.st.note; width: parent.width; wrapMode: Text.WordWrap
                   text: sty.st.note || ""; font: ws.roles.fSmall; color: ws.roles.ink2 }
            Item {
                visible: sty.cols.length > 0
                width: parent.width; height: ws.roles.u(22)
                Repeater { model: sty.cols
                    delegate: Text { required property var modelData
                        required property int index
                        x: sty.lw + index * sty.cw; width: sty.cw; horizontalAlignment: Text.AlignRight
                        text: (sty.bare ? modelData.short : modelData.label) || ""; font: modelData.picked ? ws.roles.fSmallM : ws.roles.fSmall
                        color: modelData.picked ? ws.roles.ink : ws.roles.ink3 } }
            }
            Repeater {
                model: sty.st.rows || []
                delegate: Item {
                    id: srow
                    required property var modelData
                    width: sty.width; height: ws.roles.u(22)
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ws.roles.hw; color: ws.roles.hair }
                    Text { y: ws.roles.u(3); text: srow.modelData.name || ""; font: ws.roles.fSmall; color: ws.roles.ink }
                    Repeater { model: srow.modelData.cells || []
                        delegate: Text { required property var modelData
                            required property int index
                            x: sty.lw + index * sty.cw; y: ws.roles.u(3); width: sty.cw; horizontalAlignment: Text.AlignRight
                            text: modelData.text || "-"
                            font: modelData.graded ? ws.roles.fSmallM : ws.roles.fFigS; color: modelData.graded ? ws.roles.ink : ws.roles.ink3 } }
                }
            }
        }
        // what keeps firing: a mark per shot where it fired; a mark opens that swing
        Column {
            id: rec
            objectName: "chWristRecurring"
            width: side.width; spacing: ws.roles.u(4)
            PpSectionHead { width: parent.width; roles: ws.roles; title: "What keeps firing"; sub: "each finding, shot by shot: a mark where it fired (a mark opens that swing)" }
            Text { visible: (ws.se.recurring || []).length === 0; width: parent.width; wrapMode: Text.WordWrap
                   text: ws.se.recurringEmpty || ""; font: ws.roles.fBody; color: ws.roles.ink2 }
            Repeater {
                model: ws.se.recurring || []
                delegate: Item {
                    id: rr
                    required property var modelData
                    width: rec.width; height: ws.roles.u(26)
                    readonly property var marks: modelData.marks || []
                    readonly property real lw: Math.min(ws.roles.u(300), width * 0.45)
                    readonly property real pitch: Math.min(ws.roles.u(26), (width - lw - ws.roles.u(10)) * 0.5 / Math.max(1, marks.length))
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ws.roles.hw; color: ws.roles.hair }
                    Text { y: ws.roles.u(5); width: rr.lw - ws.roles.u(10); wrapMode: Text.NoWrap; clip: true; text: rr.modelData.name || ""; font: ws.roles.fSmall; color: ws.roles.ink }
                    Repeater { model: rr.marks
                        delegate: PpMark { required property var modelData
                            required property int index
                            roles: ws.roles; brush: ws.brush; shotId: modelData.ordinal; fired: modelData.fired === true; interactive: ws.interactive
                            cx: rr.lw + (index + 0.5) * rr.pitch; cy: ws.roles.u(13)
                            onActivated: if (ws.body) ws.body.pickOrdinal(modelData.ordinal) } }
                    Text { x: rr.lw + rr.marks.length * rr.pitch + ws.roles.u(10); y: ws.roles.u(5); width: parent.width - x; wrapMode: Text.NoWrap
                           text: rr.modelData.countText || ""; font: ws.roles.fSmall; color: ws.roles.ink2 }
                }
            }
        }
    }
    }
}
