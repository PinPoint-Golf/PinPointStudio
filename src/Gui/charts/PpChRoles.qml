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

// PPCHROLES — the Charts panel's vocabulary on top of the generic typeset helper (design system §15.3,
// docs/design/charts_panel_traces_relations.md §5). A Charts register makes ONE and passes it to every
// block as `roles`:      PpChRoles { id: ro; medium: root.medium }
// Never give it the id `roles` (see PpTypeset). It IS a PpTypeset, so the generic library blocks take it.
//
// WHAT IS HERE: the Charts design values (Theme.typeset.ch, with Theme.typesetPrint.ch over it on paper),
// the inks a metric is drawn in (Theme.metricColor at the ch alphas and weights — one hue per metric, never
// a second hue), the knock-out behind a label inside a plot, the Charts flow minimums, and ONE text helper:
// html(sentence) sets a presenter Sentence ({ text, runs: [{ text, fig }] }) with its figure runs in the
// figure face. NO number and NO word is made here: every one comes from the presenters
// (ChartsTracesPresenter, ChartsRelationsPresenter) — "QML positions and paints, C++ decides the numbers".
PpTypeset {
    id: cr

    // ── the Charts design values: screen, or paper with the print keys over them ──
    readonly property var ch: isPrint ? Object.assign({}, r.ch, p.ch || {}) : r.ch

    extraFlows: ({
        facet: 22,                 // a facet's panel, a scatter (ch.facetEm)
        kindCurve: 34,             // the kinds' curve figure (ch.kindCurveEm; 30 on paper)
        figCol: 14,                // a trace row's figure column (the readings beside the plot)
        figColWide: 15,
        traceMin: 30               // a trace plot: below this it gives up its figure column first
    })
    extraElements: ({
        figColumn:  { tier: 2, else: "one folded line under each plot" },
        traceRail:  { tier: 4, else: "head and controls above the figure, notes below" },
        seqTable:   { tier: 2, else: "the chain's line and the peaks on the curves" },
        diffStrip:  { tier: 1, else: "(always under its trace while Difference is on)" }
    })

    // ── a metric's inks: its own hue, at the Charts alphas (identity, not judgement) ──
    function hue(key) { return Theme.metricColor(key) }
    function hueA(key, a) { return Qt.alpha(Theme.metricColor(key), a) }
    readonly property real envAlpha: (ch.envelopeAlpha ? (Theme.dark && !isPrint ? ch.envelopeAlpha.dark : ch.envelopeAlpha.light) : 0.12) * tintK
    readonly property real boxFill: ch.boxFill
    // a dash pattern in design units, for a Canvas
    function dash(pat) { return (pat || []).map(function (d) { return u(d) }) }

    // the knock-out behind a label inside a plot: the surface at ch.knockOutAlpha (white on paper)
    readonly property color knock: isPrint ? "#FFFFFF" : Qt.alpha(Theme.colorSurface, ch.knockOutAlpha)
    readonly property color surface: isPrint ? "#FFFFFF" : Theme.colorSurface

    // ── a presenter Sentence as rich text: the words escaped, the figure runs in the figure face ──
    // `tone` (optional) colours the whole sentence; figures keep it. Missing → "".
    function html(s, tone) {
        if (!s) return ""
        const runs = s.runs || []
        if (!runs.length) return esc(s.text || "")
        let out = ""
        for (let i = 0; i < runs.length; ++i)
            out += runs[i].fig ? figHtml(runs[i].text) : esc(runs[i].text)
        return tone ? toneHtml(out, tone) : out
    }
    // a Sentence is present (non-empty text)
    function has(s) { return !!s && String(s.text || "") !== "" }
    // a string that may be "" or undefined
    function str(x) { return x === undefined || x === null ? "" : String(x) }
}
