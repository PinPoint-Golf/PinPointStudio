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

// HISTORYSTRIP — "Across sessions": per session, its shots as clean marks and a small box plot in the
// grammar of the run's PpBoxStrip (Q1–Q3 box, median rule, whiskers to the furthest shots within
// 1.5 × IQR), its MEDIAN set as a small number beside the box (this session's in ink, the rest ink3;
// only this session's when the columns are too narrow for every label); sessions in date order on the
// same value axis as the run. A session with no reading of this measure is a centred "-", never a blank.
// EXAMINED (the pointer over a column, or `examinedColumn` for a render) that session's box shows its
// full reading (PpBoxReading: whisker ends, quartiles, median, shot and outlier counts) over the strip,
// and the other columns' resting median labels that it would cross step aside. The hover is a passive
// HoverHandler per column: the click that opens a session still works. Print / not interactive: rest only.
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   history    detail.history  (columns[{label, values[{f, stack}], fQ1, fQ3, fMedian, medianText, current}])
//   scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
//   unit       a symbol unit (°) to set on the median labels; "" for a worded unit
//   decimals   the axis's precision, for the examined reading
//   examinedColumn   a column to show examined without a pointer (renders); -1 = follow the pointer
//   rightRoom  how far a reading may reach past the strip's right edge before it opens to the left
//   readonly examIdx, examining
//   title, roles, brush, medium
//   signal sessionActivated(sessionDir)
Item {
    id: hs
    objectName: "sdHistoryStrip"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var history: null
    property var scale: null
    property string title: "Across sessions"
    property string unit: ""
    property int decimals: h.decimals !== undefined ? h.decimals : 0
    property int examinedColumn: -1
    property real rightRoom: 1e6
    property real gutterX: 1e6           // where the zone-label gutter starts, in this strip's x; and its labels' heights
    property var gutterYs: []
    property int hoverColumn: -1
    readonly property int examIdx: {
        if (medium === "print") return -1
        const i = examinedColumn >= 0 ? examinedColumn : (interactive ? hoverColumn : -1)
        return i >= 0 && i < cols.length && (cols[i].values || []).length ? i : -1
    }
    readonly property bool examining: examIdx >= 0
    readonly property var keyKinds: ["clean", "fault", "corridor"]
    signal sessionActivated(string sessionDir)
    readonly property var h: history || ({})
    readonly property var cols: h.columns || []
    // A column's marks stack LEFT of its centre, the bar and median rule sit right of it; the stack pitch
    // shrinks to keep a crowded session inside its own column, and the date labels thin out (every
    // labelEvery-th, counted from this session so it is always labelled) when the columns are narrow.
    readonly property real colW: cols.length ? width / cols.length : width
    readonly property int curIdx: { for (let i = 0; i < cols.length; ++i) if (cols[i].current) return i; return cols.length - 1 }
    readonly property int labelEvery: Math.max(1, Math.ceil(roles.u(44) / Math.max(1, colW)))
    // the column's box: just right of its centre line, the median's label just right of the box
    readonly property real boxL: roles.u(3)
    readonly property real boxW: roles.u(medium === "print" ? 6 : 8)
    readonly property real lw: medium === "print" ? roles.hw : 1
    function maxStackOf(c) { const vs = c.values || []; let m = 0; for (let i = 0; i < vs.length; ++i) m = Math.max(m, vs[i].stack || 0); return m }
    function pitchOf(c) { return Math.min(roles.u(6), Math.max(roles.u(2), (colW * 0.5 - roles.u(8)) / Math.max(1, maxStackOf(c)))) }
    // how far a column's marks reach left of its centre
    function marksLeft(c) { return (c.values || []).length ? roles.u(4) + maxStackOf(c) * pitchOf(c) + roles.markFired / 2 : 0 }
    function medianLabel(c) { return (c.values || []).length ? roles.fig(c.medianText) + unit : "" }
    // EVERY median label, or only this session's: each must end clear of the next column's marks
    // (the last may reach into the gutter before the zone labels)
    TextMetrics { id: labelM; font: hs.roles.fFigLabel; text: { let w = ""; for (let i = 0; i < hs.cols.length; ++i) { const l = hs.medianLabel(hs.cols[i]); if (l.length > w.length) w = l } return w } }
    // the whisker ends off a column's own shots, by the box's 1.5 × IQR rule
    function whiskersOf(c) {
        const fs = (c.values || []).map(v => v.f).filter(f => isFinite(f)).sort((a, b) => a - b)
        const iqr = c.fQ3 - c.fQ1
        const lo = fs.filter(f => f >= c.fQ1 - 1.5 * iqr - 1e-9), hi = fs.filter(f => f <= c.fQ3 + 1.5 * iqr + 1e-9)
        return { lo: lo.length ? Math.min(lo[0], c.fQ1) : c.fQ1, hi: hi.length ? Math.max(hi[hi.length - 1], c.fQ3) : c.fQ3,
                 n: fs.length, out: fs.length - fs.filter(f => f >= c.fQ1 - 1.5 * iqr - 1e-9 && f <= c.fQ3 + 1.5 * iqr + 1e-9).length }
    }
    function restX(i) { return (i + 0.5) * colW + boxL + boxW + roles.u(3) }
    readonly property bool allLabels: {
        const right = boxL + boxW + roles.u(3) + labelM.advanceWidth
        for (let i = 0; i < cols.length; ++i) {
            if (!(cols[i].values || []).length) continue
            const limit = i + 1 < cols.length ? colW - marksLeft(cols[i + 1]) - roles.u(2) : colW / 2 + roles.u(8)
            if (right > limit) return false
        }
        return true
    }
    implicitHeight: scale ? scale.plotTop + scale.plotHeight + roles.u(26) : 0

    Text { text: hs.title; font: hs.roles.fSmall; color: hs.roles.ink }
    PpZonePlot { anchors.fill: parent; roles: hs.roles; scale: hs.scale }
    Repeater {
        model: hs.roles ? (hs.scale ? hs.cols : []) : []
        delegate: Item {
            id: hc
            objectName: "sdHistoryColumn"
            required property var modelData
            required property int index
            readonly property bool current: !!modelData.current
            readonly property real cx: (index + 0.5) * hs.colW
            readonly property real stackPitch: hs.pitchOf(modelData)
            readonly property bool measured: (modelData.values || []).length > 0
            readonly property var wk: hs.whiskersOf(modelData)
            readonly property real fLoW: wk.lo
            readonly property real fHiW: wk.hi
            readonly property real bx: cx + hs.boxL
            readonly property real bc: Math.round(bx + hs.boxW / 2)
            function py(f) { return Math.round(hs.scale.y(f) * 2) / 2 }
            Repeater {
                model: hs.roles ? (hc.modelData.values || []) : []
                delegate: PpMark {
                    required property var modelData
                    roles: hs.roles; fired: false
                    cx: Math.round((hc.cx - hs.roles.u(4) - (modelData.stack || 0) * hc.stackPitch) * 2) / 2; cy: Math.round(hs.scale.y(modelData.f) * 2) / 2
                }
            }
            Item {
                visible: hc.measured
                Rectangle { objectName: "sdHistoryWhisker"; x: hc.bc - hs.lw / 2; width: hs.lw; y: hc.py(hc.fHiW); height: Math.max(0, hc.py(hc.modelData.fQ3) - hc.py(hc.fHiW)); color: hs.roles.ink2 }
                Rectangle { x: hc.bc - hs.lw / 2; width: hs.lw; y: hc.py(hc.modelData.fQ1); height: Math.max(0, hc.py(hc.fLoW) - hc.py(hc.modelData.fQ1)); color: hs.roles.ink2 }
                Rectangle { x: hc.bc - hs.roles.u(2); width: hs.roles.u(4); height: hs.lw; y: hc.py(hc.fHiW) - hs.lw / 2; color: hs.roles.ink2 }
                Rectangle { x: hc.bc - hs.roles.u(2); width: hs.roles.u(4); height: hs.lw; y: hc.py(hc.fLoW) - hs.lw / 2; color: hs.roles.ink2 }
                Rectangle { objectName: "sdHistoryBox"; x: hc.bx; width: hs.boxW; y: hc.py(hc.modelData.fQ3); height: Math.max(hs.lw, hc.py(hc.modelData.fQ1) - hc.py(hc.modelData.fQ3))
                            color: Qt.alpha(hs.roles.ink3, 0.10); border.width: hs.lw; border.color: hs.roles.ink2 }
                Rectangle { objectName: "sdHistoryMedian"; x: hc.bx - hs.roles.u(1); width: hs.boxW + 2 * hs.roles.u(1); y: Math.round(hs.scale.y(hc.modelData.fMedian)) - 0.75; height: hs.medium === "print" ? 1 : 1.5; color: hs.roles.ink }
                Text {
                    objectName: "sdHistoryMedianLabel"
                    // examined, the reading states it; a resting label the reading would cross steps aside
                    visible: (hc.current || hs.allLabels) && hc.index !== hs.examIdx
                             && !(hs.examining && hs.restX(hc.index) < reading.x1 && hs.restX(hc.index) + width > reading.x0)
                    text: hs.medianLabel(hc.modelData); font: hs.roles.fFigLabel; color: hc.current ? hs.roles.ink : hs.roles.ink3
                    x: hc.bx + hs.boxW + hs.roles.u(3); y: Math.round(hs.scale.y(hc.modelData.fMedian)) - height / 2
                }
            }
            // no reading of this measure in that session: "-", the panel's NA, never a blank column
            Text {
                objectName: "sdHistoryNoReading"
                visible: !hc.measured
                text: "-"; font: hs.roles.fFig; color: hs.roles.ink3
                x: hc.cx - width / 2; y: hs.scale.plotTop + hs.scale.plotHeight / 2 - height / 2
            }
            Text { visible: (hs.curIdx - hc.index) % hs.labelEvery === 0; text: hc.modelData.label; font: hs.roles.fFigLabel; color: hc.modelData.current ? hs.roles.ink : hs.roles.ink3
                   x: hc.cx - width / 2; y: hs.scale.plotTop + hs.scale.plotHeight + hs.roles.u(8) }
            MouseArea {
                enabled: hs.interactive && !hc.modelData.current; visible: enabled
                x: hc.cx - Math.min(hs.roles.u(20), hs.colW / 2); width: Math.min(hs.roles.u(40), hs.colW); height: hs.height; cursorShape: Qt.PointingHandCursor
                onClicked: hs.sessionActivated(hc.modelData.sessionDir || "")
            }
        }
    }
    // the examined session's full reading, over every column
    PpBoxReading {
        id: reading
        anchors.fill: parent
        readonly property var c: hs.examining ? hs.cols[hs.examIdx] : null
        readonly property var wk: c ? hs.whiskersOf(c) : null
        visible: !!c && ok && placed.length > 0
        roles: hs.roles; scale: hs.scale; decimals: hs.decimals; unit: hs.unit
        edgeL: c ? (hs.examIdx + 0.5) * hs.colW + hs.boxL : 0; edgeR: edgeL + hs.boxW
        roomLeft: 0; roomRight: hs.width + hs.rightRoom; gutterX: hs.gutterX; gutterYs: hs.gutterYs
        items: c ? [{ f: wk.hi }, { f: c.fQ3 }, { f: c.fMedian, strong: true }, { f: c.fQ1 }, { f: wk.lo }] : []
        footer: !c ? "" : wk.n + (wk.n === 1 ? " shot" : " shots") + (wk.out ? " · " + wk.out + (wk.out === 1 ? " outlier" : " outliers") : "")
    }
    // the hover that examines a column: passive, so the column's click (open that session) still works
    Repeater {
        model: hs.roles && hs.interactive && hs.medium !== "print" ? hs.cols.length : 0
        delegate: Item {
            required property int index
            x: index * hs.colW; width: hs.colW; height: hs.height
            HoverHandler {
                onHoveredChanged: if (hovered) hs.hoverColumn = parent.index; else if (hs.hoverColumn === parent.index) hs.hoverColumn = -1
            }
        }
    }
}
