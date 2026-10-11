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

// BARCODE — C's Watching figure: condition × shot. Each row IS a PpShotStrip (the same drawing as every
// other strip, at a wider pitch); a firings-per-shot row closes it, and the picked shot is one column
// through the whole figure. Names inline from compact up (else above their row); this shot's value from
// medium up, else FOLDED onto the row's name line (as the ledger folds); sidenotes in a right margin at
// wide (else the register puts a PpNotes block below).
// rows: source.watching ([{id, name, recurrence}]); readout supplies each row's ticks and value.
// Round 4:
//   • PRINT: the shot-number head, every row and the per-shot row are atoms of their own, and the head is
//     the list's head (printGroup), so the PpPager repeats it when the rows run onto the next page.
//   • rowsAll: give the whole list when `rows` is a slice; "firings per shot" counts from all of it.
//   • ANY LENGTH (D7): the cell narrows to fit the shots (shot numbers thin as it does), down to a
//     minimum; past that the barcode WRAPS into bands of shots (1–30, 31–50), each with its own head and
//     per-shot row. It never clips.
Item {
    id: mx
    objectName: "sdBarcode"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property bool conditionsInteractive: true    // false: the condition names take no click and show no pointer (a
                                                 // persistent cast picks shots, never opens a condition); hover still brushes
    property var rows: []
    property var rowsAll: rows                  // the whole list, when rows is a slice (per-shot counts)
    property var readout: null
    property var source: null
    property bool showMarginNotes: true
    property bool showValues: roles.shows("barcodeValue", width)
    property bool foldValues: true              // below showValues, the value joins the row's name line
    property string printGroup: "barcode"
    property bool atom: false                   // made of atoms: head, rows, per-shot row
    readonly property var keyKinds: ["bar", "tick", "stub"]
    signal conditionActivated(string id)
    signal conditionHovered(string id)
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    function rowOf(x) {
        const c = roles.byId((readout && readout.conditions) || [], x.id)
        return { id: x.id, name: x.name, rec: x.recurrence, ticks: c ? (c.ticks || []) : [], value: c && c.valueText ? c.valueText : "-",
                 fig: c ? roles.fig(c.valueText) : "-", fired: !!c && c.stateKind === "fired", na: !c || c.stateKind === "notAssessable" }
    }
    readonly property var data_: (rows || []).map(x => rowOf(x))
    readonly property var dataAll: (rowsAll || []).map(x => rowOf(x))
    readonly property int nShots: {
        let n = readout && readout.shotCount !== undefined ? readout.shotCount : 0
        for (let i = 0; i < dataAll.length; ++i) n = Math.max(n, dataAll[i].ticks.length)
        return n
    }
    readonly property var shotIds: {
        for (let i = 0; i < dataAll.length; ++i) if (dataAll[i].ticks.length >= nShots) return dataAll[i].ticks.map((t, k) => t.shotId !== undefined ? t.shotId : k + 1)
        const o = []; for (let k = 0; k < nShots; ++k) o.push(k + 1); return o
    }
    readonly property int picked: readout && readout.shotIndex !== undefined ? readout.shotIndex : (brush ? shotIds.indexOf(brush.pickedShot) : -1)
    readonly property int hoverIdx: brush && brush.hoveredShot > 0 && brush.hoveredShot !== brush.pickedShot ? shotIds.indexOf(brush.hoveredShot) : -1
    readonly property bool margin: showMarginNotes && roles.atLeast(width, "wide")
    // names stand inline (right-aligned before the bars) from compact up, as wide as the longest name line
    // needs (with its folded value); when that would take more than ~42 % of the figure, they go above
    FontMetrics { id: fmName; font: mx.roles.fSmallM }
    readonly property bool folding: !showValues && foldValues
    readonly property real nameNeed: {
        if (fmName.font.pixelSize <= 0) return 0     // reads the font, so the measure follows a theme change: advanceWidth() is a call, which a binding cannot track
        let m = 0
        for (let i = 0; i < data_.length; ++i) m = Math.max(m, fmName.advanceWidth(data_[i].name + (folding ? "    " + data_[i].value : "")))
        return Math.ceil(m) + roles.u(4)
    }
    readonly property bool inline: roles.shows("barcodeNames", width) && nameNeed <= Math.max(roles.u(184), bodyW * 0.42)
    readonly property real noteW: margin ? roles.u(260) : 0
    readonly property real bodyW: width - (margin ? noteW + roles.u(30) : 0)
    readonly property real nameW: inline ? Math.max(roles.u(184), nameNeed) : 0
    readonly property real countW: roles.u(58)
    readonly property real valW: showValues ? roles.u(150) : 0
    readonly property real cellsX: inline ? nameW + roles.u(14) : 0
    readonly property real avail: Math.max(roles.u(60), bodyW - cellsX - roles.u(14) - countW - valW)
    readonly property real maxCell: roles.u(29)
    readonly property real minCell: roles.u(12)
    // bands are balanced (45 shots in three bands of 15, never 21 + 21 + 3)
    readonly property int perSegMax: nShots <= 0 ? 1 : Math.max(1, Math.min(nShots, Math.floor(avail / minCell)))
    readonly property int perSeg: nShots <= 0 ? 1 : Math.ceil(nShots / Math.ceil(nShots / perSegMax))
    readonly property var segments: { const o = []; for (let f = 0; f < Math.max(1, nShots); f += perSeg) o.push({ from: f, to: Math.min(nShots, f + perSeg) }); return o }
    readonly property real cellW: Math.max(minCell, Math.min(maxCell, avail / Math.max(1, Math.min(nShots, perSeg))))
    readonly property real countX: cellsX + Math.min(nShots, perSeg) * cellW + roles.u(14)
    readonly property real valueX: countX + countW
    readonly property int labelStep: roles.shotLabelStep(cellW, roles.u(13))
    readonly property real nameLineH: inline ? 0 : -1               // (names above: each row measures its own)
    readonly property real rowP: medium === "print" ? roles.u(18) : roles.u(21)
    readonly property real headH: roles.u(20)
    readonly property real footH: roles.u(26)
    readonly property real barH: roles.u(13)
    readonly property var perShot: {
        const out = []
        for (let s = 0; s < nShots; ++s) {
            let n = 0, k = 0
            for (let i = 0; i < dataAll.length; ++i) { const t = dataAll[i].ticks[s]; if (t && t.state === "fired") ++n; if (t && (t.state === "fired" || t.state === "clean")) ++k }
            out.push(k ? String(n) : "-")
        }
        return out
    }
    function valueHtml(r) {
        return "<span style=\"font-family:'" + roles.r.figureFamily + "'; color:" + roles.hex(r.fired ? roles.warn : roles.ink3) + ";\">" + roles.esc(r.value) + "</span>"
    }
    implicitHeight: Math.max(body.height, margin ? sideNotes.height + headH : 0)
    height: implicitHeight

    Column {
        id: body
        width: mx.bodyW
        spacing: mx.roles.u(10)
        Repeater {
            model: mx.roles ? mx.segments : []
            delegate: Item {
                id: seg
                required property var modelData
                required property int index
                readonly property int from: modelData.from
                readonly property int to: modelData.to
                width: body.width; height: segCol.height
                Rectangle {
                    objectName: "sdPickColumn"
                    visible: mx.picked >= seg.from && mx.picked < seg.to
                    x: mx.cellsX + (mx.picked - seg.from) * mx.cellW; y: -mx.roles.u(3)
                    width: mx.cellW; height: segCol.height - mx.footH + mx.roles.u(26)
                    color: mx.roles.pickWash
                }
                Rectangle {
                    visible: mx.hoverIdx >= seg.from && mx.hoverIdx < seg.to
                    x: mx.cellsX + (mx.hoverIdx - seg.from) * mx.cellW; y: -mx.roles.u(3)
                    width: mx.cellW; height: segCol.height - mx.footH + mx.roles.u(26)
                    color: mx.roles.hoverWash
                }
                Column {
                    id: segCol
                    width: parent.width
                    // ── the head: shot numbers (thinned as the cell narrows), "fired", "this shot" ──
                    Item {
                        objectName: "sdBarcodeHead"
                        property bool atom: true
                        property bool keepNext: true
                        width: segCol.width; height: mx.headH
                        Text { visible: mx.inline; width: mx.nameW; horizontalAlignment: Text.AlignRight
                               text: mx.segments.length > 1 ? "shots " + (seg.from + 1) + "–" + seg.to : "shot"; font: mx.roles.fLabel; color: mx.roles.ink3 }
                        Repeater {
                            model: seg.to - seg.from
                            delegate: Text {
                                required property int index
                                readonly property int i: seg.from + index
                                visible: mx.roles.axisLabelShown(i, mx.nShots, mx.labelStep, [seg.from, seg.to - 1, mx.picked, mx.hoverIdx])
                                x: mx.cellsX + index * mx.cellW + (mx.cellW - width) / 2
                                text: String(i + 1); font: mx.roles.fFigLabel
                                color: i === mx.picked || i === mx.hoverIdx ? mx.roles.ink : mx.roles.ink3
                            }
                        }
                        Text { x: mx.countX; text: "fired"; font: mx.roles.fLabel; color: mx.roles.ink3 }
                        Text { visible: mx.showValues; x: mx.valueX; text: "this shot"; font: mx.roles.fLabel; color: mx.roles.ink3 }
                    }
                    // ── one atom per condition ──
                    Repeater {
                        model: mx.data_
                        delegate: Item {
                            id: row
                            objectName: "sdBarcodeRow"
                            property bool atom: true
                            required property var modelData
                            required property int index
                            readonly property var r: modelData
                            readonly property string conditionId: r.id
                            readonly property bool hot: mx.brush && mx.brush.hoveredCondition === r.id
                            readonly property bool fold: !mx.showValues && mx.foldValues
                            readonly property real nameH: mx.inline ? 0 : nm.implicitHeight + mx.roles.u(1)
                            readonly property real barY: nameH + (mx.rowP - mx.barH) / 2
                            width: mx.countX + mx.countW + mx.valW; height: mx.rowP + nameH
                            opacity: mx.brush ? mx.brush.conditionOpacity(r.id, mx.roles.dim) : 1
                            Behavior on opacity { NumberAnimation { duration: mx.roles.normal } }
                            PpPulseWash { anchors.fill: parent; roles: mx.roles; brush: mx.brush; conditionId: row.conditionId }
                            Rectangle { anchors.fill: parent; visible: row.hot; color: mx.roles.rowHover }
                            // hover by HoverHandler: the folded (rich-text) name accepts hover and hid it from a
                            // MouseArea beneath (as PpLedgerTable's rows, 11 Oct)
                            HoverHandler {
                                enabled: mx.interactive
                                onHoveredChanged: { if (mx.brush) mx.brush.hoverCondition(row.r.id, hovered); mx.conditionHovered(hovered ? row.r.id : "") }
                            }
                            MouseArea {
                                enabled: mx.interactive; visible: enabled
                                anchors.fill: parent; cursorShape: mx.conditionsInteractive ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: if (mx.conditionsInteractive) mx.conditionActivated(row.r.id)
                            }
                            Text {
                                id: nm
                                width: mx.inline ? mx.nameW : mx.countX + mx.countW
                                horizontalAlignment: mx.inline ? Text.AlignRight : Text.AlignLeft
                                y: mx.inline ? row.barY + (mx.barH - height) / 2 : 0
                                wrapMode: mx.inline ? Text.NoWrap : Text.WordWrap
                                textFormat: row.fold ? Text.RichText : Text.PlainText
                                text: row.fold ? mx.roles.esc(row.r.name) + "&nbsp;&nbsp;" + mx.valueHtml(row.r) : row.r.name
                                font: row.hot ? mx.roles.fSmallM : mx.roles.fSmall; color: mx.roles.ink
                            }
                            PpShotStrip {
                                x: mx.cellsX; y: row.barY
                                roles: mx.roles; brush: mx.brush; medium: mx.medium; interactive: mx.interactive
                                ticks: row.r.ticks.slice(seg.from, seg.to)
                                pitch: mx.cellW; barH: mx.barH; barW: Math.max(3, mx.roles.u(5))
                                pickBand: false
                                onShotActivated: (d, s) => mx.shotActivated(d, s)
                                onShotHovered: s => mx.shotHovered(s)
                            }
                            Text { x: mx.countX; y: row.barY + (mx.barH - height) / 2; text: mx.roles.shortCount(row.r.rec); font: mx.roles.fFigS; color: mx.roles.ink2 }
                            Text { objectName: "sdBarcodeValue"; visible: mx.showValues; x: mx.valueX; y: row.barY + (mx.barH - height) / 2; text: row.r.value; font: mx.roles.fFigS
                                   color: row.r.fired ? mx.roles.warn : mx.roles.ink3 }
                        }
                    }
                    // ── firings per shot, counted from every row (rowsAll) ──
                    Item {
                        property bool atom: true
                        property bool keepPrev: true
                        width: segCol.width; height: mx.footH
                        Rectangle { x: mx.cellsX; y: mx.roles.u(6); width: (seg.to - seg.from) * mx.cellW; height: mx.roles.hw; color: mx.roles.grid; opacity: mx.roles.gridOpacity }
                        Text { visible: mx.inline; width: mx.nameW; horizontalAlignment: Text.AlignRight; y: mx.roles.u(12); text: "firings per shot"; font: mx.roles.fLabel; color: mx.roles.ink3 }
                        Text { visible: !mx.inline; x: mx.countX; y: mx.roles.u(12); text: "per shot"; font: mx.roles.fLabel; color: mx.roles.ink3 }
                        Repeater {
                            model: seg.to - seg.from
                            delegate: Text {
                                required property int index
                                readonly property int i: seg.from + index
                                x: mx.cellsX + index * mx.cellW + (mx.cellW - width) / 2; y: mx.roles.u(12)
                                text: mx.perShot[i] !== undefined ? mx.perShot[i] : "-"; font: mx.roles.fFigLabel
                                color: i === mx.picked ? mx.roles.ink : mx.roles.ink3
                            }
                        }
                    }
                }
            }
        }
    }
    Column {
        id: sideNotes
        visible: mx.margin
        x: mx.width - mx.noteW; y: mx.headH
        width: mx.noteW
        spacing: mx.roles.u(14)
        PpPageKey { width: parent.width; roles: mx.roles; kinds: mx.keyKinds; readout: mx.readout }
        Text { width: parent.width; wrapMode: Text.WordWrap; font: mx.roles.fSmall; color: mx.roles.ink3
               text: "Each row is a condition seen on this session that has not yet recurred enough to be a pattern; each column a shot, in the order struck. The count is firings over the shots it could be measured on." }
        Text { width: parent.width; wrapMode: Text.WordWrap; font: mx.roles.fSmall; color: mx.roles.ink3
               text: mx.source && mx.source.coverageLine ? mx.roles.cap1(mx.source.coverageLine) + "." : "" }
    }
}
