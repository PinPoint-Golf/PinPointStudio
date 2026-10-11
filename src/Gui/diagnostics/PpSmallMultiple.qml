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

// SMALLMULTIPLE — C's Session tile: one pattern's shots (any session length) on THE COMMON SCALE (C): every corridor cut
// by the same ladder, 0 in the middle of the corridor, 2 at the pass edge, 3 at the fault line, folded
// so the side the pattern fires on is up. Identically scaled tiles compare at a glance. Header: rank,
// name, median and this shot; foot: measure, recurrence · trend, firings after, bookends.
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   card, readout, source       the live shapes
//   panelWidth                  the width of the whole grid (tiers read it, not the tile's width)
//   hdrH                        header height shared across a row (PpSmallMultiples sets it)
//   roles, brush, medium, interactive
//   signals conditionActivated(id), shotActivated(swingDir, shotId), shotHovered(shotId)
Item {
    id: tile
    objectName: "sdSmallMultiple"
    readonly property string cardId: (card && card.id) || ""
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property bool conditionsInteractive: true    // false: the condition names take no click and show no pointer (a
                                                 // persistent cast picks shots, never opens a condition); hover still brushes
    property var card: ({})
    property var readout: null
    property var source: null
    property real panelWidth: width
    property real hdrH: roles.u(36)
    // ── optional (round 3) ──
    property bool labelBand: false            // name the compressed corridor band (PpSmallMultiples: first tile)
    readonly property var keyKinds: ["fired", "clean", "ring", "median", "fault", "corridor"]
    signal conditionActivated(string id)
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    readonly property var c: card || ({})
    // ── the common scale (C) ──
    function zPoints(s) {
        const b = (s && s.bands) || []
        let k = -1
        for (let i = 0; i < b.length; ++i) if (b[i].grade === "ideal") { k = i; break }
        if (k < 0) return null
        const loOpen = k === 0 || b[k - 1].grade === "open", hiOpen = k === b.length - 1 || b[k + 1].grade === "open"
        let zl = -1, zh = 1
        if (loOpen && !hiOpen) { zl = 0; zh = 1 } else if (hiOpen && !loOpen) { zl = -1; zh = 0 }
        const pts = [[b[k].lo, zl], [b[k].hi, zh]]
        let z = zh
        for (let i = k + 1; i < b.length; ++i) { if (b[i].grade === "open" || b[i].grade === "action") break; z += 1; pts.push([b[i].hi, z]) }
        z = zl
        for (let i = k - 1; i >= 0; --i) { if (b[i].grade === "open" || b[i].grade === "action") break; z -= 1; pts.unshift([b[i].lo, z]) }
        return pts
    }
    function zOf(pts, v) {
        const n = pts.length
        if (n < 2 || v === undefined || !isFinite(v)) return NaN
        let i = 0
        if (v >= pts[n - 1][0]) i = n - 2
        else if (v > pts[0][0]) { while (i < n - 2 && v > pts[i + 1][0]) ++i }
        const a = pts[i], b = pts[i + 1]
        return a[1] + (v - a[0]) / (b[0] - a[0]) * (b[1] - a[1])
    }
    readonly property var fold: {
        const s = c.spread || {}, pts = zPoints(s), out = { run: [], med: [], stubs: [] }, t = c.ticks || []
        for (let i = 0; i < t.length; ++i) if (t[i].state !== "fired" && t[i].state !== "clean") out.stubs.push((i + 0.5) / t.length)
        if (!pts) return out
        const lo = s.axisLo, hi = s.axisHi, val = f => lo + f * (hi - lo)
        let side = 1
        const zm = zOf(pts, s.medianValue)
        if (isFinite(zm) && zm < 0) side = -1
        out.run = (s.run || []).filter(r => r.assessable !== false).map(r => ({ fx: r.fx, z: side * zOf(pts, val(r.fy)), fired: r.state === "fired", shotId: r.shotId, swingDir: r.swingDir }))
        out.med = (s.median || []).map(m => ({ fx: m.fx, z: side * zOf(pts, val(m.fy)) }))
        return out
    }
    readonly property var book: roles ? roles.byId(source ? source.bookends : [], c.id) : null
    readonly property string unit: { const t = String((c.spread && c.spread.medianText) || ""), n = String((c.spread && c.spread.medianNumber) || ""); return n && t.indexOf(n) === 0 ? t.slice(n.length).trim() : "" }
    readonly property bool symUnit: unit.length > 0 && unit.length <= 2 && !/[a-z]/i.test(unit)
    readonly property bool showMeasure: roles.shows("tileMeasure", panelWidth)
    readonly property bool bookLine: roles.shows("tileBookends", panelWidth)
    // FAULT-FORWARD (round 3): the common scale keeps its ladder (0 middle, 2 pass edge, 3 fault line) but
    // spends its height above the pass edge, where a pattern's shots are; the corridor below 1.5 collapses
    // into a thin labelled band past a drawn break. Shots that fall in it are still drawn there.
    readonly property real chartH: medium === "print" ? roles.u(60) : roles.atLeast(panelWidth, "wide") ? roles.u(108) : roles.atLeast(panelWidth, "compact") ? roles.u(92) : roles.u(64)
    readonly property real y0: -2
    // the top of the window: the ladder's 6 by default; PpSmallMultiples lowers it to what a ROW needs
    // (round 4: a row whose shots all sit near the pass edge no longer carries an empty band above them)
    property real zTop: 6
    readonly property real y1: zTop
    readonly property real zMax: { let m = -2; const r = fold.run || []; for (let i = 0; i < r.length; ++i) if (isFinite(r[i].z)) m = Math.max(m, r[i].z); return m }
    readonly property real zWin: 1.5
    readonly property real bandPx: roles.u(11)
    readonly property real breakPx: roles.u(6)
    // the picked shot's distance back to the pass edge, in the measure's own units
    readonly property var dist: {
        const s = c.spread || {}, run = (s.run || []).filter(r => r.assessable !== false), fr = fold.run
        if (s.axisLo === undefined) return null
        let k = -1
        for (let i = 0; i < run.length; ++i) if (brush ? brush.isPicked(run[i].shotId) : run[i].current) k = i
        if (k < 0) for (let i = 0; i < run.length; ++i) if (run[i].current) k = i
        if (k < 0 || !fr[k] || !(fr[k].z > 2)) return null
        const v = s.axisLo + run[k].fy * (s.axisHi - s.axisLo)
        const b = s.bands || [], pass = []
        for (let i = 1; i < b.length; ++i) if ([b[i - 1].grade, b[i].grade].sort().join("|") === "good|watch") pass.push(b[i].lo)
        if (!pass.length) return null
        const pe = pass.reduce((a, x) => Math.abs(x - v) < Math.abs(a - v) ? x : a)
        return { fx: fr[k].fx, z: fr[k].z, d: Math.abs(v - pe) }
    }
    readonly property int nShots: Math.max(1, (c.ticks || []).length || ((c.spread && c.spread.run) || []).length)
    readonly property int pickedIdx: brush ? roles.indexOfShot(c.ticks || (c.spread && c.spread.run) || [], brush.pickedShot) : -1
    readonly property var axisShots: {
        const out = [{ no: 1, picked: pickedIdx === 0 }]
        if (pickedIdx > 0 && pickedIdx < nShots - 1) out.push({ no: pickedIdx + 1, picked: true })
        if (nShots > 1) out.push({ no: nShots, picked: pickedIdx === nShots - 1 })
        return out
    }
    readonly property string afterText: c.firingsAfterText || c.recencyText || ""
    readonly property string bookText: book ? "representative " + book.representativeShotId + " · worst " + book.worstShotId + " · best " + book.bestShotId : ""
    readonly property real figW: roles.u(100)
    readonly property bool hot: brush && brush.hoveredCondition === c.id
    implicitHeight: foot.y + foot.height
    height: implicitHeight
    opacity: brush ? brush.conditionOpacity(c.id, roles.dim) : 1
    Behavior on opacity { NumberAnimation { duration: tile.roles.normal } }

    PpPulseWash { anchors.fill: parent; anchors.margins: -tile.roles.u(4); roles: tile.roles; brush: tile.brush; conditionId: tile.cardId }
    Rectangle { anchors.fill: parent; anchors.margins: -tile.roles.u(4); visible: tile.hot; color: tile.roles.rowHover }
    MouseArea {
        enabled: tile.interactive; visible: enabled
        width: parent.width; height: tile.hdrH; hoverEnabled: true; cursorShape: tile.conditionsInteractive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onContainsMouseChanged: if (tile.brush) tile.brush.hoverCondition(tile.c.id, containsMouse)
        onClicked: if (tile.conditionsInteractive) tile.conditionActivated(tile.c.id)
    }
    Row {
        id: titleRow
        width: tile.width - tile.figW
        spacing: tile.roles.u(6)
        Text { text: tile.c.rankText || ""; font: tile.roles.fFig; color: tile.roles.ink3; anchors.baseline: tName.baseline }
        Text { id: tName; text: tile.c.name || ""; width: Math.min(implicitWidth, titleRow.width - tile.roles.u(44)); wrapMode: Text.WordWrap
               font: tile.hot ? tile.roles.fBodyM : tile.roles.fBody; color: tile.roles.ink }
        Text { visible: !!tile.c.fresh; text: "new"; font: tile.roles.fLabel; color: tile.roles.ink3; anchors.baseline: tName.baseline }
    }
    Row {
        anchors.right: parent.right
        spacing: tile.roles.u(14)
        Column {
            Text { anchors.right: parent.right; text: ((tile.c.spread && tile.c.spread.medianNumber) || "-") + (tile.symUnit ? tile.unit : ""); font: tile.roles.fFig; color: tile.roles.ink2 }
            Text { anchors.right: parent.right; text: "median"; font: tile.roles.fLabel; color: tile.roles.ink3 }
        }
        Column {
            Text { anchors.right: parent.right; text: ((tile.c.spread && tile.c.spread.currentNumber) || "-") + (tile.symUnit ? tile.unit : ""); font: tile.roles.fFig
                   color: tile.c.thisShot === "fired" ? tile.roles.warn : tile.roles.ink }
            Text { anchors.right: parent.right; text: "this shot"; font: tile.roles.fLabel; color: tile.roles.ink3 }
        }
    }
    Item {
        id: chart
        y: tile.hdrH + tile.roles.u(8)
        width: tile.width; height: tile.chartH
        readonly property real winBottom: height - tile.bandPx - tile.breakPx
        function ty(z) {
            const v = Math.max(tile.y0, Math.min(tile.y1, z))
            if (v < tile.zWin) return height - (v - tile.y0) / (tile.zWin - tile.y0) * tile.bandPx
            return winBottom - (v - tile.zWin) / (tile.y1 - tile.zWin) * winBottom
        }
        // the corridor wash: from the pass edge down to the break, then the compressed band
        Rectangle { width: parent.width; y: chart.ty(2); height: chart.winBottom - chart.ty(2); color: tile.roles.wash }
        Rectangle { width: parent.width; y: chart.height - tile.bandPx; height: tile.bandPx; color: tile.roles.wash }
        Canvas {
            anchors.fill: parent
            readonly property var deps: [tile.roles.ink3, width, height]
            onDepsChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d"); ctx.reset()
                ctx.strokeStyle = tile.roles.ink3; ctx.lineWidth = tile.roles.hw
                const k = tile.roles.u(2.5), yc = chart.winBottom + tile.breakPx / 2
                for (const x0 of [0, width - 2 * k]) for (const dx of [0, k]) { ctx.beginPath(); ctx.moveTo(x0 + dx, yc + k); ctx.lineTo(x0 + dx + k, yc - k); ctx.stroke() }
            }
        }
        Text {
            visible: tile.labelBand
            anchors.right: parent.right; anchors.rightMargin: tile.roles.u(10)
            y: chart.height - tile.bandPx / 2 - height / 2
            text: "corridor, compressed"; font: tile.roles.fLabel; color: tile.roles.ink3
        }
        Rectangle { width: parent.width; y: Math.round(chart.ty(3)); height: tile.roles.hw; color: tile.roles.fault }
        Rectangle { width: parent.width; y: parent.height; height: tile.roles.hw; color: tile.roles.baseline; opacity: tile.roles.baselineOpacity }
        Canvas {
            anchors.fill: parent
            readonly property var deps: [tile.fold, tile.roles.ink2, width, height]
            onDepsChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d"); ctx.reset()
                const m = tile.fold.med
                if (!m || m.length < 2) return
                ctx.strokeStyle = tile.roles.ink2; ctx.lineWidth = Theme.curveWidth(tile.medium === "print" ? 1 : 1.2); ctx.lineJoin = "round"
                ctx.beginPath()
                let prev = false
                for (let i = 0; i < m.length; ++i) {
                    const x = m[i].fx * width, y = chart.ty(m[i].z), inWin = m[i].z >= tile.zWin
                    if (i === 0 || inWin !== prev) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    prev = inWin
                }
                ctx.stroke()
            }
        }
        // the not-measured STUB (D5): a short flat ink3 mark on the baseline, never a gap
        Repeater {
            model: tile.roles ? (tile.fold.stubs) : []
            delegate: Rectangle { required property var modelData; objectName: "sdTileStub"
                                  width: Math.max(2, Math.min(tile.roles.u(4), chart.width / tile.nShots * 0.5)); height: tile.roles.stubH(tile.roles.u(12))
                                  x: modelData * chart.width - width / 2; y: chart.height - height; color: tile.roles.ink3 }
        }
        Repeater {
            model: tile.roles ? (tile.fold.run) : []
            delegate: PpMark {
                required property var modelData
                roles: tile.roles; brush: tile.brush; interactive: tile.interactive
                shotId: modelData.shotId; fired: modelData.fired
                cx: Math.round(modelData.fx * chart.width * 2) / 2; cy: Math.round(chart.ty(modelData.z) * 2) / 2
                onActivated: tile.shotActivated(modelData.swingDir || "", shotId)
                onHoveredChanged: tile.shotHovered(hovered ? shotId : -1)
            }
        }
        // the distance to travel: the picked shot back to the pass edge
        Item {
            visible: !!tile.dist
            readonly property real px: tile.dist ? tile.dist.fx * chart.width : 0
            readonly property bool flip: px > chart.width - tile.roles.u(90)
            readonly property real lx: px + (flip ? -1 : 1) * tile.roles.u(8)
            readonly property real ya: tile.dist ? chart.ty(tile.dist.z) : 0
            readonly property real yb: chart.ty(2)
            Rectangle { x: parent.lx; y: Math.min(parent.ya, parent.yb); width: tile.roles.hw; height: Math.abs(parent.yb - parent.ya); color: tile.roles.ink2 }
            Rectangle { x: parent.lx - tile.roles.u(2.5); y: Math.round(parent.yb); width: tile.roles.u(6); height: tile.roles.hw; color: tile.roles.ink2 }
            Text {
                x: parent.flip ? parent.lx - width - tile.roles.u(4) : parent.lx + tile.roles.u(4)
                y: parent.yb - height - tile.roles.u(1)
                text: tile.dist ? Number(tile.dist.d).toFixed(tile.c.spread && tile.c.spread.decimals !== undefined ? tile.c.spread.decimals : 1) + (tile.symUnit ? tile.unit : "") + " to the pass edge" : ""
                font: tile.roles.fLabel; color: tile.roles.ink2
                style: Text.Outline; styleColor: tile.medium === "print" ? "#FFFFFF" : Theme.colorSurface
            }
        }
        // shot numbers from the data (D7): the first, the picked and the last, at their ordinals
        Repeater {
            model: tile.roles ? tile.axisShots : []
            delegate: Text {
                required property var modelData
                text: String(modelData.no); font: tile.roles.fFigLabel
                color: modelData.picked ? tile.roles.ink : tile.roles.ink3
                x: Math.max(0, Math.min(chart.width - width, (modelData.no - 0.5) / tile.nShots * chart.width - width / 2)); y: chart.height + tile.roles.u(3)
            }
        }
    }
    Column {
        id: foot
        y: chart.y + chart.height + tile.roles.u(20)
        width: tile.width
        spacing: tile.roles.u(1)
        Text { visible: tile.showMeasure; width: parent.width; wrapMode: Text.WordWrap; font: tile.roles.fSmall; color: tile.roles.ink3; bottomPadding: tile.roles.u(3)
               text: (tile.c.phase || "") + " · " + (tile.c.measure || "") + (tile.unit && !tile.symUnit ? " · in " + tile.unit : "") }
        Text { width: parent.width; wrapMode: Text.WordWrap; font: tile.roles.fSmall; color: tile.roles.ink2
               text: (tile.c.recurrence || "") + " · " + tile.roles.trendWord(tile.c) }
        Text { width: parent.width; wrapMode: Text.WordWrap; font: tile.roles.fSmall; color: tile.roles.ink3
               text: [tile.afterText, tile.bookLine ? "" : tile.bookText].filter(x => !!x).join(" · ") }
        Text { visible: tile.bookLine && tile.bookText !== ""; width: parent.width; wrapMode: Text.WordWrap; text: tile.bookText; font: tile.roles.fLabel; color: tile.roles.ink3 }
    }
}
