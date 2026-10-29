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

// PPCHPLANELABELS — the words on the Club delivery lines (docs/design/club_delivery_chart_design.md §4.2): laid over a
// plot's AREA (the PpChPlot's own box), it names the lines ChartsDeliveryPresenter.plotLayers drew there — "address 59°"
// and "shoulders 69°" just over (else under) their hairlines at the right end, further in on a short plot, "hands carry the plane here" once, at the top of its
// first stretch wide enough to hold them (else at its foot). Labels on the drawn things, never a legend; each on a knock-out in the text inks.
// A label that would leave the plot, or touch another label or a `reserve`d box (the row's name, the overlaid key), is
// dropped — never squeezed. A line outside the plot's value range draws nothing and so names nothing.
//   layers: the row's plotLayers (role, label, text; hline v, span t0/t1, labelled)
//   x0, x1, lo, hi: the plot's own mapping; reserve: [{ x, y, w, h }] in this item's coordinates; topInset: the name band
// objectName: chPlaneLabel (+ role) on each label drawn.
Item {
    id: pl
    property QtObject roles: null
    property var layers: []
    property real x0: 0
    property real x1: 1
    property real lo: 0
    property real hi: 1
    property var reserve: []
    property real topInset: 0

    FontMetrics { id: fm; font: pl.roles ? pl.roles.fSmall : Qt.font({}) }
    function xOf(t) { return (t - x0) / Math.max(1e-9, x1 - x0) * width }
    function yOf(v) { return height - (v - lo) / Math.max(1e-9, hi - lo) * height }
    function hits(a, b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h }

    readonly property var placed: {
        if (!roles || width <= 0 || height <= 0) return []
        const pad = roles.u(3), h = Math.ceil(fm.height) + 2, out = [], taken = (reserve || []).slice()
        const fits = function (r) {
            if (r.x < 0 || r.y < 0 || r.x + r.w > pl.width || r.y + r.h > pl.height) return false
            for (const t of taken) if (pl.hits(r, t)) return false
            return true
        }
        const take = function (role, text, tries) {
            const w = Math.ceil(fm.advanceWidth(text)) + 2 * pad
            for (const p of tries) {
                const r = { x: p.x, y: p.y, w: w, h: h }
                if (p.x === undefined) r.x = pl.width - w - pad              // the right end
                if (fits(r)) { taken.push(r); out.push({ role: role, text: text, x: r.x + pad, y: r.y }); return }
            }
        }
        const ls = layers || []
        // the hairlines first (they are what the eye reads off), address before the shoulders
        for (const role of ["address", "shoulders"])
            for (const l of ls) {
                if (l.k !== "hline" || l.role !== role || l.v === null || l.v === undefined || !isFinite(l.v)) continue
                if (l.v < lo || l.v > hi) continue
                const y = yOf(l.v)
                // at the right end over the line, else under it; on a short plot, further in
                const words = (l.label || "") + (l.text ? " " + l.text : ""), w = fm.advanceWidth(words) + 2 * pad
                const inner = Math.max(0, pl.width * 0.62 - w)
                take(role, words, [{ y: y - h - 1 }, { y: y + 2 }, { x: inner, y: y - h - 1 }, { x: inner, y: y + 2 }])
            }
        // the hands' stretches: named once, on the first stretch in the window wide enough to hold the words (else the
        // widest), at its top under the name band, else at its foot
        const spans = ls.filter(function (l) { return l.k === "span" && l.role === "hands" })
                        .map(function (l) { return { l: l, xa: Math.max(0, pl.xOf(l.t0)), xb: Math.min(pl.width, pl.xOf(l.t1)) } })
                        .filter(function (s) { return s.xb > s.xa })
        if (spans.length) {
            const words = (spans.filter(function (s) { return s.l.labelled })[0] || spans[0]).l.label || ""
            const need = fm.advanceWidth(words) + 2 * pad
            const fit = spans.filter(function (s) { return s.xb - s.xa >= need })[0]
            const sp = fit || spans.slice().sort(function (a, b) { return (b.xb - b.xa) - (a.xb - a.xa) })[0]
            take("hands", words, [{ x: sp.xa + 2 * pad, y: topInset + 1 }, { x: sp.xa + 2 * pad, y: height - h - 2 },
                                  { x: Math.max(0, sp.xb - need), y: topInset + 1 }, { x: Math.max(0, sp.xb - need), y: height - h - 2 }])
        }
        return out
    }

    Repeater {
        model: pl.placed
        delegate: Text {
            required property var modelData
            objectName: "chPlaneLabel"
            readonly property string role: modelData.role
            x: modelData.x; y: modelData.y
            text: modelData.text
            font: pl.roles.fSmall
            color: modelData.role === "hands" ? pl.roles.ink3 : pl.roles.ink2
            Rectangle { z: -1; anchors.fill: parent; anchors.margins: -2; color: pl.roles.knock }
        }
    }
}
