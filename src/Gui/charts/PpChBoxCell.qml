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
// a column of readings reads as one profile, or stood up beside a run or at a P-position on a trace). The
// Charts panel's adapter onto PpBoxPlot, THE box plot (typeset/): it maps the presenter's numbers to pixels,
// chooses the Charts inks and turns the presenter's per-shot values into the shot marks — a shot past the
// whiskers (warn when a diagnostics condition reading it fired on that shot), the picked shot ringed at its
// value, the hovered shot (the Brush) a lighter ring. Every number is the presenter's `box`
// ({ n, min, max, q1, med, q3, lo, hi, outliers: [ordinal], texts }) and `values` (one per shot, index =
// ordinal − 1, null where unmeasured).
// Colour (§15.4 item 1, D5): a box stands for a METRIC, so it is drawn in the metric's hue (`color`), muted
// to ink3 at rest when `muted` (a profile of 68 coloured boxes is busy) and coloured on hover or when apart.
// `corridor` (#10, the Readings rows only): the presenter's { known, lowOpen, highOpen, signalLo/Hi, faultLo/Hi },
// drawn by PpBoxPlot under the box, kept quiet (Mark, 18 Oct: "so long as the corridor and fault line are
// subtle"). The scale widens to take in the corridor's graded edges, so a session that sits wholly outside it
// still shows where it is.
// HOVER, EVERYWHERE (Mark, 18 Oct): an interactive cell lays its values on the plot while the pointer is over it
// (PpBoxPlot: across, in two lanes; stood up, the reading beside it). `texts` overrides the box's own texts —
// the Readings rows pass theirs with the corridor's numbers in.
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
    property var texts: null                     // null = the box's own texts
    property bool valuesOnHover: interactive
    property bool showValues: false              // the values without a hover (a probe; print, should it want them)
    readonly property bool showingValues: plot.showingValues
    property bool showPicked: true
    signal shotActivated(int ordinal)
    readonly property bool ok: !!roles && !!box && box.n > 0 && box.q1 !== null && box.q1 !== undefined
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
    readonly property int pickNo: brush ? brush.pickedShot : -1
    readonly property int hoverNo: brush ? brush.hoveredShot : -1
    function valueOf(ord) { const v = (values || [])[ord - 1]; return v === null || v === undefined ? NaN : v }
    implicitHeight: roles ? roles.u(roles.ch.boxRow) : 16

    // the shots: past the whiskers and the picked one (never rebuilt by a hover: the mark under the pointer
    // would be recreated and drop it), and the hovered shot's echo on its own
    readonly property var marks: {
        if (!ok) return []
        const out = [], fr = fired || []
        const sizeOf = function (f) { return (f ? roles.markFired : roles.markClean) * markScale }
        for (const o of (box.outliers || [])) {
            const v = valueOf(o)
            if (o !== pickNo && isFinite(v)) out.push({ at: v, shotId: o, fired: !!fr[o - 1], role: "outlier", interactive: interactive, size: sizeOf(!!fr[o - 1]) })
        }
        const pv = valueOf(pickNo)
        if (showPicked && pickNo > 0 && isFinite(pv))
            out.push({ at: pv, shotId: pickNo, fired: !!fr[pickNo - 1], role: "picked", ringed: true, interactive: false,
                       size: sizeOf(!!fr[pickNo - 1]), ringSize: roles.ringD * Math.min(1, markScale + 0.1), emitsHover: false })
        return out
    }
    readonly property var echo: {
        const hv = valueOf(hoverNo), fr = fired || []
        if (!ok || hoverNo <= 0 || hoverNo === pickNo || !isFinite(hv)) return null
        return { at: hv, shotId: hoverNo, fired: !!fr[hoverNo - 1], role: "hovered", ringed: false, interactive: false,
                 size: (fr[hoverNo - 1] ? roles.markFired : roles.markClean) * markScale, emitsHover: false }
    }

    PpBoxPlot {
        id: plot
        anchors.fill: parent
        roles: bc.roles; brush: bc.brush
        vertical: bc.vertical
        map: bc.pos
        cross: bc.mid
        stats: bc.ok ? bc.box : null
        bodyW: bc.bw; capW: bc.bw / 2; medianOver: 2
        lw: bc.roles && bc.roles.isPrint ? bc.roles.hw : 1
        medW: bc.roles && bc.roles.isPrint ? 1 : 1.5
        ink: !bc.roles ? "grey" : bc.muted ? bc.roles.ink3 : bc.color
        medianInk: !bc.roles ? "grey" : bc.muted ? bc.roles.ink2 : bc.color
        fill: bc.roles ? Qt.alpha(bc.muted ? bc.roles.ink3 : bc.color, bc.muted ? 0.08 : bc.roles.boxFill) : "transparent"
        corridor: bc.cOk ? bc.corridor : null
        texts: bc.texts || (bc.box ? bc.box.texts : null) || null
        valuesOnHover: bc.valuesOnHover
        examined: bc.showValues
        marks: bc.marks; echo: bc.echo
        namePrefix: "chBox"
        onMarkActivated: (m) => { if (m.role === "outlier") bc.shotActivated(m.shotId) }
    }
}
