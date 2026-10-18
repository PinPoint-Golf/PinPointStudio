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

// BOXPLOT — THE box plot. Every box in the app draws through this (Mark, 18 Oct 2026: "consolidate into a
// reusable component and make hover work everywhere a box plot is shown"): the Charts rows, runs, traces,
// kinds, scatter margins and Wrist rows (PpChBoxCell), Session diagnostics' Spread box (PpBoxStrip) and its
// across-sessions boxes (PpHistoryStrip). No number is worked out here: the statistics come from the one C++
// implementation (typeset/box_stats.h) and the labels from whoever owns their formatting.
//
// Along an axis the caller maps (`map(v)` → px; across the item, or down it when `vertical`), centred on
// `cross`: the middle half as an outlined box with a faint fill, the median rule, whiskers to `lo` and `hi`
// with short caps, the caller's shot marks (PpMark: the outliers, the picked shot ringed, a hovered echo),
// and — across only — a corridor: the typeset's wash where the condition does not fire, a fault tick at each
// fault line. EXAMINED (its own passive hover with `valuesOnHover`, or `examined` from a caller that owns the
// hover) it lays its values on the plot as text (Mark: "all the values you would see on a box plot"): across,
// the median, whisker ends and fault lines above and the quartiles and corridor edges below, a label that
// would collide with a more important one in its lane left out; stood up, PpBoxReading beside the box with a
// tick from each true height and the count line.
// PUBLIC API
//   roles          a PpTypeset (PpChRoles / PpDiagRoles)
//   vertical       the axis runs down the item
//   map            function(v) → px along the axis in this item's coordinates (v in the caller's units). Drawing
//                  snaps it to half pixels; the vertical reading's ticks start at the exact value.
//   cross          the box's centre line across the axis, px
//   stats          { lo, q1, med, q3, hi } in the caller's units; absent or non-finite = no box
//   bodyW, capW, medianOver, lw, medW   the box's thickness, the caps' length, how far the median rule reaches
//                  past the box each side, the outline/whisker and median weights
//   ink, medianInk, fill                the outline and whiskers, the median, the box's fill
//   marks          [{ at, shotId, fired, role, ringed?, size?, ringSize?, step?, interactive?, emitsHover?, … }]:
//                  a shot drawn at `at`, side-stepped `step` × stepPx across the axis; every field rides along to
//                  markActivated / markHovered. Keep it independent of the hover: a list rebuilt as the pointer
//                  moves would recreate the mark under the pointer and drop its hover.
//   echo           the hovered shot's echo, the same shape, or null — its own mark, so the hover never rebuilds
//                  `marks`
//   brush          the shared PpBrush the marks read and hover
//   corridor       { known, lowOpen, highOpen, signalLo, signalHi, faultLo, faultHi } (across only)
//   texts          { lo, q1, med, q3, hi [, signalLo, signalHi, faultLo, faultHi] } — the labels, formatted
//   valuesOnHover  examine on its own hover; examined: examine regardless
//   footer, readingRoomLeft/Right, readingGutterX/Ys, readingBottom   the vertical reading's count line and room
//   namePrefix     objectNames <prefix>Body, Median, WhiskerHi, WhiskerLo, Mark, Value, Corridor
//   signals        markActivated(var mark), markHovered(var mark, bool on)
//   readonly       hasBox, showingValues; reading (alias): the vertical PpBoxReading
Item {
    id: bp
    property QtObject roles: null
    property bool vertical: false
    property var map: function (v) { return v }
    property real cross: vertical ? width / 2 : height / 2
    property var stats: null
    property real bodyW: 9
    property real capW: Math.round(bodyW / 2)
    property real medianOver: 2
    property real lw: 1
    property real medW: 1.5
    property color ink: roles ? roles.ink2 : "grey"
    property color medianInk: ink
    property color fill: roles ? Qt.alpha(roles.ink3, 0.10) : "transparent"
    property var marks: []
    property var echo: null
    property var brush: null
    property real stepPx: roles ? roles.u(7) : 7
    property var corridor: null
    property var texts: null
    property bool valuesOnHover: false
    property bool examined: false
    property string footer: ""
    property real readingRoomLeft: -1e6
    property real readingRoomRight: 1e6
    property real readingGutterX: 1e6
    property var readingGutterYs: []
    property real readingBottom: 1e6
    property string namePrefix: "chBox"
    signal markActivated(var mark)
    signal markHovered(var mark, bool on)

    readonly property var s: stats || ({})
    function fin(v) { return v !== undefined && v !== null && isFinite(v) }
    readonly property bool hasBox: !!roles && fin(s.q1) && fin(s.q3) && fin(s.med) && fin(s.lo) && fin(s.hi)
    // drawing snaps to half pixels (lines and marks land crisp); the exact value is for the reading's ticks
    function px(v) { return Math.round(map(v) * 2) / 2 }
    readonly property bool showingValues: hasBox && !!texts && (examined || (valuesOnHover && hh.hovered))
    property alias reading: rd

    HoverHandler { id: hh; enabled: bp.valuesOnHover }

    // ── the corridor, under everything (across only) ──
    readonly property bool cOk: !vertical && !!corridor && corridor.known === true && hasBox
    readonly property real len: vertical ? height : width
    Item {
        objectName: bp.namePrefix + "Corridor"
        visible: bp.cOk
        anchors.fill: parent
        readonly property real a: visible ? (bp.corridor.lowOpen ? 0 : Math.max(0, Math.min(bp.len, bp.px(bp.corridor.signalLo)))) : 0
        readonly property real b: visible ? (bp.corridor.highOpen ? bp.len : Math.max(0, Math.min(bp.len, bp.px(bp.corridor.signalHi)))) : 0
        Rectangle { x: parent.a; width: Math.max(0, parent.b - parent.a); y: bp.cross - bp.bodyW / 2 - 2; height: bp.bodyW + 4
                    color: bp.roles ? bp.roles.wash : "transparent" }
        Repeater {
            model: bp.cOk ? [bp.corridor.lowOpen ? NaN : bp.corridor.faultLo, bp.corridor.highOpen ? NaN : bp.corridor.faultHi] : []
            delegate: Rectangle {
                required property var modelData
                readonly property real p: isFinite(modelData) ? bp.px(modelData) : NaN
                visible: isFinite(p) && p >= 0 && p <= bp.len
                x: visible ? p - width / 2 : 0; width: bp.lw
                y: bp.cross - bp.bodyW / 2 - 2; height: bp.bodyW + 4
                color: bp.roles ? bp.roles.fault : "transparent"
            }
        }
    }

    // ── the box ──
    Item {
        visible: bp.hasBox
        anchors.fill: parent
        readonly property real pLo: bp.hasBox ? bp.px(bp.s.lo) : 0
        readonly property real pQ1: bp.hasBox ? bp.px(bp.s.q1) : 0
        readonly property real pMed: bp.hasBox ? bp.px(bp.s.med) : 0
        readonly property real pQ3: bp.hasBox ? bp.px(bp.s.q3) : 0
        readonly property real pHi: bp.hasBox ? bp.px(bp.s.hi) : 0
        // along the axis the box runs from the smaller to the larger end (down the item the high values sit on top)
        readonly property real b0: Math.min(pQ1, pQ3)
        readonly property real b1: Math.max(pQ1, pQ3)
        // the whiskers: from each box edge to its end, capped
        Rectangle { objectName: bp.namePrefix + "WhiskerHi"
                    readonly property real e0: Math.min(parent.pQ3, parent.pHi)
                    readonly property real e1: Math.max(parent.pQ3, parent.pHi)
                    x: bp.vertical ? bp.cross - bp.lw / 2 : e0; width: bp.vertical ? bp.lw : e1 - e0
                    y: bp.vertical ? e0 : bp.cross - bp.lw / 2; height: bp.vertical ? e1 - e0 : bp.lw; color: bp.ink }
        Rectangle { objectName: bp.namePrefix + "WhiskerLo"
                    readonly property real e0: Math.min(parent.pQ1, parent.pLo)
                    readonly property real e1: Math.max(parent.pQ1, parent.pLo)
                    x: bp.vertical ? bp.cross - bp.lw / 2 : e0; width: bp.vertical ? bp.lw : e1 - e0
                    y: bp.vertical ? e0 : bp.cross - bp.lw / 2; height: bp.vertical ? e1 - e0 : bp.lw; color: bp.ink }
        Repeater {
            model: [parent.pLo, parent.pHi]
            delegate: Rectangle {
                required property real modelData
                x: bp.vertical ? bp.cross - bp.capW / 2 : modelData - bp.lw / 2; width: bp.vertical ? bp.capW : bp.lw
                y: bp.vertical ? modelData - bp.lw / 2 : bp.cross - bp.capW / 2; height: bp.vertical ? bp.lw : bp.capW
                color: bp.ink
            }
        }
        // the middle half: outlined, a faint fill so a corridor wash still reads through it
        Rectangle {
            objectName: bp.namePrefix + "Body"
            x: bp.vertical ? bp.cross - bp.bodyW / 2 : parent.b0; width: bp.vertical ? bp.bodyW : Math.max(bp.lw, parent.b1 - parent.b0)
            y: bp.vertical ? parent.b0 : bp.cross - bp.bodyW / 2; height: bp.vertical ? Math.max(bp.lw, parent.b1 - parent.b0) : bp.bodyW
            color: bp.fill
            border.width: bp.lw; border.color: bp.ink
        }
        Rectangle {
            objectName: bp.namePrefix + "Median"
            x: bp.vertical ? bp.cross - bp.bodyW / 2 - bp.medianOver : parent.pMed - bp.medW / 2
            width: bp.vertical ? bp.bodyW + 2 * bp.medianOver : bp.medW
            y: bp.vertical ? parent.pMed - bp.medW / 2 : bp.cross - bp.bodyW / 2 - bp.medianOver
            height: bp.vertical ? bp.medW : bp.bodyW + 2 * bp.medianOver
            color: bp.medianInk
        }
    }

    // ── the shots ──
    component BoxMark: PpMark {
        property var m: ({})
        readonly property var mark: m
        readonly property string role: m.role || ""
        objectName: bp.namePrefix + "Mark"
        readonly property real along: bp.fin(m.at) ? bp.px(m.at) : NaN
        readonly property real off: (m.step || 0) * bp.stepPx
        visible: isFinite(along)
        roles: bp.roles; brush: bp.brush
        shotId: m.shotId !== undefined ? m.shotId : -1
        fired: !!m.fired
        interactive: !!m.interactive && visible
        ringed: m.ringed !== undefined ? m.ringed : (bp.brush ? bp.brush.isPicked(shotId) : false)
        size: m.size !== undefined ? m.size : (!bp.roles ? 0 : fired ? bp.roles.markFired : bp.roles.markClean)
        ringSize: m.ringSize !== undefined ? m.ringSize : (bp.roles ? bp.roles.ringD : 0)
        cx: bp.vertical ? bp.cross + off : (isFinite(along) ? along : 0)
        cy: bp.vertical ? (isFinite(along) ? along : 0) : bp.cross + off
        onActivated: bp.markActivated(m)
        onHoveredChanged: if (m.emitsHover !== false) bp.markHovered(m, hovered)
    }
    Repeater {
        model: bp.roles ? bp.marks : []
        delegate: BoxMark { required property var modelData; m: modelData }
    }
    BoxMark { m: bp.echo || ({}); visible: !!bp.echo && bp.fin(bp.echo.at) }

    // ── EXAMINED, across: the values in two lanes, above and below ──
    FontMetrics { id: fm; font: bp.roles ? bp.roles.fFigLabel : Qt.font({}) }
    readonly property var valueLabels: {
        if (!showingValues || vertical) return []
        const t = texts, L = []
        function add(v, str, lane, pri, tone) { if (str !== undefined && str !== "" && fin(v)) { const p = px(v); if (p >= 0 && p <= len) L.push({ p: p, t: str, lane: lane, pri: pri, tone: tone }) } }
        add(s.med, t.med, 0, 0, "ink"); add(s.lo, t.lo, 0, 1, "ink"); add(s.hi, t.hi, 0, 1, "ink")
        add(s.q1, t.q1, 1, 0, "ink"); add(s.q3, t.q3, 1, 0, "ink")
        if (cOk) {
            if (!corridor.lowOpen)  { add(corridor.faultLo, t.faultLo, 0, 2, "fault"); add(corridor.signalLo, t.signalLo, 1, 1, "edge") }
            if (!corridor.highOpen) { add(corridor.faultHi, t.faultHi, 0, 2, "fault"); add(corridor.signalHi, t.signalHi, 1, 1, "edge") }
        }
        L.sort(function (a, c) { return a.pri - c.pri })
        const placed = [[], []], out = []
        for (const l of L) {
            const w = fm.advanceWidth(l.t) + 6
            const x = Math.max(0, Math.min(len - w, l.p - w / 2))
            if (placed[l.lane].every(function (q) { return x + w + 2 <= q.x || x >= q.x + q.w + 2 })) {
                placed[l.lane].push({ x: x, w: w }); out.push({ x: x, w: w, lane: l.lane, t: l.t, tone: l.tone })
            }
        }
        return out
    }
    Repeater {
        model: bp.valueLabels
        delegate: Rectangle {
            required property var modelData
            objectName: bp.namePrefix + "Value"
            readonly property string text: modelData.t
            x: modelData.x; width: modelData.w; height: fm.height
            y: modelData.lane === 0 ? bp.cross - bp.bodyW / 2 - 2 - height : bp.cross + bp.bodyW / 2 + 2
            color: bp.roles ? (bp.roles.knock !== undefined ? bp.roles.knock : bp.roles.paper) : "transparent"
            Text {
                anchors.centerIn: parent
                text: parent.modelData.t; font: fm.font
                color: !bp.roles ? "grey" : parent.modelData.tone === "fault" ? bp.roles.warn : parent.modelData.tone === "edge" ? bp.roles.ink3 : bp.roles.ink
            }
        }
    }

    // ── EXAMINED, stood up: the reading beside the box ──
    PpBoxReading {
        id: rd
        anchors.fill: parent
        visible: bp.vertical && bp.showingValues && ok && placed.length > 0
        roles: bp.roles
        edgeL: bp.cross - bp.bodyW / 2; edgeR: bp.cross + bp.bodyW / 2
        roomLeft: bp.readingRoomLeft; roomRight: bp.readingRoomRight
        gutterX: bp.readingGutterX; gutterYs: bp.readingGutterYs; footLimit: bp.readingBottom
        footer: bp.footer
        items: !(bp.vertical && bp.showingValues) ? []
               : [{ y: bp.map(bp.s.hi), text: bp.texts.hi, f: bp.s.hi }, { y: bp.map(bp.s.q3), text: bp.texts.q3, f: bp.s.q3 },
                  { y: bp.map(bp.s.med), text: bp.texts.med, f: bp.s.med, strong: true },
                  { y: bp.map(bp.s.q1), text: bp.texts.q1, f: bp.s.q1 }, { y: bp.map(bp.s.lo), text: bp.texts.lo, f: bp.s.lo }]
    }
}
