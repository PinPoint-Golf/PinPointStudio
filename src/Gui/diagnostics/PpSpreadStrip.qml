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

// SPREADSTRIP — "How far": the session's shots as a vertical dot strip on the value axis, stacked where
// they coincide, with the norm's curve drawn from the right edge. One PpMark per shot (picked ringed).
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   spread     card.spread / detail.header.spread  (dots[], curve[], bands, faultLines)
//   scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
//   title      panel title ("Spread")
//   roles, brush, medium, interactive
//   signals shotActivated(swingDir, shotId), shotHovered(shotId)
Item {
    id: ss
    objectName: "sdSpreadStrip"
    readonly property int hoveredShot: brush ? brush.hoveredShot : -1
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var spread: null
    property var scale: null
    property string title: "Spread"
    readonly property var keyKinds: ["fired", "clean", "ring", "fault", "corridor"]
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)
    readonly property var s: spread || ({})
    implicitHeight: scale ? scale.plotTop + scale.plotHeight + roles.u(26) : 0

    Text { text: ss.title; font: ss.roles.fSmall; color: ss.roles.ink }
    PpZonePlot { anchors.fill: parent; roles: ss.roles; scale: ss.scale }
    Canvas {
        anchors.fill: parent
        readonly property var deps: [ss.s, ss.width, ss.roles.ink3, ss.scale ? ss.scale.plotHeight : 0]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            const cv = ss.s.curve || []
            if (!ss.scale || cv.length < 2) return
            ctx.strokeStyle = Qt.alpha(ss.roles.ink3, 0.8); ctx.lineWidth = ss.medium === "print" ? 0.75 : 1
            ctx.beginPath()
            // the curve breaks where the axis does
            let prev = -1
            for (let i = 0; i < cv.length; ++i) {
                const x = width - cv[i].d * (width - ss.roles.u(4)), y = ss.scale.y(cv[i].f)
                const r = ss.scale.region ? ss.scale.region(cv[i].f) : 1
                if (i === 0 || r !== prev) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                prev = r
            }
            ctx.stroke()
        }
    }
    Text {
        readonly property var best: { const cv = ss.s.curve || []; let b = null; for (let i = 0; i < cv.length; ++i) if (!b || cv[i].d > b.d) b = cv[i]; return b }
        visible: !!best && !!ss.scale
        text: "norm"; font: ss.roles.fLabel; color: ss.roles.ink3
        // labelled where the curve is true to scale: its best point, or the window edge nearest it
        readonly property real fy: !best || !ss.scale ? 0 : ss.scale.region(best.f) === 1 ? best.f : (ss.scale.region(best.f) === 0 ? ss.scale.win.lo : ss.scale.win.hi)
        x: ss.roles.u(4); y: best && ss.scale ? Math.max(ss.scale.plotTop, ss.scale.y(fy) - height - ss.roles.u(4)) : 0
    }
    Repeater {
        model: ss.roles ? (ss.scale ? (ss.s.dots || []) : []) : []
        delegate: PpMark {
            required property var modelData
            required property int index
            objectName: "sdStripDot"
            readonly property bool current: !!modelData.current
            roles: ss.roles; brush: ss.brush; interactive: ss.interactive
            shotId: modelData.shotId; fired: modelData.state === "fired"
            ringed: ss.brush ? ss.brush.isPicked(shotId) : !!modelData.current
            cx: Math.round((ss.width * 0.5 + (modelData.stack || 0) * ss.roles.u(9)) * 2) / 2; cy: Math.round(ss.scale.y(modelData.f) * 2) / 2
            onActivated: ss.shotActivated(modelData.swingDir || "", shotId)
            onHoveredChanged: ss.shotHovered(hovered ? shotId : -1)
        }
    }
}
