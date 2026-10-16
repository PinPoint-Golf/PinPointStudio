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

// PPCHPLOT — the one painter every Charts curve figure draws with: a Canvas over a plot's AREA (no gutters),
// mapping time t on [x0, x1] across its width and value v on [lo, hi] up its height. It paints only what it
// is handed — the numbers are the presenters' — in back-to-front order: furniture (P-position hairlines, the
// value grid, the zero line), bands, quiet lines, dots, then strong lines.
//
//   layers: [ { k: "band",  t, lo, hi, color }                                   a wash (metric hue at envAlpha)
//           | { k: "line",  t, v, color, w, alpha, dash, runs, shot, z }          a curve; NaN = a break
//           | { k: "dots",  t, v, color, alpha, d }                               raw samples
//           | { k: "vline", t, color, dash }                                      e.g. where the curves part
//           | { k: "hline", v, color, dash }                                      e.g. the difference's zero
//           | { k: "bridges", list: [{ t0, v0, t1, v1 }], color, w, alpha, shot } ]  the presenter's BRIDGES: a
//                                                         dashed straight line across a run that is no measurement
//   runs (This shot's honesty, F18): [{ from, to, style: "measured" | "bridged" | "outside" }] inclusive index
//   ranges: a bridged run has no values (its dashed bridge is a "bridges" layer), outside the metric's phase
//   window is dimmed.
//   shot (an ordinal, optional): the curve belongs to that shot, so the Brush can light it (hover) or let it
//   recede (a question's highlight). z orders lines (higher on top).
Canvas {
    id: pl
    property QtObject roles: null
    property var brush: null
    property var layers: []
    property real x0: 0
    property real x1: 1
    property real lo: 0
    property real hi: 1
    property var pTicks: []            // [{ t, strong }] — P-position hairlines (impact strong)
    property var yTicks: []            // value grid lines
    property bool zeroLine: true
    renderTarget: Canvas.Image
    readonly property var deps: [layers, lo, hi, width, height, x0, x1, pTicks, yTicks, roles ? roles.ink : "black",
                                 brush ? brush.hoveredShot : -1, brush ? brush.highlightShots : []]
    onDepsChanged: requestPaint()

    // a drawable number: null (a presenter's "missing") and NaN draw nothing
    function fin(v) { return v !== null && v !== undefined && isFinite(v) }
    function xOf(t) { return (t - x0) / Math.max(1e-9, x1 - x0) * width }
    function yOf(v) { return height - (v - lo) / Math.max(1e-9, hi - lo) * height }
    function tOf(px) { return x0 + px / Math.max(1, width) * (x1 - x0) }

    onPaint: {
        const ctx = getContext("2d"); ctx.reset()
        const R = roles
        if (!R || width <= 0 || height <= 0) return
        const W = width, H = height
        // ── furniture ──
        ctx.lineWidth = R.hw
        for (const p of (pTicks || [])) {
            const x = Math.round(xOf(p.t)) + 0.5
            if (x < 0 || x > W) continue
            ctx.strokeStyle = Qt.alpha(R.ink, R.r.hairAlpha * (p.strong ? 2.6 : 1.3))
            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke()
        }
        ctx.strokeStyle = Qt.alpha(R.ink, R.r.hairAlpha * 0.8)
        for (const v of (yTicks || [])) {
            const y = Math.round(yOf(v)) + 0.5
            if (y < 0 || y > H) continue
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke()
        }
        if (zeroLine && lo < 0 && hi > 0) {
            ctx.strokeStyle = Qt.alpha(R.ink3, 0.5); const y = Math.round(yOf(0)) + 0.5
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke()
        }
        ctx.save(); ctx.beginPath(); ctx.rect(0, -2, W, H + 4); ctx.clip()
        const hov = brush ? brush.hoveredShot : -1
        const filt = brush && brush.shotFilter
        const ls = (layers || []).slice().sort(function (a, b) {
            const zOf = function (l) { return l.k === "span" ? -20 : l.k === "band" ? -10 : l.k === "dots" ? -5 : (l.z || 0) }
            return zOf(a) - zOf(b) })
        for (const l of ls) {
            if (!l) continue
            const lit = !filt || l.shot === undefined || l.shot < 0 || brush.shotLit(l.shot)
            const k = lit ? 1 : 0.35
            if (l.k === "span") {
                // a stretch of the time axis, full height (a gate's blind stretch, trust_and_gates §7.2)
                const xa = Math.max(0, xOf(l.t0)), xb = Math.min(W, xOf(l.t1))
                if (xb > xa) { ctx.fillStyle = l.color; ctx.fillRect(xa, 0, xb - xa, H) }
            } else if (l.k === "band") {
                ctx.fillStyle = l.color
                let open = false; const back = []
                const flush = function () { if (!open) return; for (let j = back.length - 1; j >= 0; --j) ctx.lineTo(back[j][0], back[j][1]); ctx.closePath(); ctx.fill(); back.length = 0; open = false }
                ctx.beginPath()
                for (let i = 0; i < l.t.length; ++i) {
                    const a = l.lo[i], b = l.hi[i]
                    if (!fin(a) || !fin(b)) { flush(); ctx.beginPath(); continue }
                    const x = xOf(l.t[i])
                    if (!open) { ctx.moveTo(x, yOf(b)); open = true } else ctx.lineTo(x, yOf(b))
                    back.push([x, yOf(a)])
                }
                flush()
            } else if (l.k === "dots") {
                ctx.fillStyle = Qt.alpha(l.color, (l.alpha === undefined ? 0.35 : l.alpha) * k)
                const d = l.d || 2
                for (let i = 0; i < l.t.length; ++i) {
                    const v = l.v[i], t = l.t[i]
                    if (!fin(v) || t < x0 || t > x1) continue
                    ctx.fillRect(xOf(t) - d / 2, yOf(v) - d / 2, d, d)
                }
            } else if (l.k === "vline" || l.k === "hline") {
                ctx.strokeStyle = l.color || R.ink3; ctx.lineWidth = R.hw; ctx.setLineDash(l.dash || [])
                ctx.beginPath()
                if (l.k === "vline") { const x = Math.round(xOf(l.t)) + 0.5; ctx.moveTo(x, 0); ctx.lineTo(x, H) }
                else { const y = Math.round(yOf(l.v)) + 0.5; ctx.moveTo(0, y); ctx.lineTo(W, y) }
                ctx.stroke(); ctx.setLineDash([])
            } else if (l.k === "bridges") {
                // never a measurement: thin, dashed, fainter than the curve it joins (F18)
                const hot = hov >= 0 && l.shot === hov
                ctx.strokeStyle = Qt.alpha(l.color, (hot ? 1 : (l.alpha === undefined ? 1 : l.alpha)) * 0.6 * k)
                ctx.lineWidth = Math.max(0.6, (l.w || 1) * 0.6); ctx.setLineDash([R.u(2), R.u(2)])
                ctx.beginPath()
                for (const b of (l.list || [])) {
                    if (!fin(b.t0) || !fin(b.v0) || !fin(b.t1) || !fin(b.v1)) continue
                    ctx.moveTo(xOf(b.t0), yOf(b.v0)); ctx.lineTo(xOf(b.t1), yOf(b.v1))
                }
                ctx.stroke(); ctx.setLineDash([])
            } else if (l.k === "line") {
                const hot = hov >= 0 && l.shot === hov && l.hover !== false
                const baseA = (l.alpha === undefined ? 1 : l.alpha)
                const a = hot ? 1 : baseA * k
                const w = hot ? Math.max(l.w || 1, 1.4) : (l.w || 1)
                const n = l.t.length
                // the runs: measured solid, outside the phase window dimmed; a bridged run is skipped
                const runs = (l.runs && l.runs.length) ? l.runs : [{ from: 0, to: n - 1, style: "measured" }]
                ctx.lineJoin = "round"
                for (const rn of runs) {
                    const st = rn.style || "measured"
                    if (st === "bridged") continue          // no values: its bridge is drawn by a "bridges" layer
                    ctx.strokeStyle = Qt.alpha(l.color, st === "outside" ? a * 0.35 : a)
                    ctx.lineWidth = w
                    ctx.setLineDash(l.dash || [])
                    ctx.beginPath()
                    let pen = false
                    // join each run to the one before it, so a curve never shows a gap where it changes style
                    for (let i = Math.max(0, rn.from - 1); i <= Math.min(n - 1, rn.to); ++i) {
                        const v = l.v[i], t = l.t[i]
                        if (!fin(v) || !fin(t)) { pen = false; continue }
                        const x = xOf(t), y = yOf(v)
                        if (!pen) { ctx.moveTo(x, y); pen = true } else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
                ctx.setLineDash([])
            }
        }
        ctx.restore()
    }
}
