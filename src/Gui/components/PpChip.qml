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

// A pill of tiny mono words in a tone: "↑ growing" / "↓ easing" from a trend, or any words a
// caller gives it ("NEXT", "last seen 4 Jul"). Untinted it is a grey outline, for something
// stated without weight. With no words it takes no room.

import QtQuick
import PinPointStudio

Rectangle {
    id: chip

    property int    trend:    0
    property string text:     trend > 0 ? qsTr("↑ growing") : trend < 0 ? qsTr("↓ easing") : ""
    property color  tone:     trend > 0 ? Theme.colorWarn : trend < 0 ? Theme.colorGood : Theme.colorAccent
    property bool   tinted:   true
    property real   tracking: Theme.trackingData

    visible: text !== ""
    width:  visible ? chipText.implicitWidth + Theme.sp(12) : 0
    height: Theme.sp(18)
    radius: height / 2
    color:  chip.tinted ? Qt.alpha(chip.tone, Theme.dark ? 0.12 : 0.09) : "transparent"
    border.width: 1
    border.color: chip.tinted ? Qt.alpha(chip.tone, 0.45) : Theme.colorBorderStrong

    Text {
        id: chipText
        anchors.centerIn: parent
        text: chip.text
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: chip.tracking
        color:              chip.tone
    }
}
