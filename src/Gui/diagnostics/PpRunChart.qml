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

// RUNCHART — "Shot by shot": one PpMark per shot in the order struck, the median of the last five as a
// line, a stub on the foot for a shot that could not be measured. Shot numbers thin with width: every
// shot from 22 px a shot, else 1 · 5 · 10 · 15 plus the picked and the hovered shot.
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   spread     card.spread / detail.header.spread  (run[], median[])
//   ticks      the card's 15 ticks (for the not-measured stubs)
//   scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
//   title, roles, brush, medium, interactive
//   signals shotActivated(swingDir, shotId), shotHovered(shotId)
Item {
    id: rc
    objectName: "sdRunChart"
    readonly property int hoveredShot: brush ? brush.hoveredShot : -1
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var spread: null
    property var ticks: []
    property var scale: null
    property string title: "Shot by shot"
    // ── optional (round 3) ──
    property string unit: ""                  // a symbol unit (°) to set on the distance figures
    property bool showDistance: true          // the picked shot's distance to travel
    // How far a label may reach beyond the plot on either side and still be inside its figure row
    // (PpFigureRow sets them; unset = no limit). Labels are pulled back inside, and wrap if even the
    // whole row is too narrow: nothing is drawn past the row's edge.
    property real leftRoom: 1e6
    property real rightRoom: 1e6
    function fitX(raw, w) { return Math.max(-leftRoom, Math.min(width + rightRoom - w, raw)) }
    readonly property real labelMaxW: leftRoom + width + rightRoom
    readonly property var keyKinds: ["fired", "clean", "ring", "median", "fault", "pass", "corridor"]
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)
    readonly property var s: spread || ({})
    readonly property int n: Math.max(1, (ticks || []).length || (s.run || []).length || 15)
    // the picked / hovered shot's INDEX in the run (D7: ids are not ordinals after a deleted swing)
    readonly property var ids: (ticks && ticks.length ? ticks : (s.run || [])).map((t, i) => t.shotId !== undefined ? t.shotId : i + 1)
    readonly property int pickedIdx: brush ? ids.indexOf(brush.pickedShot) : -1
    readonly property int hoverIdx: brush && brush.hoveredShot > 0 ? ids.indexOf(brush.hoveredShot) : -1
    implicitHeight: scale ? scale.plotTop + scale.plotHeight + roles.u(26) : 0

    Text { text: rc.title; font: rc.roles.fSmall; color: rc.roles.ink }
    PpZonePlot { anchors.fill: parent; roles: rc.roles; scale: rc.scale }
    Canvas {
        anchors.fill: parent
        readonly property var deps: [rc.s, rc.width, rc.roles.ink2, rc.scale ? rc.scale.plotHeight : 0]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            const m = rc.s.median || []
            if (!rc.scale || m.length < 2) return
            ctx.strokeStyle = rc.roles.ink2; ctx.lineWidth = Theme.curveWidth(rc.medium === "print" ? 1 : 1.25); ctx.lineJoin = "round"
            ctx.beginPath()
            // the line breaks where the axis does
            let prev = -1
            for (let i = 0; i < m.length; ++i) {
                const x = m[i].fx * width, y = rc.scale.y(m[i].fy), r = rc.scale.region ? rc.scale.region(m[i].fy) : 1
                if (i === 0 || r !== prev) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                prev = r
            }
            ctx.stroke()
        }
    }
    Repeater {
        model: rc.roles ? (rc.ticks || []) : []
        // the not-measured STUB (D5): a short flat ink3 mark on the baseline, never a gap
        delegate: Rectangle {
            required property var modelData
            required property int index
            objectName: "sdRunStub"
            visible: !!rc.scale && modelData.state !== "fired" && modelData.state !== "clean"
            width: Math.max(2, Math.min(rc.roles.u(4), rc.width / rc.n * 0.5)); height: rc.roles.stubH(rc.roles.u(12))
            x: (index + 0.5) / rc.n * rc.width - width / 2; y: rc.scale ? rc.scale.plotTop + rc.scale.plotHeight - height : 0
            color: rc.roles.ink3
        }
    }
    Repeater {
        model: rc.roles ? (rc.scale ? (rc.s.run || []).filter(q => q.assessable !== false) : []) : []
        delegate: PpMark {
            required property var modelData
            objectName: "sdRunMark"
            roles: rc.roles; brush: rc.brush; interactive: rc.interactive
            shotId: modelData.shotId; fired: modelData.state === "fired"
            ringed: rc.brush ? rc.brush.isPicked(shotId) : !!modelData.current
            cx: Math.round(modelData.fx * rc.width * 2) / 2; cy: Math.round(rc.scale.y(modelData.fy) * 2) / 2
            onActivated: rc.shotActivated(modelData.swingDir || "", shotId)
            onHoveredChanged: rc.shotHovered(hovered ? shotId : -1)
        }
    }
    // the picked shot, as an item tests can find (the ring itself is drawn by its PpMark)
    Item {
        objectName: "sdRunSelected"
        readonly property var p: { const r = (rc.s.run || []).filter(q => q.assessable !== false); for (let i = 0; i < r.length; ++i) if (rc.brush ? rc.brush.isPicked(r[i].shotId) : r[i].current) return r[i]; return null }
        readonly property int shotId: p ? p.shotId : -1
        visible: !!p && !!rc.scale
        x: p ? p.fx * rc.width : 0; y: p && rc.scale ? rc.scale.y(p.fy) : 0; width: 0; height: 0
    }
    Repeater {
        model: rc.roles ? (rc.scale ? rc.n : 0) : []
        delegate: Text {
            required property int index
            readonly property bool cur: index === rc.pickedIdx || index === rc.hoverIdx
            readonly property int step: rc.roles.shotLabelStep(rc.width / rc.n, rc.roles.u(14))
            readonly property bool dense: step === 1
            visible: dense || rc.roles.axisLabelShown(index, rc.n, Math.max(5, step), [rc.pickedIdx, rc.hoverIdx])
            text: String(index + 1); font: rc.roles.fFigLabel
            color: cur ? rc.roles.ink : rc.roles.ink3
            x: rc.fitX((index + 0.5) / rc.n * rc.width - width / 2, width)
            y: rc.scale.plotTop + rc.scale.plotHeight + rc.roles.u(8)
        }
    }

    // ── THE DISTANCE TO TRAVEL (round 3): the picked shot, measured to the fault line and back to the
    // pass edge on its side, drawn as a dimension line beside it. The empty zone becomes the target. For a
    // shot inside the corridor it reads the margin instead ("4 inside the pass edge").
    readonly property var dist: {
        const sc = rc.scale, s = rc.s
        if (!sc || !rc.showDistance || s.axisLo === undefined) return null
        const run = (s.run || []).filter(q => q.assessable !== false)
        let p = null
        for (let i = 0; i < run.length; ++i) if (rc.brush ? rc.brush.isPicked(run[i].shotId) : run[i].current) p = run[i]
        if (!p) for (let i = 0; i < run.length; ++i) if (run[i].current) p = run[i]
        if (!p) return null
        const val = f => s.axisLo + f * (s.axisHi - s.axisLo)
        const v = val(p.fy)
        const pass = sc.edges.filter(e => e.kind === "pass"), fault = sc.edges.filter(e => e.kind === "fault")
        const c = sc.corridor
        if (!pass.length) return null
        const loPass = pass.reduce((a, e) => e.value < a.value ? e : a), hiPass = pass.reduce((a, e) => e.value > a.value ? e : a)
        const above = pass.length > 1 ? v > hiPass.value : (c && loPass.f >= c.hi - 1e-6 ? v > loPass.value : false)
        const below = pass.length > 1 ? v < loPass.value : (c && loPass.f <= c.lo + 1e-6 ? v < loPass.value : false)
        const d = { x: p.fx, f: p.fy, v: v, out: above || below }
        if (above || below) {
            const pe = above ? hiPass : loPass
            const fl = fault.filter(e => above ? e.value >= pe.value : e.value <= pe.value)
            const fe = fl.length ? fl.reduce((a, e) => Math.abs(e.value - pe.value) < Math.abs(a.value - pe.value) ? e : a) : null
            d.passF = pe.f; d.toPass = Math.abs(v - pe.value)
            d.faultF = fe ? fe.f : NaN
            d.pastFault = fe && (above ? v > fe.value : v < fe.value) ? Math.abs(v - fe.value) : NaN
            d.toFault = fe && !(above ? v > fe.value : v < fe.value) ? Math.abs(fe.value - v) : NaN
        } else {
            const nearest = pass.reduce((a, e) => Math.abs(e.value - v) < Math.abs(a.value - v) ? e : a)
            // inside: the margin to every pass edge, each its own measured gap
            d.passF = nearest.f; d.inside = Math.abs(v - nearest.value); d.passV = nearest.value
            d.edges = pass.map(e => ({ f: e.f, value: e.value, dist: Math.abs(v - e.value) }))
        }
        return d
    }
    // distances at the precision the model reports its readings (spread.decimals), true minus never needed
    readonly property color halo: medium === "print" ? "#FFFFFF" : Theme.colorSurface   // a knock-out, not a box
    function fmtD(x) { const k = rc.s.decimals !== undefined ? rc.s.decimals : 1; return Number(x).toFixed(k) + rc.unit }
    Loader {
        active: !!rc.roles && !!rc.dist && !!rc.scale
        sourceComponent: cDim
    }
    Component {
        id: cDim
    Item {
        id: dim
        readonly property var d: rc.dist
        readonly property real px: d ? d.x * rc.width : 0
        // the labels go right of the line, unless that side is short of room and the left has more; on
        // either side they wrap inside it rather than run back across the line (12 Oct: a run sharing its
        // row with the box plot has little room past its last shots)
        readonly property bool flip: { const r = rc.width + rc.rightRoom - px, l = px + rc.leftRoom; return r < rc.roles.u(110) && l > r }
        readonly property real lx: px + (flip ? -1 : 1) * rc.roles.u(11)
        readonly property real sideW: Math.max(rc.roles.u(40), flip ? lx - rc.roles.u(5) + rc.leftRoom : rc.width + rc.rightRoom - lx - rc.roles.u(5))
        readonly property real y0: d && rc.scale ? rc.scale.y(d.f) : 0
        readonly property real y1: d && rc.scale ? rc.scale.y(d.passF) : 0
        readonly property real yf: d && rc.scale && isFinite(d.faultF) ? rc.scale.y(d.faultF) : NaN
        Rectangle { visible: !!dim.d && dim.d.out; x: dim.lx; y: Math.min(dim.y0, dim.y1); width: rc.roles.hw; height: Math.abs(dim.y1 - dim.y0); color: rc.roles.ink2 }
        Rectangle { visible: !!dim.d && dim.d.out; x: dim.lx - rc.roles.u(3); y: Math.round(dim.y1); width: rc.roles.u(7); height: rc.roles.hw; color: rc.roles.ink2 }
        // inside the corridor: a gap to each pass edge, labelled at the edge
        Repeater {
            model: dim.d && !dim.d.out ? (dim.d.edges || []) : []
            delegate: Item {
                required property var modelData
                readonly property real ye: rc.scale.y(modelData.f)
                Rectangle { x: dim.lx; y: Math.min(dim.y0, parent.ye); width: rc.roles.hw; height: Math.abs(parent.ye - dim.y0); color: rc.roles.ink2 }
                Rectangle { x: dim.lx - rc.roles.u(3); y: Math.round(parent.ye); width: rc.roles.u(7); height: rc.roles.hw; color: rc.roles.ink2 }
                Text {
                    x: rc.fitX(dim.flip ? dim.lx - width - rc.roles.u(5) : dim.lx + rc.roles.u(5), width)
                    width: Math.min(implicitWidth, dim.sideW); wrapMode: Text.WordWrap
                    y: parent.ye < dim.y0 ? parent.ye + rc.roles.u(1) : parent.ye - height - rc.roles.u(1)
                    text: rc.fmtD(modelData.dist) + " to " + rc.roles.num(modelData.value) + rc.unit
                    font: rc.roles.fLabel; color: rc.roles.ink2; style: Text.Outline; styleColor: rc.halo
                }
            }
        }
        Rectangle { visible: isFinite(dim.yf); x: dim.lx - rc.roles.u(3); y: Math.round(dim.yf); width: rc.roles.u(7); height: rc.roles.hw; color: rc.roles.warn }
        Column {
            readonly property real midY: dim.d && dim.d.out && isFinite(dim.yf) ? (dim.yf + dim.y1) / 2 : (dim.y0 + dim.y1) / 2
            x: rc.fitX(dim.flip ? dim.lx - width - rc.roles.u(5) : dim.lx + rc.roles.u(5), width)
            y: midY - height / 2
            Text {
                visible: !!dim.d && dim.d.out && isFinite(dim.d.pastFault)
                anchors.right: dim.flip ? parent.right : undefined
                width: Math.min(implicitWidth, dim.sideW); wrapMode: Text.WordWrap
                text: dim.d ? rc.fmtD(dim.d.pastFault) + " past the fault line" : ""
                font: rc.roles.fLabel; color: rc.roles.warn; style: Text.Outline; styleColor: rc.halo
            }
            Text {
                visible: !!dim.d && dim.d.out && isFinite(dim.d.toFault)
                anchors.right: dim.flip ? parent.right : undefined
                width: Math.min(implicitWidth, dim.sideW); wrapMode: Text.WordWrap
                text: dim.d ? rc.fmtD(dim.d.toFault) + " short of the fault line" : ""
                font: rc.roles.fLabel; color: rc.roles.ink2; style: Text.Outline; styleColor: rc.halo
            }
            Text {
                visible: !!dim.d && dim.d.out
                anchors.right: dim.flip ? parent.right : undefined
                width: Math.min(implicitWidth, dim.sideW); wrapMode: Text.WordWrap
                text: dim.d ? rc.fmtD(dim.d.toPass) + " to get back inside" : ""
                font: rc.roles.fLabel; color: rc.roles.ink; style: Text.Outline; styleColor: rc.halo
            }
        }
    }
    }
}
