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

// TENDENCIES — the Your tendencies page of the session report (docs/design/shot_outcomes_design.md §7.2): Home's rows on
// paper, as they stood at this session. After the order of work; absent when the map has no `tendencies`
// (report_tendencies.h). A row per club and swing length: club · usual distance · usual miss · your shots (the band bar
// over its words), with what it is seen with, or that it is still building, beneath.
// FLOWED as the Consistency page: the head starts a fresh page, each row is one atom, the column heads keep with the first
// row. Every sentence is the map's. FOLIO LIGHT through `ro` and `cr`: nothing reads a live theme token. Colour is never
// the only channel: the bar has its words. objectNames: reportTendencies (the head), reportTendencyRow, reportTendencyFoot.
Column {
    id: tp
    property QtObject ro: null
    property var td: null
    readonly property var m: td || ({})
    readonly property bool on: !!td
    readonly property var rows: m.rows || []
    visible: on
    width: 600

    readonly property real gutter: cr.g(8)
    readonly property real g3: cr.g(3)
    readonly property real g5: cr.g(5)
    readonly property real g6: cr.g(6)
    readonly property real clubW: cr.u(92)
    readonly property real distanceW: cr.u(84)
    readonly property real missW: cr.u(92)
    readonly property real mixW: Math.max(0, width - clubW - distanceW - missW - 3 * gutter)
    readonly property real barH: cr.u(5)
    readonly property color toneGood: cr.good
    readonly property color toneMain: cr.warn
    readonly property color toneOther: cr.attention

    function tone(kind) { return kind === "good" ? toneGood : kind === "main" ? toneMain : toneOther }

    PpReportSectionHead {
        objectName: "reportTendencies"
        ro: tp.ro; kicker: tp.m.kicker || ""; title: "Your tendencies"; standfirst: tp.m.standfirst || ""
        visible: tp.on
        width: tp.width
        Item { width: 1; height: cr.g(12) }
    }

    Item {
        property bool atom: true
        property bool keepNext: true
        visible: tp.on
        width: tp.width
        height: headRow.height + tp.g5
        Row {
            id: headRow
            spacing: tp.gutter
            Text { width: tp.clubW; text: "Club"; font: tp.ro.fLabel; color: tp.ro.ink3 }
            Text { width: tp.distanceW; text: "Usual distance"; font: tp.ro.fLabel; color: tp.ro.ink3 }
            Text { width: tp.missW; text: "Usual miss"; font: tp.ro.fLabel; color: tp.ro.ink3 }
            Text { width: tp.mixW; text: "Your shots"; font: tp.ro.fLabel; color: tp.ro.ink3 }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: tp.ro.hw; color: tp.ro.hair }
    }

    Repeater {
        model: tp.rows
        delegate: Item {
            id: rw
            required property var modelData
            objectName: "reportTendencyRow"
            readonly property string rowKey: modelData.key || ""
            property bool atom: true
            width: tp.width
            height: line.y + line.height + (seenText.visible ? seenText.height + (tp ? tp.g3 : 0) : 0) + (tp ? tp.g6 : 0)   // a delegate outlives `tp` at teardown
            Row {
                id: line
                y: tp.g6
                spacing: tp.gutter
                Text { width: tp.clubW; text: rw.modelData.club || ""; font: tp.ro.fBody; color: tp.ro.ink; wrapMode: Text.WordWrap }
                Text { width: tp.distanceW; text: rw.modelData.distance || "-"; font: tp.ro.fFig; color: rw.modelData.distance ? tp.ro.ink : tp.ro.ink3 }
                Text { width: tp.missW; text: rw.modelData.miss || ""; font: tp.ro.fBody; color: tp.ro.ink; wrapMode: Text.WordWrap }
                Column {
                    width: tp.mixW
                    spacing: tp.g3
                    PpBandBar {
                        width: parent.width
                        height: tp.barH
                        segments: (rw.modelData.mixParts || []).map(p => ({ count: p.count, tone: tp.tone(p.kind) }))
                    }
                    Text { width: parent.width; text: rw.modelData.mixLine || ""; font: tp.ro.fSmall; color: tp.ro.ink2; wrapMode: Text.WordWrap }
                }
            }
            Text {
                id: seenText
                y: line.y + line.height + (tp ? tp.g3 : 0)
                visible: text !== ""
                width: parent.width
                text: rw.modelData.seen || ""
                font: tp.ro.fSmall; color: tp.ro.ink2; wrapMode: Text.WordWrap
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: tp.ro ? tp.ro.hw : 1; color: tp.ro ? tp.ro.hair : "transparent" }
        }
    }

    Item {
        objectName: "reportTendencyFoot"
        property bool atom: true
        property bool keepPrev: true
        visible: tp.on && (tp.m.foot || "") !== ""
        width: tp.width
        height: footText.y + footText.height + tp.g6
        Text { id: footText; y: tp.g6; width: parent.width; text: tp.m.foot || ""; font: tp.ro.fSmall; color: tp.ro.ink3; wrapMode: Text.WordWrap }
    }

    // the Charts print roles in Folio light: this page's units and tones. LAST in the Column: children are destroyed
    // first-declared first, so everything above goes before its roles (see PpPosturePage).
    PpChRoles { id: cr; medium: "print"; fixedAesthetic: "folio" }
}
