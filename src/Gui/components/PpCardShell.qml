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

// The coaching card's background, and nothing else: the surface, a hairline, and a rule in the
// card's tone along the top edge that follows the rounded corners. The caller fills it and lays
// its own content over it.
//
// The rule is a rounded tone shape with its lower part covered by the surface again, so it bends
// with the corners where a plain bar would square them off.
//
//   standard   3 px rule, colorBorderMid hairline (colorBorderStrong when floating, so a popover
//              lifts off whatever it sits over)
//   hero       4 px rule, no hairline on the surface; a faint wash of the tone over the whole card
//              with a tone hairline, just enough to lift the one card that matters off the page
//
// The hero pieces are always there and only shown in hero, so the two draw exactly what their
// inline originals drew (HmSwingSummary's Card, HmFocus's shell).
//
// The theme sets the rule heights (Theme.cardRule / heroRule) and whether a hero is tinted
// (Theme.heroTinted). A rule of 0 draws no rule at all, the tone shape and its surface cover both
// hidden, so no sliver of tone shows at the corners. An untinted hero is a standard card: the
// surface and its hairline, no wash. Folio uses both, for a card that is only a hairline box.

import QtQuick
import PinPointStudio

Item {
    id: shell

    property color tone:     Theme.colorAccent
    property bool  hero:     false
    property bool  floating: false
    readonly property int  ruleH:  hero ? Theme.heroRule : Theme.cardRule
    readonly property bool tinted: hero && Theme.heroTinted

    Rectangle {
        anchors.fill: parent
        radius:       Theme.radiusLg
        color:        Theme.colorSurface
        border.width: shell.tinted ? 0 : 1
        border.color: shell.floating ? Theme.colorBorderStrong : Theme.colorBorderMid
    }
    Rectangle {     // the wash: the surface leaning to the tone
        visible:      shell.tinted
        anchors.fill: parent
        radius:       Theme.radiusLg
        color:        Qt.alpha(shell.tone, Theme.dark ? 0.045 : 0.05)
        border.width: 1
        border.color: Qt.alpha(shell.tone, Theme.dark ? 0.32 : 0.36)
    }
    Rectangle {
        visible: shell.ruleH > 0
        width: parent.width; height: Theme.radiusLg * 2
        radius: Theme.radiusLg
        color:  shell.tone
    }
    Rectangle {
        visible: shell.ruleH > 0
        x: 1; y: shell.ruleH
        width: parent.width - 2; height: Theme.radiusLg * 2
        color: Theme.colorSurface
        Rectangle {
            visible: shell.tinted
            anchors.fill: parent
            color: Qt.alpha(shell.tone, Theme.dark ? 0.045 : 0.05)
        }
    }
}
