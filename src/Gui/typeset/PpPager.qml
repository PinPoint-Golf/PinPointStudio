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

// PAGER — print: one document on A4. The blocks you put inside it are laid out ONCE as a galley at the
// text-block width (so the same size classes apply: portrait ≈ compact, landscape ≈ medium), then cut
// into pages only between ATOMS (A's galley + B's greedy packer):
//   • an atom is any item with `atom: true` (a table row, an entry, a figure, a note, a section head);
//     a library block (it has `keyKinds`) with no atoms inside is one atom; plain containers are walked.
//   • keepNext binds an atom to the next (headings travel with what follows); keepPrev binds a footnote
//     to its table; breakBefore starts a new page (a PpSectionHead with breakBefore: true).
//   • a page that continues a section repeats its heading: "<section>, continued" (a PpSectionHead, or any
//     block with a `section` property, e.g. a PpStandfirst, names it).
//   • an atom taller than a page overflows honestly (it is clipped at the foot, never split).
// Round 4 (from the report's PrintPager, now the library's):
//   1. REPEATING HEADS. A block with a non-empty `printGroup` names a list; its first atom is the list's
//      head (a table's column heads, a barcode's shot numbers, a path list's crosshead). A page that opens
//      part-way through the list draws that head again under "<section>, continued", at its TRUE height and
//      at 2× (a live ShaderEffectSource), and the page's room shrinks by it. Blocks may share one group.
//   2. NO STRANDED PAGES. A head always travels with the next atom. When a keep-chain would leave a page
//      less than `minFill` full, the chain may break at its last joint INSIDE a list (never between a head
//      and its first item). A list cut must carry at least `minWidow` atoms onto the next page, unless what
//      it carries is already a quarter page (height-aware).
// Furniture on every page: PpRunningHead (its left side `title`, else its own), PpFolio (+ the page's key: only the
// marks its atoms use).
// The register binds its contract to this: printPageCount: pager.pageCount, pager.printPage: printPage.
// Cross-references: pager.pageOf(item) → the 1-based page an item starts on (re-evaluates on repagination).
Item {
    id: pg
    property QtObject roles: PpTypeset {}
    property var brush: null
    property var readout: null
    property var source: null
    property string footer: ""
    property string title: ""                   // the running head's left side; "" keeps PpRunningHead's own
    property int printPage: 0
    property bool showKeys: true
    property real minFill: 0.62
    property int minWidow: 3                    // a list never leaves fewer than this many atoms for the next page
    property real blockGap: roles.u(18)
    default property alias content: galley.data
    readonly property int pageCount: Math.max(1, pages.length)
    property var pages: [{ from: 0, to: 0, section: "", cont: "", keys: [], rep: null, repH: 0 }]
    readonly property real textW: width - 2 * roles.p.marginX
    readonly property real pageH: height - roles.p.top - roles.p.bottom - contH
    readonly property real contH: roles.u(22)
    readonly property var cur: pages[printPage] || pages[0]

    property int _gid: 0
    function _collect(item, out, keys, grp) {
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const c = kids[i]
            if (!c || !c.visible || c.height <= 0) continue
            const k = c.keyKinds !== undefined ? c.keyKinds : keys
            let g = grp
            if (c.keyKinds !== undefined) g = (c.printGroup !== undefined && c.printGroup !== "") ? c.printGroup : ""
            if (c.atom === true) { out.push(_rec(c, k, g)); continue }
            if (c.keyKinds !== undefined) {
                const n = out.length
                _collect(c, out, k, g)
                if (out.length === n) out.push(_rec(c, k, g))
            } else _collect(c, out, keys, grp)
        }
    }
    function _rec(c, keys, grp) {
        const p = c.mapToItem(galley, 0, 0)
        return { item: c, top: p.y, bottom: p.y + c.height, keepNext: c.keepNext === true, keepPrev: c.keepPrev === true,
                 breakBefore: c.breakBefore === true, section: c.section || "", keys: keys || [], group: grp || "", head: false }
    }
    function paginate() {
        const atoms = []
        _collect(galley, atoms, [], "")
        atoms.sort((a, b) => a.top - b.top || a.bottom - b.bottom)
        // the first atom of every named list is its head, and a head always keeps with what follows
        const headOf = {}
        atoms.forEach((a, i) => { if (a.group && headOf[a.group] === undefined) { headOf[a.group] = i; a.head = true; a.keepNext = true } })
        const H = pageH, out = []
        let i0 = 0, section = ""
        while (i0 < atoms.length) {
            const from = atoms[i0].top
            const first = atoms[i0]
            const cont = (out.length > 0 && !first.section && !first.breakBefore) ? section : ""
            const hi = first.group ? headOf[first.group] : undefined
            const rep = (hi !== undefined && hi < i0 && !first.head) ? atoms[hi] : null
            const repH = rep ? rep.bottom - rep.top + roles.u(8) : 0
            const room = H - repH
            let best = -1, loose = -1, maxB = 0
            for (let j = i0; j < atoms.length; ++j) {
                const a = atoms[j]
                if (j > i0 && a.breakBefore) break
                if (a.bottom - from > room) break
                maxB = Math.max(maxB, a.bottom)
                const nxt = atoms[j + 1]
                const clean = !nxt || nxt.top >= maxB - 0.5
                if (!clean) continue
                if (!a.keepNext && !(nxt && nxt.keepPrev)) best = j
                // a looser joint: only INSIDE a list (same printGroup), never between a head and its first item
                else if (!a.head && a.group && nxt && nxt.group === a.group && !nxt.keepPrev && j > i0) loose = j
            }
            if (best >= 0 && loose > best && (atoms[best].bottom - from) < pg.minFill * room) best = loose
            if (best < 0) best = loose
            // widows: a list cut here must carry at least minWidow atoms onto the next page
            if (best >= i0 && atoms[best].group && atoms[best + 1] && atoms[best + 1].group === atoms[best].group) {
                // …unless what it carries is already a real share of a page (a band of an index, say)
                const grp = atoms[best].group
                let left = 0, last = best
                for (let j = best + 1; j < atoms.length && atoms[j].group === grp; ++j) { left++; last = j }
                let leftH = atoms[last].bottom - atoms[best + 1].top
                const hiG = headOf[grp]
                while (left < pg.minWidow && leftH < 0.25 * H && best - 1 > Math.max(i0, hiG)) {
                    best--; left++; leftH = atoms[last].bottom - atoms[best + 1].top
                }
            }
            if (best < 0) {
                best = i0; maxB = atoms[i0].bottom
                while (best + 1 < atoms.length && atoms[best + 1].top < maxB - 0.5) { best++; maxB = Math.max(maxB, atoms[best].bottom) }
            }
            let to = 0, keys = []
            for (let j = i0; j <= best; ++j) {
                to = Math.max(to, atoms[j].bottom)
                if (atoms[j].section) section = atoms[j].section
                ;(atoms[j].keys || []).forEach(k => { if (keys.indexOf(k) < 0) keys.push(k) })
            }
            if (rep) (rep.keys || []).forEach(k => { if (keys.indexOf(k) < 0) keys.push(k) })
            out.push({ from: from, to: to, section: section, cont: cont, keys: keys, rep: rep ? rep.item : null, repH: repH, repSrcH: rep ? rep.bottom - rep.top : 0 })
            i0 = best + 1
        }
        pages = out.length ? out : [{ from: 0, to: 0, section: "", cont: "", keys: [], rep: null, repH: 0 }]
    }
    function pageOf(item) {
        if (!item) return 0
        const y = item.mapToItem(galley, 0, 0).y
        let n = 0
        for (let p = 0; p < pages.length; ++p) if (pages[p].from <= y + 1) n = p + 1
        return n
    }
    function repaginate() { timer.restart() }
    Timer { id: timer; interval: 120; onTriggered: pg.paginate() }
    onWidthChanged: timer.restart()
    onHeightChanged: timer.restart()

    Rectangle { anchors.fill: parent; color: "#FFFFFF" }
    PpRunningHead {
        id: runningHead
        x: pg.roles.p.marginX; y: pg.roles.p.headY; width: pg.textW
        roles: pg.roles; source: pg.source; readout: pg.readout
    }
    Binding { target: runningHead; property: "title"; value: pg.title; when: pg.title !== "" }
    Text {
        id: contLabel
        visible: pg.cur.cont !== ""
        x: pg.roles.p.marginX; y: pg.roles.p.top
        text: pg.cur.cont + ", continued"; font: pg.roles.fLabel; color: pg.roles.ink3
    }
    // the repeated head of a list that runs on from the previous page
    ShaderEffectSource {
        id: repHead
        visible: !!pg.cur.rep
        x: pg.roles.p.marginX; y: pg.roles.p.top + (pg.cur.cont !== "" ? pg.contH : 0)
        width: pg.cur.rep ? pg.cur.rep.width : 0; height: pg.cur.repSrcH || 0
        textureSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
        sourceItem: pg.cur.rep || null
        hideSource: false; live: true
    }
    Item {
        id: win
        x: pg.roles.p.marginX; y: pg.roles.p.top + (pg.cur.cont !== "" ? pg.contH : 0) + (pg.cur.repH || 0)
        width: pg.textW
        height: Math.min(pg.pageH + pg.contH + 2, pg.cur.to - pg.cur.from + 2)
        clip: true
        // a scissor clip does not hold curve-rendered Shapes (Folio's round marks) — off-page marks leaked into
        // the running head. Rendering the page window as a layer clips everything, at print resolution.
        layer.enabled: true
        layer.textureSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
        layer.smooth: true
        Rectangle { anchors.fill: parent; color: "#FFFFFF" }      // the layer is opaque paper
        Column {
            id: galley
            y: -pg.cur.from
            width: pg.textW
            spacing: pg.blockGap
            onHeightChanged: timer.restart()
        }
    }
    PpFolio {
        x: pg.roles.p.marginX; width: pg.textW
        y: pg.height - pg.roles.u(14) - height
        roles: pg.roles; readout: pg.readout
        footer: pg.footer; section: pg.cur.section; page: pg.printPage + 1; pageCount: pg.pageCount
        keyKinds: pg.showKeys ? pg.cur.keys : []
    }
}
