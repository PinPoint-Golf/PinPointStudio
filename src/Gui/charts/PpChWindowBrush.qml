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

// PPCHWINDOWBRUSH — the time window over the whole recording (CONTRACT Addendum, F14): the overview sparkline
// of each visible metric in its own hue (thisShot.overview, each scaled to its own vLo…vHi), one ink3 track
// for the recording, the P-positions as hairlines with their tags (thisShot.recordingTicks; top and impact
// stronger, tags thinned by width, impact kept), the reader's window as the one darker span with two
// handles, and the playhead. Drag the window to move it, an edge to resize it, or sweep anywhere else for a
// new one; the result is a CUSTOM window handed to the body (customRequested), which the presenter then reads
// and reduces for. Pixels ↔ µs only: every time it shows is the presenter's or shotReplay's.
// objectName: chWindowBrush.
Item {
    id: wb
    objectName: "chWindowBrush"
    property QtObject roles: null
    property bool interactive: true
    property real spanUs: 0                      // the recording: 0 … spanUs (shotReplay.endUs − startUs)
    property real winStartUs: 0                  // thisShot.window.startUs
    property real winEndUs: 0                    // thisShot.window.endUs
    property var ticks: []                       // thisShot.recordingTicks [{ phase, tag, us, landmark }]
    property var overview: []                    // thisShot.overview [{ key, t (µs), v, vLo, vHi }]
    property real playUs: -1                     // shotReplay.positionUs
    signal customRequested(real startUs, real endUs)
    readonly property real trackH: roles ? roles.u(10) : 10
    readonly property real sparkH: (overview || []).length && roles ? roles.u(18) : 0
    implicitHeight: (roles ? roles.u(34) : 34) + sparkH
    height: implicitHeight
    function xOf(us) { return spanUs > 0 ? us / spanUs * width : 0 }
    function usOf(px) { return Math.max(0, Math.min(spanUs, px / Math.max(1, width) * spanUs)) }
    // the drag: "", "move", "a" (start edge), "b" (end edge), "sweep"
    property string drag: ""
    property real dA: 0
    property real dB: 0
    property real grabUs: 0
    readonly property real showA: drag !== "" ? Math.min(dA, dB) : winStartUs
    readonly property real showB: drag !== "" ? Math.max(dA, dB) : winEndUs

    // the overview: each visible metric over the whole recording, in its own hue
    Canvas {
        visible: wb.sparkH > 0
        y: 0; width: wb.width; height: wb.sparkH
        readonly property var deps: [wb.overview, width, height, wb.spanUs, wb.roles ? wb.roles.ink : "black"]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            if (!wb.roles || wb.spanUs <= 0) return
            const ok = function (v) { return v !== null && v !== undefined && isFinite(v) }
            for (const o of (wb.overview || [])) {
                const lo = o.vLo, hi = o.vHi
                if (!ok(lo) || !ok(hi)) continue
                ctx.strokeStyle = Qt.alpha(wb.roles.hue(o.key), 0.75); ctx.lineWidth = 1
                ctx.beginPath(); let pen = false
                for (let i = 0; i < (o.t || []).length; ++i) {
                    const v = o.v[i]
                    if (!ok(v)) { pen = false; continue }
                    const x = wb.xOf(o.t[i]), y = height - 1 - (v - lo) / Math.max(1e-9, hi - lo) * (height - 2)
                    if (!pen) { ctx.moveTo(x, y); pen = true } else ctx.lineTo(x, y)
                }
                ctx.stroke()
            }
        }
    }
    Item {
        id: track
        y: wb.sparkH + (wb.roles ? wb.roles.u(12) : 12)
        width: wb.width; height: wb.trackH
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 2; color: wb.roles ? Qt.alpha(wb.roles.ink3, 0.45) : "grey" }
        Repeater {
            model: wb.roles ? wb.ticks : []
            delegate: Item {
                required property var modelData
                required property int index
                readonly property real px: Math.round(wb.xOf(modelData.us))
                readonly property bool strong: modelData.landmark !== ""
                Rectangle { x: parent.px; y: 0; width: 1; height: wb.trackH; color: parent.strong ? wb.roles.ink2 : wb.roles.ink3; opacity: 0.8 }
                Text {
                    // a tag that would touch the one before it gives way (impact never does)
                    readonly property real prev: index > 0 ? wb.xOf(wb.ticks[index - 1].us) : -1e9
                    visible: parent.px - prev >= wb.roles.u(18) || modelData.landmark === "impact"
                    x: parent.px - width / 2; y: -height - wb.roles.u(1)
                    text: modelData.tag; font: wb.roles.fFigLabel; color: parent.strong ? wb.roles.ink2 : wb.roles.ink3
                }
            }
        }
        // the window
        Rectangle {
            x: Math.round(wb.xOf(wb.showA)); width: Math.max(2, Math.round(wb.xOf(wb.showB)) - x)
            anchors.verticalCenter: parent.verticalCenter; height: wb.trackH
            color: wb.roles ? Qt.alpha(wb.roles.ink, 0.10) : "transparent"
            border.width: 0
            Rectangle { x: 0; width: 2; height: parent.height; color: wb.roles ? wb.roles.ink : "black" }
            Rectangle { x: parent.width - 2; width: 2; height: parent.height; color: wb.roles ? wb.roles.ink : "black" }
        }
        Rectangle {
            visible: wb.playUs >= 0 && wb.playUs <= wb.spanUs
            x: Math.round(wb.xOf(wb.playUs)); y: -wb.roles.u(2); width: 1; height: wb.trackH + wb.roles.u(4)
            color: wb.roles ? wb.roles.ink2 : "black"
        }
        MouseArea {
            enabled: wb.interactive && wb.spanUs > 0
            anchors.fill: parent; anchors.topMargin: -wb.roles.u(8); anchors.bottomMargin: -wb.roles.u(6)
            hoverEnabled: true
            readonly property real edge: wb.roles ? wb.roles.u(6) : 6
            function zone(px) {
                const a = wb.xOf(wb.winStartUs), b = wb.xOf(wb.winEndUs)
                if (Math.abs(px - a) <= edge) return "a"
                if (Math.abs(px - b) <= edge) return "b"
                if (px > a && px < b) return "move"
                return "sweep"
            }
            cursorShape: { const z = containsMouse ? zone(mouseX) : ""; return z === "a" || z === "b" ? Qt.SizeHorCursor : z === "move" ? Qt.OpenHandCursor : Qt.CrossCursor }
            onPressed: (e) => {
                const z = zone(e.x), us = wb.usOf(e.x)
                wb.drag = z; wb.grabUs = us
                wb.dA = z === "sweep" ? us : wb.winStartUs
                wb.dB = z === "sweep" ? us : wb.winEndUs
            }
            onPositionChanged: (e) => {
                if (!pressed || wb.drag === "") return
                const us = wb.usOf(e.x)
                if (wb.drag === "a") wb.dA = us
                else if (wb.drag === "b") wb.dB = us
                else if (wb.drag === "sweep") wb.dB = us
                else { const d = us - wb.grabUs, w = wb.winEndUs - wb.winStartUs
                       const a = Math.max(0, Math.min(wb.spanUs - w, wb.winStartUs + d)); wb.dA = a; wb.dB = a + w }
            }
            onReleased: {
                const a = Math.min(wb.dA, wb.dB), b = Math.max(wb.dA, wb.dB), kind = wb.drag
                wb.drag = ""
                if (kind !== "" && b - a > wb.spanUs * 0.01) wb.customRequested(a, b)
            }
            onCanceled: wb.drag = ""
        }
    }
    Text {
        visible: wb.interactive
        anchors.right: parent.right; y: track.y + track.height + (wb.roles ? wb.roles.u(2) : 2)
        text: "drag to select · drag edges to resize"; font: wb.roles ? wb.roles.fLabel : Qt.font({}); color: wb.roles ? wb.roles.ink3 : "grey"
    }
}
