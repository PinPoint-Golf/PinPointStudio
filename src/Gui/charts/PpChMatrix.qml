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

// PPCHMATRIX — the Up reading's family against itself (design §2.3 "the family matrix"): a lower triangle of
// r set as FIGURES, an annual-report table and not a heat map (the only wash in a chart is the corridor's): a
// figure is ink when it clears chance and ink3 inside it, the pair on the scatter in the pick wash. Rows are
// named and numbered, columns carry the numbers. As many readings as their columns fit (ch.matrixCellEm);
// the rest stay in the "Moves with" ledger. A cell puts that pair on the scatter.
// objectName: chMatrix; each cell chMatrixCell (+ xId, yId).
Column {
    id: mx
    objectName: "chMatrix"
    property QtObject roles: null
    property var matrix: null                    // relations.matrix
    property bool interactive: true
    property bool atom: true
    readonly property var keyKinds: []
    signal pairActivated(string xId, string yId)
    readonly property real em: roles ? roles.em : 14
    readonly property real cellW: Math.round((roles ? roles.ch.matrixCellEm : 3.3) * em)
    readonly property real nameW: Math.round(12 * em)
    readonly property var all: matrix && matrix.readings ? matrix.readings : []
    // how many readings fit: one name column, then one figure column per reading but the last
    readonly property int n: Math.max(0, Math.min(all.length, Math.floor((width - nameW - 2 * em) / Math.max(1, cellW)) + 1))
    readonly property real rowH: roles ? roles.u(22) : 22
    visible: n >= 3
    spacing: 0
    Rectangle { width: mx.nameW + Math.round(2 * mx.em) + (mx.n - 1) * mx.cellW; height: mx.roles ? mx.roles.r.figureRuleTop : 1; color: mx.roles ? mx.roles.rule : "black" }
    Row {
        height: mx.rowH
        Item { width: mx.nameW + Math.round(2 * mx.em); height: 1 }
        Repeater {
            model: mx.roles ? Math.max(0, mx.n - 1) : 0
            delegate: Text { required property int index; width: mx.cellW; horizontalAlignment: Text.AlignRight; topPadding: mx.roles.u(5); rightPadding: mx.roles.u(4)
                             text: String(index + 1); font: mx.roles.fFigLabel; color: mx.roles.ink3 }
        }
    }
    Repeater {
        model: mx.roles ? mx.n : 0
        delegate: Item {
            id: rw
            required property int index
            readonly property var rd: mx.all[index]
            readonly property var cells: mx.matrix.cells && mx.matrix.cells[index] ? mx.matrix.cells[index] : []
            width: mx.nameW + Math.round(2 * mx.em) + (mx.n - 1) * mx.cellW; height: Math.max(mx.rowH, rn.implicitHeight + mx.roles.u(6))
            Text { width: Math.round(1.6 * mx.em); horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; text: String(rw.index + 1); font: mx.roles.fFigLabel; color: mx.roles.ink3 }
            Text { id: rn; x: Math.round(2 * mx.em); width: mx.nameW - mx.roles.u(6); anchors.verticalCenter: parent.verticalCenter; wrapMode: Text.WordWrap
                   text: rw.rd ? rw.rd.text : ""; font: mx.roles.fSmall; color: mx.roles.ink }
            Repeater {
                model: rw.cells
                delegate: Item {
                    objectName: "chMatrixCell"
                    required property var modelData
                    required property int index
                    readonly property string xId: modelData.xId
                    readonly property string yId: modelData.yId
                    x: mx.nameW + Math.round(2 * mx.em) + index * mx.cellW; width: mx.cellW; height: rw.height
                    Rectangle { anchors.fill: parent; anchors.margins: 1; visible: modelData.on === true; color: mx.roles.pickWash }
                    Text { anchors.right: parent.right; anchors.rightMargin: mx.roles.u(4); anchors.verticalCenter: parent.verticalCenter
                           text: modelData.defined ? modelData.rText : "-"; font: mx.roles.fFigS; color: modelData.defined && modelData.clear ? mx.roles.ink : mx.roles.ink3 }
                    MouseArea { anchors.fill: parent; enabled: mx.interactive && modelData.defined === true; cursorShape: Qt.PointingHandCursor; onClicked: mx.pairActivated(parent.xId, parent.yId) }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: mx.roles.hw; color: mx.roles.hair }
        }
    }
}
