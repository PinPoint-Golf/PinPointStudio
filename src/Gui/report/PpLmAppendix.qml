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

// THE LAUNCH MONITOR APPENDIX — after the client pages, before the technical appendix. Three parts, each from a fresh
// page: (a) the launch monitor panel's Session figures for each club, as the appendix's headline — the panel's own
// blocks (PpLmGraphicsBody, embedded, Folio light), fed the map's `graphics`, which is the panel's model over this
// session; (b) every shot's readings, a Ball table and a Club table, grouped by club; (c) the summary statistics per
// club: shots, average, SD, lowest, highest, range.
//
// Every sentence is the map's (rule 7); only fixed headings are set here. The tables flow (rule 4): each is a
// printGroup whose first atom is its column heads, repeated on a page that opens inside it; a club's heading row keeps
// with its first shot. Columns are sized from the widest value in the session (rule 5); when the widest values do not
// fit the text width at the table figure size, the table drops to the small figure size rather than overflow.
// No launch monitor in the session: the map has no `lmAppendix` and nothing here is drawn.
Column {
    id: la
    property QtObject ro: null
    property var lm: null
    readonly property var m: lm || ({})
    readonly property var figures: m.figures || []
    readonly property var graphics: m.graphics || ({})
    width: 600

    // ═══ (a) the figures ═══
    Column {
        objectName: "reportLaunchMonitor"
        property bool atom: true
        property bool keepNext: true
        property bool breakBefore: true
        property string section: "Launch monitor"
        visible: !!la.lm
        width: la.width
        Text { text: la.m.kicker || ""; font: la.ro.fKicker; color: la.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: la.m.title || ""; font: la.ro.fH1; color: la.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 10 }
        Text { width: parent.width; text: la.m.standfirst || ""; font: la.ro.fStand; color: la.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.2 }
        Item { width: 1; height: 20 }
    }
    Repeater {
        model: la.lm ? la.figures : []
        delegate: Column {
            id: fc
            required property var modelData
            required property int index
            width: la.width
            // the club's heading travels with its ball figure; the club figure follows (each is one atom)
            Item {
                property bool atom: true
                property bool breakBefore: fc.index > 0
                width: la.width; height: ballCol.height + 22
                Column {
                    id: ballCol
                    width: parent.width; spacing: 6
                    Text { width: parent.width; text: fc.modelData.name || ""; font: la.ro.fH2; color: la.ro.ink; wrapMode: Text.WordWrap }
                    Text { width: parent.width; text: fc.modelData.headline || ""; font: la.ro.fBody; color: la.ro.ink2; wrapMode: Text.WordWrap }
                    Item { width: 1; height: 6 }
                    PpLmGraphicsBody {
                        width: parent.width; height: embedHeight
                        medium: "print"; printAesthetic: "folio"; interactive: false; reviewing: true
                        g: la.graphics; scopeKey: fc.modelData.key; focusedShotId: -1
                        embedBlocks: ["ballHalf"]; figNo: fc.index * 2 + 1
                    }
                }
            }
            Item {
                property bool atom: true
                width: la.width; height: clubFig.height + 22
                PpLmGraphicsBody {
                    id: clubFig
                    width: parent.width; height: embedHeight
                    medium: "print"; printAesthetic: "folio"; interactive: false; reviewing: true
                    g: la.graphics; scopeKey: fc.modelData.key; focusedShotId: -1
                    embedBlocks: ["clubHalf"]; figNo: fc.index * 2 + 2
                }
            }
        }
    }

    // ═══ (b) every shot: the ball, then the club ═══
    ShotTable { tab: la.m.ball || null; kicker: "Every shot"; title: "Ball"; fresh: true; group: "lmBall" }
    ShotTable { tab: la.m.club || null; kicker: "Every shot"; title: "Club"; fresh: false; group: "lmClub"; note: la.m.tablesNote || "" }

    // ═══ (c) the summary ═══
    Column {
        id: statsHead
        objectName: "reportLaunchMonitorStats"
        property bool atom: true
        property bool keepNext: true
        property bool breakBefore: true
        property string section: "Launch monitor"
        visible: !!la.lm && !!la.m.stats
        width: la.width
        Text { text: "Summary"; font: la.ro.fKicker; color: la.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: "Averages and spread, per club"; font.family: la.ro.serif; font.pixelSize: 22; color: la.ro.ink; wrapMode: Text.WordWrap }
        Item { width: 1; height: 12 }
    }
    StatsTable { visible: statsHead.visible; groups: la.m.stats ? (la.m.stats.groups || []) : []; note: la.m.statsNote || "" }

    // ── a shot table: heads (repeated), then per club a heading row and its shots ──
    component ShotTable: Column {
        id: st
        property var tab: null
        property string kicker: ""
        property string title: ""
        property bool fresh: true
        property string group: ""
        property string note: ""
        readonly property var cols: tab ? (tab.cols || []) : []
        readonly property var groups: tab ? (tab.groups || []) : []
        readonly property bool on: !!la.lm && cols.length > 0 && groups.length > 0
        visible: on
        width: la.width

        Item { width: 1; height: st.fresh ? 0 : 30; visible: st.on && !st.fresh }
        Column {
            objectName: st.fresh ? "reportLaunchMonitorShots" : ""
            property bool atom: true
            property bool keepNext: true
            property bool breakBefore: st.fresh
            property string section: st.fresh ? "Launch monitor" : ""
            width: la.width
            Text { text: st.kicker; font: la.ro.fKicker; color: la.ro.accent }
            Item { width: 1; height: 6 }
            Text { width: parent.width; text: st.title; font.family: la.ro.serif; font.pixelSize: 22; color: la.ro.ink; wrapMode: Text.WordWrap }
            Item { width: 1; height: 12 }
        }

        FontMetrics { id: fm; font: la.ro.fFig }
        FontMetrics { id: fmS; font: la.ro.fFigS }
        FontMetrics { id: hm; font: la.ro.fMicroM }
        FontMetrics { id: hmS; font: la.ro.fFolio }
        // column 0 is the swing; the rest are the readings. Widths from the widest value and the widest WORD of the
        // head (heads wrap); `dense` when that does not fit at the table size with a 6 px gap
        function measure(figM, headM) {
            const w = []
            let m0 = headM.advanceWidth("Swing")
            for (const g of groups) for (const r of (g.rows || [])) m0 = Math.max(m0, figM.advanceWidth(String(r.swing) + (r.out ? " out" : "")))
            w.push(Math.ceil(m0))
            for (let c = 0; c < cols.length; ++c) {
                let m = headM.advanceWidth(String(cols[c].unit || ""))
                for (const wd of String(cols[c].head || "").split(" ")) m = Math.max(m, headM.advanceWidth(wd))
                for (const g of groups) for (const r of (g.rows || [])) m = Math.max(m, figM.advanceWidth(String((r.cells || [])[c] || "")))
                w.push(Math.ceil(m))
            }
            return w
        }
        readonly property var wide: measure(fm, hm)
        readonly property bool dense: wide.reduce((a, b) => a + b, 0) + 6 * cols.length > width
        readonly property var widths: {
            const w = dense ? measure(fmS, hmS) : wide
            const used = w.reduce((a, b) => a + b, 0)
            const gap = cols.length > 0 ? Math.max(4, (width - used) / cols.length) : 0
            return { cols: w, gap: gap }
        }
        function xOf(c) { let x = 0; for (let i = 0; i < c; ++i) x += widths.cols[i] + widths.gap; return x }
        readonly property font cellFont: dense ? la.ro.fFigS : la.ro.fFig
        readonly property font headFont: dense ? la.ro.fFolio : la.ro.fMicroM

        Column {
            readonly property var keyKinds: []
            property string printGroup: st.group
            width: la.width
            // the heads (repeated on a page that opens inside the table): paper behind, or the repeat prints black
            Item {
                property bool atom: true
                width: la.width; height: hdr.height + 7
                Rectangle { anchors.fill: parent; color: la.ro.paper }
                Item {
                    id: hdr
                    width: la.width; height: childrenRect.height
                    Repeater {
                        model: st.cols.length + 1
                        delegate: Column {
                            required property int index
                            x: st.xOf(index); width: st.widths.cols[index] || 0
                            Text { width: parent.width; horizontalAlignment: index === 0 ? Text.AlignLeft : Text.AlignRight; wrapMode: Text.WordWrap
                                   text: index === 0 ? "Swing" : (st.cols[index - 1].head || ""); font: st.headFont; color: la.ro.ink }
                            Text { width: parent.width; horizontalAlignment: Text.AlignRight; visible: index > 0 && text !== ""
                                   text: index > 0 ? (st.cols[index - 1].unit || "") : ""; font: la.ro.fFolio; color: la.ro.ink3 }
                        }
                    }
                }
                Rectangle { y: hdr.height + 6; width: parent.width; height: 1; color: la.ro.ink }
            }
            Repeater {
                model: st.groups
                delegate: Column {
                    id: sg
                    required property var modelData
                    width: la.width
                    Item {
                        property bool atom: true
                        property bool keepNext: true
                        width: la.width; height: 26
                        Text { y: 8; text: sg.modelData.club || ""; font: la.ro.fBodyDM; color: la.ro.ink }
                        Text { anchors.right: parent.right; y: 9; text: sg.modelData.count || ""; font: la.ro.fSmall; color: la.ro.ink3 }
                    }
                    Repeater {
                        model: sg.modelData.rows || []
                        delegate: Item {
                            id: sr
                            required property var modelData
                            required property int index
                            property bool atom: true
                            width: la.width; height: st.dense ? 17 : 19
                            Rectangle { anchors.fill: parent; color: sr.index % 2 ? Qt.alpha(la.ro.ink, 0.03) : "transparent" }
                            Text { x: 0; y: 3; width: st.widths.cols[0] || 0; text: String(sr.modelData.swing) + (sr.modelData.out ? " out" : "")
                                   font: st.cellFont; color: sr.modelData.out ? la.ro.ink3 : la.ro.ink2 }
                            Repeater {
                                model: sr.modelData.cells || []
                                delegate: Text {
                                    required property var modelData
                                    required property int index
                                    x: st.xOf(index + 1); y: 3; width: st.widths.cols[index + 1] || 0
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData; font: st.cellFont; color: sr.modelData.out ? la.ro.ink3 : la.ro.ink
                                }
                            }
                        }
                    }
                }
            }
            Item {
                property bool atom: true
                property bool keepPrev: true
                visible: st.note !== ""
                width: la.width; height: visible ? tn.height + 12 : 0
                Text { id: tn; y: 10; width: la.width; text: st.note; font: la.ro.fMicro; color: la.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            }
        }
    }

    // ── the summary table: per club, a heading row, then Ball and Club sub-heads and a row per reading ──
    component StatsTable: Column {
        id: tt
        property var groups: []
        property string note: ""
        readonly property var heads: ["Reading", "", "Shots", "Average", "SD", "Lowest", "Highest", "Range"]
        width: la.width
        FontMetrics { id: tfm; font: la.ro.fFig }
        FontMetrics { id: tlm; font: la.ro.fItem }
        FontMetrics { id: thm; font: la.ro.fMicroM }
        readonly property var widths: {
            const w = heads.map(h => Math.ceil(thm.advanceWidth(h)))
            for (const g of groups) for (const r of (g.rows || [])) {
                if (r.sub !== undefined) continue
                w[0] = Math.max(w[0], Math.ceil(tlm.advanceWidth(String(r.label || ""))))
                w[1] = Math.max(w[1], Math.ceil(thm.advanceWidth(String(r.unit || ""))))
                const cs = r.cells || []
                for (let c = 0; c < cs.length; ++c) w[c + 2] = Math.max(w[c + 2], Math.ceil(tfm.advanceWidth(String(cs[c]))))
            }
            const used = w.reduce((a, b) => a + b, 0)
            const gap = Math.max(8, (width - used) / (w.length - 1))
            return { cols: w, gap: gap }
        }
        function xOf(c) { let x = 0; for (let i = 0; i < c; ++i) x += widths.cols[i] + widths.gap; return x }
        Column {
            readonly property var keyKinds: []
            property string printGroup: "lmStats"
            width: la.width
            Item {
                property bool atom: true
                width: la.width; height: shd.height + 7
                Rectangle { anchors.fill: parent; color: la.ro.paper }
                Item {
                    id: shd
                    width: la.width; height: childrenRect.height
                    Repeater {
                        model: tt.heads
                        delegate: Text {
                            required property string modelData
                            required property int index
                            x: tt.xOf(index); width: tt.widths.cols[index]
                            horizontalAlignment: index < 2 ? Text.AlignLeft : Text.AlignRight
                            text: modelData; font: la.ro.fMicroM; color: la.ro.ink
                        }
                    }
                }
                Rectangle { y: shd.height + 6; width: parent.width; height: 1; color: la.ro.ink }
            }
            Repeater {
                model: tt.groups
                delegate: Column {
                    id: tg
                    required property var modelData
                    width: la.width
                    Item {
                        property bool atom: true
                        property bool keepNext: true
                        width: la.width; height: 28
                        Text { y: 9; text: tg.modelData.club || ""; font: la.ro.fBodyDM; color: la.ro.ink }
                        Text { anchors.right: parent.right; y: 10; text: tg.modelData.count || ""; font: la.ro.fSmall; color: la.ro.ink3 }
                    }
                    Repeater {
                        model: tg.modelData.rows || []
                        delegate: Item {
                            id: tr
                            required property var modelData
                            required property int index
                            readonly property bool isSub: tr.modelData.sub !== undefined
                            property bool atom: true
                            property bool keepNext: isSub
                            width: la.width; height: isSub ? 22 : 19
                            Rectangle { visible: !tr.isSub; anchors.fill: parent; color: tr.index % 2 ? "transparent" : Qt.alpha(la.ro.ink, 0.03) }
                            Text { visible: tr.isSub; y: 7; text: tr.modelData.sub || ""; font: la.ro.fLabel; color: la.ro.ink3 }
                            Text { visible: !tr.isSub; x: 0; y: 3; width: tt.widths.cols[0]; text: tr.modelData.label || ""; font: la.ro.fItem; color: la.ro.ink; elide: Text.ElideRight }
                            Text { visible: !tr.isSub; x: tt.xOf(1); y: 4; width: tt.widths.cols[1]; text: tr.modelData.unit || ""; font: la.ro.fMicro; color: la.ro.ink3 }
                            Repeater {
                                model: tr.isSub ? [] : (tr.modelData.cells || [])
                                delegate: Text {
                                    required property var modelData
                                    required property int index
                                    x: tt.xOf(index + 2); y: 3; width: tt.widths.cols[index + 2]
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData; font: index === 1 || index === 2 ? la.ro.fFigB : la.ro.fFig; color: la.ro.ink
                                }
                            }
                        }
                    }
                }
            }
            Item {
                property bool atom: true
                property bool keepPrev: true
                visible: tt.note !== ""
                width: la.width; height: visible ? sn.height + 12 : 0
                Text { id: sn; y: 10; width: la.width; text: tt.note; font: la.ro.fMicro; color: la.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            }
        }
    }
}
