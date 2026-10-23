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

// PPCHPOSTUREPROFILE — THE POSTURE PROFILE (docs/design/posture_chart_design.md §5.2, §5.5): one row per posture
// reading (head height, hip depth, trunk bend, lead knee, trail knee, pelvis height), P1…P8 as a discrete x axis,
// the change from address on y. It is the wrist profile's form (PpChWristProfile), per cell:
//   · the chosen pattern's band as a wash in the row's metric colour (Theme.metricColor(hueKey): identity);
//   · the fault line (where the matching condition fires) as a hairline;
//   · This shot: this swing's dot in the hue, joined; outside the band a RING and its word under the number
//     ("in 7.6"), never colour alone; the Against ghost as hollow points on a dashed line;
//   · Session: the session's box per position (PpChBoxCell → PpBoxPlot, the ONE box plot), this shot ringed,
//     the median and how wide the middle half is under it;
//   · the numbers under each column: the figure, or "-" where nothing was read; a reading under a ⚠ keeps its
//     figure and says the ⚠'s few words under it (the full reason is said once, in the trust fold);
//   · the pole word at the row's right ("in ▲"); the trail knee's row labelled with its style word;
//   · a SECOND READING under a cell where the presenter gives one (trunk bend at P3–P5: the back line, decision 16 of
//     session_posture.h): a smaller figure on its own line under the numbers, "back line −3", and its own ⚠ words —
//     not ringed, no band of its own; a row with one (ROW.hasAlt) is that line taller.
// A column head is a seek target (columnActivated); the playhead's position is the picked column through every row.
// Every word, number and scale is ChartsPosturePresenter's (profile.rows: ROW, positions: POS); QML maps to pixels.
// objectNames: chPostureProfile; each row chPostureRow (+ key); each column head chPostureCol (+ tag); each cell's
// numbers chPostureCell (+ tag, text, warned — the ⚠ drawn —, outside, rowKey); each second reading chPostureAltValue
// (+ tag, rowKey, text, warned) and its words chPostureAltWarn.
Item {
    id: pf
    objectName: "chPostureProfile"
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null               // ChartsRelationsPresenter: ticks(lo, hi, n, unit)
    property var rows: []
    property var positions: []
    property int selected: -1
    property string mode: "shot"                 // "shot" | "session"
    property bool interactive: true
    signal columnActivated(int index)

    readonly property bool ok: !!roles
    readonly property bool session: mode === "session"
    readonly property bool sideLabel: ok && roles.atLeast(width, "medium")
    readonly property real labelW: sideLabel ? roles.u(176) : 0
    readonly property real poleW: ok ? roles.u(64) : 64
    readonly property real gutY: ok ? roles.u(30) : 30
    readonly property real plotX: labelW + gutY
    readonly property real plotW: Math.max(ok ? roles.u(160) : 160, width - plotX - poleW)
    readonly property real colW: plotW / 8
    readonly property bool tagNames: ok && colW >= roles.u(78)
    readonly property real headH: ok ? roles.u(tagNames ? 36 : 22) : 22
    readonly property real plotH: ok ? roles.u(72) : 72
    readonly property real numH: ok ? roles.u(50) : 50
    readonly property real rowGap: ok ? roles.u(12) : 12
    function cx(i) { return plotX + (i + 0.5) * colW }
    function fin(v) { return v !== null && v !== undefined && isFinite(v) }
    readonly property real aboveH: sideLabel || !ok ? 0 : roles.u(22)
    readonly property real rowH: !ok ? 0 : aboveH + plotH + numH + rowGap
    readonly property real altH: ok ? roles.u(40) : 40     // the second reading's line (ROW.hasAlt)
    function rowHOf(r) { return rowH + (r && r.hasAlt ? altH : 0) }
    function rowYOf(i) { let y = headH; for (let k = 0; k < i; ++k) y += rowHOf((rows || [])[k]); return y }
    implicitHeight: rowYOf((rows || []).length)
    height: implicitHeight

    // the playhead's position: one column wash through the whole profile
    Rectangle { visible: pf.ok && pf.selected >= 0 && pf.selected < 8; x: pf.cx(pf.selected) - pf.colW / 2; width: pf.colW; y: 0; height: pf.height
                color: pf.ok ? pf.roles.pickWash : "transparent" }
    // the column heads: the P tag (and "now" on the playhead's), its name when there is room; a press seeks
    Repeater {
        model: pf.ok ? pf.positions : []
        delegate: Item {
            id: ch
            required property var modelData
            required property int index
            objectName: "chPostureCol"
            readonly property string tag: modelData.tag || ""
            readonly property bool on: index === pf.selected
            x: pf.cx(index) - pf.colW / 2; y: 0; width: pf.colW; height: pf.headH
            Column {
                x: pf.roles.u(3); y: pf.roles.u(2); width: parent.width - pf.roles.u(6)
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
                       text: ch.tag + (ch.on ? "  now" : ""); font: ch.on ? pf.roles.fSmallM : pf.roles.fSmall
                       color: ch.on ? pf.roles.ink : (ch.modelData.available ? pf.roles.ink2 : pf.roles.ink3) }
                Text { visible: pf.tagNames; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap; elide: Text.ElideRight
                       text: ch.modelData.name || ""; font: pf.roles.fMicro; color: pf.roles.ink3 }
            }
            MouseArea {
                objectName: "chPostureSeek"
                anchors.fill: parent
                enabled: pf.interactive && ch.modelData.available === true
                cursorShape: Qt.PointingHandCursor
                onClicked: pf.columnActivated(ch.index)
            }
        }
    }
    Rectangle { visible: pf.ok; x: pf.plotX; y: pf.headH - (pf.ok ? pf.roles.u(4) : 4); width: pf.plotW; height: pf.ok ? pf.roles.hw : 1; color: pf.ok ? pf.roles.hair : "grey" }

    Repeater {
        model: pf.ok ? pf.rows : []
        delegate: Item {
            id: rw
            required property var modelData
            required property int index
            objectName: "chPostureRow"
            readonly property var r: modelData
            readonly property string key: r.key || ""
            readonly property var cells: r.cells || []
            readonly property var boxes: pf.session && r.boxes ? r.boxes : []
            readonly property color hue: pf.roles.hue(r.hueKey || r.metricKey || "")
            readonly property color washC: pf.roles.hueA(r.hueKey || r.metricKey || "", Math.max(0.10, pf.roles.envAlpha * 1.4))
            readonly property var ty: pf.roles
            x: 0; y: pf.rowYOf(index); width: pf.width; height: pf.rowHOf(r)
            readonly property real topY: pf.aboveH
            readonly property real lo: pf.fin(r.yLo) ? r.yLo : -1
            readonly property real hi: pf.fin(r.yHi) ? r.yHi : 1
            function vy(v) { return topY + pf.plotH * (1 - (v - lo) / Math.max(1e-9, hi - lo)) }
            readonly property var yt: pf.ticker ? pf.ticker.ticks(lo, hi, 3, r.unit || "") : []

            // ── the label: beside at medium and up, above below that; the trail knee says its style ──
            Row {
                visible: !pf.sideLabel
                spacing: rw.ty.u(6)
                Rectangle { width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                Text { id: labName; text: rw.r.label || ""; font: rw.ty.fBodyM; color: rw.ty.ink }
                Text { anchors.baseline: labName.baseline; text: [rw.r.styleText || "", (rw.r.camera || "") + (rw.r.unit ? ", " + rw.r.unit : "")].filter(function (x) { return x !== "" }).join("   ")
                       font: rw.ty.fSmall; color: rw.ty.ink3 }
            }
            Column {
                visible: pf.sideLabel
                y: rw.topY + rw.ty.u(4); width: pf.labelW - rw.ty.u(12)
                spacing: rw.ty.u(2)
                Row { spacing: rw.ty.u(7)
                    Rectangle { width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: rw.r.label || ""; font: rw.ty.fBodyM; color: rw.ty.ink } }
                Text { objectName: "chPostureStyle"; visible: text !== ""; width: parent.width; wrapMode: Text.WordWrap
                       text: rw.r.styleText || ""; font: rw.ty.fSmallM; color: rw.ty.ink2 }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: (rw.r.camera || "") + (rw.r.unit ? ", " + rw.r.unit : "")
                       font: rw.ty.fSmall; color: rw.ty.ink3 }
            }

            // ── the plot ──
            Repeater {
                model: rw.yt
                delegate: Item {
                    required property var modelData
                    Rectangle { x: pf.plotX; width: pf.plotW; y: Math.round(rw.vy(modelData.v)); height: rw.ty.hw
                                color: modelData.v === 0 ? rw.ty.baseline : rw.ty.grid }
                    Text { x: pf.plotX - width - rw.ty.u(5); y: rw.vy(modelData.v) - height / 2; text: modelData.text; font: rw.ty.fFigLabel; color: rw.ty.ink3 }
                }
            }
            // the band per position: a wash in the metric's own colour; the fault lines as hairlines
            Repeater {
                model: rw.cells
                delegate: Item {
                    required property var modelData
                    required property int index
                    readonly property var c: modelData
                    readonly property bool banded: c.wash === "band" && pf.fin(c.bandLo) && pf.fin(c.bandHi)
                    x: pf.cx(index) - pf.colW * 0.34; width: pf.colW * 0.68
                    Rectangle { visible: parent.banded; width: parent.width
                                y: parent.banded ? rw.vy(parent.c.bandHi) : 0
                                height: parent.banded ? Math.max(1, rw.vy(parent.c.bandLo) - rw.vy(parent.c.bandHi)) : 0
                                color: rw.washC }
                    Rectangle { objectName: "chPostureFault"; visible: !!parent.c.faultHi && pf.fin(parent.c.faultHi.edge)
                                x: -pf.colW * 0.08; width: parent.width + pf.colW * 0.16; height: Math.max(1, rw.ty.hw)
                                y: visible ? Math.round(rw.vy(parent.c.faultHi.edge)) : 0; color: rw.ty.fault }
                    Rectangle { objectName: "chPostureFault"; visible: !!parent.c.faultLo && pf.fin(parent.c.faultLo.edge)
                                x: -pf.colW * 0.08; width: parent.width + pf.colW * 0.16; height: Math.max(1, rw.ty.hw)
                                y: visible ? Math.round(rw.vy(parent.c.faultLo.edge)) : 0; color: rw.ty.fault }
                }
            }
            // Session: the session's box at each position, this shot ringed (the brush's picked shot)
            Repeater {
                model: rw.boxes
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
            // the ghost and this swing's line
            Canvas {
                x: 0; y: 0; width: rw.width; height: rw.height
                readonly property var deps: [rw.r, rw.lo, rw.hi, pf.width, pf.mode, rw.hue]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset()
                    const line = function (ys) {
                        let pen = false
                        ctx.beginPath()
                        for (let i = 0; i < ys.length; ++i) {
                            if (!pf.fin(ys[i])) { pen = false; continue }
                            if (!pen) ctx.moveTo(pf.cx(i), rw.vy(ys[i])); else ctx.lineTo(pf.cx(i), rw.vy(ys[i]))
                            pen = true
                        }
                        ctx.stroke()
                    }
                    const gh = rw.cells.map(function (c) { return c.ghost })
                    if (gh.some(function (v) { return pf.fin(v) })) {
                        ctx.strokeStyle = Qt.alpha(rw.hue, 0.75); ctx.lineWidth = 1.2; ctx.setLineDash(rw.ty.dash([4, 3]))
                        line(gh)
                        ctx.setLineDash([])
                    }
                    if (!pf.session) {
                        ctx.strokeStyle = rw.hue; ctx.lineWidth = 1.7
                        line(rw.cells.map(function (c) { return c.value }))
                    }
                }
            }
            // the ghost's points: hollow, in the hue
            Repeater {
                model: rw.cells
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    visible: pf.fin(modelData.ghost)
                    width: rw.ty.u(6); height: width; radius: rw.ty.square ? 0 : width / 2
                    x: pf.cx(index) - width / 2; y: visible ? rw.vy(modelData.ghost) - height / 2 : 0
                    color: rw.ty.surface; border.width: 1; border.color: rw.hue
                }
            }
            // this swing's positions (This shot): a dot in the hue; outside the band a ring (its word is under the number)
            Repeater {
                model: pf.session ? [] : rw.cells
                delegate: Item {
                    required property var modelData
                    required property int index
                    objectName: modelData.mark === "ring" ? "chPostureRing" : "chPostureDot"
                    readonly property bool ring: modelData.mark === "ring"
                    visible: modelData.mark === "ring" || modelData.mark === "dot"
                    x: pf.cx(index); y: visible ? rw.vy(modelData.value) : 0
                    Rectangle { visible: !parent.ring; width: rw.ty.u(8); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2; color: rw.hue }
                    Rectangle { visible: parent.ring; width: rw.ty.u(12); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2
                                color: rw.ty.surface; border.width: 2; border.color: rw.ty.warn
                                Rectangle { anchors.centerIn: parent; width: rw.ty.u(4); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue } }
                }
            }
            // the pole: the direction word at the right edge, on the plus side
            Text { x: pf.plotX + pf.plotW + rw.ty.u(8); y: rw.topY; width: pf.poleW - rw.ty.u(8); wrapMode: Text.WordWrap
                   text: rw.r.poleText || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
            // the numbers line: the figure or "-", then the ring's word, then the ⚠ and its few words
            Repeater {
                model: rw.cells
                delegate: Column {
                    id: nc
                    required property var modelData
                    required property int index
                    objectName: "chPostureCell"
                    readonly property var c: modelData
                    readonly property var bc: pf.session ? (rw.boxes[index] || null) : null
                    readonly property string rowKey: rw.key
                    readonly property string tag: c.tag || ""
                    readonly property string text: pf.session ? (bc ? (bc.medianText || "-") : "-") : (c.text || "-")
                    readonly property bool warned: !!c.warn          // the ⚠ drawn: only on a figure
                    readonly property bool outside: c.outside === true
                    x: pf.cx(index) - width / 2; y: rw.topY + pf.plotH + rw.ty.u(5); width: pf.colW - rw.ty.u(4)
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap
                           text: nc.text; font: rw.ty.fFigS
                           color: nc.outside ? rw.ty.warn : (nc.text !== "-" ? rw.ty.ink : rw.ty.ink3)
                           Rectangle { visible: nc.outside; anchors.horizontalCenter: parent.horizontalCenter; y: parent.height - 1; width: parent.contentWidth; height: 1; color: rw.ty.warn } }
                    Text { visible: text !== ""; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap; elide: Text.ElideRight
                           text: nc.outside ? (nc.c.ringWord || "") : (pf.session && nc.bc ? (nc.bc.wideText || "") : "")
                           font: rw.ty.fSmall; color: nc.outside ? rw.ty.warn : rw.ty.ink3 }
                    Text { objectName: "chPostureWarn"; visible: nc.warned; width: parent.width; horizontalAlignment: Text.AlignHCenter
                           wrapMode: Text.NoWrap; elide: Text.ElideRight
                           text: (nc.c.warn || "") + (nc.c.warnShort ? " " + nc.c.warnShort : ""); font: rw.ty.fSmall; color: rw.ty.ink2 }
                }
            }
            // the second reading (decision 16): its own line under the numbers, smaller, with its own ⚠ words
            Repeater {
                model: rw.r.hasAlt ? rw.cells : []
                delegate: Column {
                    id: ac
                    required property var modelData
                    required property int index
                    readonly property var c: modelData
                    visible: c.alt === true
                    x: pf.cx(index) - width / 2; y: rw.topY + pf.plotH + pf.numH - rw.ty.u(4); width: pf.colW - rw.ty.u(4)
                    Text { objectName: "chPostureAltValue"
                           readonly property string tag: ac.c.tag || ""
                           readonly property string rowKey: rw.key
                           readonly property bool warned: !!ac.c.altWarn
                           width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap; elide: Text.ElideRight
                           text: ac.c.altText || ""; font: rw.ty.fSmall; color: ac.c.altAvailable ? rw.ty.ink2 : rw.ty.ink3 }
                    Text { objectName: "chPostureAltWarn"; visible: !!ac.c.altWarn; width: parent.width; horizontalAlignment: Text.AlignHCenter
                           wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                           text: (ac.c.altWarn || "") + (ac.c.altWarnShort ? " " + ac.c.altWarnShort : ""); font: rw.ty.fMicro; color: rw.ty.ink3 }
                }
            }
        }
    }
}
