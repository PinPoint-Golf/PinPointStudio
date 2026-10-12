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

// BOXSTRIP — "Spread": the session as a whole, as a narrow box plot standing at the right of the run
// (its marginal summary: the run is the time series, the box is where the session sits). Q1–Q3 as an
// ink2-outlined box with a faint fill, the median as a 1.5 px ink rule, whiskers to the furthest shots
// within 1.5 × IQR with short caps; a shot beyond them is a PpMark (fired / clean), clickable and
// hover-linked. The picked shot is a ringed PpMark on the box's centre line at its value, and the
// hovered shot (the shared brush) lights there the same way, so "this shot" reads against "the session".
// At rest the median's value stands just right of the box (ink); EXAMINED (the pointer anywhere over the
// column, or `examined` for a render) the box shows its full reading (PpBoxReading): whisker ends,
// quartiles, median, the shot and outlier counts. The hover is a passive HoverHandler, so the outlier
// marks keep their clicks and the run's shot-hover brush is untouched. Print / not interactive: rest only.
// Replaced PpSpreadStrip (a beeswarm + the norm's density curve) on 12 Oct 2026: the corridor bands
// drawn by PpZonePlot already carry the norm's judgement.
// The quartiles are worked out here from the published per-shot positions (spread.dots[].f: the axis
// is linear in f, and scale.y(f) handles the compressed bands) with the convention of the C++
// spreadQuantile (session_spread.h, type 7: linear between order statistics), so the box's median is
// the published session median (spread.medianNumber).
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   spread     card.spread / detail.header.spread  (dots[{shotId, swingDir, f, state, current}])
//   scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
//   title      panel title ("Spread")
//   unit, decimals   the axis's number formatting for the labels (unit: a symbol unit, else "")
//   examined   show the full reading without a pointer (renders); rightRoom: how far the reading may
//              reach past the panel's right edge before it opens to the left instead
//   roles, brush, medium, interactive
//   signals shotActivated(swingDir, shotId), shotHovered(shotId)
//   read-only  fQ1, fMedian, fQ3, fLoWhisker, fHiWhisker (axis fractions; NaN with no shot), outliers[],
//              examining
Item {
    id: bx
    objectName: "sdBoxStrip"
    readonly property int hoveredShot: brush ? brush.hoveredShot : -1
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var spread: null
    property var scale: null
    property string title: "Spread"
    property string unit: ""
    property int decimals: s.decimals !== undefined ? s.decimals : 0
    property bool examined: false
    property real rightRoom: 1e6
    property real gutterX: 1e6           // where the zone-label gutter starts, in this panel's x; and its labels' heights
    property var gutterYs: []
    readonly property bool examining: hasBox && medium !== "print" && (examined || (interactive && hover.hovered))
    readonly property var keyKinds: ["fired", "clean", "ring", "median", "fault", "corridor"]
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)
    readonly property var s: spread || ({})
    implicitHeight: scale ? scale.plotTop + scale.plotHeight + roles.u(26) : 0

    // ── the numbers ──
    readonly property var dots: (s.dots || []).filter(d => d && d.f !== undefined && d.f !== null && isFinite(d.f))
    readonly property var sorted: dots.map(d => d.f).sort((a, b) => a - b)
    // spreadQuantile: h = (n − 1)·q, linear between the order statistics either side of it
    function quantile(q) {
        const v = sorted, n = v.length
        if (!n) return NaN
        const h = (n - 1) * Math.max(0, Math.min(1, q))
        const lo = Math.floor(h), hi = Math.min(n - 1, lo + 1)
        return v[lo] + (h - lo) * (v[hi] - v[lo])
    }
    readonly property real fQ1: quantile(0.25)
    readonly property real fMedian: quantile(0.5)
    readonly property real fQ3: quantile(0.75)
    readonly property real iqr: isFinite(fQ1) ? fQ3 - fQ1 : 0
    readonly property real loFence: fQ1 - 1.5 * iqr
    readonly property real hiFence: fQ3 + 1.5 * iqr
    // a whisker never stops inside the box: with interpolated quartiles the nearest shot inside the fence
    // can lie between Q3 and the median, and then there is no upper whisker (the same on the low side)
    readonly property real fLoWhisker: { const v = sorted.filter(f => f >= loFence - 1e-9); return v.length ? Math.min(v[0], fQ1) : NaN }
    readonly property real fHiWhisker: { const v = sorted.filter(f => f <= hiFence + 1e-9); return v.length ? Math.max(v[v.length - 1], fQ3) : NaN }
    // the shots past the whiskers, each with a side-step where two would print on one spot
    readonly property var outliers: {
        if (!scale) return []
        const out = dots.filter(d => d.f < loFence - 1e-9 || d.f > hiFence + 1e-9).slice().sort((a, b) => a.f - b.f)
        const near = roles.markFired + 1
        let prevY = NaN, run = 0
        for (let i = 0; i < out.length; ++i) {
            const y = scale.y(out[i].f)
            run = isFinite(prevY) && Math.abs(y - prevY) < near ? run + 1 : 0
            prevY = y
            out[i] = Object.assign({}, out[i], { step: run === 0 ? 0 : (run % 2 ? 1 : -1) * Math.ceil(run / 2) })
        }
        return out
    }
    function isOutlier(id) { for (let i = 0; i < outliers.length; ++i) if (outliers[i].shotId === id) return true; return false }
    function dotOf(id) { for (let i = 0; i < dots.length; ++i) if (dots[i].shotId === id) return dots[i]; return null }
    // the picked shot (the brush's, else the model's current), and the hovered one, unless already drawn
    readonly property var picked: { for (let i = 0; i < dots.length; ++i) if (brush ? brush.isPicked(dots[i].shotId) : !!dots[i].current) return dots[i]; return null }
    readonly property var hovered: {
        const id = brush ? brush.hoveredShot : -1
        if (id < 0 || (picked && picked.shotId === id) || isOutlier(id)) return null
        return dotOf(id)
    }

    // ── the geometry ──
    readonly property real boxW: Math.max(roles.u(8), Math.min(width - roles.u(12), roles.u(medium === "print" ? 14 : 18)))
    // the median's resting label: the model's medianNumber (the box's median IS the session median)
    readonly property string medianLabel: {
        const n = s.medianNumber
        if (n !== undefined && n !== null && n !== "" && n !== "-") return n + unit
        return hasBox ? reading.fmt(fMedian) : ""
    }
    TextMetrics { id: medM; font: bx.roles.fFigLabel; text: bx.medianLabel }
    // centred, unless the median label would then reach the zone labels (they start u(10) past the edge)
    readonly property real cx: Math.round(Math.max(boxW / 2 + roles.u(2), Math.min(width / 2, width + roles.u(7) - medM.advanceWidth - roles.u(3) - boxW / 2)))
    readonly property real capW: Math.round(boxW * 0.5)
    readonly property real lw: medium === "print" ? roles.hw : 1            // the box's outline and the whiskers
    readonly property real medW: medium === "print" ? 1 : 1.5               // the median rule
    function py(f) { return Math.round(scale.y(f) * 2) / 2 }
    readonly property bool hasBox: !!scale && sorted.length > 0

    Text { text: bx.title; font: bx.roles.fSmall; color: bx.roles.ink }
    PpZonePlot { anchors.fill: parent; roles: bx.roles; scale: bx.scale }

    Item {
        visible: bx.hasBox
        anchors.fill: parent
        // the whiskers: a hairline from each box edge to the furthest shot inside the fence, capped
        Rectangle { objectName: "sdBoxWhiskerHi"; x: bx.cx - bx.lw / 2; width: bx.lw
                    y: bx.hasBox ? bx.py(bx.fHiWhisker) : 0; height: bx.hasBox ? Math.max(0, bx.py(bx.fQ3) - bx.py(bx.fHiWhisker)) : 0; color: bx.roles.ink2 }
        Rectangle { x: bx.cx - bx.capW / 2; width: bx.capW; height: bx.lw; y: bx.hasBox ? bx.py(bx.fHiWhisker) - bx.lw / 2 : 0; color: bx.roles.ink2 }
        Rectangle { objectName: "sdBoxWhiskerLo"; x: bx.cx - bx.lw / 2; width: bx.lw
                    y: bx.hasBox ? bx.py(bx.fQ1) : 0; height: bx.hasBox ? Math.max(0, bx.py(bx.fLoWhisker) - bx.py(bx.fQ1)) : 0; color: bx.roles.ink2 }
        Rectangle { x: bx.cx - bx.capW / 2; width: bx.capW; height: bx.lw; y: bx.hasBox ? bx.py(bx.fLoWhisker) - bx.lw / 2 : 0; color: bx.roles.ink2 }
        // the box: the middle half, outlined, a faint fill so the corridor wash still reads through it
        Rectangle {
            objectName: "sdBoxBody"
            x: bx.cx - bx.boxW / 2; width: bx.boxW
            y: bx.hasBox ? bx.py(bx.fQ3) : 0; height: bx.hasBox ? Math.max(bx.lw, bx.py(bx.fQ1) - bx.py(bx.fQ3)) : 0
            color: Qt.alpha(bx.roles.ink3, 0.10)
            border.width: bx.lw; border.color: bx.roles.ink2
        }
        Rectangle {
            objectName: "sdBoxMedian"
            readonly property real f: bx.fMedian
            x: bx.cx - bx.boxW / 2 - bx.roles.u(2); width: bx.boxW + 2 * bx.roles.u(2)
            y: bx.hasBox ? bx.py(bx.fMedian) - bx.medW / 2 : 0; height: bx.medW; color: bx.roles.ink
        }
    }

    Text {
        objectName: "sdBoxMedianLabel"
        visible: bx.hasBox && !bx.examining && bx.medianLabel !== ""
        text: bx.medianLabel; font: bx.roles.fFigLabel; color: bx.roles.ink
        x: bx.cx + bx.boxW / 2 + bx.roles.u(3); y: bx.hasBox ? Math.round(bx.scale.y(bx.fMedian) - height / 2) : 0
    }

    // a shot as a mark on the centre line (fired or clean as its dot), clickable and hover-linked
    component BoxMark: PpMark {
        property var dot: null
        property real step: 0
        property bool emitsHover: true
        objectName: "sdBoxMark"
        visible: !!dot && !!bx.scale
        roles: bx.roles; brush: bx.brush; interactive: bx.interactive && visible
        shotId: dot ? dot.shotId : -1; fired: !!dot && dot.state === "fired"
        ringed: !!dot && (bx.brush ? bx.brush.isPicked(shotId) : !!dot.current)
        cx: bx.cx + step * bx.roles.u(7); cy: dot && bx.scale ? bx.py(dot.f) : 0
        onActivated: if (dot) bx.shotActivated(dot.swingDir || "", shotId)
        onHoveredChanged: if (emitsHover) bx.shotHovered(hovered ? shotId : -1)
    }
    Repeater {
        model: bx.roles ? bx.outliers : []
        delegate: BoxMark {
            required property var modelData
            readonly property string role: "outlier"
            dot: modelData; step: modelData.step || 0
        }
    }
    BoxMark { readonly property string role: "picked"; dot: bx.picked && !bx.isOutlier(bx.picked.shotId) ? bx.picked : null }
    // the hovered shot's echo: lit while the pointer is on that shot elsewhere; never under the pointer
    // itself, so it takes no hover of its own
    BoxMark { readonly property string role: "hovered"; dot: bx.hovered; interactive: false; emitsHover: false }

    // EXAMINED: the full reading, right of the box unless the room there is short and the left has more
    PpBoxReading {
        id: reading
        anchors.fill: parent
        visible: bx.examining && ok && placed.length > 0
        roles: bx.roles; scale: bx.scale; decimals: bx.decimals; unit: bx.unit
        edgeL: bx.cx - bx.boxW / 2; edgeR: bx.cx + bx.boxW / 2
        roomRight: bx.width + bx.rightRoom; gutterX: bx.gutterX; gutterYs: bx.gutterYs
        items: bx.hasBox ? [{ f: bx.fHiWhisker }, { f: bx.fQ3 }, { f: bx.fMedian, strong: true }, { f: bx.fQ1 }, { f: bx.fLoWhisker }] : []
        footer: !bx.hasBox ? "" : bx.sorted.length + (bx.sorted.length === 1 ? " shot" : " shots")
                + (bx.outliers.length ? " · " + bx.outliers.length + (bx.outliers.length === 1 ? " outlier" : " outliers") : "")
    }
    HoverHandler { id: hover; enabled: bx.interactive && bx.medium !== "print" }
}
