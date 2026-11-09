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

// A row-wide press area above a PpBoxPlot must not stop its hover: Your distances lays one over each row,
// and the box reads out its average ± spread through the plot's own HoverHandler.

import QtQuick
import QtTest
import PinPointStudio

Item {
    id: probe
    width: 400; height: 120

    PpTypeset { id: ts }

    Item {
        id: cell
        x: 100; y: 40; width: 200; height: 18
        PpBoxPlot {
            id: box
            anchors.fill: parent
            roles: ts
            namePrefix: "testBand"
            map: function (v) { return (v - 90) / 120 * width }
            stats: ({ lo: 168, q1: 168, med: 188, q3: 208, hi: 208 })
            bodyW: 10
            capW: 0
            valuesOnHover: true
            texts: ({ med: "188 ± 20 yd" })
        }
    }
    MouseArea {                         // as AthleteClubsSection's row: over everything, with a cursor
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
    }

    TestCase {
        name: "BoxPlotRowHover"
        when: windowShown

        function test_a_box_under_a_row_wide_press_area_reads_on_hover() {
            verify(!box.showingValues, "quiet until hovered")
            mouseMove(probe, 230, 49)
            tryCompare(box, "showingValues", true)
            compare(box.valueLabels.length, 1, "one label, the average and its spread")
            compare(box.valueLabels[0].t, "188 ± 20 yd")
            mouseMove(probe, 10, 100)
            tryCompare(box, "showingValues", false)
        }
    }
}
