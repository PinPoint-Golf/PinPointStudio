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

// The chip's trend case is the trend tag — "↑ growing" in the fault coral, "↓ easing" in the accent,
// body face semibold, a tinted pill with no outline — and ONLY the trend case: a chip that sets its
// own words (NEXT, ● Live, a device tag, the work-ons' own "↓ easing") keeps the mono pill.
Item {
    id: probe
    width: 400; height: 200

    PpChip { id: growing; trend: 1 }
    PpChip { id: easing;  trend: -2 }
    PpChip { id: none }
    PpChip { id: words;   text: "NEXT"; tone: Theme.colorAccent }
    PpChip { id: ownEasing; text: qsTr("↓ easing"); tone: Theme.colorGood }   // the work-ons' form


    TestCase {
        id: tc
        name: "PpChip"
        when: windowShown

        property int was: 0
        function initTestCase() { was = Theme.themeIndex }
        function cleanupTestCase() { Theme.themeIndex = was }

        function test_trendWordsAndTones() {
            for (let t = 0; t < Theme.themeCount; ++t) {
                Theme.themeIndex = t
                const where = Theme.aesthetic + (Theme.dark ? " dark" : " light")
                compare(growing.text, "↑ growing"); compare(easing.text, "↓ easing")
                verify(Qt.colorEqual(growing.tone, Theme.colorWarn), where + ": growing is the fault coral")
                verify(Qt.colorEqual(easing.tone, Theme.colorAccent), where + ": easing is the accent")
            }
        }

        function test_noTrendNoWordsNoRoom() {
            verify(!none.visible); compare(none.width, 0); verify(!none.isTrend)
        }

        function test_trendCaseLook() {
            for (const c of [growing, easing]) {
                verify(c.isTrend)
                const l = tc.findChild(c, "chipText")
                compare(l.font.family, Theme.fontBody)
                compare(l.font.pixelSize, Theme.fontSzLabel)
                compare(l.font.weight, Font.DemiBold)
                compare(c.border.width, 0)
                verify(c.color.a > 0, "a tinted pill")
            }
        }

        // The nine non-trend users keep exactly the old pill.
        function test_wordsKeepTheMonoPill() {
            for (const c of [words, ownEasing]) {
                verify(!c.isTrend)
                const l = tc.findChild(c, "chipText")
                compare(l.font.family, Theme.fontData)
                compare(l.font.pixelSize, Theme.fontSzMicro)
                compare(l.font.letterSpacing, Theme.trackingData)
                compare(c.height, Theme.sp(18))
                compare(c.border.width, 1)
            }
            verify(Qt.colorEqual(ownEasing.tone, Theme.colorGood), "a caller's own tone wins")
        }
    }
}
