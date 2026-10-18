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
// The box is drawn by PpBoxPlot (typeset/), THE box plot, and its numbers are the model's: spread.box, from
// the one C++ box implementation (typeset/box_stats.h) — edges as axis fractions for scale.y(f), the
// outliers as shot ids, the examined reading's labels at the axis's precision. Until 18 Oct 2026 this file
// worked the quartiles and whiskers out again from the dots.
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   spread     card.spread / detail.header.spread  (dots[{shotId, swingDir, f, state, current}],
//              box{n, fLo, fQ1, fMed, fQ3, fHi, outliers[shotId], outlierCount, texts})
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

    // ── the numbers: the model's box ──
    readonly property var dots: (s.dots || []).filter(d => d && d.f !== undefined && d.f !== null && isFinite(d.f))
    readonly property var box: s.box || ({})
    function num(v) { return v === undefined || v === null ? NaN : v }
    readonly property real fQ1: num(box.fQ1)
    readonly property real fMedian: num(box.fMed)
    readonly property real fQ3: num(box.fQ3)
    readonly property real fLoWhisker: num(box.fLo)
    readonly property real fHiWhisker: num(box.fHi)
    // the shots past the whiskers, each with a side-step where two would print on one spot
    readonly property var outliers: {
        if (!scale) return []
        const ids = box.outliers || []
        const out = dots.filter(d => ids.indexOf(d.shotId) >= 0).slice().sort((a, b) => a.f - b.f)
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
        return hasBox && box.texts ? box.texts.med : ""
    }
    TextMetrics { id: medM; font: bx.roles.fFigLabel; text: bx.medianLabel }
    // centred, unless the median label would then reach the zone labels (they start u(10) past the edge)
    readonly property real cx: Math.round(Math.max(boxW / 2 + roles.u(2), Math.min(width / 2, width + roles.u(7) - medM.advanceWidth - roles.u(3) - boxW / 2)))
    readonly property real capW: Math.round(boxW * 0.5)
    readonly property real lw: medium === "print" ? roles.hw : 1            // the box's outline and the whiskers
    readonly property real medW: medium === "print" ? 1 : 1.5               // the median rule
    function py(f) { return Math.round(scale.y(f) * 2) / 2 }
    readonly property bool hasBox: !!scale && dots.length > 0 && isFinite(fQ1) && isFinite(fQ3) && isFinite(fMedian)

    Text { text: bx.title; font: bx.roles.fSmall; color: bx.roles.ink }
    PpZonePlot { anchors.fill: parent; roles: bx.roles; scale: bx.scale }

    Text {
        objectName: "sdBoxMedianLabel"
        visible: bx.hasBox && !bx.examining && bx.medianLabel !== ""
        text: bx.medianLabel; font: bx.roles.fFigLabel; color: bx.roles.ink
        x: bx.cx + bx.boxW / 2 + bx.roles.u(3); y: bx.hasBox ? Math.round(bx.scale.y(bx.fMedian) - height / 2) : 0
    }

    // the shots on the centre line (fired or clean as their dots): the outliers, side-stepped where two would
    // print on one spot, and the picked shot ringed — a list the hover never rebuilds — and on its own the
    // hovered one's echo (lit while the pointer is on that shot elsewhere; never under the pointer itself, so it
    // takes no hover of its own)
    readonly property var marks: {
        const out = []
        const ringedOf = d => bx.brush ? bx.brush.isPicked(d.shotId) : !!d.current
        for (const d of outliers)
            out.push({ at: d.f, shotId: d.shotId, swingDir: d.swingDir || "", fired: d.state === "fired", role: "outlier",
                       step: d.step || 0, ringed: ringedOf(d), interactive: bx.interactive })
        if (picked && !isOutlier(picked.shotId))
            out.push({ at: picked.f, shotId: picked.shotId, swingDir: picked.swingDir || "", fired: picked.state === "fired",
                       role: "picked", ringed: ringedOf(picked), interactive: bx.interactive })
        return out
    }
    readonly property var echo: !hovered ? null
        : { at: hovered.f, shotId: hovered.shotId, swingDir: hovered.swingDir || "", fired: hovered.state === "fired",
            role: "hovered", ringed: bx.brush ? bx.brush.isPicked(hovered.shotId) : !!hovered.current, interactive: false, emitsHover: false }
    // THE box (typeset/PpBoxPlot), stood up; EXAMINED, its reading right of the box unless the room there is
    // short and the left has more
    PpBoxPlot {
        id: plot
        anchors.fill: parent
        roles: bx.roles; brush: bx.brush
        vertical: true
        map: function (f) { return bx.scale ? bx.scale.y(f) : 0 }
        cross: bx.cx
        stats: bx.hasBox ? { lo: bx.fLoWhisker, q1: bx.fQ1, med: bx.fMedian, q3: bx.fQ3, hi: bx.fHiWhisker } : null
        bodyW: bx.boxW; capW: bx.capW; medianOver: bx.roles.u(2)
        lw: bx.lw; medW: bx.medW
        ink: bx.roles.ink2; medianInk: bx.roles.ink; fill: Qt.alpha(bx.roles.ink3, 0.10)
        marks: bx.marks; echo: bx.echo
        texts: bx.box.texts || null
        examined: bx.examining
        footer: !bx.hasBox ? "" : bx.dots.length + (bx.dots.length === 1 ? " shot" : " shots")
                + (bx.outliers.length ? " · " + bx.outliers.length + (bx.outliers.length === 1 ? " outlier" : " outliers") : "")
        readingRoomRight: bx.width + bx.rightRoom; readingGutterX: bx.gutterX; readingGutterYs: bx.gutterYs
        readingBottom: bx.scale ? bx.scale.plotTop + bx.scale.plotHeight + bx.roles.u(22) : 1e6
        namePrefix: "sdBox"
        onMarkActivated: (m) => bx.shotActivated(m.swingDir || "", m.shotId)
        onMarkHovered: (m, on) => bx.shotHovered(on ? m.shotId : -1)
    }
    HoverHandler { id: hover; enabled: bx.interactive && bx.medium !== "print" }
}
