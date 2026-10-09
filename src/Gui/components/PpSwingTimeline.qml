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

// Six stops through the swing, Address to Finish; where something first shows is marked and the
// track runs on from there in the tone. stopX(i) and trackY let a caller set words on the track.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Item {
    id: tl

    property int    start: 0            // 0 Address … 5 Finish
    property color  tone: Theme.gradientCool
    readonly property var stops: [qsTr("ADDRESS"), qsTr("BACK"), qsTr("TOP"),
                                  qsTr("DOWN"), qsTr("IMPACT"), qsTr("FINISH")]
    TextMetrics { id: firstLabel; text: tl.stops[0]; font.family: Theme.fontData
                  font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData }
    TextMetrics { id: lastLabel; text: tl.stops[5]; font.family: Theme.fontData
                  font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData }
    // First label's left edge on the item's left edge; last label's right edge on its right.
    readonly property real inset: Math.ceil(firstLabel.advanceWidth / 2)
    readonly property real step: (width - inset - Math.ceil(lastLabel.advanceWidth / 2)) / 5
    function stopX(i) { return inset + i * step }
    readonly property int trackY: Theme.sp(7)
    implicitHeight: trackY + Theme.sp(10) + Theme.fontSzMicro + Theme.sp(4)

    // Base track, then the tone from the start stop on.
    Rectangle {
        x: tl.stopX(0); y: tl.trackY - height / 2
        width: tl.stopX(5) - tl.stopX(0); height: 1
        color: Theme.colorBorderStrong
    }
    Rectangle {
        x: tl.stopX(tl.start); y: tl.trackY - height / 2
        width: tl.stopX(5) - tl.stopX(tl.start); height: Theme.sp(2); radius: height / 2
        color: Qt.alpha(tl.tone, 0.7)
    }
    Repeater {
        model: 6
        Item {
            id: stop
            required property int index
            readonly property bool isStart: index === tl.start
            readonly property bool after:   index > tl.start
            x: tl.stopX(index); y: tl.trackY

            Rectangle {        // halo on the start stop
                visible: stop.isStart
                width: Theme.sp(16); height: width; radius: width / 2
                x: -width / 2; y: -height / 2
                color: Qt.alpha(tl.tone, 0.22)
            }
            Rectangle {
                readonly property int d: stop.isStart ? Theme.sp(9) : Theme.sp(5)
                width: d; height: d; radius: d / 2
                x: -d / 2; y: -d / 2
                color: stop.isStart || stop.after ? tl.tone : Theme.colorSurface
                border.width: stop.isStart || stop.after ? 0 : 1
                border.color: Theme.colorBorderStrong
            }
            PpMicro {
                x: -implicitWidth / 2
                y: Theme.sp(10)
                text: tl.stops[stop.index]
                font.letterSpacing: Theme.trackingData
                color: stop.isStart ? Theme.colorText : Theme.colorText3
            }
        }
    }
}
