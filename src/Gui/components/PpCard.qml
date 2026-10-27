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

// A card, in one of two looks.
//
// THE COACHING CARD (the default): the shell, a mono title in the card's tone, an optional aside on
// the title's baseline at the right, and a column of content under them, padded and spaced. The
// aside right-aligns over a column `asideWidth` wide when it is narrower than that, so a
// "SESSIONS" heading sits over the pips it heads rather than at the card's edge.
//
// THE TABLE CARD (`table: true`, the §2.5 visual language): a hairline box on a colorBg2 ground
// (Theme.tableRadius, no tone rule — PpCardShell is not used, so its defaults and Theme.cardRule
// stay the session panels'), full-bleed rows separated by colorHairline hairlines.
//   title / aside   optional; when set they take a caption-height band of their own at the top
//   columns         optional column template, [{ title, width | fill, align }]:
//                     width   pixels (the caller sp()s it, as for any width)
//                     fill    true, or a weight: shares what the fixed columns leave
//                     align   "left" (default) · "right" · "center"
//                   Any titled column draws a caption row (Theme.tableHeaderH, quiet sentence-case
//                   captions, a hairline under it).
//   children        rows (PpTableRow, PpExpandRow, or anything else), stacked with no gap; a
//                   hairline is drawn above every visible row but the first.
// The template is resolved once, here, against the row content box (the card's width less
// Theme.rowPadH each side) and published as `columnLayout` ([{ x, width, align, title }], x from the
// content box's left), which PpTableRow reads by finding this card up its parent chain. A row with a
// template of its own (an expanded area's item rows) asks `resolveColumns()` for one on the same
// width.

import QtQuick
import PinPointStudio

Item {
    id: card

    property color  tone:       Theme.colorAccent
    property string title:      ""
    property string aside:      ""
    property int    asideWidth: 0               // right-align the aside over a column this wide
    property int    pad:        Theme.gap(20)
    property int    itemGap:    Theme.gap(18)
    property int    bodyGap:    Theme.gap(18)
    default property alias content: body.data
    // The coaching card's padded width; the table card's rows are full bleed, so its whole width.
    readonly property int innerWidth: card.table ? width : width - 2 * card.pad
    // The title, for a caller to set something on its baseline.
    readonly property alias titleItem: cardTitle

    // ── The table card ───────────────────────────────────────────────────────
    property bool table:     false
    property var  columns:   []
    property int  columnGap: Theme.gap(16)
    // What a PpTableRow looks for up its parent chain.
    readonly property bool isTableCard: card.table
    // A row's horizontal padding, and the width of the content box the template is laid on.
    readonly property int rowPad:       Theme.sp(Theme.rowPadH)
    readonly property int contentWidth: Math.max(0, width - 2 * card.rowPad)
    readonly property var columnLayout: card.table ? card.resolveColumns(card.columns) : []
    // The rows' column; a row that is its first or last child rounds its hover to the corner.
    readonly property alias bodyItem: body
    readonly property int  cornerRadius: Theme.sp(Theme.tableRadius)
    readonly property bool hasCaptions: {
        if (!card.table) return false
        for (const c of card.columns || []) if ((c.title || "") !== "") return true
        return false
    }
    readonly property int titleBandH:   card.table && card.title !== "" ? Theme.sp(Theme.tableHeaderH) : 0
    readonly property int captionBandH: card.hasCaptions ? Theme.sp(Theme.tableHeaderH) : 0
    // Everything above the first row.
    readonly property int headerHeight: card.titleBandH + card.captionBandH

    // A column template resolved on this card's content width: fixed columns keep their width,
    // fill columns share what is left by weight, columnGap between each.
    function resolveColumns(cols) {
        const list = cols || []
        const n = list.length
        if (n === 0) return []
        let fixed = 0, weights = 0
        for (const c of list) {
            if (c.fill) weights += (typeof c.fill === "number" ? c.fill : 1)
            else        fixed   += Math.max(0, c.width || 0)
        }
        const spare = Math.max(0, card.contentWidth - fixed - card.columnGap * (n - 1))
        const out = []
        let x = 0
        for (const c of list) {
            const w = c.fill ? Math.floor(spare * (typeof c.fill === "number" ? c.fill : 1) / weights)
                             : Math.max(0, c.width || 0)
            out.push({ x: x, width: w, align: c.align || "left", title: c.title || "" })
            x += w + card.columnGap
        }
        return out
    }

    implicitHeight: card.table ? body.y + body.implicitHeight
                               : body.y + body.implicitHeight + card.pad

    PpCardShell {
        visible: !card.table
        anchors.fill: parent
        tone: card.tone
    }
    Rectangle {     // the table card's ground; its edge is drawn over the rows, below
        visible: card.table
        anchors.fill: parent
        radius: card.cornerRadius
        color:  Theme.colorBg2
    }

    PpMicro {
        id: cardTitle
        visible: !card.table || card.title !== ""
        x: card.table ? card.rowPad : card.pad
        y: card.table ? Math.round((card.titleBandH - implicitHeight) / 2) : card.pad + Theme.sp(2)
        text:  card.title
        color: card.tone
    }
    PpMicro {
        visible: card.aside !== ""
        anchors.baseline: cardTitle.baseline
        x: card.width - (card.table ? card.rowPad : card.pad) - Math.max(implicitWidth, card.asideWidth)
           + (card.asideWidth > implicitWidth ? card.asideWidth - implicitWidth : 0)
        text: card.aside
        font.letterSpacing: Theme.trackingData
    }

    // The caption row: quiet sentence-case captions over the template's columns.
    Item {
        id: captions
        visible: card.hasCaptions
        x: card.rowPad; y: card.titleBandH
        width: card.contentWidth; height: card.captionBandH
        Repeater {
            model: captions.visible ? card.columnLayout : []
            Text {
                required property var modelData
                x: modelData.x; width: modelData.width
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.title
                horizontalAlignment: modelData.align === "right"  ? Text.AlignRight
                                   : modelData.align === "center" ? Text.AlignHCenter
                                   :                                Text.AlignLeft
                elide:          Text.ElideRight
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText3
            }
        }
    }
    Rectangle {     // under the header (title and/or captions)
        visible: card.table && card.headerHeight > 0
        y: card.headerHeight - 1
        width: card.width; height: 1
        color: Theme.colorHairline
    }

    Column {
        id: body
        x: card.table ? 0 : card.pad
        y: card.table ? card.headerHeight : cardTitle.y + cardTitle.implicitHeight + card.bodyGap
        width: card.innerWidth
        spacing: card.table ? 0 : card.itemGap
    }

    // The table card's row hairlines: one above every visible row but the first. Drawn over the
    // rows, so a row's hover never covers them.
    Repeater {
        model: card.table ? body.children.length : 0
        Rectangle {
            required property int index
            readonly property Item row: index < body.children.length ? body.children[index] : null
            visible: row !== null && row.visible && row.height > 0 && row.y > 0
            y: body.y + (row ? row.y : 0)
            width: card.width; height: 1
            color: Theme.colorHairline
        }
    }
    Rectangle {     // the table card's edge, over the full-bleed rows
        visible: card.table
        anchors.fill: parent
        radius: card.cornerRadius
        color:  "transparent"
        border.width: 1
        border.color: Theme.colorBorder
    }
}
