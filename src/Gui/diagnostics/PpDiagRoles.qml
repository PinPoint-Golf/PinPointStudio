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

// PPDIAGROLES — the Session diagnostics vocabulary on top of the generic typeset helper (design system
// §15.3). Everything a typeset block draws with comes from PpTypeset; what is particular to the
// diagnostics model lives here: its extra flow minimums and element tiers, the ledger's column tier
// tables (A's fired / session / watching / path), the Replay session row's geometry, and the text
// helpers that turn the model's strings and shapes (valueText, recurrence, corridorText, trend, cards,
// the readout) into figures and sentences. A diagnostics register makes ONE and passes it as `roles`:
//     PpDiagRoles { id: ro; medium: root.medium }
// Never give it the id `roles` (see PpTypeset). Generic blocks accept it too: it IS a PpTypeset.
PpTypeset {
    id: dr

    extraFlows: ({
        entry: 30, entryMargin: 36,           // a session entry; with its sparkline in the margin
        firedEntry: 26,                       // run-in fired entries
        watchingTable: 46,                    // one watching table column
        dotRich: 52, dotTight: 34,            // a dot-plot block (tier · name · plot · value) / tight
        runPanel: 14,                         // the run panel inside a figure row
        figuresInRow: 62,                     // spread | run | history in one row
        openingRow: 74, figPair: 66,          // the Detail opening in one row; Fig. 2 beside Fig. 3
        dagCol: 12                            // a cause-graph column
    })

    // the ledger's columns (PpLedgerTable reads tiers[kind]); fo = order on the row's second line
    extraTiers: ({
        fired: [
            { key: "name",     tier: 1, kind: "name",  w: 0,   head: "" },
            { key: "tier",     tier: 4, kind: "small", w: 5,   head: "Tier",      fo: 5, fold: "" },
            { key: "value",    tier: 1, kind: "fig",   w: 4.4, head: "Shot",      align: "r" },
            { key: "unit",     tier: 1, kind: "small", w: 8.6, head: "Unit" },
            { key: "corridor", tier: 4, kind: "small", w: 18.5, head: "Corridor", fo: 3, fold: "" },
            { key: "outby",    tier: 3, kind: "figs",  w: 3.8, head: "Out by",    align: "r", fo: 4, fold: "out by" },
            { key: "strip",    tier: 2, kind: "strip", w: 0,   head: "Shots",     fo: 1 },
            { key: "frac",     tier: 2, kind: "figs",  w: 3,   head: "Fired",     align: "r", fo: 2, fold: "" }
        ],
        session: [
            { key: "rank",    tier: 1, kind: "figs",  w: 1.6, head: "#", align: "r", ink3: true },
            { key: "name",    tier: 1, kind: "name",  w: 0,   head: "" },
            { key: "strip",   tier: 2, kind: "strip", w: 0,   head: "Shots", fo: 1 },
            { key: "frac",    tier: 1, kind: "figs",  w: 3,   head: "Fired", align: "r" },
            { key: "trend",   tier: 4, kind: "small", w: 6.6, head: "Trend", fo: 3, fold: "" },
            { key: "median",  tier: 3, kind: "fig",   w: 4.2, head: "Median", align: "r", fo: 2, fold: "median" },
            { key: "shot",    tier: 1, kind: "fig",   w: 4.2, head: "Shot", align: "r" },
            { key: "unit",    tier: 2, kind: "small", w: 7,   head: "Unit", fo: 2.5, fold: "" },
            { key: "typical", tier: 4, kind: "shot",  w: 3.2, head: "Typical", align: "r", group: "Go to shot", fo: 4, fold: "typical shot" },
            { key: "worst",   tier: 4, kind: "shot",  w: 3,   head: "Worst",   align: "r", group: "Go to shot", fo: 5, fold: "worst" },
            { key: "best",    tier: 4, kind: "shot",  w: 3,   head: "Best",    align: "r", group: "Go to shot", fo: 6, fold: "best" }
        ],
        watching: [
            { key: "name",  tier: 1, kind: "name",  w: 0,   head: "Condition" },
            { key: "value", tier: 1, kind: "fig",   w: 4.4, head: "Shot", align: "r" },
            { key: "unit",  tier: 1, kind: "small", w: 7.5, head: "Unit" },
            { key: "strip", tier: 2, kind: "strip", w: 0,   head: "Shots", fo: 1 },
            { key: "frac",  tier: 1, kind: "figs",  w: 3,   head: "Fired", align: "r" }
        ],
        path: [
            { key: "no",    tier: 1, kind: "figs",  w: 1.6, head: "No.", align: "r", ink3: true },
            { key: "path",  tier: 1, kind: "rich",  w: 0,   head: "" },
            { key: "links", tier: 3, kind: "small", w: 13,  head: "Links", fo: 1, fold: "links" },
            { key: "open",  tier: 1, kind: "link",  w: 2.8, head: "", align: "r", screenOnly: true }
        ]
    })

    // single elements, with where they go (the two generic ones re-stated with the diagnostics' own else)
    extraElements: ({
        breadcrumb:      { tier: 2, else: "the head names the condition; Back stays" },
        longStandfirst:  { tier: 3, else: "short form; the rest opens 'What this leads to'" },
        pathEvidence:    { tier: 2, else: "behind Open" },
        highMeans:       { tier: 2, else: "the fact file's 'Reads' line" },
        tileMeasure:     { tier: 2, else: "DETAIL's meta line" },
        tileBookends:    { tier: 2, else: "folded into the firings line" },
        barcodeValue:    { tier: 3, else: "every value is in This shot" },
        barcodeNames:    { tier: 2, else: "names stand above their barcode" },
        causeGraph:      { tier: 3, else: "the indented outline (PpCauseGraph)" },
        evidenceEarly:   { tier: 2, else: "the evidence paragraph follows the figures" }
    })

    // ── the Replay session row's geometry (PpSparkRow and PpSparkHead share it, so a head always aligns) ──
    // name (wraps) · strip · this shot's value · sparkline (tier 2)
    function sparkGeo(w, n, spark) {
        const valW = u(48), gap = u(12)
        const sk = spark === undefined ? shows(2, w) : spark
        const skW = sk ? flows.sparkline * em : 0
        const stripW = stripWidthFor(n || 15)
        const nameW = Math.max(u(80), w - stripW - valW - skW - gap * (sk ? 3 : 2))
        const stripX = nameW + gap
        return { nameW: nameW, stripX: stripX, stripW: stripW, valX: stripX + stripW + gap, valW: valW, skX: w - skW, skW: skW, gap: gap, spark: sk }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    // TEXT HELPERS — figures, units and sentences built from the model's live data (A's parsers, B's copy).
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    readonly property var _numRe: /^\s*([−\-+]?\d[\d.,]*(?::1)?)\s*(.*)$/
    function fig(v)    { const m = String(v === undefined || v === null ? "" : v).match(_numRe); return m ? m[1] : "-" }
    function unitOf(v) { const m = String(v === undefined || v === null ? "" : v).match(_numRe); return m ? m[2] : "" }
    function shortUnit(s) { return String(s || "").replace(/ (width|length)$/, "") }
    function recParts(rec) { const m = String(rec || "").match(/(\d+)\s+of\s+(\d+)/); return m ? { n: +m[1], of: +m[2] } : null }
    function frac(rec)   { const q = recParts(rec); return q ? q.n + "/" + q.of : "-" }
    function fracOf(rec) { const q = recParts(rec); return q ? q.n + " of " + q.of : "-" }
    function rate(rec)   { const q = recParts(rec); return q && q.of > 0 ? q.n / q.of : 0 }
    function shortCount(rec) { return String(rec || "").replace(" measurable shots", "") }
    function corridorShort(t) {
        let s = String(t || "")
        s = s.replace(/pass up to /, "pass ≤ ").replace(/pass from (\S+) up/, "pass ≥ $1").replace(/fault at /, "fault ")
        return s.replace(/(\d)[^\d]*$/, "$1")
    }
    function corridorParts(t) {
        const out = []
        String(corridorShort(t)).split(" · ").forEach(q => {
            let m
            if ((m = q.match(/^pass (.*)$/)))               out.push({ label: "Pass", figure: m[1] })
            else if ((m = q.match(/^fault outside (.*)$/))) out.push({ label: "Fault outside", figure: m[1] })
            else if ((m = q.match(/^fault (.*)$/)))         out.push({ label: "Fault line", figure: m[1] })
        })
        return out
    }
    function corridorClause(ct) {
        if (!ct) return ""
        let pass = "", flt = ""
        String(ct).split(" · ").forEach(q => { if (q.indexOf("pass ") === 0) pass = q.slice(5); else if (q.indexOf("fault ") === 0) flt = q.slice(6) })
        let s = ""
        if (pass) s += "passes " + pass
        if (flt) s += (s ? "; fault " : "fault ") + flt
        return s
    }
    function trendWord(c) {
        if (!c) return "-"
        return c.trend === "worsening" ? "worsening ↑" : c.trend === "improving" ? "improving ↓"
             : c.trend === "stable" ? "steady" : (c.trend || "-")
    }
    function recWords(rec, shotCount) {
        const q = recParts(rec)
        if (!q) return esc(rec)
        if (q.of === 0) return "never measurable"
        if (q.n === q.of) return "every one of the " + figHtml(q.of) + " shots"
        return figHtml(q.n) + " of " + figHtml(q.of) + (shotCount > 0 && q.of < shotCount ? " measurable" : "") + " shots"
    }
    function trendWords(c) {
        if (!c) return ""
        if (c.trend === "worsening") return "and getting worse"
        if (c.trend === "improving" || c.resolving) return "and easing"
        return "holding steady"
    }
    function afterWords(n) { return (n === undefined || n === null) ? "" : n === 0 ? "none after this one" : (word(n) + " more after this one") }

    // ── B's sentences (long and short forms), built from the data ──
    function shotStandfirst(readout, long) {
        const cs = (readout && readout.conditions) || []
        const fired = cs.filter(c => c.stateKind === "fired"), clean = cs.filter(c => c.stateKind === "clean")
        const na = cs.filter(c => c.stateKind === "notAssessable")
        const pats = fired.filter(c => c.tierTag === "pattern").length, m = readout ? readout.measurableSetCount : 0
        const naTail = na.length ? ", and " + word(na.length) + " could not be measured." : "."
        // zero counts are words, never "no" (R, round 4): "All 78 … stayed inside", "none of them is a session pattern yet"
        if (fired.length === 0) {
            if (!long) return "All " + figHtml(m) + " conditions stayed inside their corridors."
            return "All " + figHtml(m) + " conditions read on this swing stayed inside their corridors" + naTail
        }
        const watched = fired.length - pats
        if (!long) return cap1(word(fired.length)) + " of " + figHtml(m) + " conditions went outside their corridor; "
                        + (pats === 0 ? "none of them is a session pattern yet." : word(pats) + (pats === 1 ? " is a session pattern." : " are session patterns."))
        const split = pats === 0 ? "none of them among the session’s patterns, all still being watched"
                    : watched === 0 ? (fired.length === 1 ? "one of the session’s patterns" : "all of them among the session’s patterns")
                    : word(pats) + " of the session’s patterns and " + word(watched) + " still being watched"
        return "Of the " + figHtml(m) + " conditions read on this swing, " + word(fired.length) + " went outside their corridor: " + split
             + ". The other " + figHtml(clean.length) + " stayed inside" + naTail
    }
    function sessionStandfirst(cards, readout, long) {
        const cs = cards || [], f = cs.filter(c => c.thisShot === "fired").length
        const n = readout ? readout.shotCount : 0, s = shotNo(readout)
        if (!long) return "Counted over " + figHtml(n) + " shots. Shot " + figHtml(s) + " was outside on " + countWord(f) + " of them."
        return "Counted over all " + figHtml(n) + " shots, " + word(cs.length) + (cs.length === 1 ? " condition recurs" : " conditions recur") + " often enough to call habits. Shot "
             + figHtml(s) + " went outside the corridor on " + countWord(f) + " of them."
    }
    function watchStandfirst(source, readout, long) {
        const ws = (source && source.watching) || [], cs = (readout && readout.conditions) || []
        const here = ws.filter(w => { const c = byId(cs, w.id); return c && c.stateKind === "fired" }).length
        const n = readout ? readout.shotCount : 0, s = shotNo(readout)
        if (!long) return "Outside at least once, not yet a habit. Shot " + figHtml(s) + " was outside on " + countWord(here) + "."
        return cap1(word(ws.length)) + (ws.length === 1 ? " condition" : " conditions") + " went outside their corridor at least once in " + figHtml(n)
             + " shots, but not often enough to count as habits. Shot " + figHtml(s) + " went outside on " + countWord(here) + " of them."
    }
    function firedEntry(c, card, shotCount) {
        let h = strongHtml(c.name) + "&nbsp;&nbsp;" + figHtml(c.valueText, warn) + ". "
        const cc = corridorClause(c.corridorText)
        if (cc) h += "The corridor " + esc(cc) + ". "
        if (c.tierTag === "pattern") h += cap1(recWords(c.recurrence, shotCount)) + ", " + trendWords(card) + "; " + afterWords(c.firingsAfter) + "."
        else h += "Seen on " + recWords(c.recurrence, shotCount) + "; " + afterWords(c.firingsAfter) + "."
        return h
    }
    function sessionEntry(c, withName, xrefPage, shotCount) {
        const sp = c.spread || {}
        let h = withName ? strongHtml(c.name) + "&nbsp;&nbsp;" : ""
        h += (c.fresh ? "New this session: " + recWords(c.recurrence, shotCount) : cap1(recWords(c.recurrence, shotCount))) + ", " + trendWords(c) + ". "
        h += esc(sentence(c.directionText)) + " Here " + figHtml(c.valueText, c.thisShot === "fired" ? warn : ink)
        if (sp.medianNumber !== undefined && sp.medianNumber !== "") h += " against a session median of " + figHtml(sp.medianNumber)
        h += "."
        if (xrefPage > 0) h += " The feature on p.&nbsp;" + xrefPage + " reads it in full."
        return h
    }
    function consequenceSplit(card) {
        const c = String(card && card.consequence || "")
        const m = c.match(/^(.*?[.!?])\s+(.*)$/)
        return m ? [m[1], m[2]] : [c, ""]
    }
    function headlineHtml(s) {
        s = String(s || ""); const i = s.indexOf(": ")
        return i < 0 ? esc(s) : strongHtml(s.slice(0, i) + ".") + " " + esc(cap1(s.slice(i + 2)))
    }
}
