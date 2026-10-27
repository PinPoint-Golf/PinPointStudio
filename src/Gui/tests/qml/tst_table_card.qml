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
import QtTest
import PinPointStudio

// PpCard's table mode (the §2.5 table card), loaded for real, and the coaching card it must leave
// alone.
//
// The table card's job is ONE set of column edges: the captions and every row's cells are laid on
// the same resolved template, so a figure sits under its caption whatever the card's width. The
// oracle is the template's own arithmetic — fixed columns keep their width, fill columns share the
// rest by weight, columnGap between — not a recorded render. The coaching card's oracle is its
// own layout before table mode existed: HmSwingSummary and HmGoesTogether still draw it.
Item {
    id: probe
    width: 900; height: 700

    Column {
        width: probe.width
        spacing: 20

        PpCard {
            id: table
            width: 720
            table: true
            columns: [
                { title: "Area",     width: 120 },
                { title: "Habits",   fill: true },
                { title: "Work-ons", width: 90, align: "right" }
            ]
            PpTableRow {
                id: r0
                Text { id: r0c0; text: "Setup" }
                Text { id: r0c1; text: "bar" }
                Text { id: r0c2; text: "1 active" }
            }
            PpTableRow { id: r1; secondLine: true; Text { text: "Posture" } }
            PpTableRow { id: r2; Text { text: "Release" } }
        }

        PpCard {
            id: bare     // a table card with no captions and no title: rows start at the top
            width: 400
            table: true
            PpTableRow { id: b0; Text { text: "one" } }
            PpTableRow { id: b1; Text { text: "two" } }
        }

        PpCard {
            id: weighted
            width: 500
            table: true
            columns: [ { fill: 1 }, { fill: 3 } ]
        }

        PpCard {
            id: coaching   // the default: unchanged
            width: 400
            title: "WHAT YOU DO WELL"
            Rectangle { id: coachingChild; width: 50; height: 30 }
            Rectangle { width: 50; height: 20 }
        }
    }

    TestCase {
        name: "TableCard"
        when: windowShown

        function init() { table.width = 720 }

        function test_template_is_resolved_on_the_content_box() {
            const pad = Theme.sp(Theme.rowPadH)
            compare(table.rowPad, pad)
            compare(table.contentWidth, 720 - 2 * pad)
            const L = table.columnLayout
            compare(L.length, 3)
            compare(L[0].x, 0);                compare(L[0].width, 120)
            compare(L[2].width, 90);           compare(L[2].align, "right")
            // The fill column takes what the fixed ones and the two gaps leave.
            compare(L[1].x, 120 + table.columnGap)
            compare(L[1].width, table.contentWidth - 120 - 90 - 2 * table.columnGap)
            // The right column ends on the content box's right edge.
            compare(L[2].x + L[2].width, table.contentWidth)
            compare(L[1].title, "Habits")
        }

        function test_template_follows_the_card_width() {
            const before = table.columnLayout[1].width
            table.width = 820
            compare(table.columnLayout[1].width, before + 100)
            compare(table.columnLayout[2].x + table.columnLayout[2].width, table.contentWidth)
        }

        function test_fill_weights_share_the_rest() {
            const L = weighted.columnLayout
            const spare = weighted.contentWidth - weighted.columnGap
            compare(L[0].width, Math.floor(spare / 4))
            compare(L[1].width, Math.floor(spare * 3 / 4))
        }

        function test_rows_sit_under_one_caption_band_with_no_gaps() {
            compare(table.headerHeight, Theme.sp(Theme.tableHeaderH))
            compare(r0.mapToItem(table, 0, 0).y, table.headerHeight)
            compare(r1.y, r0.y + r0.height)
            compare(r2.y, r1.y + r1.height)
            compare(table.implicitHeight, table.headerHeight + r0.height + r1.height + r2.height)
            // Full bleed: a row is the card's whole width.
            compare(r0.width, table.width)
            compare(table.innerWidth, table.width)
        }

        function test_rows_take_the_row_heights() {
            compare(r0.height, Theme.sp(Theme.rowH))
            compare(r1.height, Theme.sp(Theme.rowH2))
        }

        function test_cells_land_on_their_columns() {
            const L = table.columnLayout
            compare(r0.cellBox.x, table.rowPad)
            compare(r0c0.x, L[0].x)
            compare(r0c1.x, L[1].x)
            // Right-aligned: the cell's right edge is the column's.
            // (within half a pixel: the cell's left edge is snapped to a whole pixel, its text width is not)
            fuzzyCompare(r0c2.x + r0c2.width, L[2].x + L[2].width, 0.5)
            // Centred vertically.
            compare(r0c0.y, Math.round((r0.height - r0c0.height) / 2))
        }

        function test_rows_find_their_card() {
            compare(r0.card, table)
            compare(r0.layout, table.columnLayout)
        }

        function test_hover_rounds_only_the_card_corners() {
            // Under captions, the first row's top is square; the last row's bottom is round.
            verify(!r0.roundTop)
            verify(!r0.roundBottom)
            verify(!r1.roundBottom)
            verify(r2.roundBottom)
            // With no header the first row meets the card's top corners.
            compare(bare.headerHeight, 0)
            verify(b0.roundTop)
            verify(!b1.roundTop)
            verify(b1.roundBottom)
        }

        function test_no_captions_no_header() {
            verify(!bare.hasCaptions)
            compare(b0.y, 0)
            compare(bare.implicitHeight, b0.height + b1.height)
            compare(bare.columnLayout.length, 0)
        }

        function test_coaching_card_is_unchanged() {
            verify(!coaching.table)
            compare(coaching.columnLayout.length, 0)
            compare(coaching.innerWidth, coaching.width - 2 * coaching.pad)
            const title = coaching.titleItem
            compare(title.x, coaching.pad)
            compare(title.y, coaching.pad + Theme.sp(2))
            const top = title.y + title.implicitHeight + coaching.bodyGap
            compare(coachingChild.mapToItem(coaching, 0, 0).y, top)
            compare(coachingChild.mapToItem(coaching, 0, 0).x, coaching.pad)
            compare(coaching.implicitHeight, top + 30 + coaching.itemGap + 20 + coaching.pad)
        }
    }
}
