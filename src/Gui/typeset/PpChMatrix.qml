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

// PPCHMATRIX — a table of short cells under one heavy rule (design system components/PpChMatrix.md). An annual-report
// table: rows are named, a cell is a figure or a word in ink or ink3, and it works one of two ways.
// THE FAMILY (`matrix`, Charts Relations, design §2.3): the Up reading's family against itself, a lower triangle of r
// as figures, numbered rows and columns, ink when it clears chance and ink3 inside it, the pair on the scatter in the
// pick wash. As many readings as their columns fit (ch.matrixCellEm); the rest stay in the "Moves with" ledger, and
// under three it hides. A cell puts that pair on the scatter (pairActivated).
// THE RECTANGLE (`columns` + `rows`, Consistency): named columns across the full width and generic cells on an optional
// wash, the one exception to "the corridor is the only wash" (§15.4): the word stays in the cell. When the words do not
// fit, `short` turns on: each cell takes its short form on the same wash and `keyText` appears. No column is ever
// dropped. Whether they fit is MEASURED (R25, Mark 7 Nov 2026): the widest word in use (in fSmall, fSmallM when heavy),
// its gutters and any mark, and the widest full head, so no word overflows its cell at any width. A picked cell (`on`) takes an outline, because the wash has taken over its old highlight.
//   columns: [string | { text, short }]    rows: [{ id, name, cells: [{ text, short, strong, heavy, on, mark, tap, heat, soft }] }]
// One body draws both: `family` is mapped onto rows here. `roles` is any PpTypeset. Give it a width.
// objectName: chMatrix; each cell chMatrixCell (+ xId, yId in the family).
Column {
    id: mx
    objectName: "chMatrix"
    property QtObject roles: null
    property var matrix: null                    // relations.matrix
    property var columns: []
    property var rows: []
    property bool wash: true                     // false: the cells' heat is not drawn
    property string keyText: ""                  // the marks' words, from the presenter; shown only in the short form
    property real nameEm: 8.4                    // the rectangle's row-name column in the full form, in em; a page of fixed
                                                 // width narrows it so the words keep their cells (R33: the printed page)
    property bool interactive: true
    property bool atom: true
    readonly property var keyKinds: []
    signal pairActivated(string xId, string yId)
    signal cellActivated(string rowId, int column)
    readonly property real em: roles ? roles.em : 14
    readonly property int nCols: columns ? columns.length : 0
    readonly property bool family: nCols === 0
    readonly property real nameMax: Math.round(nameEm * em)
    // The full form's narrowest cell (R25): the widest word the cells hold, measured in the face it is set in, with the
    // gutter g(5) each side, or g(5), the word, g(3), the mark and g(3); and no narrower than the widest full head with its
    // g(5) gutters. With no roles nothing draws, so the old fixed width stands in.
    FontMetrics { id: fmWord;  font: mx.roles ? mx.roles.fSmall : Qt.font({}) }
    FontMetrics { id: fmHeavy; font: mx.roles ? mx.roles.fSmallM : Qt.font({}) }
    FontMetrics { id: fmFig;   font: mx.roles ? mx.roles.fFigS : Qt.font({}) }
    FontMetrics { id: fmHead;  font: mx.roles ? mx.roles.fFigLabel : Qt.font({}) }
    function _advance(fm, t) { return t && fm.font ? fm.advanceWidth(t) : 0 }   // reads fm.font, so wordW rebinds when a face changes
    readonly property real wordW: {
        if (!roles || family) return Math.round(4.3 * em)
        const g3 = roles.g(3), g5 = roles.g(5)
        let w = 0
        for (const r of rows || [])
            for (const c of (r && r.cells) || []) {
                if (!c) continue
                const word = _advance(c.heavy ? fmHeavy : fmWord, c.text || "")
                w = Math.max(w, c.mark ? g5 + word + g3 + _advance(fmFig, c.mark) + g3 : g5 + word + g5)
            }
        for (const c of columns || [])
            w = Math.max(w, g5 + _advance(fmHead, typeof c === "string" ? c : (c && c.text) || "") + g5)
        return Math.ceil(w)
    }
    // the words fit, or the short forms take their place
    readonly property bool short: !family && width - nameMax < nCols * wordW
    readonly property real indent: family ? Math.round(2 * em) : 0   // the row numbers
    readonly property real nameW: family ? Math.round(12 * em)
                                : mx.short ? Math.max(Math.round(6.2 * em), Math.min(nameMax, width - nCols * Math.round(1.9 * em))) : nameMax
    readonly property real cellW: family ? Math.round((roles ? roles.r.ch.matrixCellEm : 3.3) * em) : Math.floor((width - nameW) / Math.max(1, nCols))
    readonly property var all: matrix && matrix.readings ? matrix.readings : []
    // how many readings fit: one name column, then one figure column per reading but the last
    readonly property int n: Math.max(0, Math.min(all.length, Math.floor((width - nameW - 2 * em) / Math.max(1, cellW)) + 1))
    readonly property real rowH: roles ? roles.u(family ? 22 : 26) : 22
    readonly property real tableW: family ? nameW + indent + (n - 1) * cellW : width
    readonly property var heads: {
        const out = []
        for (let i = 0; i < (family ? n - 1 : nCols); ++i) {
            const c = columns[i]
            out.push(family ? String(i + 1) : typeof c === "string" ? c : (mx.short && c.short) || c.text)
        }
        return out
    }
    readonly property var body: {
        if (!family) return rows
        const out = [], cs = matrix && matrix.cells ? matrix.cells : []
        for (let i = 0; i < n; ++i) {
            const src = cs[i] || [], cells = []
            for (let j = 0; j < src.length; ++j) {
                const c = src[j]
                cells.push({ text: c.defined ? c.rText : "-", strong: c.defined && c.clear, on: c.on === true, tap: c.defined === true, xId: c.xId, yId: c.yId })
            }
            out.push({ name: all[i] ? all[i].text : "", cells: cells })
        }
        return out
    }
    visible: !family || n >= 3
    spacing: 0
    Rectangle { width: mx.tableW; height: mx.roles ? mx.roles.r.figureRuleTop : 1; color: mx.roles ? mx.roles.rule : "transparent" }
    Row {
        height: mx.rowH
        Item { width: mx.indent + mx.nameW; height: 1 }
        Repeater {
            model: mx.roles ? mx.heads : []
            delegate: Text { required property var modelData; width: mx.cellW; horizontalAlignment: mx.family ? Text.AlignRight : Text.AlignLeft
                             topPadding: mx.roles.g(mx.family ? 5 : 6); leftPadding: mx.family ? 0 : mx.roles.g(5); rightPadding: mx.family ? mx.roles.g(4) : 0
                             text: modelData; font: mx.roles.fFigLabel; color: mx.roles.ink3 }
        }
    }
    Repeater {
        model: mx.roles ? mx.body : []
        delegate: Item {
            id: rw
            required property int index
            required property var modelData
            width: mx.tableW; height: Math.max(mx.rowH, rn.implicitHeight + mx.roles.u(6))
            Text { visible: mx.family; width: Math.round(1.6 * mx.em); horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; text: String(rw.index + 1); font: mx.roles.fFigLabel; color: mx.roles.ink3 }
            Text { id: rn; x: mx.indent; width: mx.nameW - mx.roles.u(6); anchors.verticalCenter: parent.verticalCenter
                   wrapMode: mx.family ? Text.WordWrap : Text.NoWrap; elide: mx.family ? Text.ElideNone : Text.ElideRight
                   text: rw.modelData.name; font: mx.roles.fSmall; color: mx.roles.ink }
            Repeater {
                model: rw.modelData.cells
                delegate: Item {
                    objectName: "chMatrixCell"
                    required property var modelData
                    required property int index
                    readonly property string xId: modelData.xId || ""
                    readonly property string yId: modelData.yId || ""
                    x: mx.indent + mx.nameW + index * mx.cellW; width: mx.cellW; height: rw.height
                    Rectangle { anchors.fill: parent; anchors.margins: 1; visible: mx.wash && (modelData.heat || 0) > 0; color: mx.roles.heat(modelData.heat, modelData.soft === true) }
                    Rectangle { anchors.fill: parent; anchors.margins: 1; visible: modelData.on === true; color: mx.family ? mx.roles.pickWash : "transparent"
                                border.width: mx.family ? 0 : mx.roles.r.figureRuleTop; border.color: mx.roles.ink }
                    Text { id: tx; x: mx.family ? parent.width - width - mx.roles.g(4) : Math.max(1, Math.min(mx.roles.g(5), parent.width - width - (modelData.mark ? mk.width + mx.roles.g(4) : 0))); anchors.verticalCenter: parent.verticalCenter
                           text: (mx.short ? modelData.short : modelData.text) || ""
                           font: mx.family || mx.short ? mx.roles.fFigS : modelData.heavy ? mx.roles.fSmallM : mx.roles.fSmall
                           color: modelData.strong ? mx.roles.ink : mx.roles.ink3 }
                    Text { id: mk; visible: !!modelData.mark; x: Math.min(tx.x + tx.width + mx.roles.g(3), parent.width - width - mx.roles.g(3)); anchors.verticalCenter: parent.verticalCenter
                           text: modelData.mark || ""; font: mx.roles.fFigS; color: mx.roles.ink }
                    MouseArea { anchors.fill: parent; enabled: mx.interactive && modelData.tap === true; cursorShape: Qt.PointingHandCursor
                                onClicked: mx.family ? mx.pairActivated(parent.xId, parent.yId) : mx.cellActivated(rw.modelData.id, index) }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: mx.roles.hw; color: mx.roles.hair }
        }
    }
    Repeater {
        model: mx.roles && mx.short && mx.keyText !== "" ? 1 : 0
        delegate: Text { width: mx.width; topPadding: mx.roles.g(6); wrapMode: Text.WordWrap; text: mx.keyText; font: mx.roles.fSmall; color: mx.roles.ink3 }
    }
}
