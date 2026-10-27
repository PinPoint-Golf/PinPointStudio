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

// A pill of tiny mono words in a tone, or any words a caller gives it ("NEXT", "last seen 4 Jul").
// Untinted it is a grey outline, for something stated without weight. With no words it takes no
// room.
//
// Given a `trend` it is the trend tag: "↑ growing" in the fault coral, "↓ easing" in the accent,
// set in the body face, semibold, on a tinted pill with no outline. Only the trend case looks that
// way; every chip that sets its own words keeps the mono pill.

import QtQuick
import PinPointStudio

Rectangle {
    id: chip

    property int    trend:    0
    property string text:     trend > 0 ? qsTr("↑ growing") : trend < 0 ? qsTr("↓ easing") : ""
    property color  tone:     trend > 0 ? Theme.colorWarn : Theme.colorAccent   // easing (and no trend): the accent
    property bool   tinted:   true
    property real   tracking: isTrend ? Theme.trackingNormal : Theme.trackingData
    // The trend tag's look: a trend was given. A chip that only sets its own words is not one.
    readonly property bool isTrend: trend !== 0

    visible: text !== ""
    width:  visible ? chipText.implicitWidth + Theme.sp(chip.isTrend ? 14 : 12) : 0
    height: Theme.sp(chip.isTrend ? 20 : 18)
    radius: height / 2
    color:  chip.tinted ? Qt.alpha(chip.tone, chip.isTrend ? (Theme.dark ? 0.18 : 0.13)
                                                           : (Theme.dark ? 0.12 : 0.09))
                        : "transparent"
    border.width: chip.isTrend && chip.tinted ? 0 : 1
    border.color: chip.tinted ? Qt.alpha(chip.tone, 0.45) : Theme.colorBorderStrong

    Text {
        id: chipText
        objectName: "chipText"
        anchors.centerIn: parent
        text: chip.text
        font.family:        chip.isTrend ? Theme.fontBody : Theme.fontData
        font.pixelSize:     chip.isTrend ? Theme.fontSzLabel : Theme.fontSzMicro
        font.weight:        chip.isTrend ? Font.DemiBold : Font.Normal
        font.letterSpacing: chip.tracking
        color:              chip.tone
    }
}
