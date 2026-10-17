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

// PPCHPROFILE — readings as ruled rows (design §2.1 "Apart from the session", D5): each with this shot's
// value, the session median, its place and a box of the session with this shot ringed (PpChBoxCell), under
// family crossheads when `groups` is given. Every field is a ChartsRelationsPresenter READING ROW's. A value a
// diagnostics condition graded and found outside its corridor is set in warn AND carries its word
// (firedWord). D5: boxes are muted at rest and take the metric's hue on hover, or when the reading is apart.
// A row opens IN PLACE (body.openId): its curve with a box at every P-position (PpChReadingTrace) when it has
// one, else its run through the session (PpChRunBox) — `openRow` is the presenter's.
//   column          tier  below it folds to
//   name, value     1     (line 1)
//   box             1     (line 1, narrower: ch.profileBoxEm by class)
//   unit            1     (line 1)
//   at (P tag)      3     line 2, first
//   median          3     line 2, "median 72"
//   place           3     line 2, "highest of 15"
// objectNames: chApart (the apart list) / chProfile (every reading); each row chApartRow (+ readingId),
// the open one also chApartRowOpen.
Column {
    id: pt
    property QtObject roles: null
    property var brush: null
    property QtObject presenter: null            // ChartsRelationsPresenter (ticks, seeks) for an open row
    property var rows: []                        // a flat list of READING ROWs …
    property var groups: null                    // … or [{ name, rows }] with crossheads
    property string headLabel: "Reading"
    property string shotLabel: ""                // the value column's head ("Shot 8")
    property string countLabel: ""               // the box column's head
    property string openId: ""
    property var openRow: null                   // relations.openRow
    property real playMs: NaN
    property string playText: ""
    property bool interactive: true
    property bool showPlace: true
    readonly property var keyKinds: ["box", "ring", "fired"]
    signal rowActivated(string readingId)
    signal shotActivated(int ordinal)
    // a value a gate refused (row.trustSubject): the gate's words stand in the value's place and open the reason
    signal trustClicked(string subject)
    objectName: "chProfile"

    readonly property real em: roles ? roles.em : 14
    readonly property string cls: roles ? roles.sizeClass(width) : "compact"
    readonly property bool wideCols: roles ? roles.shows(3, width) : true
    readonly property real boxW: Math.round((roles ? roles.ch.profileBoxEm[cls] : 13) * em)
    readonly property real valW: Math.round(4.6 * em)
    readonly property real unitW: Math.round((cls === "narrow" ? 3.2 : 4.2) * em)
    readonly property real atW: Math.round(3.2 * em)
    readonly property real medW: Math.round(4.6 * em)
    readonly property real placeW: Math.round(8.6 * em)
    readonly property real gap: roles ? roles.u(cls === "narrow" ? 8 : 12) : 12
    readonly property real nameW: Math.max(6 * em, width - boxW - valW - unitW - gap * 3 - (wideCols ? atW + medW + (showPlace ? placeW + gap : 0) + gap * 2 : 0))
    // the display list: crossheads and rows, in the presenter's order
    readonly property var items: {
        if (groups) {
            let out = []
            for (const g of groups) { out.push({ kind: "group", name: g.name }); for (const r of (g.rows || [])) out.push({ kind: "row", row: r }) }
            return out
        }
        return (rows || []).map(function (r) { return { kind: "row", row: r } })
    }

    // the column head
    Item {
        width: pt.width; height: pt.roles ? pt.roles.u(22) : 22
        Rectangle { width: parent.width; height: pt.roles ? pt.roles.r.figureRuleTop : 1; color: pt.roles ? pt.roles.rule : "black" }
        Row {
            y: pt.roles ? pt.roles.u(6) : 6
            spacing: pt.gap
            Text { width: pt.nameW; text: pt.headLabel; font: pt.roles.fLabel; color: pt.roles.ink3; wrapMode: Text.WordWrap }
            Text { visible: pt.wideCols; width: pt.atW; text: "At"; font: pt.roles.fLabel; color: pt.roles.ink3 }
            Text { width: pt.valW; horizontalAlignment: Text.AlignRight; text: pt.shotLabel; font: pt.roles.fLabel; color: pt.roles.ink3 }
            Text { width: pt.unitW; text: "Unit"; font: pt.roles.fLabel; color: pt.roles.ink3 }
            Text { visible: pt.wideCols; width: pt.medW; horizontalAlignment: Text.AlignRight; text: "Median"; font: pt.roles.fLabel; color: pt.roles.ink3 }
            Text { visible: pt.wideCols && pt.showPlace; width: pt.placeW; text: "Place"; font: pt.roles.fLabel; color: pt.roles.ink3 }
            Text { width: pt.boxW; text: pt.countLabel; font: pt.roles.fLabel; color: pt.roles.ink3; horizontalAlignment: Text.AlignHCenter }
        }
    }
    Repeater {
        model: pt.roles ? pt.items : []
        delegate: Loader {
            required property var modelData
            width: pt.width
            sourceComponent: modelData.kind === "group" ? cGroup : cRow
            property var md: modelData
        }
    }
    Component {
        id: cGroup
        Item {
            width: pt.width; height: pt.roles.u(30)
            Text { anchors.bottom: parent.bottom; anchors.bottomMargin: pt.roles.u(5); text: parent.parent ? parent.parent.md.name : ""; font: pt.roles.fSmallM; color: pt.roles.ink2 }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: pt.roles.hw; color: pt.roles.hair }
        }
    }
    Component {
        id: cRow
        Item {
            id: row
            readonly property var r: parent && parent.md ? parent.md.row : null
            readonly property string readingId: r ? r.readingId : ""
            readonly property bool open: pt.openId !== "" && pt.openId === readingId
            objectName: open ? "chApartRowOpen" : "chApartRow"
            readonly property bool two: !pt.wideCols || (r && (r.fired || r.caution))
            readonly property real line1: Math.max(pt.roles.u(24), name.implicitHeight + pt.roles.u(8))
            readonly property real line2: two ? pt.roles.u(16) : 0
            readonly property bool hot: ma.containsMouse
            width: pt.width
            height: line1 + line2 + (open ? openL.height + pt.roles.u(10) : 0) + pt.roles.u(2)
            Rectangle { anchors.fill: parent; visible: row.hot && pt.interactive; color: pt.roles.rowHover }
            Row {
                y: pt.roles.u(4)
                spacing: pt.gap
                Text {
                    id: name; width: pt.nameW; wrapMode: Text.WordWrap
                    leftPadding: pt.roles.u(pt.roles.ch.swatch) + pt.roles.u(5)
                    text: row.r ? row.r.label + (row.r.wrappedMark ? " " + row.r.wrappedMark : "") : ""
                    font: row.open ? pt.roles.fBodyM : pt.roles.fBody; color: pt.roles.ink
                    Rectangle { y: Math.round(name.baselineOffset - height - name.font.pixelSize * 0.18); width: pt.roles.u(pt.roles.ch.swatch); height: width
                                radius: pt.roles.square ? 0 : width / 2; color: row.r ? pt.roles.hue(row.r.key) : "transparent" }
                }
                Text { visible: pt.wideCols; width: pt.atW; text: row.r ? row.r.tag : ""; font: pt.roles.fSmall; color: pt.roles.ink3; anchors.baseline: name.baseline }
                // a gated value takes the unit column too (the unit is hidden), so its words never crowd the name
                Text { id: valT
                       readonly property bool gated: !!(row.r && row.r.trustSubject)
                       objectName: gated ? "chProfileWhy:" + row.r.trustSubject : ""
                       width: gated ? pt.valW + pt.gap + pt.unitW : pt.valW
                       horizontalAlignment: gated ? Text.AlignLeft : Text.AlignRight; anchors.baseline: name.baseline
                       elide: gated ? Text.ElideRight : Text.ElideNone
                       text: row.r ? (row.r.valueText + (gated ? " ›" : "")) : "-"; font: gated ? pt.roles.fSmall : pt.roles.fFig
                       color: gated ? pt.roles.link : row.r && row.r.fired ? pt.roles.warn : row.r && row.r.measured ? pt.roles.ink : pt.roles.ink3 }
                Text { visible: !valT.gated; width: pt.unitW; text: row.r ? (row.r.unitText || "") : ""; font: pt.roles.fSmall; color: pt.roles.ink3; anchors.baseline: name.baseline }
                Text { visible: pt.wideCols; width: pt.medW; horizontalAlignment: Text.AlignRight; anchors.baseline: name.baseline
                       text: row.r ? row.r.medianText : "-"; font: pt.roles.fFig; color: pt.roles.ink2 }
                Text { visible: pt.wideCols && pt.showPlace; width: pt.placeW; text: row.r ? (row.r.place || "") : ""; font: pt.roles.fSmall; color: pt.roles.ink2; anchors.baseline: name.baseline }
                PpChBoxCell {
                    width: pt.boxW; height: pt.roles.u(pt.roles.ch.boxRow); anchors.verticalCenter: name.verticalCenter
                    roles: pt.roles; brush: pt.brush; interactive: pt.interactive
                    box: row.r ? row.r.box : null; values: row.r ? row.r.values : []; fired: row.r ? row.r.firedShots : []
                    color: row.r ? pt.roles.hue(row.r.key) : "grey"
                    muted: !(row.r && row.r.apart) && !row.hot && !row.open
                    onShotActivated: (o) => pt.shotActivated(o)
                }
            }
            // line 2: the folded columns, and the warn word (a fired value is a mark AND a word)
            Row {
                visible: row.two
                y: row.line1 - pt.roles.u(3); x: pt.roles.u(12)
                spacing: pt.roles.u(8)
                Text { visible: !pt.wideCols; text: row.r ? row.r.tag : ""; font: pt.roles.fSmall; color: pt.roles.ink3 }
                Text { visible: !pt.wideCols; text: "median"; font: pt.roles.fSmall; color: pt.roles.ink3 }
                Text { visible: !pt.wideCols; text: row.r ? row.r.medianText : ""; font: pt.roles.fFigLabel; color: pt.roles.ink3 }
                Text { visible: !pt.wideCols && !!(row.r && row.r.place); text: row.r ? (row.r.place || "") : ""; font: pt.roles.fSmall; color: pt.roles.ink3 }
                Text { visible: !!(row.r && row.r.fired); text: row.r ? (row.r.firedWord || "") : ""; font: pt.roles.fSmall; color: pt.roles.warn
                       Rectangle { anchors.top: parent.baseline; anchors.topMargin: 2; width: parent.width; height: 1; color: pt.roles.warn } }
                // an estimate a gate refused: shown, with its health warning (why: the trust fold)
                Text { objectName: "chProfileCaution"; visible: !!(row.r && row.r.caution); text: row.r ? (row.r.cautionText || "") : ""
                       font: pt.roles.fSmall; color: pt.roles.ink2 }
            }
            Loader {
                id: openL
                active: row.open && !!pt.openRow && pt.openRow.readingId === row.readingId
                y: row.line1 + row.line2 + pt.roles.u(4)
                width: parent.width
                sourceComponent: pt.openRow && pt.openRow.opens === "trace" && pt.openRow.trace ? cTrace : cRun
            }
            Component { id: cTrace
                PpChReadingTrace { roles: pt.roles; brush: pt.brush; presenter: pt.presenter; trace: pt.openRow.trace; width: openL.width
                                   interactive: pt.interactive; playMs: pt.playMs; playText: pt.playText
                                   onShotActivated: (o) => pt.shotActivated(o) } }
            Component { id: cRun
                PpChRunBox { roles: pt.roles; brush: pt.brush; ticker: pt.presenter; run: pt.openRow ? pt.openRow.run : null; width: openL.width
                             interactive: pt.interactive; onShotActivated: (o) => pt.shotActivated(o) } }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: pt.roles.hw; color: pt.roles.hair }
            MouseArea {
                id: ma; enabled: pt.interactive; hoverEnabled: true
                width: pt.nameW + pt.valW + pt.unitW + 2 * pt.gap + (pt.wideCols ? pt.atW + pt.gap : 0); height: row.line1 + row.line2
                cursorShape: Qt.PointingHandCursor
                // a click on a gated value's words opens the reason; anywhere else, the row
                onClicked: (mouse) => {
                    if (valT.gated) {
                        const q = valT.mapFromItem(ma, mouse.x, mouse.y)
                        if (q.x >= 0 && q.x <= Math.min(valT.width, valT.implicitWidth + 4)) { pt.trustClicked(row.r.trustSubject); return }
                    }
                    pt.rowActivated(row.readingId)
                }
            }
        }
    }
}
