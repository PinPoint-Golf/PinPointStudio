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

// An athlete's initials in a small circle, in that athlete's own colour. The athlete picker's cards
// and its list rows both draw one, and one athlete must be the same colour in both.
//
// The six pastels are identity colours, not theme colours: an athlete keeps theirs in every theme.
// Which one is picked from the first character of the uuid, so it never changes for that athlete.

import QtQuick
import PinPointStudio

Rectangle {
    id: root

    property string uuid:     ""
    property string initials: ""

    readonly property var avatarColors: [
        "#A8C4E0", "#7EBFAA", "#E0C4A8", "#C4A8E0", "#A8E0C4", "#E0A8C4"
    ]
    readonly property string avatarColor:
        root.uuid.length > 0 ? avatarColors[root.uuid.charCodeAt(0) % avatarColors.length]
                             : avatarColors[0]

    width:        Theme.sp(28)
    height:       Theme.sp(28)
    radius:       Theme.sp(14)
    color:        root.avatarColor
    border.width: 1
    border.color: Qt.darker(root.avatarColor, 1.3)

    Text {
        anchors.centerIn: parent
        text:           root.initials || "?"
        font.family:    Theme.fontData
        font.pixelSize: Theme.sp(10)
        color:          Qt.darker(root.avatarColor, 2.2)
    }
}
