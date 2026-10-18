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

// BOXREADING — a box plot's full reading, shown while the box is EXAMINED (hovered): the upper whisker
// end, Q3, the median, Q1 and the lower whisker end, each a small number at its own height with a
// hairline tick from the box edge, plus "15 shots · 1 outlier" under them. The labels stand on one side
// of the box — right by default, left when the right lacks the room or would run into a zone label in
// the figure's right gutter at the reading's heights (`forceSide` overrides); labels closer than a line are
// nudged apart as a group centred on their true heights, and each tick bends to its label from the
// TRUE height, so nothing points at a value it does not hold. A paper backing keeps the numbers legible
// over whatever they cross (the next column's shots, the zone labels).
// Drawn by PpBoxPlot (the one box plot) for a VERTICAL box: Session diagnostics' Spread box and across-sessions
// strip, and the Charts boxes stood up at the P-positions of an opened reading.
// PUBLIC API:
//   items              [{ y, text, strong, f }] in any order: each value's TRUE height in this item's coordinates and
//                      its label, formatted by whoever owns the numbers; strong = the median, set in ink; f, the
//                      caller's own value, rides along on the label
//   footLimit          how far down the reading may reach before its count line goes above it instead
//   footer             the count line ("15 shots · 1 outlier")
//   edgeL, edgeR       the box's left and right edges, in this item's coordinates
//   roomLeft, roomRight   how far the reading may reach on each side (x limits, this item's coordinates)
//   gutterX, gutterYs  where the zone-label gutter starts and the heights of its labels, to keep clear of
//   forceSide          "" (decide) | "right" | "left"; readonly side
//   readonly blockW    the reading's width from the box edge, for the caller's choice of side
//   readonly x0, x1    the reading's extent, in this item's coordinates
Item {
    id: rd
    objectName: "sdBoxReading"
    property QtObject roles: null
    property var items: []
    property real footLimit: 1e6
    property string footer: ""
    property real edgeL: 0
    property real edgeR: 0
    property string forceSide: ""
    property real roomLeft: -1e6
    property real roomRight: 1e6
    property real gutterX: 1e6
    property var gutterYs: []
    property color paper: roles ? roles.paper : "white"
    readonly property bool ok: !!roles
    readonly property var texts: (items || []).map(it => it.text || "")
    TextMetrics { id: numM; font: rd.roles ? rd.roles.fFigLabel : Qt.font({}); text: { let w = ""; for (const t of rd.texts) if (t.length > w.length) w = t; return w } }
    TextMetrics { id: footM; font: rd.roles ? rd.roles.fLabel : Qt.font({}); text: rd.footer }
    readonly property real lineH: ok ? Math.ceil(numM.height) + roles.u(1) : 12
    readonly property real tickOut: ok ? roles.u(4) : 4
    readonly property real gap: ok ? roles.u(8) : 8
    readonly property real textW: Math.max(numM.advanceWidth, footer ? footM.advanceWidth : 0)
    readonly property real blockW: gap + textW + (ok ? roles.u(4) : 4)
    readonly property real spanTop: Math.min(topY, footer ? footY : topY) - lineH / 2
    readonly property real spanBot: Math.max(botY, footer ? footY + lineH : botY) + lineH / 2
    readonly property bool fitsRight: edgeR + blockW <= roomRight
                                      && !(edgeR + blockW > gutterX && (gutterYs || []).some(y => y >= spanTop && y <= spanBot))
    readonly property bool fitsLeft: edgeL - blockW >= roomLeft
    readonly property string side: forceSide !== "" ? forceSide : (fitsRight || !fitsLeft ? "right" : "left")
    readonly property bool onRight: side !== "left"
    readonly property real x0: onRight ? edgeR + (ok ? roles.u(6) : 6) : edgeL - blockW
    readonly property real x1: onRight ? edgeR + blockW : edgeL - (ok ? roles.u(6) : 6)

    // the labels' heights: groups that would overlap merge, each group centred on its members' true heights
    readonly property var placed: {
        if (!ok) return []
        const its = (items || []).map((it, i) => ({ i: i, f: it.f, strong: !!it.strong, text: texts[i], y: it.y }))
                                 .filter(q => isFinite(q.y)).sort((a, b) => a.y - b.y)
        const h = lineH
        let cl = its.map(q => ({ m: [q], top: q.y - h / 2 }))
        let merged = true
        while (merged) {
            merged = false
            for (let k = 0; k + 1 < cl.length; ++k) {
                if (cl[k].top + cl[k].m.length * h > cl[k + 1].top) {
                    const m = cl[k].m.concat(cl[k + 1].m)
                    const mean = m.reduce((a, q) => a + q.y, 0) / m.length
                    cl.splice(k, 2, { m: m, top: mean - m.length * h / 2 })
                    merged = true
                    break
                }
            }
        }
        const out = []
        for (const c of cl) for (let k = 0; k < c.m.length; ++k) out.push(Object.assign({}, c.m[k], { ly: c.top + (k + 0.5) * h }))
        return out
    }
    readonly property real topY: placed.length ? placed[0].ly - lineH / 2 : 0
    readonly property real botY: placed.length ? placed[placed.length - 1].ly + lineH / 2 : 0
    // the count line goes under the labels, or over them when that would leave the plot
    readonly property bool footAbove: ok && botY + lineH + roles.u(2) > footLimit
    readonly property real footY: footAbove ? topY - lineH - roles.u(2) : botY + roles.u(2)

    visible: ok && placed.length > 0
    Rectangle {
        x: rd.x0; width: rd.x1 - rd.x0
        y: Math.min(rd.topY, rd.footer ? rd.footY : rd.topY) - (rd.ok ? rd.roles.u(2) : 2)
        height: Math.max(rd.botY, rd.footer ? rd.footY + rd.lineH : rd.botY) - y + (rd.ok ? rd.roles.u(2) : 2)
        color: Qt.alpha(rd.paper, 0.92)
    }
    Canvas {
        id: leaders
        x: Math.min(rd.edgeL, rd.x0) - 2; y: 0
        width: Math.max(rd.edgeR, rd.x1) - x + 2; height: rd.parent ? Math.max(rd.parent.height, rd.botY + rd.lineH * 2) : 0
        readonly property var deps: [rd.placed, rd.side, rd.edgeL, rd.edgeR, rd.roles ? rd.roles.ink3 : "black"]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            if (!rd.ok) return
            ctx.strokeStyle = rd.roles.ink3; ctx.lineWidth = rd.roles.hw
            const dir = rd.onRight ? 1 : -1, e = (rd.onRight ? rd.edgeR : rd.edgeL) - x
            for (const q of rd.placed) {
                const yt = Math.round(q.y) + 0.5, yl = Math.round(q.ly) + 0.5
                ctx.beginPath()
                ctx.moveTo(e + dir * 1, yt); ctx.lineTo(e + dir * rd.tickOut, yt)
                ctx.lineTo(e + dir * (rd.gap - 2), yl)
                ctx.stroke()
            }
        }
    }
    Repeater {
        model: rd.placed
        delegate: Text {
            required property var modelData
            objectName: "sdBoxReadingLabel"
            readonly property var f: modelData.f        // the caller's own value for this label, carried for its checks
            text: modelData.text || ""; font: rd.roles.fFigLabel
            color: modelData.strong ? rd.roles.ink : rd.roles.ink2
            x: rd.onRight ? rd.edgeR + rd.gap : rd.edgeL - rd.gap - width
            y: Math.round(modelData.ly - height / 2)
        }
    }
    Text {
        objectName: "sdBoxReadingCount"
        visible: rd.footer !== ""
        text: rd.footer; font: rd.roles ? rd.roles.fLabel : Qt.font({}); color: rd.roles ? rd.roles.ink3 : "grey"
        x: rd.onRight ? rd.edgeR + rd.gap : rd.edgeL - rd.gap - width
        y: Math.round(rd.footY + (rd.lineH - height) / 2)
    }
}
