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
// `corridor` (#10, the Readings rows only): the presenter's { known, lowOpen, highOpen, signalLo/Hi, faultLo/Hi }.
// Drawn UNDER the box and kept quiet (Mark, 18 Oct: "so long as the corridor and fault line are subtle") in the
// typeset's own corridor marks, as Session diagnostics draws them: roles.wash where the condition does not fire,
// a short roles.fault tick at each fault line. The scale widens to
// take in the corridor's graded edges, so a session that sits wholly outside it still shows where it is.
// `valuesOnHover` (the profile rows): while the pointer is over the plot its numbers are laid on it as text (Mark,
// 18 Oct: "all the values you would see on a box plot") — the median and the whiskers' ends above, the quartiles
// below, and with a corridor its edges below and its fault lines above in the fault colour. `texts` is the
// presenter's boxText; a label that would collide with a more important one in its lane is left out.
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
    property var corridor: null
    property var texts: null                     // the presenter's boxText: the plot's numbers, formatted
    property bool valuesOnHover: false
    property bool showValues: false              // the values without a hover (a probe; print, should it want them)
    readonly property bool showingValues: (showValues || (valuesOnHover && hh.hovered)) && ok && !vertical && !!texts
    property bool showPicked: true
    signal shotActivated(int ordinal)
    readonly property bool ok: !!roles && !!box && box.n > 0 && box.q1 !== null && box.q1 !== undefined
    // the box to draw: the presenter's, or an empty one while there is none (bindings never read a null)
    readonly property var b: ok ? box : ({ n: 0, min: 0, max: 0, q1: 0, med: 0, q3: 0, lo: 0, hi: 0, outliers: [] })
    readonly property bool cOk: ok && !!corridor && corridor.known === true
    // the corridor's graded edges (a ceiling has only its high one, a floor only its low one), wherever they fall
    readonly property var cEdges: !cOk ? [] : (corridor.lowOpen ? [] : [corridor.signalLo]).concat(corridor.highOpen ? [] : [corridor.signalHi])
    readonly property real sLo: !ok ? 0 : Math.min.apply(null, [box.min].concat(cEdges))
    readonly property real sHi: !ok ? 1 : Math.max.apply(null, [box.max].concat(cEdges))
    readonly property real pad: ok ? 0.06 * Math.max(1e-6, sHi - sLo) : 0
    readonly property real dLo: isFinite(lo) ? lo : (ok ? sLo - pad : 0)
    readonly property real dHi: isFinite(hi) ? hi : (ok ? sHi + pad : 1)
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

    HoverHandler { id: hh; enabled: bc.valuesOnHover }
    FontMetrics { id: fm; font: bc.roles ? bc.roles.fFigLabel : Qt.font({}) }
    // the labels to draw: [{ x, w, lane (0 above, 1 below), t, tone }], most important first, none overlapping in a lane
    readonly property var valueLabels: {
        if (!showingValues) return []
        const t = texts, L = []
        function add(v, s, lane, pri, tone) { if (s !== undefined && s !== "" && isFinite(v) && v >= dLo && v <= dHi) L.push({ v: v, t: s, lane: lane, pri: pri, tone: tone }) }
        add(b.med, t.med, 0, 0, "ink"); add(b.lo, t.lo, 0, 1, "ink"); add(b.hi, t.hi, 0, 1, "ink")
        add(b.q1, t.q1, 1, 0, "ink"); add(b.q3, t.q3, 1, 0, "ink")
        if (cOk) {
            if (!corridor.lowOpen)  { add(corridor.faultLo, t.faultLo, 0, 2, "fault"); add(corridor.signalLo, t.signalLo, 1, 1, "edge") }
            if (!corridor.highOpen) { add(corridor.faultHi, t.faultHi, 0, 2, "fault"); add(corridor.signalHi, t.signalHi, 1, 1, "edge") }
        }
        L.sort(function (a, c) { return a.pri - c.pri })
        const placed = [[], []], out = []
        for (const l of L) {
            const w = fm.advanceWidth(l.t) + 6
            const x = Math.max(0, Math.min(len - w, pos(l.v) - w / 2))
            if (placed[l.lane].every(function (p) { return x + w + 2 <= p.x || x >= p.x + p.w + 2 })) {
                placed[l.lane].push({ x: x, w: w }); out.push({ x: x, w: w, lane: l.lane, t: l.t, tone: l.tone })
            }
        }
        return out
    }
    // the corridor, under everything: where the condition does not fire, and its fault lines
    Item {
        objectName: "chBoxCorridor"
        visible: bc.cOk && !bc.vertical
        anchors.fill: parent
        readonly property real a: visible ? (bc.corridor.lowOpen ? 0 : bc.pos(Math.max(bc.dLo, bc.corridor.signalLo))) : 0
        readonly property real b: visible ? (bc.corridor.highOpen ? bc.len : bc.pos(Math.min(bc.dHi, bc.corridor.signalHi))) : 0
        Rectangle { x: parent.a; width: Math.max(0, parent.b - parent.a); y: bc.mid - bc.bw / 2 - 2; height: bc.bw + 4
                    color: bc.roles ? bc.roles.wash : "transparent" }
        Repeater {
            model: bc.cOk && !bc.vertical ? [bc.corridor.lowOpen ? NaN : bc.corridor.faultLo, bc.corridor.highOpen ? NaN : bc.corridor.faultHi] : []
            delegate: Rectangle {
                required property var modelData
                visible: isFinite(modelData) && modelData >= bc.dLo && modelData <= bc.dHi
                x: visible ? bc.pos(modelData) - width / 2 : 0; width: bc.lw
                y: bc.mid - bc.bw / 2 - 2; height: bc.bw + 4
                color: bc.roles ? bc.roles.fault : "transparent"
            }
        }
    }
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
    Repeater {
        model: bc.valueLabels
        delegate: Rectangle {
            required property var modelData
            objectName: "chBoxValue"
            x: modelData.x; width: modelData.w; height: fm.height
            y: modelData.lane === 0 ? bc.mid - bc.bw / 2 - 2 - height : bc.mid + bc.bw / 2 + 2
            color: bc.roles ? (bc.roles.knock !== undefined ? bc.roles.knock : bc.roles.paper) : "transparent"
            Text {
                anchors.centerIn: parent
                text: parent.modelData.t; font: fm.font
                color: !bc.roles ? "grey" : parent.modelData.tone === "fault" ? bc.roles.warn : parent.modelData.tone === "edge" ? bc.roles.ink3 : bc.roles.ink
            }
        }
    }
}
