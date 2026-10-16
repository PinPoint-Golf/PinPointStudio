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

// PRINT — the report register (B's editorial voice). The same blocks as Replay and Analyse, laid out as a
// document on A4 and cut into pages by the library PpPager (repeating list heads, no stranded pages):
//   masthead · 1 Session patterns (numbered entries, marginal sparklines, answers as footnotes)
//   · 2 This shot (standfirst, the fired table, the clean index, what could not be measured)
//   · 3 Watching (the barcode) · 4 the open pattern as a feature, always from a fresh page.
// Nothing here is clickable: links are cross-references ("The feature on p. 5 reads it in full").
// Round 4: on the library's PpPager and PpPathChains (PrintPager / PrintPaths are gone); the barcode prints
// row by row with its head repeated; PpKeyFigures excludes by key; shot labels are ordinals.
// "Every pattern" would repeat the feature block once per card, each from a fresh page (see NOTES3_print).
Item {
    id: root
    // ── harness contract ──
    property string view: "shot"
    property var readout: null
    property var cards: []
    property var source: null
    property var header: null
    property var detail: null
    property string medium: "print"
    property string printOrientation: ""
    property int printPage: 0
    readonly property int printPageCount: pager.pageCount
    property var body: null
    property string demoState: ""
    property string surface: "panel"

    // ── data ──
    readonly property var conds: (readout && readout.conditions) || []
    readonly property var fired: conds.filter(c => c.stateKind === "fired")
    readonly property var clean: conds.filter(c => c.stateKind === "clean").slice().sort((a, b) => String(a.name).localeCompare(String(b.name)))
    readonly property var notMeasured: conds.filter(c => c.stateKind === "notAssessable")
    readonly property var watching: (source && source.watching) || []
    readonly property var allCards: cards || []
    readonly property var detailCard: detail && detail.id ? (ro.byId(allCards, detail.id) || detail.header || null) : null
    readonly property int shotCount: readout ? readout.shotCount : 0
    readonly property int shotNo: ro.shotNo(readout)

    // ── the galley's flows, counted from the text width (portrait ≈ compact, landscape ≈ medium) ──
    readonly property real textW: width - 2 * ro.p.marginX
    readonly property int sesCols: ro.columns(textW, ro.flows.entry, ro.gutter(textW))
    readonly property var sesRows: { const out = []; for (let i = 0; i < allCards.length; i += sesCols) out.push(allCards.slice(i, i + sesCols)); return out }
    readonly property int indexCols: ro.columns(textW, ro.flows.index, ro.u(22))
    readonly property int indexBand: 6                    // rows per index band: the unit print may cut between
    readonly property var cleanBands: { const n = indexCols * indexBand, out = []; for (let i = 0; i < clean.length; i += n) out.push(clean.slice(i, i + n)); return out }

    // ── answers, printed as footnotes (the question is an affordance; its answer is content) ──
    // The library's PpQuestions decides WHICH questions exist (only those with an answer); the report words
    // the answers as short sentences, because a column of "fired together on 15 of its 15 shots" repeated
    // three times is noise on paper.
    function listOf(xs) { return xs.length <= 1 ? xs.join("") : xs.slice(0, -1).join(", ") + " and " + xs[xs.length - 1] }
    // patterns that fired on exactly the same shots: said once, as a group, rather than eight times
    readonly property var together: {
        const groups = []
        allCards.forEach(c => {
            const key = qx.firedShots(c.ticks).join(",")
            if (!key) return
            let g = groups.find(x => x.key === key)
            if (!g) { g = { key: key, n: key.split(",").length, cards: [] }; groups.push(g) }
            g.cards.push(c)
        })
        return groups.filter(g => g.cards.length >= 2)
    }
    function groupOf(card) { return together.find(g => g.cards.some(c => c.id === card.id)) || null }
    readonly property string togetherHtml: together.map(g =>
        ro.cap1(ro.word(g.cards.length)) + (g.n === root.shotCount ? " patterns fired on every one of the " + ro.figHtml(g.n) + " shots: " : " patterns fired on exactly the same " + ro.figHtml(g.n) + " shots: ")
        + listOf(g.cards.map(c => ro.esc(String(c.rankText || "").replace("#", "")) + "&nbsp;" + ro.esc(c.name))) + ".").join(" ")
    function answersFor(card) {
        if (!card) return []
        const qs = qx.forCondition(card, readout, source), out = []
        qs.forEach(q => {
            if (q.id === "clean") out.push("Clean on shot" + (q.shots.length > 1 ? "s " : " ") + listOf(q.shots.map(String)) + ".")
            else if (q.id === "together") {
                const grp = groupOf(card)
                if (grp) { out.push("Fired with the other " + ro.word(grp.cards.length - 1) + " on every one of its shots (see above)."); return }
                const t = qx.firedTogether(readout, card.id, 3)
                const all = t.every(x => x.n === x.of)
                if (all) out.push("Fired together with " + listOf(t.map(x => x.name)) + " on every one of its " + t[0].of + " firings.")
                else out.push("Fired together with " + listOf(t.map(x => x.name + " (" + x.n + " of " + x.of + ")")) + ".")
            } else if (q.id === "worse") out.push("Getting worse: the misses grow from the first third of the run to the last.")
            else if (q.answer) out.push(q.answer)
        })
        return out
    }
    function answerHtml(card) {
        const a = answersFor(card)
        if (!a.length) return ""
        return "<span style=\"color:" + ro.hex(ro.ink) + ";\">" + ro.esc(String(card.rankText || "").replace("#", "")) + "&nbsp;&nbsp;" + ro.esc(card.name) + ".</span> "
             + a.map(s => ro.esc(s)).join(" ")
    }

    PpPager {
        id: pager
        anchors.fill: parent
        roles: ro; brush: br
        readout: root.readout; source: root.source
        printPage: root.printPage
        footer: root.header ? ro.cap1(root.header.reviewNote) : ""

        // ═══ masthead ═══
        PpMasthead { width: pager.textW; roles: ro; medium: "print"; readout: root.readout; cards: root.allCards; source: root.source }

        // ═══ 1 Session patterns ═══
        PpSectionHead { width: pager.textW; roles: ro; medium: "print"; number: "1"; title: "Session patterns"
                      sub: root.header ? root.header.countLine : "" }
        Repeater {
            model: root.sesRows
            delegate: PpEntryFlow {
                required property var modelData
                width: pager.textW; roles: ro; brush: br; medium: "print"
                kind: "session"; items: modelData; cols: root.sesCols; readout: root.readout
                xrefId: root.detail ? (root.detail.id || "") : ""; xrefPage: pager.pageOf(detailHead)
            }
        }
        // "How far can I trust this?" (trust_and_gates_design.md §7.4), set open: a report has nothing to unfold
        PpTrustSummaryFold { width: pager.textW; roles: ro; printed: true; scope: "session"
                             sessionDir: root.source ? String(root.source.sessionDir || "") : "" }
        PpNotes { property bool keepNext: true
                width: pager.textW; roles: ro; medium: "print"; rule: true; kicker: "Asked of the data"
                text: "The questions the app asks beside each pattern, answered from the " + root.shotCount + " shots and numbered as above. They report what happened together, never what caused what." }
        PpNotes { width: pager.textW; roles: ro; medium: "print"; tone: 2; small: true; visible: root.togetherHtml.length > 0
                text: root.togetherHtml }
        Column {
            width: pager.textW
            spacing: ro.u(1)
            Repeater {
                model: root.allCards.filter(c => root.answersFor(c).length > 0)
                delegate: PpNotes {
                    required property var modelData
                    width: pager.textW; roles: ro; medium: "print"; tone: 2
                    text: root.answerHtml(modelData)
                }
            }
        }

        // ═══ 2 This shot ═══
        // sections after the first open on B's kicker + head + standfirst; the kicker carries the number
        PpStandfirst { section: "This shot"
                     width: pager.textW; roles: ro; medium: "print"
                     kicker: "2  ·  This shot  ·  " + (root.header ? root.header.shotLabel : "")
                     head: root.fired.length ? ro.cap1(ro.word(root.fired.length)) + " outside the corridor" : "A clean swing"
                     standfirst: ro.shotStandfirst(root.readout, false) }
        PpTrustSummaryFold { width: pager.textW; roles: ro; printed: true; scope: "shot"
                             swingDir: root.readout ? String(root.readout.swingDir || "") : ""
                             sessionDir: root.source ? String(root.source.sessionDir || "") : "" }
        PpLedgerTable {
            printGroup: "fired"
            width: pager.textW; roles: ro; brush: br; medium: "print"
            kind: "fired"; rows: root.fired; readout: root.readout; title: "Outside the corridor"
            xrefId: root.detail ? (root.detail.id || "") : ""; xrefPage: pager.pageOf(detailHead)
        }
        PpNotes { width: pager.textW; roles: ro; medium: "print"; tone: 2; small: true
                text: "Out by: how far past the corridor edge, in multiples of the distance to it. Fired: shots outside, of those measurable. The other " + ro.figHtml(root.clean.length) + " conditions stayed inside their corridor; they are listed alphabetically, with this shot’s value, in the index on p.&nbsp;" + pager.pageOf(indexHead) + "." }

        // ═══ 3 Watching ═══
        PpStandfirst { section: "Watching"
                     width: pager.textW; roles: ro; medium: "print"
                     kicker: "3  ·  Watching  ·  " + root.watching.length + " conditions"
                     head: "Seen, not yet a pattern"
                     standfirst: ro.watchStandfirst(root.source, root.readout, false) }
        PpBarcode { width: pager.textW; roles: ro; brush: br; medium: "print"
                  rows: root.watching; readout: root.readout; source: root.source; showMarginNotes: false }

        // ═══ The open pattern, as a feature. A report carrying every pattern repeats this block once per card;
        //     each starts a fresh page and names itself for "continued". ═══
        PpFeatureHead {
            id: detailHead
            property bool breakBefore: true
            property string section: root.detail && root.detail.name ? root.detail.name : "The pattern in detail"
            width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout; source: root.source
            citationOpen: false                   // the norm's source prints in full as a footnote under the figures
        }
        // C's figures, as ONE atom: the Loader carries atom + keyKinds, so the Pager never cuts inside the row
        // (in the mock it loaded by URL to survive a figure block mid-rework; in the module it is a type)
        Loader {
            id: figs
            property bool atom: true
            readonly property var keyKinds: ["fired", "clean", "ring", "median", "fault", "pass", "corridor"]
            width: pager.textW
            sourceComponent: Component {
                PpFigureRow {
                    width: figs.width
                    roles: ro; brush: br; medium: "print"; interactive: false
                    detail: root.detail; readout: root.readout
                }
            }
        }
        // A's key figures, without the two the pull figure already gives (shot 8, session median)
        PpKeyFigures {
            id: kfig
            width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout
            exclude: ["median", "shot"]
        }
        PpNotes { width: pager.textW; roles: ro; medium: "print"
                readonly property var sp: root.detail && root.detail.header ? (root.detail.header.spread || {}) : ({})
                visible: !!sp.normCitation
                text: "<i>The norm.</i> " + ro.esc(sp.normTagLong || "") + " " + ro.esc(sp.normCitation || "") }
        PpNotes { width: pager.textW; roles: ro; medium: "print"; visible: root.answersFor(root.detailCard).length > 0
                kicker: "Asked of the data"; tone: 2; small: true
                text: root.answersFor(root.detailCard).map(s => ro.esc(s)).join(" ") }
        // causes, then effects: one atom per path, so the lists split cleanly and repeat their heads
        PpPathChains { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout; which: "causes"; rule: true }
        PpPathChains { width: pager.textW; roles: ro; medium: "print"; detail: root.detail; readout: root.readout; which: "effects"; rule: true }
        // ═══ Index: the conditions inside the corridor, at the back as a book's index is. It follows on from
        //     the last feature without a page break (so the feature's tail never strands a page), cut into
        //     bands of six rows so print can break between bands; the head repeats on every page it runs onto. ═══
        Column {
            width: pager.textW
            Item {
                id: indexHead
                readonly property string section: "Index"
                readonly property var keyKinds: []
                readonly property string printGroup: "clean"
                property bool atom: true
                property bool keepNext: true
                width: pager.textW; height: ih.height + ro.u(6)
                Flow {
                    id: ih
                    width: parent.width; spacing: ro.u(8)
                    Text { id: iht; text: "Index · inside the corridor on shot " + root.shotNo; font: ro.fSection; color: ro.ink }
                    Text { text: root.clean.length; font: ro.fFigS; color: ro.ink3; height: iht.height; verticalAlignment: Text.AlignVCenter }
                    Item { width: ro.u(8); height: 1 }
                    Text { width: Math.min(implicitWidth, ih.width); wrapMode: Text.WordWrap; height: Math.max(implicitHeight, iht.height); verticalAlignment: Text.AlignVCenter
                           text: "Alphabetical. Ink: a session pattern, clean on this shot  ·  ° watching  ·  the rest clean all session"
                           font: ro.fMicro; color: ro.ink3 }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ro.hw; color: ro.hair }
            }
            Repeater {
                model: root.cleanBands
                delegate: PpIndexList {
                    required property var modelData
                    readonly property string printGroup: "clean"
                    width: pager.textW; roles: ro; brush: br; medium: "print"
                    conditions: modelData; showHead: false
                }
            }
        }
        PpNotes { keepPrev: true; width: pager.textW; roles: ro; medium: "print"; visible: root.notMeasured.length > 0
                text: "<i>Not measurable on this swing</i>, though clean all session: "
                      + root.notMeasured.map(c => "<b>" + ro.esc(String(c.name).replace(/ not measurable$/, "")) + "</b>, " + ro.esc(c.reason)).join("; ") + "." }
    }

    // computes the answers (never shown itself)
    PpQuestions { id: qx; visible: false; roles: ro; brush: null; applyToBrush: false }

    // ── the two shared objects, declared last ──
    PpDiagRoles { id: ro; medium: "print" }
    PpBrush {
        id: br
        cards: root.allCards
        pickedShot: root.readout ? root.readout.shotId : -1
        pickedCondition: root.detail ? (root.detail.id || "") : ""
    }
}
