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

// WHY THIS ORDER — page 3 (design §4.2): the ordering rules as applied, each with what it did TODAY (generated
// from the work-ons' `reasons` in C++; nothing on this page is written here), the bars of the quantity that set the
// order, a swing timeline placing each work-on where it happens, then up to three short columns — Linked, not
// proven · Not on the list, and why · Worth checking next time — each drawn only when it has something in it
// (rule 6), the survivors sharing the width.
// Atoms: the head; each rule; each bar; the timeline with its caption; the columns as one block (they are capped:
// worth checking 5 per kind). The section may run on to a second page and says so.
Column {
    id: op
    property QtObject ro: null
    property var rep: ({})
    property var host: null
    readonly property var ord: rep.order || ({})
    readonly property var items: (ord.items || []).slice(0, 5)
    width: 600
    // TIGHT (as PpWorkOnPage): a section that would run on by less than a fifth of a page is set once more, closer.
    property bool tight: false
    Timer { id: fitCheck; interval: 40; onTriggered: if (!op.tight && op.height > op.pageH + 1 && op.height <= op.pageH * 1.2) op.tight = true }
    onHeightChanged: fitCheck.restart()
    onRepChanged: tight = false
    function gap(n) { return tight ? Math.round(n * 0.55) : n }

    Column {
        objectName: "reportOrder"
        property bool atom: true
        property bool breakBefore: true
        property bool keepNext: true
        property string section: "Why this order"
        width: op.width
        Text { text: "Why this order"; font: op.ro.fKicker; color: op.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: op.ord.title || "One thing first, and why it is that one"; font: op.ro.fH1; color: op.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 8; visible: !!op.ord.intro }
        Text { visible: !!op.ord.intro; width: parent.width; text: op.ord.intro || ""; font: op.ro.fStand; color: op.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.3 }
        Item { width: 1; height: op.gap(16) }
    }

    // the rules, numbered, each with what it did today
    Repeater {
        model: op.ord.rules || []
        delegate: Item {
            id: rule
            required property var modelData
            required property int index
            property bool atom: true
            width: op.width; height: rcol.height + op.gap(14)
            Text { text: rule.index + 1; font: op.ro.fNumeral; color: op.ro.accent; width: 34 }
            Column {
                id: rcol
                x: 40; width: op.width - 40; spacing: 4
                Text { width: parent.width; text: rule.modelData.title || ""; font: op.ro.fH3; color: op.ro.ink; wrapMode: Text.WordWrap }
                Text { width: parent.width; text: rule.modelData.body || ""; font: op.tight ? op.ro.fSmall : op.ro.fItem; color: op.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32 }
                Item {
                    visible: !!rule.modelData.today
                    width: parent.width; height: visible ? Math.max(tl.height, tt.height) + 4 : 0
                    Text { id: tl; y: 5; text: "Today"; font: op.ro.fSmallM; color: op.ro.accent }
                    Text { id: tt; y: 4; x: tl.width + 8; width: parent.width - x; text: rule.modelData.today || ""; font: op.tight ? op.ro.fSmall : op.ro.fItem; color: op.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.32 }
                }
            }
        }
    }

    // the bars: the quantity that set today's order
    Item {
        property bool atom: true
        property bool keepNext: true
        visible: op.items.length > 0
        width: op.width; height: visible ? 14 : 0
        Rectangle { y: 2; width: parent.width; height: op.ro.hw; color: op.ro.hair }
    }
    FontMetrics { id: goalFm; font: op.ro.fItemM }
    readonly property real goalW: {
        let w = 0
        for (const it of items) w = Math.max(w, goalFm.advanceWidth(it.goal || ""))
        return Math.max(18 * ro.em, Math.min(width * 0.55, w + 2 * ro.em))
    }
    Repeater {
        model: op.items
        delegate: Item {
            id: bar
            required property var modelData
            property bool atom: true
            readonly property bool first: modelData.rank === 1
            width: op.width; height: bcol.height + op.gap(10)
            Text { text: bar.modelData.rank; font.family: op.ro.serif; font.pixelSize: 16; color: bar.first ? op.ro.accent : op.ro.ink3; width: 22; y: 1 }
            Column {
                id: bcol
                x: 26; width: op.goalW - 26
                Text { width: parent.width; text: bar.modelData.goal || ""; font: bar.first ? op.ro.fItemM : op.ro.fItem; color: op.ro.ink; wrapMode: Text.WordWrap }
                Text { width: parent.width; text: [bar.modelData.name, bar.modelData.phase, bar.modelData.count].filter(s => !!s).join("  ·  ")
                       font: op.ro.fFolio; color: op.ro.ink3; wrapMode: Text.WordWrap }
            }
            Rectangle {
                x: op.goalW + 10; y: 6
                width: Math.max(2, (op.width - op.goalW - 10 - 4.5 * op.ro.em) * Math.max(0, Math.min(1, bar.modelData.share || 0)))
                height: 9; color: bar.first ? op.ro.accent : Qt.alpha(op.ro.ink, 0.3)
                visible: (bar.modelData.share || 0) > 0
            }
            Text { anchors.right: parent.right; y: 3; text: op && op.host && op.host.woPage(bar.modelData.rank) > 0 ? "p. " + op.host.woPage(bar.modelData.rank) : ""
                   font: op.ro.fMicro; color: op.ro.ink3 }
        }
    }
    Item {
        property bool atom: true
        property bool keepPrev: true
        visible: !!op.ord.barsNote && op.items.length > 0
        width: op.width; height: visible ? bn.height + 4 : 0
        Text { id: bn; width: parent.width; text: op.ord.barsNote || ""; font: op.ro.fMicro; color: op.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    }

    // where each one happens in the swing (the stops are the swing's own; the markers are the map's positions)
    Item { width: 1; height: op.gap(22); visible: tlBlock.visible }
    Column {
        id: tlBlock
        objectName: "reportTimeline"
        property bool atom: true
        visible: (op.ord.timeline || []).length > 0
        width: op.width; spacing: 8
        Text { text: "Where each one happens in your swing"; font: op.ro.fH3; color: op.ro.ink }
        Item {
            id: tl
            width: op.width
            readonly property var stops: [{ k: "Address", p: "P1" }, { k: "Backswing", p: "P2–P3" }, { k: "Top", p: "P4" },
                                          { k: "Coming down", p: "P5–P6" }, { k: "Impact", p: "P7" }, { k: "Finish", p: "P8–P10" }]
            readonly property real x0: 20
            readonly property real x1: width - 20
            function sx(pos) { return x0 + pos * (x1 - x0) / (stops.length - 1) }
            // markers that would touch stack upward, nearest the line first
            readonly property var marks: {
                const ms = (op.ord.timeline || []).map(t => ({ r: t.rank, x: sx(Math.max(0, Math.min(stops.length - 1, t.pos))), lane: 0 }))
                ms.sort((a, b) => a.x - b.x || a.r - b.r)
                const lanes = []
                for (const m of ms) {
                    let l = 0
                    while (lanes[l] !== undefined && m.x - lanes[l] < 24) l++
                    lanes[l] = m.x; m.lane = l
                }
                return ms
            }
            readonly property int laneCount: marks.reduce((a, m) => Math.max(a, m.lane + 1), 1)
            readonly property real lineY: 24 * laneCount + 6
            height: lineY + 40
            Rectangle { x: tl.x0; y: tl.lineY; width: tl.x1 - tl.x0; height: 1.5; color: Qt.alpha(op.ro.ink, 0.35) }
            Repeater {
                model: tl.stops
                delegate: Item {
                    required property var modelData
                    required property int index
                    Rectangle { x: tl.sx(index) - 4; y: tl.lineY - 3; width: 8; height: 8; radius: 4; color: op.ro.paper; border.color: op.ro.ink3; border.width: 1 }
                    Text { x: Math.max(0, Math.min(tl.width - width, tl.sx(index) - width / 2)); y: tl.lineY + 10; text: modelData.k; font: op.ro.fMicro; color: op.ro.ink2 }
                    Text { x: Math.max(0, Math.min(tl.width - width, tl.sx(index) - width / 2)); y: tl.lineY + 24; text: modelData.p; font: op.ro.fFigS; color: op.ro.ink3 }
                }
            }
            Repeater {
                model: tl.marks
                delegate: Rectangle {
                    required property var modelData
                    x: modelData.x - 10; y: tl.lineY - 28 - 24 * modelData.lane; width: 20; height: 20; radius: 10
                    color: modelData.r === 1 ? op.ro.accent : op.ro.paper; border.color: modelData.r === 1 ? op.ro.accent : op.ro.ink2; border.width: 1
                    Text { anchors.centerIn: parent; text: modelData.r; font.family: op.ro.serif; font.pixelSize: 12; color: modelData.r === 1 ? op.ro.paper : op.ro.ink }
                }
            }
        }
        Text { visible: !!op.ord.timelineNote; width: op.width; text: op.ord.timelineNote || ""; font: op.ro.fSmall; color: op.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.3 }
    }

    // linked · not on the list · worth checking: only the columns with something in them, sharing the width. Side by
    // side when they fit in what this section's first page has left; otherwise each becomes a full-width block (its
    // heading, then its entries, each an atom) so the section flows on rather than leaving a page a quarter full.
    readonly property var lists: {
        const out = []
        const ln = ord.links || [], nl = ord.notListed || [], ck = (rep.checks || [])
        if (ln.length) out.push({ key: "links", title: "Linked, not proven", rule: "ink",
                                  items: ln.map(l => "<b>" + l.from + " → " + l.to + ".</b> " + (l.words || "")) })
        if (nl.length) out.push({ key: "notListed", title: "Not on the list, and why", rule: "ink",
                                  items: nl.map(x => "<b>" + x.name + ".</b> " + (x.why || "")) })
        if (ck.length) out.push({ key: "checks", title: "Worth checking next time", rule: "accent",
                                  items: ck.map(c => "<font color=\"" + ro.ink3 + "\">" + (c.title || "") + "</font><br>" + (c.body || "")) })
        return out
    }
    property real pageH: 950
    readonly property bool side: lists.length > 0 && colsGap.y + colsGap.height + measureRow.height <= pageH - 2

    component ColsRow: Row {
        id: cr
        property QtObject ro: null
        property var lists: []
        spacing: 18
        readonly property real cw: (width - (lists.length - 1) * spacing) / Math.max(1, lists.length)
        Repeater {
            model: cr.lists
            delegate: Column {
                id: oc
                required property var modelData
                objectName: "reportOrder_" + modelData.key
                width: cr.cw; spacing: 6
                Rectangle { width: parent.width; height: 1.2; color: oc.modelData.rule === "accent" ? cr.ro.accent : cr.ro.ink }
                Text { width: parent.width; text: oc.modelData.title; font: cr.ro.fH3; color: cr.ro.ink; wrapMode: Text.WordWrap }
                Repeater {
                    model: oc.modelData.items
                    delegate: Text { required property var modelData; width: cr.cw; text: modelData; textFormat: Text.StyledText
                                     font: cr.ro.fSmall; color: cr.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.35 }
                }
            }
        }
    }
    // the side-by-side block, measured off the galley (a zero-height holder the pager skips)
    Item {
        width: op.width; height: 0
        ColsRow { id: measureRow; opacity: 0; width: op.width; ro: op.ro; lists: op.lists }
    }
    Item { id: colsGap; width: 1; height: op.gap(20); visible: op.lists.length > 0 }
    Item {
        id: cols
        objectName: "reportOrderColumns"
        property bool atom: true
        visible: op.side
        width: op.width; height: visible ? shown.height : 0
        ColsRow { id: shown; width: op.width; ro: op.ro; lists: op.side ? op.lists : [] }
    }
    Repeater {
        model: op.side ? [] : op.lists
        delegate: Column {
            id: blk
            required property var modelData
            required property int index
            objectName: "reportOrder_" + modelData.key
            width: op.width
            Item {
                property bool atom: true
                property bool keepNext: true
                width: op.width; height: bt.height + 16 + (blk.index > 0 ? 14 : 0)
                Rectangle { y: blk.index > 0 ? 14 : 0; width: parent.width; height: 1.2; color: blk.modelData.rule === "accent" ? op.ro.accent : op.ro.ink }
                Text { id: bt; y: (blk.index > 0 ? 14 : 0) + 7; width: parent.width; text: blk.modelData.title; font: op.ro.fH3; color: op.ro.ink; wrapMode: Text.WordWrap }
            }
            Repeater {
                model: blk.modelData.items
                delegate: Item {
                    required property var modelData
                    property bool atom: true
                    width: op.width; height: et.height + 6
                    Text { id: et; width: op.width; text: modelData; textFormat: Text.StyledText
                           font: op.ro.fSmall; color: op.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.35 }
                }
            }
        }
    }
}
