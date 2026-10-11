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

// FIGURESCALE — the value axis that PpSpreadStrip, PpRunChart and PpHistoryStrip share inside a PpFigureRow
// (C's "one figure grammar": panels in a row share one y scale). It maps the model's normalised
// position f (0..1 along spread.axisLo..axisHi) to a pixel y, and names the zone edges once.
//
// FAULT-FORWARD (round 3). The axis spends its height where the shots are. Give it the shots' f values
// (`values`) and it keeps a LINEAR WINDOW over [the shots ∪ the pass edge they must get back to], padded
// a little; whatever lies outside that window — for a fault, the empty corridor; for a clean condition,
// the empty watch and fault zones — collapses into a thin COMPRESSED BAND at that end of the axis,
// separated from the window by a drawn BREAK. True values stay readable: ticks and zone labels are in
// the measure's own units, a band is labelled with the values it holds, and anything in a band is still
// drawn there (compressed, never hidden). With no `values`, or `faultForward: false`, it is linear.
// Panels only ever call y(f), region(f) and read corridor / edges / bandLo / bandHi.
QtObject {
    id: fs
    property var bands: []
    property var faultLines: []
    property var yTicks: []
    property real plotTop: 0
    property real plotHeight: 100
    // ── optional (round 3) ──
    property var values: []              // f of every shot the panels plot
    property bool faultForward: true
    property real bandPx: 16             // height of a compressed band
    property real breakPx: 7             // the drawn gap between a band and the window
    property real minOmit: 0.12          // collapse an end only when it would waste at least this share
    property bool clean: false           // no shot fired: keep every pass edge in the window (the margin is the story)
    property real axisLo: NaN            // the value at f = 0 and f = 1, to label a band in true units
    property real axisHi: NaN
    function valueOf(f) { return isFinite(axisLo) && isFinite(axisHi) ? axisLo + f * (axisHi - axisLo) : NaN }

    // each zone edge once: kind fault (warn hairline) · pass (dashed ink3) · aim · grid
    readonly property var edges: {
        const out = [], b = bands || []
        for (let i = 1; i < b.length; ++i) {
            const pair = [b[i - 1].grade, b[i].grade].sort().join("|")
            let kind = "grid"
            if (pair === "action|watch") kind = "fault"
            else if (pair === "good|watch") kind = "pass"
            else if (pair.indexOf("open") >= 0) kind = "aim"
            out.push({ f: b[i].f0, value: b[i].lo, kind: kind })
        }
        return out
    }
    // the corridor (ideal + good + open) as an f range, for the wash
    readonly property var corridor: {
        let lo = 1, hi = 0
        const b = bands || []
        for (let i = 0; i < b.length; ++i)
            if (b[i].grade === "ideal" || b[i].grade === "good" || b[i].grade === "open") { lo = Math.min(lo, b[i].f0); hi = Math.max(hi, b[i].f1) }
        return hi > lo ? { lo: lo, hi: hi } : null
    }
    readonly property var passFs: edges.filter(e => e.kind === "pass").map(e => e.f)
    readonly property var faultFs: edges.filter(e => e.kind === "fault").map(e => e.f)

    // the linear window [lo, hi] in f, and which ends collapse
    readonly property var win: {
        const vs = (values || []).filter(v => v !== undefined && v !== null && isFinite(v))
        if (!faultForward || vs.length === 0) return { lo: 0, hi: 1, bandLo: false, bandHi: false }
        let dLo = Math.min.apply(null, vs), dHi = Math.max.apply(null, vs)
        let lo = dLo, hi = dHi
        const c = corridor
        const pass = passFs
        // every pass edge that sits between the shots and the corridor joins the window: it is the target
        for (let i = 0; i < pass.length; ++i) {
            const p = pass[i]
            if (c && p >= c.hi - 1e-6 && dHi > p) lo = Math.min(lo, p)        // shots above the high pass edge
            if (c && p <= c.lo + 1e-6 && dLo < p) hi = Math.max(hi, p)        // shots below the low pass edge
        }
        // a clean set (every shot inside the corridor) keeps the whole corridor: the margin is the story
        if (clean || (c && dLo >= c.lo - 1e-6 && dHi <= c.hi + 1e-6)) {
            for (let i = 0; i < pass.length; ++i) { lo = Math.min(lo, pass[i]); hi = Math.max(hi, pass[i]) }
            if (!pass.length && c) { lo = Math.min(lo, c.lo); hi = Math.max(hi, c.hi) }
        }
        const pad = Math.max(0.03, (hi - lo) * 0.08)
        lo = Math.max(0, lo - pad); hi = Math.min(1, hi + pad)
        const bandLo = lo > minOmit, bandHi = (1 - hi) > minOmit
        return { lo: bandLo ? lo : 0, hi: bandHi ? hi : 1, bandLo: bandLo, bandHi: bandHi }
    }
    readonly property bool bandLo: win.bandLo
    readonly property bool bandHi: win.bandHi
    readonly property bool broken: bandLo || bandHi
    // pixel extents: [top band][break][window][break][bottom band]
    readonly property real winTop: plotTop + (bandHi ? bandPx + breakPx : 0)
    readonly property real winBottom: plotTop + plotHeight - (bandLo ? bandPx + breakPx : 0)
    readonly property real loBandTop: plotTop + plotHeight - bandPx
    readonly property real hiBandBottom: plotTop + bandPx

    function region(f) { return (bandLo && f < win.lo) ? 0 : (bandHi && f > win.hi) ? 2 : 1 }
    function y(f) {
        if (f === undefined || f === null || !isFinite(f)) return plotTop + plotHeight
        const w = win
        if (bandLo && f < w.lo) return plotTop + plotHeight - Math.max(0, f) / w.lo * bandPx
        if (bandHi && f > w.hi) return plotTop + bandPx - (Math.min(1, f) - w.hi) / (1 - w.hi) * bandPx
        return winBottom - (f - w.lo) / Math.max(1e-6, w.hi - w.lo) * (winBottom - winTop)
    }
    // ticks only where the scale is true, and clear of the breaks
    function tickVisible(f) { return region(f) === 1 && y(f) >= winTop - 1 && y(f) <= winBottom + 1 }
    // the zone edges that fall inside a band, so the band can be labelled with the values it holds
    function edgesIn(r) { return edges.filter(e => region(e.f) === r) }
}
