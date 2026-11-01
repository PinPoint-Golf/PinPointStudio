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

// The frame of one device card in Settings (cameras, IMUs, phones): the surface, a state-coloured
// border drawn over everything, and the 54 px header row that opens with a status dot. Only the
// frame lives here; each panel keeps its own body.
//   borderColor / statusColor  the border and the dot (both fade when they change)
//   dimmed                     half opacity, for a device that is excluded or forgotten
//   headerItems: [ … ]         what follows the dot in the header (alias field, toggles, buttons)
//   headerRow                  the header itself, to anchor the body under and to sum heights from
// Anything declared inside the card is its body: anchor it to `headerRow.bottom`, and set the
// card's implicitHeight from the header and the body (the header's top margin is not counted for
// you, because the panels differ on whether they count it).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: card

    property color borderColor: Theme.colorBorderStrong
    property color statusColor: Theme.colorText3
    property bool  dimmed: false

    property alias headerItems: dcHeader.data
    readonly property alias headerRow: dcHeader

    opacity: card.dimmed ? 0.5 : 1.0
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

    clip: true

    // Background fill — drawn first, behind all content
    Rectangle {
        anchors.fill: parent
        color:        Theme.colorSurface
        radius:       Theme.radius
    }

    // Border overlay — z:100 so content never occludes it
    Rectangle {
        anchors.fill: parent
        color:        "transparent"
        border.width: 1
        border.color: card.borderColor
        radius:       Theme.radius
        z:            100
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }

    // ── Header row ───────────────────────────────────────────────────────────
    RowLayout {
        id: dcHeader
        anchors.top:     parent.top
        anchors.left:    parent.left
        anchors.right:   parent.right
        anchors.margins: Theme.gap(14)
        height:  Theme.sp(54)
        spacing: Theme.gap(10)

        // Status dot
        PpBadge {
            kind:     "dot"
            plain:    true
            size:     Theme.sp(6)
            tone:     card.statusColor
            animated: true
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
