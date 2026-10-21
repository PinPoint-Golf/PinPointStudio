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

// BETWEEN NOW AND NEXT TIME — the practice steps (studio text, §7.1) and YOUR NUMBERS: every swing from the launch
// monitor, a Typical row (the median; one per club when there were several) and the Habits column (design §3 page 8).
//
// THE TABLE FLOWS (§9.1 rule 4: all swings, repeating its heads). It is a printGroup whose head is the column heads,
// so a page that opens inside it draws them again; each swing is an atom. Columns are sized from the widest value
// in the session (rule 5) and their heads wrap onto two lines rather than widen the table; the spare width is
// shared between the gaps. No launch monitor: the map has no `numbers` and only the practice steps remain (and
// with neither, the section is not drawn at all).
// `fresh`: the section starts a page (the report); false when the note carries its few swings on the cover.
Column {
    id: nt
    property QtObject ro: null
    property var practice: null
    property var numbers: null
    property bool fresh: true
    readonly property var steps: practice ? (practice.steps || []).slice(0, 6) : []
    readonly property var tab: numbers || ({})
    readonly property var cols: tab.cols || []
    readonly property var rows: tab.rows || []
    readonly property var typical: tab.typical || []
    readonly property bool habits: !!tab.habitsHead
    width: 600

    // ═══ how to practise a change ═══
    Column {
        id: practiceHead
        objectName: "reportPractice"
        property bool atom: true
        property bool breakBefore: nt.fresh
        property string section: "Practice and numbers"
        visible: nt.steps.length > 0
        width: nt.width
        Text { text: "Between now and next time"; font: nt.ro.fKicker; color: nt.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: nt.practice ? (nt.practice.title || "") : ""; font: nt.ro.fH1; color: nt.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 18 }
        Grid {
            id: stepGrid
            readonly property int n: Math.min(4, Math.max(1, nt.steps.length))
            columns: n; columnSpacing: 16; rowSpacing: 18
            readonly property real cw: (nt.width - (n - 1) * columnSpacing) / n
            Repeater {
                model: nt.steps
                delegate: Column {
                    required property var modelData
                    required property int index
                    width: stepGrid.cw; spacing: 6
                    Text { text: index + 1; font.family: nt.ro.serif; font.pixelSize: 26; color: nt.ro.accent }
                    Text { width: parent.width; text: modelData.head || ""; font: nt.ro.fH3; color: nt.ro.ink; wrapMode: Text.WordWrap }
                    Text { width: parent.width; text: modelData.body || ""; font: nt.ro.fItem; color: nt.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.32 }
                }
            }
        }
    }

    // ═══ your numbers ═══
    Item { width: 1; height: 34; visible: practiceHead.visible && numHead.visible }
    Column {
        id: numHead
        objectName: "reportNumbers"
        property bool atom: true
        property bool keepNext: true
        property bool breakBefore: nt.fresh && !practiceHead.visible
        property string section: nt.fresh && !practiceHead.visible ? "Practice and numbers" : ""
        visible: nt.cols.length > 0 && nt.rows.length > 0
        width: nt.width
        Text { text: "Your numbers"; font: nt.ro.fKicker; color: nt.ro.accent }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: "Every swing, from the launch monitor"; font.family: nt.ro.serif; font.pixelSize: 22; color: nt.ro.ink; wrapMode: Text.WordWrap }
        Item { width: 1; height: 12 }
    }

    // column widths from the widest value (figures) and the widest WORD of the head (heads wrap to two lines)
    FontMetrics { id: figFm; font: nt.ro.fFig }
    FontMetrics { id: figBoldFm; font: nt.ro.fFigB }
    FontMetrics { id: headFm; font: nt.ro.fMicroM }
    FontMetrics { id: typFm; font: nt.ro.fSmallM }
    readonly property var widths: {
        const n = cols.length + (habits ? 1 : 0)
        const w = []
        for (let c = 0; c < cols.length; ++c) {
            let m = 0
            for (const wd of String(cols[c] || "").split(" ")) m = Math.max(m, headFm.advanceWidth(wd))
            m = Math.max(m, headFm.advanceWidth(String((tab.units || [])[c] || "")))
            for (const r of rows) m = Math.max(m, figFm.advanceWidth(String((r.cells || [])[c] || "")))
            for (const t of typical) m = Math.max(m, (c === 0 ? typFm : figBoldFm).advanceWidth(String((t.cells || [])[c] || "")))
            w.push(Math.ceil(m))
        }
        if (habits) {
            let m = headFm.advanceWidth(tab.habitsHead || "")
            for (const r of rows) m = Math.max(m, figFm.advanceWidth(r.habitsText || ""))
            w.push(Math.ceil(m))
        }
        const used = w.reduce((a, b) => a + b, 0)
        const gap = n > 1 ? Math.max(6, (width - used) / (n - 1)) : 0
        return { cols: w, gap: gap, total: used + gap * (n - 1) }
    }
    function xOf(c) { let x = 0; for (let i = 0; i < c; ++i) x += widths.cols[i] + widths.gap; return x }
    // first column left (a label), the rest right-aligned to their column
    function align(c) { return c === 0 || (cols[c] === "Club") ? Text.AlignLeft : Text.AlignRight }

    Column {
        id: table
        objectName: "reportNumbersTable"
        readonly property var keyKinds: []
        property string printGroup: "numbers"
        visible: numHead.visible
        width: nt.width
        // the head: column names and units (repeated on a page that opens inside the table)
        Item {
            property bool atom: true
            width: nt.width; height: hdr.height + 7
            // paper behind it: the pager repeats this head as a texture, and a transparent one prints black
            Rectangle { anchors.fill: parent; color: nt.ro.paper }
            Item {
                id: hdr
                width: nt.width; height: childrenRect.height
                Repeater {
                    model: nt.cols.length + (nt.habits ? 1 : 0)
                    delegate: Column {
                        required property int index
                        readonly property bool habitCol: index >= nt.cols.length
                        x: nt.xOf(index); width: nt.widths.cols[index]
                        Text { width: parent.width; horizontalAlignment: parent.habitCol ? Text.AlignRight : nt.align(index)
                               text: parent.habitCol ? (nt.tab.habitsHead || "") : (nt.cols[index] || ""); font: nt.ro.fMicroM; color: nt.ro.ink; wrapMode: Text.WordWrap }
                        Text { width: parent.width; horizontalAlignment: nt.align(index); visible: text !== ""
                               text: parent.habitCol ? "" : ((nt.tab.units || [])[index] || ""); font: nt.ro.fFolio; color: nt.ro.ink3 }
                    }
                }
            }
            Rectangle { y: hdr.height + 6; width: parent.width; height: 1; color: nt.ro.ink }
        }
        Repeater {
            model: nt.rows
            delegate: Item {
                id: nr
                required property var modelData
                required property int index
                property bool atom: true
                property bool keepPrev: index === 1             // no orphan: the heads never stand with one swing
                width: nt.width; height: 21
                Rectangle { anchors.fill: parent; color: nr.index % 2 ? Qt.alpha(nt.ro.ink, 0.03) : "transparent" }
                Repeater {
                    model: nr.modelData.cells || []
                    delegate: Text {
                        required property var modelData
                        required property int index
                        x: nt.xOf(index); y: 3; width: nt.widths.cols[index] || 0
                        horizontalAlignment: nt.align(index)
                        text: modelData; font: nt.ro.fFig; color: nt.ro.ink
                    }
                }
                Text { visible: nt.habits; x: nt.xOf(nt.cols.length); y: 3; width: nt.widths.cols[nt.cols.length] || 0; horizontalAlignment: Text.AlignRight
                       text: nr.modelData.habitsText || ""; font: nt.ro.fFig; color: nt.ro.ink2 }
            }
        }
        // the Typical rows: the median of every column (per club with several clubs)
        Item {
            property bool atom: true
            property bool keepPrev: true
            visible: nt.typical.length > 0
            width: nt.width; height: visible ? 3 + nt.typical.length * 22 : 0
            Rectangle { width: parent.width; height: 1; color: nt.ro.ink }
            Repeater {
                model: nt.typical
                delegate: Item {
                    id: ty
                    required property var modelData
                    required property int index
                    y: 3 + index * 22; width: nt.width; height: 22
                    Repeater {
                        model: ty.modelData.cells || []
                        delegate: Text {
                            required property var modelData
                            required property int index
                            x: nt.xOf(index); y: 3; width: nt.widths.cols[index] || 0
                            horizontalAlignment: nt.align(index)
                            text: modelData; font: index === 0 ? nt.ro.fSmallM : nt.ro.fFigB; color: nt.ro.ink
                        }
                    }
                }
            }
        }
        Item {
            property bool atom: true
            property bool keepPrev: true
            visible: !!nt.tab.note
            width: nt.width; height: visible ? nn.height + 12 : 0
            Text { id: nn; y: 10; width: nt.width; text: nt.tab.note || ""; font: nt.ro.fMicro; color: nt.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
        }
    }
}
