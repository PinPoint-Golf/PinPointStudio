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

// How firmly something holds, as a signal-strength mark: 3 / 2 / 1 bars for firm / probably /
// possibly, the word under it. The bar count carries it, so colour is never the only channel.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Column {
    id: strength

    property string tier: "firm"
    property color  tone: Theme.gradientCool
    readonly property int bars: tier === "firm" ? 3 : tier === "probably" ? 2 : 1

    spacing: Theme.sp(6)
    Row {
        spacing: Theme.sp(3)
        Repeater {
            model: 3
            Item {
                id: slot
                required property int index
                width: Theme.sp(5); height: Theme.sp(18)
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: Theme.sp(8) + slot.index * Theme.sp(5)
                    radius: Theme.sp(1.5)
                    color: slot.index < strength.bars ? strength.tone : Theme.colorBorderStrong
                }
            }
        }
    }
    PpMicro {
        text: strength.tier === "firm" ? qsTr("CLEAR")
            : strength.tier === "probably" ? qsTr("LIKELY") : qsTr("POSSIBLE")
        color: Theme.colorText2
        font.letterSpacing: Theme.trackingData
    }
}
