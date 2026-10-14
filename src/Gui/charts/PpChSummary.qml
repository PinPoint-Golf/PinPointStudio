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

// PPCHSUMMARY — the window-scoped readings of today's summary cards (CONTRACT Addendum: F33–F40, S9, S10),
// as a ruled ledger: one row per preset member (the traced rows, then the curve-less cards), each with its
// tiles — the card's own instants ("at impact"), PEAK, Δ and PK RATE recomputed for the reader's window —
// each value with its ± and unit as the presenter wrote them; "partial" when part of the window was never
// measured; "-" when the window has no valid data; the value in warn plus its word when its band says out.
// Line 2 states what backs the row's ± (σ provenance, F36) and the ⚠ wrong-structure caveat (F37) inline —
// never a tooltip. Hovering a row shows its tiles' own notes there instead ("Recorded sample there: raw 503").
// Companion readings (shaft lie on shaft lean, F39) arrive as tiles on their card: drawn as given.
// objectNames: chSummary; each row chSummaryRow (+ key).
Column {
    id: sm
    objectName: "chSummary"
    property QtObject roles: null
    property var rows: []                        // thisShot.rows (traced) — their tiles
    property var cards: []                       // thisShot.cards (instant-only, curve-less)
    property bool interactive: true
    property bool atom: false
    readonly property var keyKinds: []
    readonly property var all: (rows || []).concat(cards || [])
    readonly property real em: roles ? roles.em : 14
    readonly property bool side: roles ? roles.atLeast(width, "compact") : true
    readonly property real nameW: side ? Math.round(12 * em) : width
    readonly property real cellW: Math.round(7.2 * em)
    readonly property real gap: roles ? roles.u(12) : 12
    visible: all.length > 0
    Rectangle { width: sm.width; height: sm.roles ? sm.roles.r.figureRuleTop : 1; color: sm.roles ? sm.roles.rule : "black" }
    Repeater {
        model: sm.roles ? sm.all : []
        delegate: Item {
            id: rw
            objectName: "chSummaryRow"
            required property var modelData
            readonly property string key: modelData.key
            readonly property var tips: (modelData.tiles || []).filter(function (t) { return !!t.tip }).map(function (t) { return t.tip })
            width: sm.width
            readonly property real bodyH: sm.side ? Math.max(nameCol.y + nameCol.height, cells.y + cells.height) : cells.y + cells.height
            height: bodyH + (note.visible ? note.implicitHeight + sm.roles.u(4) : 0) + sm.roles.u(8)
            Rectangle { anchors.fill: parent; visible: ma.containsMouse && sm.interactive; color: sm.roles.rowHover }
            Column {
                id: nameCol
                y: sm.roles.u(5); width: sm.nameW
                Row {
                    spacing: sm.roles.u(5)
                    Rectangle { anchors.verticalCenter: nm.verticalCenter; width: sm.roles.u(sm.roles.ch.swatch); height: width; radius: sm.roles.square ? 0 : width / 2; color: sm.roles.hue(rw.key) }
                    Text { id: nm; text: rw.modelData.label; font: sm.roles.fBody; color: sm.roles.ink; width: Math.min(implicitWidth, sm.nameW - sm.roles.u(12)); wrapMode: Text.WordWrap }
                }
                Row {
                    spacing: sm.roles.u(6)
                    Text { text: rw.modelData.unit || ""; font: sm.roles.fSmall; color: sm.roles.ink3 }
                    Text { visible: !!rw.modelData.sigmaText; text: rw.modelData.sigmaText || ""; font: sm.roles.fFigS; color: sm.roles.ink3 }
                    Text { visible: rw.modelData.partial === true; text: rw.modelData.partialWord || ""; font: sm.roles.fSmall; color: sm.roles.ink2 }
                    Text { visible: rw.modelData.grossWarn === true; text: rw.modelData.grossWord || ""; font: sm.roles.fSmall; color: sm.roles.warn }
                }
            }
            Flow {
                id: cells
                x: sm.side ? sm.nameW + sm.gap : 0
                y: sm.side ? sm.roles.u(5) : nameCol.y + nameCol.height + sm.roles.u(4)
                width: sm.side ? sm.width - x : sm.width
                spacing: sm.gap
                Repeater {
                    model: rw.modelData.tiles || []
                    delegate: Column {
                        required property var modelData
                        width: Math.max(sm.cellW, tv.implicitWidth + tsub.implicitWidth + tu.implicitWidth + sm.roles.u(8))
                        Text { text: modelData.label; font: sm.roles.fLabel; color: sm.roles.ink3 }
                        Row {
                            spacing: sm.roles.u(4)
                            Text { id: tv; text: modelData.text; font: sm.roles.fFig; color: modelData.verdict === "warn" ? sm.roles.warn : modelData.ok === false ? sm.roles.ink3 : sm.roles.ink }
                            Text { id: tsub; visible: !!modelData.sub; anchors.baseline: tv.baseline; text: modelData.sub || ""; font: sm.roles.fFigLabel; color: sm.roles.ink3 }
                            Text { id: tu; visible: !!modelData.unit; anchors.baseline: tv.baseline; text: modelData.unit || ""; font: sm.roles.fSmall; color: sm.roles.ink3 }
                            Text { visible: !!modelData.verdictWord && modelData.verdict !== ""; anchors.baseline: tv.baseline; text: modelData.verdictWord || ""; font: sm.roles.fSmall
                                   color: modelData.verdict === "warn" ? sm.roles.warn : sm.roles.ink2 }
                        }
                    }
                }
            }
            // line 2: what backs the ±, the ⚠ caveat; on hover, the tiles' own notes
            Text {
                id: note
                readonly property string txt: ma.containsMouse && rw.tips.length ? rw.tips.join("  ") : [rw.modelData.sigmaTip || "", rw.modelData.grossWarn ? (rw.modelData.grossTip || "") : ""].filter(function (s) { return s !== "" }).join("  ")
                visible: txt !== ""
                y: rw.bodyH + sm.roles.u(4)
                width: sm.width; wrapMode: Text.WordWrap
                text: txt; font: sm.roles.fSmall; color: sm.roles.ink3
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: sm.roles.hw; color: sm.roles.hair }
            MouseArea { id: ma; anchors.fill: parent; enabled: sm.interactive; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        }
    }
}
