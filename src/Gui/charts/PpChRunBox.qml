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

// PPCHRUNBOX — one reading through the session (ChartsRelationsPresenter RUN): one PpMark per shot in the order
// struck (warn where a condition reading it fired), this shot ringed, a not-measured shot a STUB on the foot
// (never a gap), and right of it the session's box on the same value axis (spec 15.8's run + box). The drift
// line (least squares on shot order) is drawn only when the presenter gives one (it clears chance); the
// caption always carries r and its interval. Ticks are the presenter's (ticks()). ON PAPER (the session report's
// Wrist page) a point may carry its own `label` (the report's swing number, where the ordinal only places it), and
// `hueOf` gives the metric's ink theme-free (the report's palette: roles.hue reads the live theme).
// objectName: chRun (+ readingId).
Item {
    id: rb
    objectName: "chRun"
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null
    property var run: null
    property bool interactive: false
    property bool showCaption: true
    property bool showBox: true
    property var hueOf: null                    // print: key → colour, theme-free; null = roles.hue
    property real plotH: roles ? roles.u(roles.isPrint ? 84 : 96) : 96
    property bool atom: true
    readonly property var keyKinds: ["box", "ring", "fired"]
    signal shotActivated(int ordinal)
    readonly property string readingId: run ? run.readingId : ""
    readonly property string key: run ? run.key : ""
    readonly property var pts: run && run.points ? run.points : []
    readonly property int n: pts.length
    readonly property real lo: run && run.yRange ? run.yRange.lo : 0
    readonly property real hi: run && run.yRange ? run.yRange.hi : 1
    readonly property var yt: ticker && run ? ticker.ticks(lo, hi, 3, run.unit) : []
    readonly property real gutL: roles ? roles.u(40) : 40
    readonly property real boxW: showBox && roles ? roles.u(22) : 0
    readonly property real pTop: roles ? (showCaption ? capT.height + roles.u(6) : roles.u(6)) + (run && run.unitText ? roles.u(13) : 0) : 20
    readonly property real pw: Math.max(10, width - gutL - boxW - (roles ? roles.u(showBox ? 12 : 4) : 12))
    function sx(ord) { return gutL + (ord - 0.5) / Math.max(1, n) * pw }
    function vy(v) { return pTop + (1 - (v - lo) / Math.max(1e-9, hi - lo)) * plotH }
    readonly property int step: roles ? roles.shotLabelStep(pw / Math.max(1, n), roles.u(14)) : 1
    implicitHeight: pTop + plotH + (roles ? roles.u(22) : 22)
    height: implicitHeight

    Text {
        id: capT
        visible: rb.showCaption && !!rb.run
        width: parent.width; wrapMode: Text.WordWrap
        text: rb.run ? (rb.run.caption || "") : ""
        font: rb.roles ? rb.roles.fSmall : Qt.font({}); color: rb.roles ? rb.roles.ink2 : "grey"
    }
    Repeater {
        model: rb.roles ? rb.yt : []
        delegate: Item {
            required property var modelData
            Rectangle { x: rb.gutL; width: rb.pw; y: Math.round(rb.vy(modelData.v)); height: rb.roles.hw; color: rb.roles.grid; opacity: rb.roles.gridOpacity }
            Text { x: rb.gutL - width - rb.roles.u(5); y: rb.vy(modelData.v) - height / 2; text: modelData.text; font: rb.roles.fFigLabel; color: rb.roles.ink3 }
        }
    }
    Text { visible: !!rb.run && !!rb.run.unitText; x: 0; y: rb.pTop - height - (rb.roles ? rb.roles.u(1) : 1); text: rb.run ? (rb.run.unitText || "") : ""
           font: rb.roles ? rb.roles.fLabelUnit : Qt.font({}); color: rb.roles ? rb.roles.ink3 : "grey" }
    Rectangle { visible: !!rb.roles; x: rb.gutL; y: rb.pTop + rb.plotH; width: rb.pw; height: rb.roles ? rb.roles.hw : 1; color: rb.roles ? rb.roles.baseline : "grey" }
    // the drift line, in the metric's hue, only where it clears chance (the presenter's line)
    Canvas {
        anchors.fill: parent
        readonly property var ln: rb.run && rb.run.drift ? rb.run.drift.line : null
        readonly property var deps: [ln, rb.width, rb.lo, rb.hi, rb.plotH, rb.key]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            if (!rb.roles || !ln) return
            ctx.strokeStyle = rb.hueOf ? rb.hueOf(rb.key) : rb.roles.hue(rb.key); ctx.lineWidth = rb.roles.isPrint ? 0.75 : 1.2
            ctx.beginPath(); ctx.moveTo(rb.sx(ln.x0), rb.vy(ln.y0)); ctx.lineTo(rb.sx(ln.x1), rb.vy(ln.y1)); ctx.stroke()
        }
    }
    Repeater {
        model: rb.roles ? rb.pts : []
        delegate: Item {
            required property var modelData
            required property int index
            readonly property bool has: modelData.measured === true && modelData.value !== null && modelData.value !== undefined
            PpMark {
                visible: parent.has
                roles: rb.roles; brush: rb.brush; interactive: rb.interactive
                shotId: modelData.ordinal; fired: modelData.fired === true
                cx: Math.round(rb.sx(modelData.ordinal) * 2) / 2; cy: parent.has ? Math.round(rb.vy(modelData.value) * 2) / 2 : 0
                onActivated: rb.shotActivated(modelData.ordinal)
            }
            // not measured: a stub on the foot, never a gap
            Rectangle {
                visible: !parent.has
                width: Math.max(2, rb.roles.u(4)); height: rb.roles.stubH(rb.roles.u(12))
                x: rb.sx(modelData.ordinal) - width / 2; y: rb.pTop + rb.plotH - height; color: rb.roles.ink3
            }
            Text {
                readonly property int pk: rb.brush ? rb.brush.pickedShot : -1
                readonly property int hv: rb.brush ? rb.brush.hoveredShot : -1
                visible: rb.step === 1 || rb.roles.axisLabelShown(index, rb.n, Math.max(5, rb.step), [pk - 1, hv - 1])
                x: rb.sx(modelData.ordinal) - width / 2; y: rb.pTop + rb.plotH + rb.roles.u(4)
                text: modelData.label !== undefined ? String(modelData.label) : String(modelData.ordinal); font: rb.roles.fFigLabel
                color: modelData.ordinal === pk || modelData.ordinal === hv ? rb.roles.ink : rb.roles.ink3
            }
        }
    }
    PpChBoxCell {
        visible: rb.showBox && !!rb.run
        vertical: true
        x: rb.gutL + rb.pw + (rb.roles ? rb.roles.u(12) : 12); y: rb.pTop; width: rb.boxW; height: rb.plotH
        lo: rb.lo; hi: rb.hi
        roles: rb.roles; brush: rb.brush; interactive: rb.interactive
        box: rb.run ? rb.run.box : null
        values: rb.pts.map(function (p) { return p.measured ? p.value : null })
        fired: rb.pts.map(function (p) { return p.fired === true })
        color: rb.hueOf ? rb.hueOf(rb.key) : rb.roles && rb.key ? rb.roles.hue(rb.key) : "grey"
        onShotActivated: (o) => rb.shotActivated(o)
    }
}
