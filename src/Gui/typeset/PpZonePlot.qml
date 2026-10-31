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

// ZONEPLOT — the common ground every value panel stands on: the corridor wash, the zone edges (fault in
// warn, pass dashed, the rest grid hairlines) and the baseline, all through one PpFigureScale.
// Round 3: when the scale is broken (fault-forward), the wash is drawn piece by piece so the BREAK stays
// clear, edges that fall in a compressed band are drawn there at reduced weight, and the break itself is
// marked at both ends of the panel with the conventional pair of slashes — never hidden.
// FIGURE-INTERNAL (used by PpRunChart, PpBoxStrip, PpHistoryStrip).
Item {
    id: zp
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var scale: null
    readonly property var sc: scale
    // the corridor wash, split at the breaks (PpFigureScale.washPieces)
    Repeater {
        model: zp.roles && zp.sc ? zp.sc.washPieces : []
        delegate: Rectangle {
            required property var modelData
            width: zp.width
            y: zp.sc.y(modelData.f1)
            height: Math.max(0, zp.sc.y(modelData.f0) - zp.sc.y(modelData.f1))
            color: zp.roles.wash
        }
    }
    Repeater {
        model: zp.roles ? (zp.sc ? zp.sc.edges : []) : []
        delegate: Item {
            required property var modelData
            readonly property bool banded: zp.sc.region(modelData.f) !== 1
            width: zp.width; height: 1
            y: Math.round(zp.sc.y(modelData.f))
            opacity: banded ? 0.6 : 1
            Rectangle { visible: modelData.kind !== "pass"; width: parent.width; height: zp.roles.hw
                        color: modelData.kind === "fault" ? zp.roles.fault : zp.roles.grid
                        opacity: modelData.kind === "fault" ? 1 : zp.roles.gridOpacity }
            Canvas {
                visible: modelData.kind === "pass"
                width: parent.width; height: 2
                readonly property var deps: [zp.roles.passEdge, width]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset()
                    ctx.strokeStyle = zp.roles.passEdge; ctx.lineWidth = zp.roles.hw
                    ctx.beginPath(); zp.roles.dashPath(ctx, 0, width, 0.5, zp.roles.r.passDash); ctx.stroke()
                }
            }
        }
    }
    // the breaks: a pair of slashes at each end of the gap
    PpBreakSlash {
        anchors.fill: parent
        visible: !!zp.sc && zp.sc.broken
        roles: zp.roles
        k: zp.roles.u(3)
        ys: zp.sc && zp.sc.broken ? zp.sc.breakYs : []
    }
    Rectangle {
        visible: !!zp.sc
        width: parent.width; y: zp.sc ? zp.sc.plotTop + zp.sc.plotHeight + zp.roles.u(4) : 0
        height: zp.roles ? zp.roles.hw : 1; color: zp.roles ? zp.roles.baseline : "transparent"; opacity: zp.roles ? zp.roles.baselineOpacity : 0
    }
}
