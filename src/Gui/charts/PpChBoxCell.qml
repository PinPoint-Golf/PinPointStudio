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

// PPCHBOXCELL — one reading's session as a box plot, and this shot on it (spec 15.8's box, laid on its side so
// a column of readings reads as one profile, or stood up beside a run or at a P-position on a trace).
// Q1–Q3 outlined with a faint fill, the median a 1.5 px rule, whiskers with short caps, a shot past them a
// PpMark (warn when a diagnostics condition reading it fired on that shot), the picked shot ringed at its
// value, the hovered shot (the Brush) a lighter ring. Every number is the presenter's `box`
// ({ n, min, max, q1, med, q3, lo, hi, outliers: [ordinal] }) and `values` (one per shot, index =
// ordinal − 1, null where unmeasured); QML maps them to pixels.
// Colour (§15.4 item 1, D5): a box stands for a METRIC, so it is drawn in the metric's hue (`color`), muted
// to ink3 at rest when `muted` (a profile of 68 coloured boxes is busy) and coloured on hover or when apart.
Item {
    id: bc
    property QtObject roles: null
    property var brush: null
    property var box: null
    property var values: []
    property var fired: []                       // bool per shot
    property real lo: NaN                        // the scale; NaN = the box's own min…max, padded
    property real hi: NaN
    property color color: roles ? roles.ink2 : "grey"
    property bool muted: false
    property bool vertical: false
    property bool interactive: false
    property real markScale: 1
    property bool showPicked: true
    signal shotActivated(int ordinal)
    readonly property bool ok: !!roles && !!box && box.n > 0 && box.q1 !== null && box.q1 !== undefined
    // the box to draw: the presenter's, or an empty one while there is none (bindings never read a null)
    readonly property var b: ok ? box : ({ n: 0, min: 0, max: 0, q1: 0, med: 0, q3: 0, lo: 0, hi: 0, outliers: [] })
    readonly property real pad: ok ? 0.06 * Math.max(1e-6, box.max - box.min) : 0
    readonly property real dLo: isFinite(lo) ? lo : (ok ? box.min - pad : 0)
    readonly property real dHi: isFinite(hi) ? hi : (ok ? box.max + pad : 1)
    readonly property real len: vertical ? height : width
    function pos(v) { const f = (v - dLo) / Math.max(1e-9, dHi - dLo); return vertical ? Math.round((1 - f) * len * 2) / 2 : Math.round(f * len * 2) / 2 }
    readonly property real mid: Math.round((vertical ? width : height) / 2)
    readonly property real bw: Math.max(4, Math.round((roles ? roles.u(roles.ch.boxBody) : 9) * markScale))
    readonly property real lw: roles && roles.isPrint ? roles.hw : 1
    readonly property color ink: !roles ? "grey" : muted ? roles.ink3 : color
    readonly property int pickNo: brush ? brush.pickedShot : -1
    readonly property int hoverNo: brush ? brush.hoveredShot : -1
    function valueOf(ord) { const v = (values || [])[ord - 1]; return v === null || v === undefined ? NaN : v }
    implicitHeight: roles ? roles.u(roles.ch.boxRow) : 16

    Item {
        visible: bc.ok
        anchors.fill: parent
        // whiskers and caps
        Rectangle { visible: !bc.vertical; x: bc.pos(bc.b.lo); width: Math.max(0, bc.pos(bc.b.q1) - bc.pos(bc.b.lo)); y: bc.mid - bc.lw / 2; height: bc.lw; color: bc.ink }
        Rectangle { visible: !bc.vertical; x: bc.pos(bc.b.q3); width: Math.max(0, bc.pos(bc.b.hi) - bc.pos(bc.b.q3)); y: bc.mid - bc.lw / 2; height: bc.lw; color: bc.ink }
        Rectangle { visible: !bc.vertical; x: bc.pos(bc.b.lo) - bc.lw / 2; width: bc.lw; y: bc.mid - bc.bw / 4; height: bc.bw / 2; color: bc.ink }
        Rectangle { visible: !bc.vertical; x: bc.pos(bc.b.hi) - bc.lw / 2; width: bc.lw; y: bc.mid - bc.bw / 4; height: bc.bw / 2; color: bc.ink }
        Rectangle { visible: bc.vertical; y: bc.pos(bc.b.hi); height: Math.max(0, bc.pos(bc.b.q3) - bc.pos(bc.b.hi)); x: bc.mid - bc.lw / 2; width: bc.lw; color: bc.ink }
        Rectangle { visible: bc.vertical; y: bc.pos(bc.b.q1); height: Math.max(0, bc.pos(bc.b.lo) - bc.pos(bc.b.q1)); x: bc.mid - bc.lw / 2; width: bc.lw; color: bc.ink }
        Rectangle { visible: bc.vertical; y: bc.pos(bc.b.hi) - bc.lw / 2; height: bc.lw; x: bc.mid - bc.bw / 4; width: bc.bw / 2; color: bc.ink }
        Rectangle { visible: bc.vertical; y: bc.pos(bc.b.lo) - bc.lw / 2; height: bc.lw; x: bc.mid - bc.bw / 4; width: bc.bw / 2; color: bc.ink }
        // the middle half
        Rectangle {
            x: bc.vertical ? bc.mid - bc.bw / 2 : bc.pos(bc.b.q1)
            y: bc.vertical ? bc.pos(bc.b.q3) : bc.mid - bc.bw / 2
            width: bc.vertical ? bc.bw : Math.max(bc.lw, bc.pos(bc.b.q3) - bc.pos(bc.b.q1))
            height: bc.vertical ? Math.max(bc.lw, bc.pos(bc.b.q1) - bc.pos(bc.b.q3)) : bc.bw
            color: bc.roles ? Qt.alpha(bc.muted ? bc.roles.ink3 : bc.color, bc.muted ? 0.08 : bc.roles.boxFill) : "transparent"
            border.width: bc.lw; border.color: bc.ink
        }
        // the median
        Rectangle {
            readonly property real mw: bc.roles && bc.roles.isPrint ? 1 : 1.5
            x: bc.vertical ? bc.mid - bc.bw / 2 - 2 : bc.pos(bc.b.med) - mw / 2
            y: bc.vertical ? bc.pos(bc.b.med) - mw / 2 : bc.mid - bc.bw / 2 - 2
            width: bc.vertical ? bc.bw + 4 : mw; height: bc.vertical ? mw : bc.bw + 4
            color: bc.muted ? bc.roles.ink2 : bc.color
        }
    }
    // shots past the whiskers, then the picked shot, then the hovered echo
    Repeater {
        model: bc.ok ? (bc.b.outliers || []) : []
        delegate: PpMark {
            required property var modelData
            readonly property real v: bc.valueOf(modelData)
            visible: modelData !== bc.pickNo && isFinite(v)
            roles: bc.roles; brush: bc.brush; interactive: bc.interactive
            shotId: modelData; fired: !!(bc.fired || [])[modelData - 1]
            size: (fired ? bc.roles.markFired : bc.roles.markClean) * bc.markScale
            cx: bc.vertical ? bc.mid : (isFinite(v) ? bc.pos(v) : 0); cy: bc.vertical ? (isFinite(v) ? bc.pos(v) : 0) : bc.mid
            onActivated: bc.shotActivated(modelData)
        }
    }
    PpMark {
        readonly property real v: bc.valueOf(bc.pickNo)
        visible: bc.ok && bc.showPicked && bc.pickNo > 0 && isFinite(v)
        roles: bc.roles; brush: bc.brush; interactive: false
        shotId: bc.pickNo; fired: !!(bc.fired || [])[bc.pickNo - 1]; ringed: true
        size: bc.roles ? (fired ? bc.roles.markFired : bc.roles.markClean) * bc.markScale : 0
        ringSize: bc.roles ? bc.roles.ringD * Math.min(1, bc.markScale + 0.1) : 0
        cx: visible ? (bc.vertical ? bc.mid : bc.pos(v)) : 0; cy: visible ? (bc.vertical ? bc.pos(v) : bc.mid) : 0
    }
    PpMark {
        readonly property real v: bc.valueOf(bc.hoverNo)
        visible: bc.ok && bc.hoverNo > 0 && bc.hoverNo !== bc.pickNo && isFinite(v)
        roles: bc.roles; brush: bc.brush; interactive: false
        shotId: bc.hoverNo; fired: !!(bc.fired || [])[bc.hoverNo - 1]; ringed: false
        size: bc.roles ? (fired ? bc.roles.markFired : bc.roles.markClean) * bc.markScale : 0
        cx: visible ? (bc.vertical ? bc.mid : bc.pos(v)) : 0; cy: visible ? (bc.vertical ? bc.pos(v) : bc.mid) : 0
    }
}
