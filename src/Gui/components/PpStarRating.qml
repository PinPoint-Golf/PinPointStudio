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

// Reusable star rating — read-only on shot cards, interactive in the review
// panel, and the threshold picker in the filter. Tapping the star that equals
// the current value clears it (emits rated(0)); the host writes the new value
// back to the model (no state is kept here).

import QtQuick
import PinPointStudio

Row {
    id: root

    property int   value:       0
    property int   max:         5
    property bool  interactive: false
    property int   starSize:    Theme.fontSzMicro
    property color onColor:     Theme.colorAttention
    property color offColor:    Theme.colorText3

    signal rated(int newValue)

    spacing: Theme.sp(2)

    Repeater {
        model: root.max

        Text {
            text:           "★"
            font.family:    Theme.fontSymbol
            font.pixelSize: root.starSize
            color:          (index + 1) <= root.value ? root.onColor : root.offColor

            MouseArea {
                anchors.fill:    parent
                anchors.margins: -Theme.sp(2)   // forgiving hit target
                enabled:         root.interactive
                cursorShape:     root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked:       root.rated((index + 1) === root.value ? 0 : index + 1)
            }
        }
    }
}
