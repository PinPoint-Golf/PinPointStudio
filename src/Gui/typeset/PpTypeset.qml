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

// PPTYPESET — the per-panel helper of the typeset system (design system §15.3). It turns Theme's typeset
// DATA (Theme.typeset for the aesthetic, Theme.typesetPrint for paper) into what a block draws with: the
// type roles, the inks, the marks, the size classes, the flows and tiers, and the small generic text
// helpers. Not a singleton: the MEDIUM belongs to the instance, so one window can show a panel and lay
// out its printed report at the same time. A panel (or a register) makes ONE and passes it to every
// block as `roles`:
//     PpTypeset { id: ts; medium: root.medium }        …        PpLedgerTable { roles: ts; … }
// Never give it the id `roles`: in `roles: roles` the block's own property shadows the id, and the block
// silently uses its default. Declare it LAST in the file (children are destroyed in creation order).
//
// GENERIC only. A panel's own vocabulary (its sentences, its column tier tables, its extra flows and
// element tiers) lives in a subclass — PpDiagRoles for Session diagnostics — which adds tables through
// extraFlows / extraTiers / extraElements and adds its own functions. No word of one panel belongs here.
QtObject {
    id: ts
    property string medium: "screen"                 // "screen" | "print"
    readonly property bool isPrint: medium === "print"

    // ── the data: Theme's merged typeset table, and the print block ──
    readonly property var r: Theme.typeset
    readonly property var p: Theme.typesetPrint

    // ── a subclass's vocabulary, merged over the generic tables below ──
    property var extraFlows: ({})                    // more flow minimums, in em
    property var extraTiers: ({})                    // a block's column tier tables, keyed by kind
    property var extraElements: ({})                 // more named elements, each { tier, else }

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    // FLOWS — minimum measures in em of the body role. columns = ⌊(w + gap) / (min·em + gap)⌋ (rule 4).
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    readonly property var _flows: ({
        prose: 26, measure: 44,               // a prose column; the longest run of prose
        index: 20,                            // an index line (name … value)
        contents: 22,                         // a contents line with a sub-line
        tile: 22,                             // a small-multiple tile
        keyFigure: 10,                        // a key figure
        chart: 30,                            // a chart
        proseTwo: 62,                         // two prose columns side by side
        rail: 18, margin: 18,                 // a side rail / sidenote margin
        sparkline: 7                          // a sparkline
    })
    readonly property var flows: Object.assign({}, _flows, extraFlows)

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    // TIERS — the responsive rule as data. 1 always · 2 compact+ · 3 medium+ · 4 wide only.
    // Below its tier an element moves to its `else` (rule 3: fold, don't drop). The shared elements
    // (chrome, rails, margins) are here; a block's own column tables come from the subclass.
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    readonly property var _elements: ({
        tabCounts:       { tier: 2, else: "each view's head states its count" },
        breadcrumb:      { tier: 2, else: "the head names the subject; Back stays" },
        reviewNote:      { tier: 3, else: "a note at the foot of the view" },
        rail:            { tier: 4, else: "head above; legend, bookends, footnotes below" },
        margin:          { tier: 4, else: "notes under the figure, in prose columns" },
        longStandfirst:  { tier: 3, else: "short form; the rest opens a later section" }
    })
    readonly property var tiers: Object.assign({}, extraTiers, { elements: Object.assign({}, _elements, extraElements) })

    // ── scale and classes ──────────────────────────────────────────────────────────────────────
    function u(n) { return isPrint ? Math.round(n * p.printScale * 2) / 2 : Theme.sp(n) }
    readonly property real em: fBody.pixelSize
    function sizeClass(w) { const e = w / em; return e >= 90 ? "wide" : e >= 60 ? "medium" : e >= 42 ? "compact" : "narrow" }
    function rank(cls) { return cls === "wide" ? 4 : cls === "medium" ? 3 : cls === "compact" ? 2 : 1 }
    function atLeast(w, cls) { return rank(sizeClass(w)) >= rank(cls) }
    // tier: a number, or an element key of tiers.elements; w: a width in px
    function shows(tier, w) {
        const t = typeof tier === "number" ? tier : ((tiers.elements[tier] || {}).tier || 1)
        return rank(sizeClass(w)) >= t
    }
    function gutter(w) { return u(r.gutter[sizeClass(w)] || 14) }
    function columns(w, minEm, gap) {
        const g = gap === undefined ? gutter(w) : gap
        return Math.max(1, Math.floor((w + g) / (minEm * em + g)))
    }
    function measureW(w) { return Math.min(w, flows.measure * em) }
    // fold decisions, named once
    function railFolds(w) { return !shows("rail", w) }
    function marginFolds(w) { return !shows("margin", w) }

    // ── ink ────────────────────────────────────────────────────────────────────────────────────
    readonly property color ink: Theme.colorText
    readonly property color ink2: Theme.colorText2
    readonly property color ink3: Theme.colorText3
    readonly property color warn: Theme.colorWarn            // "outside its corridor", and nothing else
    readonly property color accent: Theme.colorAccent
    readonly property color paper: isPrint ? "#FFFFFF" : Theme.colorBg
    readonly property color link: r.linkAccent ? Theme.colorAccent : Theme.colorText2
    readonly property color rule: Qt.alpha(Theme.colorText, r.ruleAlpha)
    readonly property color hair: Qt.alpha(Theme.colorText, r.hairAlpha)
    readonly property real hw: isPrint ? p.hair : 1                           // hairline width
    readonly property real tintK: isPrint ? p.tintScale : 1
    readonly property color grid: Theme.gridColor(Qt.alpha(Theme.colorText3, 0.28))
    readonly property real gridOpacity: Theme.gridOpacity(1.0)
    readonly property color baseline: Theme.baselineColor(Qt.alpha(Theme.colorText3, 0.6))
    readonly property real baselineOpacity: Theme.baselineOpacity(1.0)
    readonly property color wash: Qt.alpha(Theme.colorText, r.zoneTint * tintK)   // THE chart wash: the corridor
    readonly property color fault: Qt.alpha(Theme.colorWarn, r.faultAlpha)        // the fault line
    readonly property color passEdge: Qt.alpha(Theme.colorText3, r.passAlpha)     // the pass edge (dashed)
    readonly property color pickWash: Qt.alpha(Theme.colorText, Theme.dark ? 0.10 : 0.07)   // the picked shot, as a column
    readonly property color hoverWash: Qt.alpha(Theme.colorText, Theme.dark ? 0.065 : 0.045)  // a hovered shot: lighter than the pick
    readonly property color rowHover: Qt.alpha(Theme.colorText, Theme.dark ? 0.05 : 0.03)
    readonly property real dim: 0.28                                         // what recedes under a highlight
    readonly property int fast: Theme.durationFast
    readonly property int normal: Theme.durationNormal
    // marks
    readonly property bool square: r.markShape === "square"
    readonly property real markFired: u(r.markFired)
    readonly property real markClean: u(r.markClean)
    readonly property real ringD: isPrint ? u(p.ring) : u(r.ring)
    readonly property real stripPitch: u(r.stripPitch)
    readonly property real stripBar: Math.max(2, Math.round(u(r.stripBar)))
    readonly property real stripH: u(r.stripH)
    function stateColor(s) { return s === "fired" ? warn : ink3 }

    // ── THE STRIP AT ANY SESSION LENGTH (D7) ───────────────────────────────────────────────────
    // Nothing assumes 15 shots. A strip wants stripPitch a shot; given a width it narrows the pitch to
    // fit, down to stripMinPitch, and only then wraps onto a second line (never clips, never drops a
    // shot). stripWidthFor(n) is the column a block should reserve for n shots: n × pitch up to
    // stripMaxShots × pitch, so a 50-shot session narrows its marks rather than eating the name column.
    readonly property real stripMinPitch: Math.max(2.5, u(3))
    readonly property int stripMaxShots: 22
    function stripWidthFor(n) { const k = Math.max(1, n || 0); return Math.min(k, stripMaxShots) * stripPitch }
    // → { pitch, perRow, rows, barW, width, height } for n shots in `avail` px (avail ≤ 0: unconstrained)
    function stripFit(n, avail, pitch, barW, barH, rowGap) {
        const want = pitch || stripPitch, k = Math.max(0, n || 0)
        const bw0 = barW || stripBar, bh = barH || stripH, gap = rowGap === undefined ? u(4) : rowGap
        let p = want
        if (avail > 0 && k > 0 && k * want > avail) p = Math.max(stripMinPitch, avail / k)
        const perMax = (avail > 0 && k > 0) ? Math.max(1, Math.min(k, Math.floor((avail + 0.01) / p))) : Math.max(1, k)
        const rows = Math.max(1, Math.ceil(k / perMax))
        const per = Math.max(1, Math.ceil(k / rows))                     // balanced lines, never a stub line
        const bw = Math.max(1, Math.min(bw0, Math.round(p * 0.62 * 2) / 2))
        return { pitch: p, perRow: per, rows: rows, barW: bw, width: Math.min(k, per) * p, height: rows * bh + (rows - 1) * gap }
    }
    // the not-measured STUB (D5): a short ink3 mark on the strip's baseline — present, never a gap,
    // and unlike the clean tick (a tall hairline) it is a low flat block
    function stubH(barH) { return Math.max(1.5, Math.round((barH || stripH) * 0.22 * 2) / 2) }
    // shot numbers along an axis thin by pitch: every shot from `full` px, else every 5th (+ ends)
    function shotLabelStep(pitchPx, labelPx) {
        const need = (labelPx || u(14)) + u(3)
        for (const s of [1, 2, 5, 10, 20, 25, 50]) if (s * pitchPx >= need) return s
        return 100
    }
    // whether shot i (0-based) of n gets its number on a thinned axis: the kept ones (first, last, picked,
    // hovered) always; the others on every step-th shot, unless a kept number is closer than a step
    function axisLabelShown(i, n, step, keep) {
        const ks = [0, n - 1].concat(keep || []).filter(k => k >= 0)
        if (ks.indexOf(i) >= 0) return true
        if (step <= 1) return true
        if ((i + 1) % step !== 0) return false
        for (const k of ks) if (Math.abs(i - k) < step) return false
        return true
    }
    // ordinals: "Shot N of M" is the ORDINAL (shotIndex + 1), never the shot id (ids survive a deleted swing).
    // A shot readout here is anything with shotIndex / shotId / shotCount (/ club for sessionMeta).
    function shotNo(readout) { return readout ? (readout.shotIndex !== undefined && readout.shotIndex >= 0 ? readout.shotIndex + 1 : readout.shotId) : 0 }
    function shotCountOf(readout, ticks) { return readout && readout.shotCount !== undefined ? readout.shotCount : (ticks || []).length }
    function shotOfText(readout) { return readout ? "shot " + shotNo(readout) + " of " + readout.shotCount : "" }
    // the index of a shot id in a run of ticks (the column to shade), or -1
    function indexOfShot(ticks, id) {
        if (id === undefined || id < 0) return -1
        const t = ticks || []
        for (let i = 0; i < t.length; ++i) if ((t[i].shotId !== undefined ? t[i].shotId : i + 1) === id) return i
        return -1
    }

    // ── LINK GRADES (D6): how the library draws a graded link, ported from PpChainLink / styleOf() exactly,
    // with ONE change for rule 7: "moved together" was colorAccent; accent means a live control and colour
    // means "out", so the strongest grade is full ink at the heaviest weight instead. Arrowheads ONLY where
    // the evidence supports a direction (moved together, conditionally dependent). The model publishes
    // stroke/width/arrow/opacity per link; linkStyle() prefers those and falls back to this table.
    // (Generic because the PpPageKey keys these strokes, and the key is print furniture every panel uses.)
    readonly property var linkGrades: ({
        movedTogether:          { glyph: "⇒", word: "Moved together",          stroke: "solid",  width: 2.0, arrow: true,  opacity: 1.0, tone: 0 },
        conditionallyDependent: { glyph: "→", word: "Conditionally dependent", stroke: "solid",  width: 1.5, arrow: true,  opacity: 1.0, tone: 1 },
        coherent:               { glyph: "╌", word: "Coherent",                stroke: "dashed", width: 1.5, arrow: false, opacity: 1.0, tone: 1 },
        presentTogether:        { glyph: "∼", word: "Present together",        stroke: "dotted", width: 1.0, arrow: false, opacity: 1.0, tone: 2 },
        unanchored:             { glyph: "⋯", word: "Unanchored",              stroke: "dotted", width: 1.0, arrow: false, opacity: 0.5, tone: 2 }
    })
    readonly property var linkGradeOrder: ["movedTogether", "conditionallyDependent", "coherent", "presentTogether", "unanchored"]
    function linkStyle(l) {
        const g = linkGrades[(l && l.grade) || ""] || linkGrades.unanchored
        const st = String((l && l.stroke) || "")
        const stroke = st === "solidAccent" || st === "solid" ? "solid" : st === "dashed" ? "dashed" : st.indexOf("dotted") === 0 ? "dotted" : g.stroke
        return {
            grade: (l && linkGrades[l.grade]) ? l.grade : "unanchored", glyph: g.glyph, word: (l && l.word) || g.word, note: (l && l.note) || "",
            stroke: stroke, width: (l && l.width) || g.width, arrow: l && l.arrow !== undefined ? l.arrow === true : g.arrow,
            opacity: l && l.opacity !== undefined ? l.opacity : g.opacity,
            color: g.tone === 0 ? ink : g.tone === 1 ? ink2 : ink3
        }
    }
    function dashFor(stroke) { return stroke === "dashed" ? [u(4), u(3)] : stroke === "dotted" ? [Math.max(1, u(1.2)), u(2.6)] : null }

    // ── type roles ─────────────────────────────────────────────────────────────────────────────
    function _f(fam, sz, w, extra) {
        const o = { family: fam, pixelSize: Math.round(u(sz)), weight: w }
        if (extra) for (const k in extra) o[k] = extra[k]
        return Qt.font(o)
    }
    readonly property font fFeature: _f(r.displayFamily, r.featureSize, r.displayWeight)
    readonly property font fTitle:   _f(r.displayFamily, r.titleSize, r.displayWeight)
    readonly property font fStand:   _f(r.standFamily, r.standSize, r.textWeight)
    readonly property font fBody:    _f(r.textFamily, r.bodySize, r.textWeight)
    readonly property font fBodyM:   _f(r.textFamily, r.bodySize, r.strongWeight)
    readonly property font fBodyL:   _f(r.textFamily, r.bodySize, r.textWeight, { underline: r.linkUnderline })
    readonly property font fSmall:   _f(r.textFamily, r.smallSize, r.textWeight)
    readonly property font fSmallM:  _f(r.textFamily, r.smallSize, r.strongWeight)
    readonly property font fSmallI:  _f(r.textFamily, r.smallSize, r.textWeight, { italic: r.italicNotes })
    readonly property font fSmallL:  _f(r.textFamily, r.smallSize, r.textWeight, { underline: r.linkUnderline })
    readonly property font fMicro:   _f(r.textFamily, r.microSize, r.textWeight)
    readonly property font fLabel:   _f(r.labelFamily, r.labelSize, r.textWeight,
                                        { letterSpacing: r.labelTracking, capitalization: r.labelCaps ? Font.AllUppercase : Font.MixedCase })
    readonly property font fLabelL:  _f(r.labelFamily, r.labelSize, r.textWeight,
                                        { letterSpacing: r.labelTracking, underline: r.linkUnderline,
                                          capitalization: r.labelCaps ? Font.AllUppercase : Font.MixedCase })
    readonly property font fKicker:  _f(r.labelFamily, r.smallSize, r.kickerWeight,
                                        { letterSpacing: r.labelTracking, capitalization: r.labelCaps ? Font.AllUppercase : Font.MixedCase })
    readonly property font fSection: _f(r.textFamily, r.sectionCaps ? r.smallSize : r.bodySize, r.sectionWeight,
                                        { letterSpacing: r.sectionTracking, capitalization: r.sectionCaps ? Font.AllUppercase : Font.MixedCase })
    readonly property font fFig:     _f(r.figureFamily, r.bodySize, r.textWeight)
    readonly property font fFigS:    _f(r.figureFamily, r.smallSize, r.textWeight)
    readonly property font fFigLabel: _f(r.figureFamily, r.labelSize, r.textWeight)
    readonly property font fFigL:    _f(r.figureFamily, r.titleSize, r.textWeight)
    readonly property font fPull:    _f(r.displayFamily, r.featureSize, r.displayWeight)
    readonly property font fTab:     _f(r.labelCaps ? r.labelFamily : r.textFamily, r.labelCaps ? r.labelSize : r.bodySize, r.textWeight,
                                        { letterSpacing: r.labelCaps ? r.labelTracking : 0, capitalization: r.labelCaps ? Font.AllUppercase : Font.MixedCase })

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    // TEXT HELPERS — generic: case, escaping, numbers in words and figures, the inline-figure HTML,
    // and the session as a line of furniture. Missing values show "-", never 0; negatives use "−".
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    function cap1(s)   { s = String(s || ""); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }
    function lower1(s) { s = String(s || ""); return s.length ? s.charAt(0).toLowerCase() + s.slice(1) : s }
    function sentence(s) { s = String(s || "").trim(); if (!s) return ""; s = cap1(s); return /[.!?]$/.test(s) ? s : s + "." }
    function esc(s) { return String(s === undefined || s === null ? "" : s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    function hex(c) { return String(c) }
    function num(v) {
        if (v === undefined || v === null || !isFinite(v)) return "-"
        const x = Math.abs(v - Math.round(v)) < 0.05 ? Math.round(v) : Math.round(v * 10) / 10
        return (x < 0 ? "−" : "") + String(Math.abs(x))
    }
    readonly property var _words: ["no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
        "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"]
    function word(n) { return (n >= 0 && n < _words.length) ? _words[n] : String(n) }
    // a count standing alone as a noun phrase ("outside on none of them"): zero is "none", never "no"
    function countWord(n) { return n === 0 ? "none" : word(n) }
    function byId(list, id) { for (let i = 0; i < (list || []).length; ++i) if (list[i].id === id) return list[i]; return null }
    // the inline figure: the figure face inside a sentence, optionally coloured
    function figHtml(s, c) {
        return "<span style=\"font-family:'" + r.inlineFigureFamily + "';" + (c ? " color:" + hex(c) + ";" : "") + "\">"
             + esc(s).replace(/ /g, "&nbsp;") + "</span>"
    }
    function strongHtml(s) { return "<span style=\"font-weight:" + (r.strongWeight === Font.Medium ? 500 : 600) + "; color:" + hex(ink) + ";\">" + esc(s) + "</span>" }
    function toneHtml(s, c) { return "<span style=\"color:" + hex(c) + ";\">" + s + "</span>" }

    // the session as a line of furniture (golfer · date · club · shot n of N), for running heads
    function sessionMeta(source, readout, longMonth) {
        const dir = String(source ? source.sessionDir : "")
        const m = dir.match(/(\d{4})-(\d{2})-(\d{2})_([^_\/]+)_/)
        const mon = longMonth ? ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
                              : ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
        return {
            golfer: m ? m[4].replace(/-/g, " ") : "",
            date: m ? (+m[3]) + " " + mon[+m[2] - 1] + " " + m[1] : "",
            club: readout && readout.club ? String(readout.club).toLowerCase() : "",
            shot: shotOfText(readout)
        }
    }
}
