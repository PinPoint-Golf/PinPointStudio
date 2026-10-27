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

// PpBandBar, loaded for real. The bar's whole claim is proportion, so the oracle is arithmetic:
// the pieces and their gaps fill the bar exactly, each piece's width follows its count, a zero
// draws nothing, a count that is there is never rounded away, and nothing to count is a plain
// track rather than a bar that looks like a result.
Item {
    id: probe
    width: 400; height: 100

    PpBandBar { id: bar; width: 201 }

    TestCase {
        name: "BandBar"
        when: windowShown

        function init() {
            bar.width = 201
            bar.segments = []
        }

        function sumOf(pieces) {
            let w = 0
            for (const p of pieces) w += p.width
            return w + bar.gap * Math.max(0, pieces.length - 1)
        }

        function test_its_height_is_the_token() {
            compare(bar.height, Theme.sp(Theme.bandBarH))
        }

        function test_nothing_to_count_is_a_plain_track() {
            compare(bar.total, 0)
            compare(bar.pieces.length, 0)
            bar.segments = [ { count: 0, tone: Theme.bandTone("habit") } ]
            compare(bar.pieces.length, 0)
        }

        function test_the_pieces_fill_the_bar_in_order_and_tone() {
            bar.segments = [
                { count: 3, tone: Theme.bandTone("habit") },
                { count: 2, tone: Theme.bandTone("occasional") },
                { count: 5, tone: Theme.bandTone("clean") },
                { count: 0, tone: Theme.bandTone("unclear") }
            ]
            const p = bar.pieces
            compare(p.length, 3)                       // the zero draws nothing
            compare(sumOf(p), bar.width)
            compare(p[0].x, 0)
            compare(p[1].x, p[0].width + bar.gap)
            compare(p[2].x + p[2].width, bar.width)
            compare(p[0].tone, Theme.bandTone("habit"))
            compare(p[2].tone, Theme.bandTone("clean"))
            // Widths by count: clean (5) is the widest, occasional (2) the narrowest.
            verify(p[2].width > p[0].width && p[0].width > p[1].width)
            const room = bar.width - 2 * bar.gap
            fuzzyCompare(p[2].width / room, 0.5, 0.03)
        }

        function test_a_small_count_is_never_rounded_away() {
            bar.width = 120
            bar.segments = [ { count: 1000, tone: "red" }, { count: 1, tone: "blue" } ]
            compare(bar.pieces.length, 2)
            verify(bar.pieces[1].width >= bar.minSegment)
            compare(sumOf(bar.pieces), bar.width)
        }

        function test_bad_counts_count_as_none() {
            bar.segments = [ { count: -4, tone: "red" }, { tone: "green" }, { count: 2, tone: "blue" } ]
            compare(bar.total, 2)
            compare(bar.pieces.length, 1)
            compare(bar.pieces[0].width, bar.width)
        }

        function test_a_piece_without_a_tone_is_unlit() {
            bar.segments = [ { count: 1 } ]
            compare(bar.pieces[0].tone, Theme.colorMeterUnlit)
        }
    }
}
