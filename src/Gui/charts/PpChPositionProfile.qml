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

// PPCHPOSITIONPROFILE — THE P1–P8 POSITION PROFILE, the ONE form the Posture and Wrist sections draw
// (docs/design/posture_chart_design.md §5.2, docs/design/wrist_motion_into_charts.md §4 B): one row per reading, the
// eight positions as a discrete x axis, the change from address on y. Per cell: the band as a wash (the chart wash, or
// the row's metric colour: `metricWash`), the fault lines as hairlines; This shot: this swing's dot in the row's hue,
// joined, outside the band a warn mark AND a word under its number, never colour alone; the Against ghost as hollow
// points on a dashed line; the curve behind on demand (ROW.trace); Session: the session's box per position
// (PpChBoxCell → PpBoxPlot, this shot ringed), the median and how wide the middle half is under it. The numbers under
// every column: the figure or "-"; a value a gate left empty says the gate's words as a link (trustClicked); a reading
// under a ⚠ keeps its figure and says the ⚠'s few words under it; a SECOND READING (ROW.hasAlt) on its own smaller line
// under the numbers; a numbers-only row (ROW.numbersOnly) has no plot. The pole words at the row's right. A column head
// is a seek target (columnActivated); the playhead's position is the picked column through every row.
// The section's presenter row goes through `rowOf(row, mode)` into this form's ROW (PpChPostureProfile and
// PpChWristProfile each map their presenter's; every word, number and scale is still the presenter's):
//   ROW  = { key, dof, hueKey, name, dot (the label's swatch), aboveSub (the narrow label's line), sideY (design
//            units), sideLines: [{ text, font, ink (role names), name (objectName), keep (shown when "") }],
//            lo, hi, unit, numbersOnly, hasAlt, poleTop, poleBottom, trace: [{ x, v }], boxes: [BOXCELL], cells: [CELL] }
//   CELL = { tag, value (the line; null breaks it), mark ("" | "dot" | "ring" | "out" | "fault"), markName, banded,
//            bandLo, bandHi, faultHi, faultLo (edges | null), ghost, text, dim, outside, trust, word, wordShort (optional:
//            used while the columns are narrow), warned, warnText, alt, altText, altAvailable, altWarnText }
// objectNames, from `prefix`: <prefix>Row (+ key, dof); <prefix>Col (+ tag); <prefix>Seek; <prefix>Cell (+ tag, text,
// warned, outside, rowKey); <prefix>Why:<subject> on a gated value; <prefix>Warn; <prefix>AltValue (+ tag, rowKey, text,
// warned) and <prefix>AltWarn; <prefix>Fault; each mark its CELL.markName.
Item {
    id: pf
    property string prefix: "chProfile"
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null               // ChartsRelationsPresenter: ticks(lo, hi, n, unit)
    property var rows: []
    property var positions: []
    property int selected: -1
    property string mode: "shot"                 // "shot" | "session"
    property bool interactive: true
    signal columnActivated(int index)
    // an empty value a gate left (CELL.trust): the gate's words stand in it and open the reason
    signal trustClicked(string subject)

    // the section's presenter row → ROW (see the header)
    property var rowOf: function (r, m) { return r }
    // the geometry, in design units, and the look of the optional parts
    property real labelU: 176
    property real poleU: 64
    property real plotU: 72
    property real numU: 50
    property real gapU: 12
    property real bandW: 0.68                    // the band's width, of a column
    property bool metricWash: true               // the band in the row's metric colour (else the chart wash)
    property bool elide: true                    // a head's name and a cell's word elide (else run on)
    property bool poleWrap: true                 // the pole word wraps in its gutter

    readonly property bool ok: !!roles
    readonly property bool session: mode === "session"
    readonly property var rowsN: { const m = mode, f = rowOf; return ok ? (rows || []).map(function (r) { return f(r, m) }) : [] }
    readonly property bool sideLabel: ok && roles.atLeast(width, "medium")
    readonly property real labelW: sideLabel ? roles.u(labelU) : 0
    readonly property real poleW: ok ? roles.u(poleU) : poleU
    readonly property real gutY: ok ? roles.u(30) : 30
    readonly property real plotX: labelW + gutY
    readonly property real plotW: Math.max(ok ? roles.u(160) : 160, width - plotX - poleW)
    readonly property real colW: plotW / 8
    readonly property bool tagNames: ok && colW >= roles.u(78)
    readonly property bool longWords: ok && colW >= roles.u(96)
    readonly property real headH: ok ? roles.u(tagNames ? 36 : 22) : 22
    readonly property real plotH: ok ? roles.u(plotU) : plotU
    readonly property real numH: ok ? roles.u(numU) : numU
    readonly property real rowGap: ok ? roles.g(gapU) : Theme.gap(gapU)
    readonly property real altH: ok ? roles.u(40) : 40     // the second reading's line (ROW.hasAlt)
    function cx(i) { return plotX + (i + 0.5) * colW }
    readonly property real aboveH: sideLabel || !ok ? 0 : roles.u(22)
    function rowH(r) { return !ok ? 0 : aboveH + (r.numbersOnly ? roles.u(24) : plotH + numH) + rowGap + (r.hasAlt ? altH : 0) }
    readonly property var rowYs: { let y = headH; const ys = []; for (const r of rowsN) { ys.push(y); y += rowH(r) } ys.push(y); return ys }
    implicitHeight: rowYs[rowYs.length - 1]
    height: implicitHeight

    // the playhead's position: one column wash through the whole profile
    Rectangle { visible: pf.ok && pf.selected >= 0 && pf.selected < 8 && !pf.roles.isPrint
                x: pf.cx(pf.selected) - pf.colW / 2; width: pf.colW; y: 0; height: pf.height
                color: pf.ok ? pf.roles.pickWash : "transparent" }
    // the column heads: the P tag (and "now" on the playhead's), its name when there is room; a press seeks
    Repeater {
        model: pf.ok ? pf.positions : []
        delegate: Item {
            id: ch
            required property var modelData
            required property int index
            objectName: pf.prefix + "Col"
            readonly property string tag: modelData.tag || ""
            readonly property bool on: index === pf.selected
            x: pf.cx(index) - pf.colW / 2; y: 0; width: pf.colW; height: pf.headH
            Column {
                x: pf.roles.u(3); y: pf.roles.u(2); width: parent.width - pf.roles.u(6)
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
                       text: ch.tag + (ch.on ? "  now" : ""); font: ch.on ? pf.roles.fSmallM : pf.roles.fSmall
                       color: ch.on ? pf.roles.ink : (ch.modelData.available ? pf.roles.ink2 : pf.roles.ink3) }
                Text { visible: pf.tagNames; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap
                       elide: pf.elide ? Text.ElideRight : Text.ElideNone
                       text: ch.modelData.name || ""; font: pf.roles.fMicro; color: pf.roles.ink3 }
            }
            MouseArea {
                objectName: pf.prefix + "Seek"
                readonly property string tag: ch.tag
                anchors.fill: parent
                enabled: pf.interactive && ch.modelData.available === true
                cursorShape: Qt.PointingHandCursor
                onClicked: pf.columnActivated(ch.index)
            }
        }
    }
    Rectangle { visible: pf.ok; x: pf.plotX; y: pf.headH - (pf.ok ? pf.roles.u(4) : 4); width: pf.plotW; height: pf.ok ? pf.roles.hw : 1; color: pf.ok ? pf.roles.hair : "grey" }

    Repeater {
        model: pf.ok ? pf.rowsN : []
        delegate: Item {
            id: rw
            required property var modelData
            required property int index
            objectName: pf.prefix + "Row"
            readonly property var r: modelData
            readonly property string key: r.key || ""
            readonly property string dof: r.dof || ""
            readonly property var cells: r.cells || []
            readonly property color hue: pf.roles.hue(r.hueKey || "")
            readonly property color washC: pf.metricWash ? pf.roles.hueA(r.hueKey || "", Math.max(0.10, pf.roles.envAlpha * 1.4)) : pf.roles.wash
            readonly property var ty: pf.roles
            x: 0; y: pf.rowYs[index]; width: pf.width; height: pf.rowH(r)
            readonly property real topY: pf.aboveH
            readonly property real lo: r.lo
            readonly property real hi: r.hi
            function vy(v) { return topY + pf.plotH * (1 - (v - lo) / Math.max(1e-9, hi - lo)) }
            readonly property var yt: pf.ticker && !r.numbersOnly ? pf.ticker.ticks(lo, hi, 3, r.unit || "") : []

            // ── the label: beside at medium and up, above below that ──
            Row {
                visible: !pf.sideLabel
                spacing: rw.ty.g(6)
                Rectangle { visible: rw.r.dot === true; width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                Text { id: labName; text: rw.r.name || ""; font: rw.ty.fBodyM; color: rw.ty.ink }
                Text { anchors.baseline: labName.baseline; text: rw.r.aboveSub || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
            }
            Column {
                visible: pf.sideLabel
                y: rw.topY + rw.ty.u(rw.r.sideY || 0); width: pf.labelW - rw.ty.u(12)
                spacing: rw.ty.g(2)
                Row { spacing: rw.ty.g(7)
                    Rectangle { visible: rw.r.dot === true; width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: rw.r.name || ""; font: rw.ty.fBodyM; color: rw.ty.ink } }
                Repeater {
                    model: rw.r.sideLines || []
                    delegate: Text {
                        required property var modelData
                        objectName: modelData.name || ""
                        visible: modelData.keep === true || text !== ""
                        width: parent.width; wrapMode: Text.WordWrap
                        text: modelData.text || ""; font: rw.ty[modelData.font || "fSmall"]; color: rw.ty[modelData.ink || "ink3"]
                    }
                }
            }

            // ── the plot ──
            Item {
                visible: !rw.r.numbersOnly
                anchors.fill: parent
                Repeater {
                    model: rw.yt
                    delegate: Item {
                        required property var modelData
                        Rectangle { x: pf.plotX; width: pf.plotW; y: Math.round(rw.vy(modelData.v)); height: rw.ty.hw
                                    color: modelData.v === 0 ? rw.ty.baseline : rw.ty.grid }
                        Text { x: pf.plotX - width - rw.ty.u(5); y: rw.vy(modelData.v) - height / 2; text: modelData.text; font: rw.ty.fFigLabel; color: rw.ty.ink3 }
                    }
                }
                // the band per position, and the fault lines as hairlines
                Repeater {
                    model: rw.r.numbersOnly ? [] : rw.cells
                    delegate: Item {
                        required property var modelData
                        required property int index
                        readonly property var c: modelData
                        readonly property bool banded: c.banded === true
                        x: pf.cx(index) - pf.colW * pf.bandW / 2; width: pf.colW * pf.bandW
                        Rectangle { visible: parent.banded; width: parent.width
                                    y: parent.banded ? rw.vy(parent.c.bandHi) : 0
                                    height: parent.banded ? Math.max(1, rw.vy(parent.c.bandLo) - rw.vy(parent.c.bandHi)) : 0
                                    color: rw.washC }
                        Rectangle { objectName: pf.prefix + "Fault"; visible: rw.ty.fin(parent.c.faultHi)
                                    x: -pf.colW * 0.08; width: parent.width + pf.colW * 0.16; height: Math.max(1, rw.ty.hw)
                                    y: visible ? Math.round(rw.vy(parent.c.faultHi)) : 0; color: rw.ty.fault }
                        Rectangle { objectName: pf.prefix + "Fault"; visible: rw.ty.fin(parent.c.faultLo)
                                    x: -pf.colW * 0.08; width: parent.width + pf.colW * 0.16; height: Math.max(1, rw.ty.hw)
                                    y: visible ? Math.round(rw.vy(parent.c.faultLo)) : 0; color: rw.ty.fault }
                    }
                }
                // Session: the session's box at each position, this shot ringed (the brush's picked shot)
                Repeater {
                    model: rw.r.boxes || []
                    delegate: PpChBoxCell {
                        required property var modelData
                        required property int index
                        vertical: true
                        x: pf.cx(index) - rw.ty.u(12); width: rw.ty.u(24); y: rw.topY; height: pf.plotH
                        lo: rw.lo; hi: rw.hi
                        roles: rw.ty; brush: pf.brush; interactive: pf.interactive
                        box: modelData.box; values: modelData.values || []; fired: []
                        color: rw.hue
                    }
                }
                // the curve behind, the ghost, this swing's line
                Canvas {
                    anchors.fill: parent
                    readonly property var deps: [rw.r, rw.lo, rw.hi, pf.width, pf.mode, rw.hue]
                    onDepsChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        const ty = rw.ty
                        const line = function (xs, ys) {
                            let pen = false
                            ctx.beginPath()
                            for (let i = 0; i < xs.length; ++i) {
                                if (!ty.fin(ys[i])) { pen = false; continue }
                                if (!pen) ctx.moveTo(xs[i], rw.vy(ys[i])); else ctx.lineTo(xs[i], rw.vy(ys[i]))
                                pen = true
                            }
                            ctx.stroke()
                        }
                        const tr = rw.r.trace || []
                        if (tr.length) {
                            ctx.strokeStyle = Qt.alpha(rw.hue, 0.42); ctx.lineWidth = 1.1; ctx.setLineDash([])
                            line(tr.map(function (t) { return pf.plotX + (t.x + 0.5) * pf.colW }), tr.map(function (t) { return t.v }))
                        }
                        const xs = rw.cells.map(function (c, i) { return pf.cx(i) })
                        const gh = rw.cells.map(function (c) { return c.ghost })
                        if (gh.some(function (v) { return ty.fin(v) })) {
                            ctx.strokeStyle = Qt.alpha(rw.hue, 0.75); ctx.lineWidth = 1.2; ctx.setLineDash(ty.dash([4, 3]))
                            line(xs, gh)
                            ctx.setLineDash([])
                        }
                        if (!pf.session) {
                            ctx.strokeStyle = rw.hue; ctx.lineWidth = 1.7
                            line(xs, rw.cells.map(function (c) { return c.value }))
                        }
                    }
                }
                // the ghost's points: hollow, in the hue
                Repeater {
                    model: rw.r.numbersOnly ? [] : rw.cells
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        visible: rw.ty.fin(modelData.ghost)
                        width: rw.ty.u(6); height: width; radius: rw.ty.square ? 0 : width / 2
                        x: pf.cx(index) - width / 2; y: visible ? rw.vy(modelData.ghost) - height / 2 : 0
                        color: rw.ty.surface; border.width: 1; border.color: rw.hue
                    }
                }
                // this swing's positions (This shot): a dot in the hue; outside the band a warn mark (its word is under
                // the number): a ring around a small dot, a hollow ring, or filled for a fault
                Repeater {
                    model: pf.session || rw.r.numbersOnly ? [] : rw.cells
                    delegate: Item {
                        required property var modelData
                        required property int index
                        objectName: modelData.markName || ""
                        readonly property string mark: modelData.mark || ""
                        visible: mark !== ""
                        x: pf.cx(index); y: visible ? rw.vy(modelData.value) : 0
                        Rectangle { visible: parent.mark === "dot"; width: rw.ty.u(8); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2; color: rw.hue }
                        Rectangle { visible: parent.mark !== "dot"; width: rw.ty.u(parent.mark === "ring" ? 12 : 11); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2
                                    color: parent.mark === "fault" ? rw.ty.warn : rw.ty.surface; border.width: 2; border.color: rw.ty.warn
                                    Rectangle { visible: parent.parent.mark === "ring"; anchors.centerIn: parent; width: rw.ty.u(4); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue } }
                    }
                }
                // the poles: the direction words at the right edge
                Text { x: pf.plotX + pf.plotW + rw.ty.u(8); y: rw.topY
                       width: pf.poleWrap ? pf.poleW - rw.ty.u(8) : implicitWidth; wrapMode: pf.poleWrap ? Text.WordWrap : Text.NoWrap
                       text: rw.r.poleTop || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
                Text { x: pf.plotX + pf.plotW + rw.ty.u(8); y: rw.topY + pf.plotH - height; text: rw.r.poleBottom || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
                // the numbers line: the figure or "-" (a gated value: the gate's words, a link), then the word, then the ⚠
                Repeater {
                    model: rw.r.numbersOnly ? [] : rw.cells
                    delegate: Column {
                        id: nc
                        required property var modelData
                        required property int index
                        objectName: pf.prefix + "Cell"
                        readonly property var c: modelData
                        readonly property string rowKey: rw.key
                        readonly property string tag: c.tag || ""
                        readonly property string text: c.text || ""
                        readonly property bool warned: c.warned === true      // the ⚠ drawn: only on a figure
                        readonly property bool outside: c.outside === true
                        x: pf.cx(index) - width / 2; y: rw.topY + pf.plotH + rw.ty.u(5); width: pf.colW - rw.ty.u(4)
                        Text {
                            id: nv
                            readonly property bool why: !!nc.c.trust
                            objectName: why ? pf.prefix + "Why:" + nc.c.trust : ""
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            wrapMode: why ? Text.WordWrap : Text.NoWrap
                            text: why ? nc.text + " ›" : nc.text
                            font: why ? rw.ty.fSmall : rw.ty.fFigS
                            color: why ? rw.ty.link : nc.outside ? rw.ty.warn : nc.c.dim ? rw.ty.ink3 : rw.ty.ink
                            MouseArea { enabled: nv.why && pf.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: pf.trustClicked(nc.c.trust) }
                            Rectangle { visible: nc.outside; anchors.horizontalCenter: parent.horizontalCenter; y: parent.height - 1; width: parent.contentWidth; height: 1; color: rw.ty.warn }
                        }
                        Text { visible: text !== ""; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap
                               elide: pf.elide ? Text.ElideRight : Text.ElideNone
                               text: (pf.longWords || nc.c.wordShort === undefined ? nc.c.word : nc.c.wordShort) || ""
                               font: rw.ty.fSmall; color: nc.outside ? rw.ty.warn : rw.ty.ink3 }
                        Text { objectName: pf.prefix + "Warn"; visible: nc.warned; width: parent.width; horizontalAlignment: Text.AlignHCenter
                               wrapMode: Text.NoWrap; elide: Text.ElideRight
                               text: nc.c.warnText || ""; font: rw.ty.fSmall; color: rw.ty.ink2 }
                    }
                }
                // the second reading: its own line under the numbers, smaller, with its own ⚠ words
                Repeater {
                    model: rw.r.hasAlt && !rw.r.numbersOnly ? rw.cells : []
                    delegate: Column {
                        id: ac
                        required property var modelData
                        required property int index
                        readonly property var c: modelData
                        visible: c.alt === true
                        x: pf.cx(index) - width / 2; y: rw.topY + pf.plotH + pf.numH - rw.ty.u(4); width: pf.colW - rw.ty.u(4)
                        Text { objectName: pf.prefix + "AltValue"
                               readonly property string tag: ac.c.tag || ""
                               readonly property string rowKey: rw.key
                               readonly property bool warned: !!ac.c.altWarnText
                               width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap; elide: Text.ElideRight
                               text: ac.c.altText || ""; font: rw.ty.fSmall; color: ac.c.altAvailable ? rw.ty.ink2 : rw.ty.ink3 }
                        Text { objectName: pf.prefix + "AltWarn"; visible: !!ac.c.altWarnText; width: parent.width; horizontalAlignment: Text.AlignHCenter
                               wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                               text: ac.c.altWarnText || ""; font: rw.ty.fMicro; color: rw.ty.ink3 }
                    }
                }
            }
            // a numbers-only row: the figures alone (a gated value: the gate's words, a link)
            Repeater {
                model: rw.r.numbersOnly ? rw.cells : []
                delegate: Text {
                    id: cv
                    required property var modelData
                    required property int index
                    readonly property bool why: !!modelData.trust
                    objectName: why ? pf.prefix + "Why:" + modelData.trust : ""
                    x: pf.cx(index) - width / 2; y: rw.topY + rw.ty.u(3); width: pf.colW - rw.ty.u(4); horizontalAlignment: Text.AlignHCenter
                    wrapMode: why ? Text.WordWrap : Text.NoWrap
                    text: why ? (modelData.text || "") + " ›" : (modelData.text || "")
                    font: why ? rw.ty.fSmall : rw.ty.fFigS
                    color: why ? rw.ty.link : modelData.dim ? rw.ty.ink3 : rw.ty.ink2
                    MouseArea { enabled: cv.why && pf.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: pf.trustClicked(cv.modelData.trust) }
                }
            }
        }
    }
}
