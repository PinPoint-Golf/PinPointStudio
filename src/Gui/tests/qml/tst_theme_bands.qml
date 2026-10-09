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

// The shared band rules in Theme — the verdict a judged reading carries, the corridor bands, and
// the backing behind text over video. Each used to be copied into several files (the charts, the
// chart summary and its key; the corridor strip, the value run, the session history and the model
// editor's plot; the camera tiles, the camera frame and markup), and the copies had drifted. These
// cases pin the one rule each now has, so a copy cannot quietly come back with a different answer.
TestCase {
    name: "ThemeBands"

    function same(a, b) { return Qt.colorEqual(a, b) }

    // Either spelling of a band is read; anything else is NO verdict, never a pass. The chart dots
    // once fell through to green, which drew every unscored dot as a pass.
    function test_bandVerdict_readsBothSpellings() {
        compare(Theme.bandVerdict("good"), "good")
        compare(Theme.bandVerdict("green"), "good")
        compare(Theme.bandVerdict("attention"), "attention")
        compare(Theme.bandVerdict("yellow"), "attention")
        compare(Theme.bandVerdict("amber"), "attention")
        compare(Theme.bandVerdict("warn"), "warn")
        compare(Theme.bandVerdict("red"), "warn")
        compare(Theme.bandVerdict(""), "")
        compare(Theme.bandVerdict(undefined), "")
        compare(Theme.bandVerdict("ok"), "")
    }

    function test_verdictColor_noVerdictIsQuiet() {
        verify(same(Theme.verdictColor("good"), Theme.colorGood))
        verify(same(Theme.verdictColor("attention"), Theme.colorAttention))
        verify(same(Theme.verdictColor("warn"), Theme.colorWarn))
        // A dot with no verdict only marks a position: grey. Running text passes its own colour.
        verify(same(Theme.verdictColor(""), Theme.colorText3))
        verify(same(Theme.verdictColor("", Theme.colorText), Theme.colorText))
        verify(same(Theme.verdictColor("warn", Theme.colorText), Theme.colorWarn))
    }

    function test_verdictWords_neverColourAlone() {
        compare(Theme.verdictWords("good"), "in range")
        compare(Theme.verdictWords("attention"), "watch")
        compare(Theme.verdictWords("warn"), "outside")
        compare(Theme.verdictWords(""), "")
    }

    // One strength everywhere a corridor is shaded (Mark, 9 Oct 2026). The session history and the
    // value run used to be a shade fainter than the corridor strip.
    function test_corridorFill_oneStrength() {
        verify(same(Theme.corridorFill("ideal"),  Qt.alpha(Theme.colorGood, 0.20)))
        verify(same(Theme.corridorFill("good"),   Qt.alpha(Theme.colorGood, 0.09)))
        verify(same(Theme.corridorFill("watch"),  Qt.alpha(Theme.colorAttention, 0.20)))
        verify(same(Theme.corridorFill("action"), Qt.alpha(Theme.colorWarn, 0.16)))
        verify(same(Theme.corridorFill("open"), "transparent"))
    }

    // Past the fault line is the faults' coral, never the alarm red (13.2). The model editor's plot
    // used to paint Action in colorError.
    function test_corridorTone_actionIsTheFaultColour() {
        verify(same(Theme.corridorTone("ideal"), Theme.colorGood))
        verify(same(Theme.corridorTone("good"), Qt.alpha(Theme.colorGood, 0.5)))
        verify(same(Theme.corridorTone("watch"), Theme.colorAttention))
        verify(same(Theme.corridorTone("action"), Theme.colorWarn))
        verify(!same(Theme.corridorTone("action"), Theme.colorError))
    }

    function test_corridorWord() {
        compare(Theme.corridorWord("ideal"), "IDEAL")
        compare(Theme.corridorWord("good"), "GOOD")
        compare(Theme.corridorWord("watch"), "WATCH")
        compare(Theme.corridorWord("action"), "ACTION")
        compare(Theme.corridorWord("open"), "")
    }

    function test_colorScrim() {
        verify(same(Theme.colorScrim, Qt.alpha(Theme.poseInk, 0.6)))
    }
}
