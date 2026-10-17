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

// PPCHWRISTPROFILE — THE CHECKPOINT PROFILE (docs/design/wrist_motion_into_charts.md §4 B, W1 W2 W10–W13): one
// row per wrist DOF, the eight checkpoints as a discrete x axis, Δ from address on y. The band is the chart
// wash per checkpoint; this swing is drawn in its metric's hue (Theme.metricColor, identity); a value outside
// its band is a warn mark (filled = fault, ring = watch) AND a word under its number ("fault · 19° open"), never
// colour alone; the playhead's checkpoint is the picked column through every row; the Against ghost is dashed
// with hollow points; in Session scope a box per checkpoint (PpChBoxCell, this shot ringed) and, under it, the
// median and how wide the middle half is; the curve behind on demand. The numbers under every column ARE the
// old grid; the grid-only DOF (lead elbow) is a numbers-only row.
// Every word, number and scale is ChartsWristPresenter's (profile.rows: ROW, positions: POS); QML maps to pixels.
// objectNames: chWristProfile; each row chWristRow (+ dof); each column head chWristCol (+ tag).
Item {
    id: pf
    objectName: "chWristProfile"
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null               // ChartsRelationsPresenter: ticks(lo, hi, n, unit)
    property var rows: []
    property var positions: []
    property int selected: -1
    property string mode: "shot"                 // "shot" | "session"
    property bool interactive: true
    signal columnActivated(int index)
    // an empty value a gate left (point.trust / cell.trust): the gate's words stand in it and open the reason
    signal trustClicked(string subject)

    readonly property bool ok: !!roles
    readonly property bool sideLabel: ok && roles.atLeast(width, "medium")
    readonly property real labelW: sideLabel ? roles.u(200) : 0
    readonly property real poleW: ok ? roles.u(62) : 62
    readonly property real gutY: ok ? roles.u(30) : 30
    readonly property real plotX: labelW + gutY
    readonly property real plotW: Math.max(ok ? roles.u(160) : 160, width - plotX - poleW)
    readonly property real colW: plotW / 8
    readonly property bool tagNames: ok && colW >= roles.u(78)
    readonly property bool longWords: ok && colW >= roles.u(96)
    readonly property real headH: ok ? roles.u(tagNames ? 36 : 22) : 22
    readonly property real plotH: ok ? roles.u(96) : 96
    readonly property real numH: ok ? roles.u(mode === "session" ? 36 : 36) : 36
    readonly property real rowGap: ok ? roles.u(14) : 14
    function cx(i) { return plotX + (i + 0.5) * colW }
    function aboveH() { return sideLabel || !ok ? 0 : roles.u(22) }
    function rowH(r) { return !ok ? 0 : aboveH() + (r.numbersOnly ? roles.u(24) : plotH + numH) + rowGap }
    readonly property var rowYs: { let y = headH; const ys = []; for (const r of (rows || [])) { ys.push(y); y += rowH(r) } ys.push(y); return ys }
    implicitHeight: rowYs[rowYs.length - 1]
    height: implicitHeight

    // the playhead's checkpoint: one column wash through the whole profile
    Rectangle { visible: pf.ok && pf.selected >= 0 && !(pf.roles && pf.roles.isPrint); x: pf.cx(pf.selected) - pf.colW / 2; width: pf.colW; y: 0; height: pf.height
                color: pf.ok ? pf.roles.pickWash : "transparent" }
    // the column heads: the P tag (and "now" on the playhead's), its name when there is room
    Repeater {
        model: pf.ok ? pf.positions : []
        delegate: Column {
            required property var modelData
            required property int index
            objectName: "chWristCol"
            readonly property string tag: modelData.tag
            readonly property bool on: index === pf.selected
            x: pf.cx(index) - width / 2; y: pf.roles.u(2); width: pf.colW - pf.roles.u(6)
            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter
                   text: modelData.tag + (parent.on ? "  now" : ""); font: parent.on ? pf.roles.fSmallM : pf.roles.fSmall
                   color: parent.on ? pf.roles.ink : (modelData.available ? pf.roles.ink2 : pf.roles.ink3) }
            Text { visible: pf.tagNames; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap
                   text: modelData.name || ""; font: pf.roles.fMicro; color: pf.roles.ink3 }
        }
    }
    Rectangle { visible: pf.ok; x: pf.plotX; y: pf.headH - (pf.ok ? pf.roles.u(4) : 4); width: pf.plotW; height: pf.ok ? pf.roles.hw : 1; color: pf.ok ? pf.roles.hair : "grey" }

    Repeater {
        model: pf.ok ? pf.rows : []
        delegate: Item {
            id: rw
            required property var modelData
            required property int index
            objectName: "chWristRow"
            readonly property var r: modelData
            readonly property string dof: r.dof || ""
            readonly property color hue: pf.roles.hue(r.key || "")
            readonly property var ty: pf.roles
            x: 0; y: pf.rowYs[index]; width: pf.width; height: pf.rowH(r)
            readonly property real topY: pf.aboveH()
            readonly property real lo: r.yLo !== undefined && r.yLo !== null ? r.yLo : -10
            readonly property real hi: r.yHi !== undefined && r.yHi !== null ? r.yHi : 10
            function vy(v) { return topY + pf.plotH * (1 - (v - lo) / Math.max(1e-9, hi - lo)) }
            readonly property var yt: pf.ticker && !r.numbersOnly ? pf.ticker.ticks(lo, hi, 3, "°") : []

            // ── the label: beside at medium and up, above below that ──
            Row {
                visible: !pf.sideLabel
                spacing: rw.ty.u(6)
                Rectangle { visible: !rw.r.numbersOnly; width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                Text { id: labName; text: rw.r.name || ""; font: rw.ty.fBodyM; color: rw.ty.ink }
                Text { anchors.baseline: labName.baseline; text: (rw.r.sub || "") + (rw.r.sourceText ? "   " + rw.r.sourceText : ""); font: rw.ty.fSmall; color: rw.ty.ink3 }
            }
            Column {
                visible: pf.sideLabel
                y: rw.topY + rw.ty.u(rw.r.numbersOnly ? 2 : 6); width: pf.labelW - rw.ty.u(12)
                spacing: rw.ty.u(2)
                Row { spacing: rw.ty.u(7)
                    Rectangle { visible: !rw.r.numbersOnly; width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: rw.r.name || ""; font: rw.ty.fBodyM; color: rw.ty.ink } }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: rw.r.sub || ""; font: rw.ty.fSmall; color: rw.ty.ink2 }
                Text { visible: !!rw.r.sourceText; width: parent.width; wrapMode: Text.WordWrap; text: rw.r.sourceText || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
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
                // the band per checkpoint: the chart wash
                Repeater {
                    model: rw.r.points || []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        visible: modelData.banded === true && modelData.bandLo !== null && modelData.bandHi !== null
                        x: pf.cx(index) - pf.colW * 0.32; width: pf.colW * 0.64
                        y: visible ? rw.vy(modelData.bandHi) : 0
                        height: visible ? Math.max(1, rw.vy(modelData.bandLo) - rw.vy(modelData.bandHi)) : 0
                        color: rw.ty.wash
                    }
                }
                // Session scope: the session's box at each checkpoint, this shot ringed
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
                        const fin = function (v) { return v !== null && v !== undefined && isFinite(v) }
                        const line = function (xs, ys) {
                            let pen = false
                            ctx.beginPath()
                            for (let i = 0; i < xs.length; ++i) {
                                if (!fin(ys[i])) { pen = false; continue }
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
                        const xs = []; for (let i = 0; i < 8; ++i) xs.push(pf.cx(i))
                        if (rw.r.ghost) {
                            ctx.strokeStyle = Qt.alpha(rw.hue, 0.75); ctx.lineWidth = 1.2; ctx.setLineDash(rw.ty.dash([4, 3]))
                            line(xs, rw.r.ghost)
                            ctx.setLineDash([])
                        }
                        if (pf.mode === "shot") {
                            ctx.strokeStyle = rw.hue; ctx.lineWidth = 1.7
                            line(xs, (rw.r.points || []).map(function (p) { return p.available ? p.value : null }))
                        }
                    }
                }
                // the ghost's points: hollow, in the hue
                Repeater {
                    model: rw.r.ghost || []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        visible: modelData !== null && modelData !== undefined && isFinite(modelData)
                        width: rw.ty.u(6); height: width; radius: rw.ty.square ? 0 : width / 2
                        x: pf.cx(index) - width / 2; y: visible ? rw.vy(modelData) - height / 2 : 0
                        color: rw.ty.surface; border.width: 1; border.color: rw.hue
                    }
                }
                // this swing's checkpoints: in the hue; outside the band a warn mark (its word is under the number)
                Repeater {
                    model: pf.mode === "shot" ? (rw.r.points || []) : []
                    delegate: Item {
                        required property var modelData
                        required property int index
                        objectName: modelData.out ? "chWristOut" : "chWristIn"
                        readonly property bool out: modelData.out === true
                        visible: modelData.available === true
                        x: pf.cx(index); y: visible ? rw.vy(modelData.value) : 0
                        Rectangle { visible: !parent.out; width: rw.ty.u(8); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2; color: rw.hue }
                        Rectangle { visible: parent.out; width: rw.ty.u(11); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2
                                    color: modelData.fault === true ?rw.ty.warn : rw.ty.surface; border.width: 2; border.color: rw.ty.warn }
                    }
                }
                // the poles: direction words at the right edge (W12)
                Text { x: pf.plotX + pf.plotW + rw.ty.u(8); y: rw.topY; text: rw.r.poleTopText || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
                Text { x: pf.plotX + pf.plotW + rw.ty.u(8); y: rw.topY + pf.plotH - height; text: rw.r.poleBottomText || ""; font: rw.ty.fSmall; color: rw.ty.ink3 }
                // the numbers line (the old grid): this shot's value and, out of band, its word; Session: median, width
                Repeater {
                    model: rw.r.points || []
                    delegate: Column {
                        required property var modelData
                        required property int index
                        readonly property bool out: pf.mode === "shot" && modelData.out === true
                        readonly property var bc: pf.mode === "session" && rw.r.boxes ? rw.r.boxes[index] : null
                        x: pf.cx(index) - width / 2; y: rw.topY + pf.plotH + rw.ty.u(5); width: pf.colW - rw.ty.u(4)
                        Text {
                            id: nv
                            readonly property bool why: pf.mode !== "session" && !!modelData.trust
                            objectName: why ? "chWristWhy:" + modelData.trust : ""
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            wrapMode: why ? Text.WordWrap : Text.NoWrap
                            text: pf.mode === "session" ? (parent.bc ? parent.bc.medianText || "-" : "-")
                                  : why ? (modelData.valueText || "") + " ›" : (modelData.valueText || "-")
                            font: why ? rw.ty.fSmall : rw.ty.fFigS
                            color: why ? rw.ty.link : parent.out ? rw.ty.warn : (modelData.available || pf.mode === "session" ? rw.ty.ink : rw.ty.ink3)
                            MouseArea { enabled: nv.why && pf.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: pf.trustClicked(nv.parent.modelData.trust) }
                            Rectangle { visible: parent.parent.out; anchors.horizontalCenter: parent.horizontalCenter; y: parent.height - 1; width: parent.contentWidth; height: 1; color: rw.ty.warn }
                        }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.NoWrap
                            visible: text !== ""
                            text: pf.mode === "session" ? (parent.bc ? parent.bc.wideText || "" : "")
                                  : parent.out ? (pf.longWords ? modelData.word || "" : modelData.wordShort || "") : ""
                            font: rw.ty.fSmall; color: parent.out ? rw.ty.warn : rw.ty.ink3
                        }
                    }
                }
            }
            // the grid-only DOF: numbers only (W11)
            Repeater {
                model: rw.r.numbersOnly ? (rw.r.cells || []) : []
                delegate: Text {
                    required property var modelData
                    required property int index
                    x: pf.cx(index) - width / 2; y: rw.topY + rw.ty.u(3); width: pf.colW - rw.ty.u(4); horizontalAlignment: Text.AlignHCenter
                    id: cv
                    readonly property bool why: !!modelData.trust
                    objectName: why ? "chWristWhy:" + modelData.trust : ""
                    wrapMode: why ? Text.WordWrap : Text.NoWrap
                    text: why ? (modelData.valueText || "") + " ›" : (modelData.valueText || "-")
                    font: why ? rw.ty.fSmall : rw.ty.fFigS
                    color: why ? rw.ty.link : modelData.available ? rw.ty.ink2 : rw.ty.ink3
                    MouseArea { enabled: cv.why && pf.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: pf.trustClicked(cv.modelData.trust) }
                }
            }
        }
    }
    // a checkpoint is a seek target: press its column head
    Repeater {
        model: pf.ok && pf.interactive ? pf.positions : []
        delegate: MouseArea {
            required property var modelData
            required property int index
            objectName: "chWristSeek"
            readonly property string tag: modelData.tag
            x: pf.cx(index) - pf.colW / 2; width: pf.colW; y: 0; height: pf.headH
            enabled: modelData.available === true
            cursorShape: Qt.PointingHandCursor
            onClicked: pf.columnActivated(index)
        }
    }
}
