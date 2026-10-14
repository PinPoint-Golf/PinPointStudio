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

// PPCHSEQTABLE — the per-segment peak table (CONTRACT Addendum, F31): in the order the segments should peak,
// should / actual rank ("?" on an unplaced one, the late one in warn with its word), the peak's time ± σ,
// its speed ± σ and its % of pro (the tour average, Cheetham 2008) ± σ; a trunk not peaked by impact keeps
// its open chevron and "rising" (S8). Every text is ChartsTracesPresenter.thisShot.sequence.table's.
// Tiers: segment, should, actual, peak — 1; °/s and % of pro — 2 (below compact they fold to line 2).
// objectNames: chSeqTable; each row chSeqRow:<segment>.
Column {
    id: st
    objectName: "chSeqTable"
    property QtObject roles: null
    property var sequence: null
    property bool atom: true
    readonly property var keyKinds: []
    readonly property var rows: sequence && sequence.table ? sequence.table : []
    readonly property bool wide: roles ? roles.shows(2, width) : true
    readonly property real em: roles ? roles.em : 14
    readonly property real gap: roles ? roles.u(12) : 12
    readonly property real segW: Math.round(6.5 * em)
    readonly property real rankW: Math.round(3.6 * em)
    readonly property real peakW: Math.round(7.5 * em)
    readonly property real speedW: Math.round(7.5 * em)
    readonly property real pctW: Math.round(7 * em)
    visible: rows.length > 0
    Item {
        width: st.width; height: st.roles ? st.roles.u(22) : 22
        Rectangle { width: parent.width; height: st.roles ? st.roles.r.figureRuleTop : 1; color: st.roles ? st.roles.rule : "black" }
        Row {
            y: st.roles ? st.roles.u(6) : 6; spacing: st.gap
            Text { width: st.segW; text: "Segment"; font: st.roles.fLabel; color: st.roles.ink3 }
            Text { width: st.rankW; horizontalAlignment: Text.AlignRight; text: "Should"; font: st.roles.fLabel; color: st.roles.ink3 }
            Text { width: st.rankW; horizontalAlignment: Text.AlignRight; text: "Actual"; font: st.roles.fLabel; color: st.roles.ink3 }
            Text { width: st.peakW; horizontalAlignment: Text.AlignRight; text: st.roles.labelUnit("Peak", "ms"); font: st.roles.fLabelUnit; color: st.roles.ink3 }
            Text { visible: st.wide; width: st.speedW; horizontalAlignment: Text.AlignRight; text: st.roles.labelUnit("Speed", "°/s"); font: st.roles.fLabelUnit; color: st.roles.ink3 }
            Text { visible: st.wide; width: st.pctW; horizontalAlignment: Text.AlignRight; text: st.roles.labelCase("Of pro"); font: st.roles.fLabelUnit; color: st.roles.ink3 }
        }
    }
    Repeater {
        model: st.roles ? st.rows : []
        delegate: Item {
            id: rw
            objectName: "chSeqRow:" + modelData.segment
            required property var modelData
            readonly property real line1: st.roles.u(24)
            width: st.width; height: line1 + (st.wide ? 0 : st.roles.u(15)) + st.roles.u(2)
            opacity: modelData.placed ? 1 : 0.7
            Row {
                y: st.roles.u(4); spacing: st.gap
                Text { id: sn; width: st.segW; wrapMode: Text.WordWrap; text: rw.modelData.label; font: st.roles.fBody; color: st.roles.ink }
                Text { width: st.rankW; horizontalAlignment: Text.AlignRight; anchors.baseline: sn.baseline
                       text: rw.modelData.shouldRank; font: st.roles.fFig; color: st.roles.ink2 }
                Row {
                    width: st.rankW; layoutDirection: Qt.RightToLeft; spacing: st.roles.u(4)
                    Text { text: rw.modelData.actualRank; font: st.roles.fFig; color: rw.modelData.late ? st.roles.warn : rw.modelData.placed ? st.roles.ink : st.roles.ink3 }
                    Text { visible: rw.modelData.late && !!rw.modelData.lateWord; text: rw.modelData.lateWord || ""; font: st.roles.fSmall; color: st.roles.warn }
                }
                Row {
                    width: st.peakW; layoutDirection: Qt.RightToLeft; spacing: st.roles.u(4)
                    Text { text: rw.modelData.peakText; font: st.roles.fFigS; color: st.roles.ink }
                    Text { visible: rw.modelData.rising; text: "› rising"; font: st.roles.fSmall; color: st.roles.ink2 }
                }
                Text { visible: st.wide; width: st.speedW; horizontalAlignment: Text.AlignRight; text: rw.modelData.speedText; font: st.roles.fFigS; color: st.roles.ink }
                Text { visible: st.wide; width: st.pctW; horizontalAlignment: Text.AlignRight; text: rw.modelData.pctText; font: st.roles.fFigS; color: st.roles.ink }
            }
            Row {
                visible: !st.wide
                x: st.segW + st.gap; y: rw.line1 - st.roles.u(3); spacing: st.roles.u(10)
                Text { id: fs; text: rw.modelData.speedText; font: st.roles.fFigS; color: st.roles.ink2 }
                Text { anchors.baseline: fs.baseline; text: "°/s"; font: st.roles.fSmall; color: st.roles.ink3 }
                Text { anchors.baseline: fs.baseline; text: rw.modelData.pctText; font: st.roles.fFigS; color: st.roles.ink2 }
                Text { anchors.baseline: fs.baseline; text: "of pro"; font: st.roles.fSmall; color: st.roles.ink3 }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: st.roles.hw; color: st.roles.hair }
        }
    }
    Text {
        visible: !!st.sequence && !!st.sequence.proNote
        topPadding: st.roles ? st.roles.u(4) : 4
        width: st.width; wrapMode: Text.WordWrap
        text: st.sequence ? (st.sequence.proNote || "") : ""
        font: st.roles ? st.roles.fSmall : Qt.font({}); color: st.roles ? st.roles.ink3 : "grey"
    }
}
