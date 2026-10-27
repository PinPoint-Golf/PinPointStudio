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

// PpTableRow, loaded for real, outside a card: a row that is given its template, a row that has
// none (its cells stay where the caller put them — HmWorkOns' rows rely on it), and the row's own
// furniture (height, padding, hover, rule). Inside a table card it is pressed in tst_table_card.
Item {
    id: probe
    width: 600; height: 400

    readonly property var template: [
        { x: 0,   width: 100, align: "left" },
        { x: 120, width: 200, align: "center" },
        { x: 340, width: 160, align: "right" }
    ]

    Column {
        width: 540

        PpTableRow {
            id: given
            layout: probe.template
            Text { id: g0; text: "Lead wrist" }
            Text { id: g1; text: "mid" }
            Text { id: g2; text: "9/22" }
        }
        PpTableRow {
            id: offset
            layout: probe.template
            columnOffset: 1
            Text { id: o0; text: "first cell, second column" }
        }
        PpTableRow {
            id: none
            topRule: true
            Text { id: n0; x: 7; y: 3; text: "placed by hand" }
        }
        PpTableRow { id: tall; secondLine: true }
    }

    TestCase {
        name: "TableRow"
        when: windowShown

        function init() { given.highlighted = false }

        function test_the_row_heights_are_the_tokens() {
            compare(given.height, Theme.sp(Theme.rowH))
            compare(tall.height, Theme.sp(Theme.rowH2))
        }

        function test_the_padding_is_the_token() {
            compare(given.padH, Theme.sp(Theme.rowPadH))
            compare(given.cellBox.x, given.padH)
            compare(given.cellBox.width, given.width - 2 * given.padH)
        }

        function test_cells_take_their_columns_by_align() {
            compare(g0.x, 0)
            compare(g1.x, Math.round(120 + (200 - g1.width) / 2))
            fuzzyCompare(g2.x + g2.width, 500, 0.5)    // left edge pixel-snapped; text width fractional
            compare(g0.y, Math.round((given.height - g0.height) / 2))
        }

        function test_columnOffset_shifts_the_cells() {
            compare(o0.x, 120 + Math.round((200 - o0.width) / 2))
            compare(offset.columnX(0), 120)
            compare(offset.columnWidth(0), 200)
        }

        function test_a_row_without_a_template_leaves_its_cells_alone() {
            compare(none.card, null)
            compare(none.layout.length, 0)
            compare(n0.x, 7)
            compare(n0.y, 3)
        }

        function test_hover_is_held_by_highlighted() {
            verify(!given.lit)
            given.highlighted = true
            verify(given.lit)
            given.hoverEnabled = false
            verify(!given.lit)
            given.hoverEnabled = true
            // Outside a table card the wash is colorBg2.
            compare(given.hoverColor, Theme.colorBg2)
        }

        function test_a_layout_change_moves_the_cells() {
            given.layout = [ { x: 10, width: 100, align: "left" } ]
            compare(g0.x, 10)
            given.layout = probe.template
            compare(g0.x, 0)
        }
    }
}
