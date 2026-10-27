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

// Session pips: one per session, oldest first; `maxPips` keeps the latest N (0 = all), for marks and
// for ticks alike; size and gap are the theme's pipSize / pipGap.
Item {
    id: probe
    width: 400; height: 100

    PpPips { id: pips }

    function drawn() {
        const out = []
        for (let i = 0; i < pips.children.length; ++i)
            if (pips.children[i].objectName === "pip") out.push(pips.children[i])
        return out
    }
    function kinds() { return probe.drawn().map(p => p.kind) }

    TestCase {
        name: "PpPips"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }
        function init() { pips.marks = []; pips.ticks = []; pips.maxPips = 0 }

        function test_allByDefault() {
            pips.marks = [true, false, true, true, false, true, true, true, false, true]
            compare(probe.drawn().length, 10)
            compare(probe.kinds()[1], "unseen"); compare(probe.kinds()[0], "seen")
        }

        function test_maxPipsKeepsTheLatest() {
            pips.marks = [false, false, true, true, false, true]
            pips.maxPips = 4
            compare(probe.drawn().length, 4)
            compare(probe.kinds().join(","), "seen,seen,unseen,seen")
            pips.maxPips = 8                       // more room than sessions: every one
            compare(probe.drawn().length, 6)
            pips.maxPips = 0
            compare(probe.drawn().length, 6)
        }

        function test_ticksWinAndAreCutToo() {
            pips.marks = [true, true]
            pips.ticks = [{ state: "clean" }, { state: "fired" }, { state: "notAssessable" }, { state: "fired" }]
            pips.maxPips = 3
            verify(pips.useTicks)
            compare(probe.kinds().join(","), "fired,notAssessable,fired")
        }

        function test_sizeAndGapFromTheTokens() {
            pips.marks = [true, false, true]
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const where = Theme.aesthetic + (Theme.dark ? " dark" : " light")
                compare(pips.size, Theme.sp(Theme.pipSize), where + ": size")
                compare(pips.spacing, Theme.sp(Theme.pipGap), where + ": gap")
                compare(probe.drawn()[0].width, Theme.sp(Theme.pipSize), where + ": pip")
                tryCompare(pips, "implicitWidth", 3 * Theme.sp(Theme.pipSize) + 2 * Theme.sp(Theme.pipGap))   // the Row lays out on polish
            }
        }
    }
}
