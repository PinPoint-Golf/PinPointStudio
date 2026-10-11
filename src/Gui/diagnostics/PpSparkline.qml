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

// SPARKLINE — one pattern's shots as a mini run chart, for Replay rows and B's marginal entries.
// Same grammar as PpRunChart, small: the corridor wash, the fault line (warn hairline), one PpMark per shot
// in the order struck, the picked shot ringed, a stub on the foot for a shot not measured.
// Round 3, FAULT-FORWARD: the same PpFigureScale as the detail figures, so the line spends its height where
// the shots are and the empty corridor collapses to a thin band past a break (marked by a pair of slashes
// at the left). Marks are inset by their ring's radius, so nothing spills out of the box into the next row.
// PUBLIC API (stable; the internals belong to the fault-forward figure work):
//   card            a session card (uses card.spread.{run,bands,faultLines} and card.ticks)
//   roles, brush, medium
//   width (given), implicitHeight = roles.u(22)
//   signals shotActivated(swingDir, shotId), shotHovered(shotId)
Item {
    id: sk
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var card: null
    readonly property var sp: (card && card.spread) || ({})
    readonly property var keyKinds: ["fired", "clean", "ring", "fault", "corridor"]
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    implicitWidth: roles.flows.sparkline * roles.em
    implicitHeight: roles.u(22)
    readonly property real ringSz: roles.ringD * 0.7
    readonly property real pad: Math.max(roles.u(2), ringSz / 2 + 1)
    readonly property var run: (sp.run || []).filter(q => q.assessable !== false)
    // any session length (D7): the shot pitch is the width over the card's own shot count
    readonly property int n: Math.max(1, ((card && card.ticks) || []).length || (sp.run || []).length)
    clip: false

    PpFigureScale {
        id: sc
        bands: sk.sp.bands || []
        faultLines: sk.sp.faultLines || []
        plotTop: sk.pad
        plotHeight: Math.max(4, sk.height - 2 * sk.pad)
        values: sk.run.map(q => q.fy)
        bandPx: Math.max(3, sk.roles.u(3))
        breakPx: Math.max(2, sk.roles.u(2))
        minOmit: 0.18
    }
    function fy(f) { return sc.y(Math.max(0, Math.min(1, f))) }
    // corridor wash, split at the break
    Repeater {
        model: {
            if (!sk.roles || !sc.corridor) return []
            const c = sc.corridor, w = sc.win, out = []
            const cuts = [0, sc.bandLo ? w.lo : 0, sc.bandHi ? w.hi : 1, 1]
            for (let i = 0; i < 3; ++i) { const a = Math.max(c.lo, cuts[i]), b = Math.min(c.hi, cuts[i + 1]); if (b > a + 1e-6) out.push({ f0: a, f1: b }) }
            return out
        }
        delegate: Rectangle {
            required property var modelData
            width: sk.width; y: sk.fy(modelData.f1); height: Math.max(0, sk.fy(modelData.f0) - sk.fy(modelData.f1)); color: sk.roles.wash
        }
    }
    Repeater {
        model: sk.roles ? (sk.sp.faultLines || []) : []
        delegate: Rectangle {
            required property var modelData
            visible: sc.region(modelData.f) === 1
            width: sk.width; height: sk.roles.hw; y: Math.round(sk.fy(modelData.f)); color: sk.roles.fault
        }
    }
    Canvas {
        anchors.fill: parent
        visible: sc.broken
        readonly property var deps: [sc.win, sk.height, sk.roles.ink3]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            if (!sc.broken) return
            ctx.strokeStyle = sk.roles.ink3; ctx.lineWidth = sk.roles.hw
            const k = 2
            const gaps = []
            if (sc.bandLo) gaps.push((sc.winBottom + sc.loBandTop) / 2)
            if (sc.bandHi) gaps.push((sc.winTop + sc.hiBandBottom) / 2)
            for (const yc of gaps) for (const dx of [0, k]) { ctx.beginPath(); ctx.moveTo(dx, yc + k); ctx.lineTo(dx + k, yc - k); ctx.stroke() }
        }
    }
    Repeater {
        model: sk.roles ? ((sk.card && sk.card.ticks) || []) : []
        // the not-measured STUB (D5): a short flat ink3 mark on the foot, never a gap
        delegate: Rectangle {
            required property var modelData
            required property int index
            objectName: "sdSparkStub"
            visible: modelData.state !== "fired" && modelData.state !== "clean"
            width: Math.max(2, Math.min(sk.roles.u(3), sk.width / sk.n * 0.6)); height: sk.roles.stubH(sk.roles.u(9))
            x: (index + 0.5) / sk.n * sk.width - width / 2; y: sk.height - height; color: sk.roles.ink3
        }
    }
    Repeater {
        model: sk.roles ? sk.run : []
        delegate: PpMark {
            required property var modelData
            roles: sk.roles; brush: sk.brush; interactive: sk.interactive
            shotId: modelData.shotId; fired: modelData.state === "fired"
            // a mark never wider than half a shot's pitch, so n shots stay n marks at 7 em
            size: Math.round(Math.max(2.5, Math.min((fired ? sk.roles.markFired : sk.roles.markClean) * 0.8, sk.width / sk.n * 0.55)) * 2) / 2
            ringSize: sk.ringSz
            cx: Math.round(Math.max(sk.ringSz / 2, Math.min(sk.width - sk.ringSz / 2, modelData.fx * sk.width)) * 2) / 2; cy: Math.round(sk.fy(modelData.fy) * 2) / 2
            onActivated: sk.shotActivated(modelData.swingDir || (sk.brush ? sk.brush.dirOf(shotId) : ""), shotId)
            onHoveredChanged: sk.shotHovered(hovered ? shotId : -1)
        }
    }
}
