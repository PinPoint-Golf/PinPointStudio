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

// SWING BY SWING — page 2 (design §3): one grid, a row per habit and a column per swing in the order hit; carry as
// bars under the swings; the reference swing at four positions; Earlier today.
//
// ANY SESSION LENGTH (§9.1 rule 4). Columns come in BANDS of at most 20 swings (60 swings: three bands, each with its
// own swing numbers and carry bars, the pitch the same in every band). Each band is a printGroup, so a band cut by a
// page repeats its swing numbers. Rows are capped per group — habits 5, seen 4, ball 4 — and the map words the rest
// ("and 2 more seen on fewer swings"). The name column is sized from the widest name (10–18 em) and wraps beyond
// it; the count column from the widest count. Earlier today shows the newest six sessions at most.
// Photos (§7.3): without approval there is no reference swing, here or anywhere.
Column {
    id: sg
    property QtObject ro: null
    property var rep: ({})
    property bool photos: true
    readonly property var g: rep.grid || ({})
    readonly property var swings: g.swings || []
    readonly property int n: swings.length
    width: 600

    // A swing's mark: fired = a filled warn dot, clean = a hollow ring, could not be measured = a short stub.
    component SwingMark: Item {
        id: sm
        property QtObject ro: null
        property string state: "clean"
        property real d: 9
        width: d; height: d
        Rectangle { anchors.centerIn: parent; visible: sm.state === "fired"; width: sm.d; height: width; radius: width / 2; color: sm.ro ? sm.ro.warn : "transparent" }
        Rectangle { anchors.centerIn: parent; visible: sm.state === "clean"; width: sm.d - 1.5; height: width; radius: width / 2; color: "transparent"
                    border.color: sm.ro ? sm.ro.ink3 : "transparent"; border.width: 1 }
        Rectangle { anchors.centerIn: parent; visible: sm.state !== "fired" && sm.state !== "clean"; width: sm.d * 0.6; height: 1.2; color: sm.ro ? sm.ro.ink3 : "transparent" }
    }

    // ── widths from the text ──
    FontMetrics { id: nameFm; font: sg.ro.fItem }
    FontMetrics { id: countFm; font: sg.ro.fFig }
    readonly property var groupCaps: ({ habit: 5, seen: 4, ball: 4 })
    readonly property var groups: (g.groups || []).map(gr => ({ key: gr.key, title: gr.title,
        rows: (g.rows || []).filter(r => r.group === gr.key).slice(0, groupCaps[gr.key] || 4),
        more: (g.more || {})[gr.key] || "" })).filter(gr => gr.rows.length > 0)
    readonly property var todayRows: ((rep.today || {}).rows || [])
    readonly property real nameW: {
        let w = 0
        for (const gr of groups) for (const r of gr.rows) w = Math.max(w, nameFm.advanceWidth(r.name || ""))
        for (const r of todayRows) w = Math.max(w, nameFm.advanceWidth(r.name || ""))
        if (g.carryLabel) w = Math.max(w, nameFm.advanceWidth(g.carryLabel))
        return Math.max(10 * ro.em, Math.min(18 * ro.em, w + ro.em))
    }
    readonly property real countW: {
        let w = nameFm.advanceWidth("swings")
        for (const gr of groups) for (const r of gr.rows) w = Math.max(w, countFm.advanceWidth(r.count || ""))
        return Math.max(3.5 * ro.em, w + 0.5 * ro.em)
    }
    readonly property int perBand: n <= 20 ? Math.max(1, n) : Math.ceil(n / Math.ceil(n / 20))
    readonly property var bands: {
        const out = []
        for (let i = 0; i < n; i += perBand) out.push({ from: i, to: Math.min(n, i + perBand) })
        return out
    }
    readonly property real pitch: (width - nameW - countW) / Math.max(1, perBand)
    function mx(i) { return nameW + (i + 0.5) * pitch }
    // rows tighten when there are many of them (the mock's rule: more than nine)
    readonly property int rowCount: groups.reduce((a, gr) => a + gr.rows.length, 0)
    readonly property real rowH: tight ? 20 : rowCount > 9 ? 22 : 25
    // TIGHT (as PpWorkOnPage): a section that would run on by less than a fifth of a page is set once more, closer —
    // a one-shot check of the laid-out height, never undone while the map stays the same.
    property real pageH: 950
    property bool tight: false
    Timer { id: fitCheck; interval: 40; onTriggered: if (!sg.tight && sg.height > sg.pageH + 1 && sg.height <= sg.pageH * 1.2) sg.tight = true }
    onHeightChanged: fitCheck.restart()
    onRepChanged: tight = false
    readonly property var carry: g.carry || []
    readonly property real cmax: carry.length ? Math.max.apply(null, carry) : 1
    readonly property real cmin: carry.length ? Math.min.apply(null, carry) - Math.max(10, 0.15 * (cmax - Math.min.apply(null, carry))) : 0

    // ═══ the section head ═══
    Column {
        objectName: "reportGrid"
        property bool atom: true
        property bool breakBefore: true
        property bool keepNext: true
        property string section: "Swing by swing"
        width: sg.width; spacing: 0
        Text { text: "Swing by swing"; font: sg.ro.fKicker; color: sg.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: sg.g.title || "Swing by swing"; font: sg.ro.fH1; color: sg.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 10; visible: !!sg.g.standfirst }
        Text { visible: !!sg.g.standfirst; width: Math.min(parent.width, 52 * sg.ro.em); text: sg.g.standfirst || ""
               font: sg.tight ? sg.ro.fH2 : sg.ro.fStand; color: sg.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.3 }
        Item { width: 1; height: sg.tight ? 10 : 18 }
    }

    // ═══ the grid, in bands of at most 20 swings ═══
    Repeater {
        model: sg.n > 0 ? sg.bands : []
        delegate: Column {
            id: band
            required property var modelData
            required property int index
            readonly property var keyKinds: []
            property string printGroup: "grid" + index
            readonly property int from: modelData.from
            readonly property int to: modelData.to
            width: sg.width

            // the band's head: its swing numbers (repeated when the band runs on)
            Item {
                property bool atom: true
                width: sg.width; height: 20 + (band.index > 0 ? 10 : 0)
                // paper behind it: the pager repeats a list's head as a texture, and a transparent one prints black
                Rectangle { anchors.fill: parent; color: sg.ro.paper }
                Repeater {
                    model: band.to - band.from
                    delegate: Text {
                        required property int index
                        readonly property var sw: sg.swings[band.from + index]
                        x: sg.mx(index) - width / 2; y: parent.height - 20
                        text: sw; font: sw === sg.g.best ? sg.ro.fFigB : sg.ro.fFigM
                        color: sw === sg.g.best ? sg.ro.accent : sg.ro.ink3
                    }
                }
                Text { x: sg.width - sg.countW; y: parent.height - 19; width: sg.countW; horizontalAlignment: Text.AlignRight
                       text: "swings"; font: sg.ro.fFolio; color: sg.ro.ink3 }
            }
            Repeater {
                model: sg.groups
                delegate: Column {
                    id: grp
                    required property var modelData
                    width: sg.width
                    Item {
                        property bool atom: true
                        property bool keepNext: true
                        width: sg.width; height: gt.height + (sg.tight ? 11 : 15)
                        Text { id: gt; y: sg.tight ? 7 : 10; width: sg.width; text: grp.modelData.title || ""; font: sg.ro.fMicroM; color: sg.ro.ink2; wrapMode: Text.WordWrap }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: sg.ro.hw; color: sg.ro.hair }
                    }
                    Repeater {
                        model: grp.modelData.rows
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            property bool atom: true
                            property bool keepPrev: index === 1     // no orphan: a group title never stands with one row
                            width: sg.width; height: Math.max(sg.rowH, rn.height + (sg.tight ? 5 : 8))
                            Text { id: rn; y: 4; width: sg.nameW - 10; text: row.modelData.name || ""; font: sg.ro.fItem; color: sg.ro.ink; wrapMode: Text.WordWrap }
                            Repeater {
                                model: band.to - band.from
                                delegate: SwingMark {
                                    required property int index
                                    ro: sg.ro
                                    x: sg.mx(index) - width / 2; y: 8
                                    state: (row.modelData.marks || [])[band.from + index] || "na"
                                    d: Math.min(9, sg.pitch * 0.62)
                                }
                            }
                            Text { x: sg.width - sg.countW; y: 4; width: sg.countW; horizontalAlignment: Text.AlignRight
                                   text: band.index === 0 ? (row.modelData.count || "") : ""; font: sg.ro.fFig; color: sg.ro.ink2 }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: sg.ro.hw; color: sg.ro.faint }
                        }
                    }
                    Item {
                        property bool atom: true
                        visible: !!grp.modelData.more && band.index === sg.bands.length - 1
                        width: sg.width; height: visible ? gm.height + 6 : 0
                        Text { id: gm; y: 4; width: sg.width; text: grp.modelData.more || ""; font: sg.ro.fMicro; color: sg.ro.ink3; wrapMode: Text.WordWrap }
                    }
                }
            }
            // carry, as bars under this band's swings
            Item {
                id: carryBar
                property bool atom: true
                visible: sg.carry.length > 0
                width: sg.width; height: visible ? 66 : 0
                Text { y: 26; width: sg.nameW - 10; text: sg.g.carryLabel || ""; font: sg.ro.fItem; color: sg.ro.ink; wrapMode: Text.WordWrap }
                Repeater {
                    model: carryBar.visible ? band.to - band.from : 0
                    delegate: Item {
                        required property int index
                        readonly property real v: sg.carry[band.from + index]
                        readonly property bool best: band.from + index + 1 === sg.g.best
                        readonly property real bw: Math.min(14, sg.pitch * 0.62)
                        readonly property real h: 46 * (v - sg.cmin) / Math.max(1, sg.cmax - sg.cmin)
                        x: sg.mx(index) - bw / 2; y: 62 - h; width: bw; height: h
                        Rectangle { anchors.fill: parent; color: parent.best ? sg.ro.accent : Qt.alpha(sg.ro.ink, 0.22) }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.top; anchors.bottomMargin: 2
                               text: Math.round(parent.v); font: sg.ro.fFigS; color: sg.ro.ink3 }
                    }
                }
                Rectangle { y: 62; width: parent.width; height: sg.ro.hw; color: sg.ro.hair }
            }
            Item { width: 1; height: 8 }
        }
    }

    // the key: every mark with its word (a warn mark is never alone)
    Item {
        objectName: "reportGridKey"
        property bool atom: true
        property bool keepPrev: true
        visible: sg.n > 0
        width: sg.width; height: visible ? keyRow.height + 4 : 0
        Row {
            id: keyRow
            spacing: 16
            Row { spacing: 5; SwingMark { ro: sg.ro; state: "fired"; d: 8; y: 3 } Text { text: "showed on this swing"; font: sg.ro.fMicro; color: sg.ro.ink3 } }
            Row { spacing: 5; SwingMark { ro: sg.ro; state: "clean"; d: 8; y: 3 } Text { text: "did not"; font: sg.ro.fMicro; color: sg.ro.ink3 } }
            Row { spacing: 5; SwingMark { ro: sg.ro; state: "na"; d: 8; y: 3 } Text { text: "could not be measured"; font: sg.ro.fMicro; color: sg.ro.ink3 } }
        }
    }
    Item {
        property bool atom: true
        visible: !!sg.g.sessionNote
        width: sg.width; height: visible ? sn.height + 6 : 0
        Text { id: sn; y: 4; width: sg.width; text: sg.g.sessionNote || ""; font: sg.ro.fSmall; color: sg.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    }

    // ═══ the reference swing (§6): four positions of the swing that came closest ═══
    Item { width: 1; height: sg.tight ? 10 : 18; visible: refSwing.visible }
    Item {
        id: refSwing
        objectName: "reportReferenceSwing"
        property bool atom: true
        readonly property var stills: (sg.g.bestStills || []).slice(0, 4)
        visible: sg.photos && stills.length > 0
        width: sg.width; height: visible ? Math.max(refText.height, refStills.height) : 0
        readonly property real leftW: 15 * sg.ro.em
        Column {
            id: refText
            width: refSwing.leftW; spacing: 8
            Rectangle { width: parent.width; height: 1.5; color: sg.ro.accent }
            Text { text: "Your reference swing"; font: sg.ro.fH3; color: sg.ro.ink }
            Text { width: parent.width; text: sg.g.bestText || ""; font: sg.ro.fItem; color: sg.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32 }
        }
        Row {
            id: refStills
            x: refSwing.leftW + 22; spacing: 8
            readonly property real w: Math.min(sg.tight ? 78 : sg.rowCount > 9 ? 90 : 104, (sg.width - refSwing.leftW - 22 - 3 * 8) / 4)
            Repeater {
                model: refSwing.visible ? refSwing.stills : []
                delegate: PpStill {
                    required property var modelData
                    required property int index
                    ro: sg.ro; w: refStills.w
                    src: modelData; caption: (sg.g.bestLabels || [])[index] || ""
                }
            }
        }
    }

    // ═══ earlier today: the same habits across the day's sessions (the newest six) ═══
    Column {
        id: today
        objectName: "reportEarlierToday"
        readonly property var keyKinds: []
        property string printGroup: "today"
        readonly property var t: sg.rep.today || ({})
        readonly property int skip: Math.max(0, (t.sessions || []).length - 6)
        readonly property var sessions: (t.sessions || []).slice(skip)
        readonly property real colW: (sg.width - sg.nameW) / Math.max(1, sessions.length)
        visible: sessions.length > 0 && sg.todayRows.length > 0
        width: sg.width
        Item {
            property bool atom: true
            width: sg.width; height: todayHead.height
            Rectangle { anchors.fill: parent; color: sg.ro.paper }
            Column {
                id: todayHead
                width: parent.width; spacing: 6
                Item { width: 1; height: sg.tight ? 6 : 14 }
                Text { text: "Earlier today"; font: sg.ro.fH3; color: sg.ro.ink }
                Rectangle { width: parent.width; height: sg.ro.hw; color: sg.ro.hair }
                Row {
                    Item { width: sg.nameW; height: 1 }
                    Repeater {
                        model: today.sessions
                        delegate: Text { required property var modelData; width: today.colW; horizontalAlignment: Text.AlignHCenter
                                         text: modelData; font: sg.ro.fMicro; color: sg.ro.ink3; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
        Repeater {
            model: today.visible ? sg.todayRows : []
            delegate: Item {
                id: trow
                required property var modelData
                required property int index
                property bool atom: true
                property bool keepPrev: index === 1             // no orphan: the heads never stand with one row
                width: sg.width; height: Math.max(sg.tight ? 19 : 22, tn.height + (sg.tight ? 4 : 6))
                Text { id: tn; y: 3; width: sg.nameW - 10; text: trow.modelData.name || ""; font: sg.ro.fItem; color: sg.ro.ink; wrapMode: Text.WordWrap }
                Repeater {
                    model: (trow.modelData.marks || []).slice(today.skip)
                    delegate: Item {
                        required property var modelData
                        required property int index
                        x: sg.nameW + index * today.colW; width: today.colW; height: 18
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter; spacing: 5; y: 3
                            SwingMark { ro: sg.ro; y: 3; d: 8
                                        state: modelData.state === "pattern" ? "fired" : (modelData.state === "none" ? "na" : "clean") }
                            Text { text: modelData.text || ""; font: sg.ro.fFigM; color: modelData.state === "pattern" ? sg.ro.ink : sg.ro.ink3 }
                        }
                    }
                }
            }
        }
        Item {
            property bool atom: true
            property bool keepPrev: true
            visible: !!today.t.note || !!today.t.more
            width: sg.width; height: visible ? tnote.height + 8 : 0
            Text { id: tnote; y: 4; width: sg.width; font: sg.ro.fSmall; color: sg.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.3
                   text: [today.t.note || "", today.t.more || ""].filter(s => !!s).join(" ") }
        }
    }
}
