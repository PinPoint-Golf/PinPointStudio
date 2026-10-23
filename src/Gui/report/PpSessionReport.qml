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

// THE SESSION REPORT REGISTER (docs/design/session_report_design.md §3, §9; the map it binds is specified in
// build/report-build/REPORT_MAP.md). A PDF a coach sends a golfer: the client pages, the launch monitor appendix
// (PpLmAppendix, when the session has readings; it is part of the galley), then the technical appendix.
//
// ONE GALLEY, CUT BY THE LIBRARY PAGER (§9.1 rule 1). Every block below is laid out once at the text width and
// PpPager cuts it into A4 pages between atoms. Nothing has a fixed height but the page, with ONE exception, the
// cover (rule 3), which PpReportCover fits to exactly one page. Sections start a fresh page (`breakBefore` on
// their first atom) and may run on: a continued page says "Work-on 3, continued" and a table repeats its heads
// (printGroup). Every list is capped in the map AND here (rule 4: the map words the "N more"; the register only
// slices, as a guard). Empty means absent (rule 6): a block with nothing to say is not drawn, heading and all.
// The register writes no sentence of its own (rule 7): every sentence is a map string; only fixed headings
// ("Going well", "What to do") and cross-references ("p. 4") are set here.
//
// FOLIO LIGHT, WHATEVER THE APP'S THEME (§7.1). A client document has one look, and printing must not re-theme the
// app. So nothing here reads a LIVE theme token (Theme.colorText follows the app; in a dark theme it is near-white).
// `rr` below is the report's own palette and type, read from the Folio entry's LIGHT block of Theme's theme table
// (Theme._themes.folio — data, not the current theme) and Theme.serifFamily (Georgia, else Gelasio: the system's,
// not the theme's). The studio's accent replaces Folio's accent in the report's roles only; faults stay Folio's
// warn, and every warn mark has a word beside it. tst_session_report proves it: the same fixture in two app themes
// draws every Text in the same family, size and colour, and print_report.qml renders the pages pixel-identical.
//
// THE PAGER'S OWN FURNITURE IS COVERED, NOT USED. PpPager draws PpRunningHead / PpFolio / its "continued" label in
// the APP's theme with the diagnostics' words. The register needs the studio, the golfer and both dates (§8.4)
// instead, and the cover none at all. The pager is given a print PpTypeset only for its geometry and units (both
// theme-free: Theme.typesetPrint is one block for every theme, and u() on paper ignores fontScale); its furniture
// bands lie outside its content window, so the register paints paper over them and draws its own.
//
// PRINT INTERFACE (CONTRACTS §4): report, medium, printPage (rw), printPageCount, printPageHasPhoto (the current
// page draws a photograph: a still or the coach's photo — the PDF writer stores that page as JPEG), appendixBody
// (a live PpSessionDiagnosticsBody-shaped object from the print host: when non-null the technical appendix,
// PpDiagPrint fed exactly as diag_print_pdf.qml's feed() does, follows from a fresh page with the page count and
// folio continued; null — fixtures, qml_ui — no appendix pages).
Item {
    id: root
    property var report: null
    property var appendixBody: null
    property string medium: "print"
    property int printPage: 0
    readonly property int clientPageCount: pager.pageCount
    readonly property int appendixPageCount: appendixLoader.item ? Math.max(1, appendixLoader.item.printPageCount) : 0
    readonly property int printPageCount: clientPageCount + appendixPageCount
    readonly property bool inAppendix: printPage >= clientPageCount
    readonly property bool printPageHasPhoto: !inAppendix && photoPages.indexOf(printPage) >= 0
    // The grab resolution for the current page (PpPdfPrinter reads it per page): the technical appendix at
    // 200 dpi — it is read on screen, and half the bytes — the client pages at the printer's own (0).
    readonly property int printPageDpi: inAppendix ? 200 : 0
    width: 794; height: 1123

    // ── the map ──
    readonly property var rep: report || ({})
    readonly property string kind: rep.kind || "report"
    readonly property var brand: rep.brand || ({})
    readonly property var meta: rep.meta || ({})
    readonly property bool photosIncluded: !rep.photos || rep.photos.included !== false
    readonly property var workOns: (rep.workOns || []).slice(0, 5)                 // the work-on rule's own cap

    // ── geometry, from the pager (Theme.typesetPrint: the same for every theme) ──
    readonly property real mx: pager.roles.p.marginX
    readonly property real tw: pager.textW
    readonly property real pageH: pager.pageH

    // ── cross-references and photo pages: recomputed ONCE per pagination, imperatively ──
    // Not bindings that walk the tree: a binding that reads every item's `children` subscribes to all of them, so
    // while a 60-swing galley is being built (thousands of delegates arriving) every page reference re-walked the
    // whole tree on every arrival — quadratic, and it never finished. One walk after each pagination instead; the
    // page references bind only to the two results.
    property var sectionPages: ({})                 // objectName of a section's first atom → its 1-based page
    property var photoPages: []                     // 0-based client pages that draw a photograph
    function _refresh() {
        const sec = {}, photos = []
        const walk = (item) => {
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) {
                const c = kids[i]
                if (!c || !c.visible) continue
                const n = String(c.objectName || "")
                if (n.indexOf("report") === 0 && c.breakBefore === true && c.height > 0) sec[n] = pager.pageOf(c)
                if (c.isPhoto === true) { const p = pager.pageOf(c) - 1; if (p >= 0 && photos.indexOf(p) < 0) photos.push(p) }
                else walk(c)
            }
        }
        walk(pager)
        sectionPages = sec
        photoPages = (root.photosIncluded || !!root.brand.coachPhoto) ? photos : []
    }
    Connections { target: pager; function onPagesChanged() { root._refresh() } }
    function sectionPage(name) { return root.sectionPages[name] || 0 }
    function woPage(rank) { return sectionPage("reportWorkOn" + rank) }

    // ── what the test reads: each client page's extent and what it holds ──
    function pageInfo() {
        const out = []
        for (let i = 0; i < pager.pages.length; ++i) {
            const p = pager.pages[i], nx = pager.pages[i + 1]
            out.push({ from: p.from, to: p.to, used: p.to - p.from, room: pager.pageH - (p.repH || 0), section: p.section,
                       cont: p.cont, sectionEnds: !nx || nx.cont === "" })
        }
        return out
    }
    readonly property alias galleyPager: pager

    // ═══════════════════════ the galley ═══════════════════════
    PpPager {
        id: pager
        anchors.fill: parent
        roles: pagerRoles
        showKeys: false
        blockGap: 0
        minTail: 0.25                                   // no section ends on a page less than a quarter full (§9.2)
        // hidden by OPACITY, never `visible`: the pager collects only visible atoms, and an item under an invisible
        // parent reads visible = false, so a repagination while hidden would cut the report down to nothing
        opacity: root.inAppendix ? 0 : 1
        printPage: root.inAppendix ? 0 : root.printPage

        PpReportCover {
            width: pager.textW
            ro: rr; rep: root.rep; pageH: pager.pageH; mastheadH: masthead.height; host: root
        }
        PpSwingGrid {
            width: pager.textW
            visible: root.kind !== "note" && !!root.rep.grid
            ro: rr; rep: root.rep; photos: root.photosIncluded; pageH: pager.pageH
        }
        PpOrderPage {
            width: pager.textW
            visible: root.kind === "report" && !!root.rep.order && root.workOns.length > 0
            ro: rr; rep: root.rep; host: root; pageH: pager.pageH
        }
        Repeater {
            model: root.kind === "report" ? root.workOns : []
            delegate: PpWorkOnPage {
                required property var modelData
                required property int index
                width: pager.textW
                ro: rr; wo: modelData; rank: index + 1; photos: root.photosIncluded; pageH: pager.pageH
            }
        }
        // the posture page (posture_chart_design.md §5.7): a client page about this session's swings, so after the
        // work-ons and before the back matter; absent when the map has no `posture`
        PpPosturePage {
            width: pager.textW
            visible: root.kind !== "note" && !!root.rep.posture
            ro: rr; po: root.rep.posture || null
        }
        // the wrist page (session_report_design.md "The Wrist page"): after the Posture page, before the back matter;
        // absent when the map has no `wrist` (no swing of the session reads a wrist DOF). Not on the cover's Inside line.
        PpWristPage {
            width: pager.textW
            visible: root.kind !== "note" && !!root.rep.wrist
            ro: rr; wr: root.rep.wrist || null
        }
        PpNumbersTable {
            width: pager.textW
            visible: root.kind !== "note" && (!!root.rep.practice || !!root.rep.numbers)
            ro: rr; practice: root.rep.practice || null; numbers: root.rep.numbers || null
        }
        PpReportAbout {
            width: pager.textW
            visible: root.kind !== "note" && !!root.rep.about
            ro: rr; rep: root.rep; photos: root.photosIncluded
        }
        PpLmAppendix {
            width: pager.textW
            visible: !!root.rep.lmAppendix
            ro: rr; lm: root.rep.lmAppendix || null
        }
    }

    // ═══════════════════════ furniture: paper over the pager's, then the report's own ═══════════════════════
    Item {
        id: furniture
        anchors.fill: parent
        visible: true
        readonly property int page: root.printPage                     // 0-based, across the whole document
        readonly property bool cover: page === 0
        readonly property var cur: root.inAppendix ? null : pager.cur
        // paper over the pager's running head and folio (and, below, its theme-coloured "continued" label)
        Rectangle { x: 0; y: 0; width: parent.width; height: pager.roles.p.top; color: rr.paper }
        Rectangle { y: root.height - pager.roles.p.bottom + 2; width: parent.width; height: pager.roles.p.bottom - 2; color: rr.paper }
        Rectangle {
            visible: !!furniture.cur && furniture.cur.cont !== ""
            x: root.mx; y: pager.roles.p.top; width: root.tw; height: pager.contH; color: rr.paper
            Text {
                objectName: "reportContinued"
                y: 2
                text: furniture.cur && furniture.cur.cont !== "" ? furniture.cur.cont + ", continued" : ""
                font: rr.fKicker; color: rr.ink3
            }
        }

        // the cover's masthead: the studio, its coach, and "Session report" with the prepared date (§8.4)
        Item {
            id: masthead
            visible: furniture.cover && !root.inAppendix
            x: root.mx; y: 30; width: root.tw
            height: Math.max(mastLeft.height, mastRight.height)
            Rectangle { x: -root.mx; y: -30; width: root.width; height: 6; color: rr.accent }
            Row {
                id: mastLeft
                spacing: 12
                width: parent.width - mastRight.width - 24
                PpReportCover.Mark { ro: rr; brand: root.brand; d: 38; anchors.verticalCenter: parent.verticalCenter }
                Column {
                    anchors.verticalCenter: parent.verticalCenter; spacing: 1
                    width: mastLeft.width - 50
                    Text { width: parent.width; text: root.brand.studio || ""; font: rr.fStudio; color: rr.ink; wrapMode: Text.WordWrap
                           maximumLineCount: 2; elide: Text.ElideRight; property bool fitted: true }
                    Text { width: parent.width; visible: text !== ""; text: root.brand.coachLine || ""; font: rr.fFurn; color: rr.ink2; wrapMode: Text.WordWrap
                           maximumLineCount: 2; elide: Text.ElideRight; property bool fitted: true }
                }
            }
            Column {
                id: mastRight
                anchors.right: parent.right; spacing: 2
                Text { anchors.right: parent.right; text: "Session report"; font: rr.fKicker; color: rr.accent }
                Text { anchors.right: parent.right; text: root.meta.mastheadDate || ""; font: rr.fFurn; color: rr.ink2 }
            }
        }

        // the running head: every page but the cover — the studio, and the golfer with the SESSION date (§8.4)
        Item {
            visible: !furniture.cover
            x: root.mx; y: 34; width: root.tw; height: 22
            Row {
                id: rhLeft
                spacing: 8; anchors.verticalCenter: parent.verticalCenter
                PpReportCover.Mark { ro: rr; brand: root.brand; d: 18; anchors.verticalCenter: parent.verticalCenter }
                Text { anchors.verticalCenter: parent.verticalCenter; width: Math.min(implicitWidth, root.tw - rhRight.implicitWidth - 46)
                       text: root.brand.studio || ""; font: rr.fFurnM; color: rr.ink; elide: Text.ElideRight; property bool fitted: true }
            }
            Text { id: rhRight; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                   text: root.meta.runningHead || ""; font: rr.fFurn; color: rr.ink2 }
            Rectangle { anchors.top: parent.bottom; anchors.topMargin: 6; width: parent.width; height: rr.hw; color: rr.hair }
        }

        // the folio: the PREPARED date, the section and the page, on every page (the cover: what is inside)
        Item {
            x: root.mx; y: root.height - 40; width: root.tw; height: 16
            readonly property string section: root.inAppendix ? "Technical appendix" : (furniture.cur ? furniture.cur.section : "")
            Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: 8; width: parent.width; height: rr.hw; color: rr.hair }
            Text {
                id: folioLeft
                width: Math.min(implicitWidth, parent.width - folioRight.implicitWidth - 24)
                text: furniture.cover ? (root.brand.contact || "") : (root.brand.footLine || "")
                font: rr.fFolio; color: rr.ink3; elide: Text.ElideRight; property bool fitted: true
            }
            Text {
                id: folioRight
                anchors.right: parent.right
                font: rr.fFolio; color: rr.ink3
                text: furniture.cover && root.kind !== "note" ? root.insideLine
                    : (root.meta.folioDate || "") + (parent.section ? "  ·  " + parent.section : "") + "   " + (furniture.page + 1) + " / " + root.printPageCount
            }
        }
    }
    // the cover folio's table of contents: fixed section names and their pages (absent sections are left out). The
    // appendices are not listed: the line shares the folio with the studio's contact, which it would push into an ellipsis
    readonly property string insideLine: {
        const parts = []
        const add = (label, name) => { const n = sectionPage(name); if (n > 1) parts.push(label + " p. " + n) }
        add("swing by swing", "reportGrid")
        add("why this order", "reportOrder")
        add("your work-ons", "reportWorkOn1")
        if (root.sectionPages.reportPractice) add("practice and numbers", "reportPractice")
        else add("your numbers", "reportNumbers")
        if (parts.length === 0) return ""
        return "Inside: " + parts.join("  ·  ")
    }

    // ═══════════════════════ the technical appendix (always appended when the host provides the body) ═══════════════════════
    Loader {
        id: appendixLoader
        anchors.fill: parent
        active: root.appendixBody !== null && root.appendixBody !== undefined
        opacity: root.inAppendix ? 1 : 0                // not `visible`: see the pager above (PpDiagPrint paginates too)
        sourceComponent: PpDiagPrint {
            width: root.width; height: root.height
            medium: "print"
            printAesthetic: "folio"                     // Folio light, as the client pages (PpTypeset.fixedAesthetic)
            body: root.appendixBody
            readout: root.appendixBody ? root.appendixBody.readout : null
            cards: root.appendixBody ? root.appendixBody.cards : []
            source: root.appendixBody ? root.appendixBody.source : null
            header: root.appendixBody ? root.appendixBody.header : null
            detail: root.appendixBody ? root.appendixBody.detail : null
            printPage: Math.max(0, root.printPage - root.clientPageCount)
        }
    }
    // the appendix carries the report's furniture too (its own folio counts only its own pages)
    Item {
        anchors.fill: parent
        visible: root.inAppendix
        z: 1
        Rectangle { x: 0; y: 0; width: parent.width; height: ts.p.top; color: rr.paper }
        Rectangle { y: root.height - ts.p.bottom + 2; width: parent.width; height: ts.p.bottom - 2; color: rr.paper }
        Item {
            x: root.mx; y: 34; width: root.tw; height: 22
            Row {
                spacing: 8; anchors.verticalCenter: parent.verticalCenter
                PpReportCover.Mark { ro: rr; brand: root.brand; d: 18; anchors.verticalCenter: parent.verticalCenter }
                Text { anchors.verticalCenter: parent.verticalCenter; text: root.brand.studio || ""; font: rr.fFurnM; color: rr.ink }
            }
            Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                   text: root.meta.runningHead || ""; font: rr.fFurn; color: rr.ink2 }
        }
        Item {
            x: root.mx; y: root.height - 40; width: root.tw; height: 16
            Rectangle { anchors.bottom: parent.top; anchors.bottomMargin: 8; width: parent.width; height: rr.hw; color: rr.hair }
            Text { width: Math.min(implicitWidth, parent.width - 220); text: root.brand.footLine || ""; font: rr.fFolio; color: rr.ink3; elide: Text.ElideRight }
            Text { anchors.right: parent.right; font: rr.fFolio; color: rr.ink3
                   text: (root.meta.folioDate || "") + "  ·  Technical appendix   " + (root.printPage + 1) + " / " + root.printPageCount }
        }
    }

    // ═══════════════════════ the report's palette and type: Folio, light (see the header) ════════════
    // Declared near the end: children are destroyed in creation order, and the blocks bind to it to the last.
    QtObject {
        id: rr
        readonly property var folio: Theme._themes.folio
        readonly property var pal: folio.light
        readonly property color paper: "#FFFFFF"
        readonly property color ink: pal.colorText
        readonly property color ink2: pal.colorText2
        readonly property color ink3: pal.colorText3
        readonly property color warn: pal.colorWarn
        // the studio's accent (one colour, §7.1), else Folio's own
        readonly property color accent: /^#[0-9a-fA-F]{6}$/.test(String(root.brand.accent || "")) ? root.brand.accent : pal.colorAccent
        readonly property color hair: Qt.alpha(ink, 0.16)
        readonly property color faint: Qt.alpha(ink, 0.07)
        readonly property color wash: Qt.alpha(accent, 0.075)                   // the target range, as a wash
        readonly property color tint: Qt.alpha(accent, 0.045)                   // a drill card's ground
        readonly property color edge: Qt.alpha(accent, 0.25)
        readonly property string serif: Theme.serifFamily
        readonly property string sans: folio.fontBody
        readonly property string figs: folio.fontData
        readonly property real hw: 0.75                                         // a hairline on paper
        // sizes in px at 96 dpi logical (the mock's, which Mark judged); lineHeight is proportional
        readonly property real em: 13                                           // the body size: widths are counted in it
        readonly property real lead: 1.32
        readonly property real smallLead: 1.3
        function f(fam, px, w, extra) {
            const o = { family: fam, pixelSize: px, weight: w === undefined ? Font.Normal : w }
            if (extra) for (const k in extra) o[k] = extra[k]
            return Qt.font(o)
        }
        readonly property font fName:    f(serif, 44)
        readonly property font fH1:      f(serif, 31)
        readonly property font fH2:      f(serif, 19)
        readonly property font fStand:   f(serif, 17)
        readonly property font fHead:    f(serif, 19)                          // the cover's headline
        readonly property font fNote:    f(serif, 14, Font.Normal, { italic: true })
        readonly property font fStudio:  f(serif, 17)
        readonly property font fNumeral: f(serif, 22)
        readonly property font fKicker:  f(sans, 11, Font.DemiBold, { letterSpacing: 0.6 })
        readonly property font fH3:      f(sans, 13, Font.DemiBold)
        readonly property font fLabel:   f(sans, 11, Font.DemiBold)
        readonly property font fBody:    f(sans, 13)
        readonly property font fBodyI:   f(sans, 13, Font.Normal, { italic: true })
        readonly property font fBodyM:   f(sans, 13, Font.DemiBold)
        readonly property font fBodyD:   f(sans, 12)                           // a dense page's body (the mock's 11.5)
        readonly property font fBodyDM:  f(sans, 12, Font.DemiBold)
        readonly property font fItem:    f(sans, 12)
        readonly property font fItemM:   f(sans, 13, Font.DemiBold)
        readonly property font fSmall:   f(sans, 11)
        readonly property font fSmallM:  f(sans, 11, Font.DemiBold)
        readonly property font fMicro:   f(sans, 10)
        readonly property font fMicroM:  f(sans, 10, Font.DemiBold)
        readonly property font fFurn:    f(sans, 10)                           // running head and folio
        readonly property font fFurnM:   f(sans, 10, Font.DemiBold)
        readonly property font fFolio:   f(sans, 9)
        readonly property font fFig:     f(figs, 11)
        readonly property font fFigS:    f(figs, 9)
        readonly property font fFigM:    f(figs, 10)
        readonly property font fFigL:    f(figs, 24)
        readonly property font fFigB:    f(figs, 11, Font.DemiBold)
        // the inline HTML a map string may carry (<i>, <b>, <br>) is StyledText; a colour inside it is this ink
        function hex(c) { return String(c) }
        // A METRIC'S OWN COLOUR, in Folio light whatever the app shows: the metric's colour NAME (the catalogue's, or
        // the user's choice — an identity, the same in every theme) read through Folio's light palette (data, not the
        // live tokens), with the user's own retune of that name for Folio light when there is one.
        function hue(key) {
            const name = Theme.metricColorName(key)
            const own = appSettings.metricPalette ? appSettings.metricPalette["folio/light/" + name] : undefined
            if (own !== undefined && own !== "") return own
            const v = Theme._metricPalettes.folio.light[name]
            return v !== undefined ? v : ink3
        }
    }

    // THE PAGER'S ROLES: the report's own page geometry, everything else a print PpTypeset's. The pager, its running
    // head and its folio read only these members of `roles`; their furniture is painted over (above), so the
    // theme-coloured ones never show. The geometry is the mock's (Mark judged it): the body from y 80 to the folio
    // rule at y 1075, where Theme.typesetPrint's (76 / 92, tuned for the diagnostics print) would lose a sixth of a
    // page to white. A continued page's label sits at the top of the body (the pager reserves contH on every page).
    QtObject {
        id: pagerRoles
        readonly property var p: Object.assign({}, ts.p, { marginX: 60, headY: 34, top: 80, bottom: 62 })
        readonly property string medium: "print"
        function u(n) { return ts.u(n) }
        readonly property font fLabel: ts.fLabel
        readonly property font fMicro: ts.fMicro
        readonly property font fSmall: ts.fSmall
        readonly property color ink: ts.ink
        readonly property color ink2: ts.ink2
        readonly property color ink3: ts.ink3
        readonly property color rule: ts.rule
        readonly property color hair: ts.hair
        readonly property real hw: ts.hw
        function sessionMeta(source, readout, longMonth) { return { golfer: "", date: "", club: "", shot: "" } }
        function shotOfText(readout) { return "" }
    }
    // the units of paper (declared last: children are destroyed in creation order)
    PpTypeset { id: ts; medium: "print"; fixedAesthetic: "folio" }
}
