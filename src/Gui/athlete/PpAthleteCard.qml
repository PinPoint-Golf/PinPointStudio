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
import PinPointStudio

PpSelectTile {
    id: root

    property var  athleteData: ({})
    property bool isSelected:  false

    height:   Theme.sp(116)
    // The tile's "tint" look: the accent wash composited OVER the opaque surface
    // (Qt.tint) so both ends of the ColorAnimation stay opaque and hover brightens
    // monotonically (no bright-then-dim flash — the home-tile fix). Selected vs
    // hover differ by border: accent vs accentMid.
    style:    "tint"
    selected: isSelected
    clip:     true
    // Hover-grow / press-dip; the selected card holds its grown state (like the
    // home tiles) so the active athlete stays subtly raised.
    held:     isSelected

    Column {
        anchors {
            left:    parent.left
            right:   parent.right
            top:     parent.top
            margins: Theme.gap(14)
        }
        spacing: Theme.gap(4)

        // Initials circle
        PpAvatar {
            uuid:     root.athleteData.uuid || ""
            initials: root.athleteData.initials || ""
        }

        // Name
        Text {
            width:          parent.width
            text:           athleteData.name || ""
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Font.Normal
            color:          Theme.colorText
            elide:          Text.ElideRight
        }

        // Meta: handedness · handicap · sessions
        Text {
            width:          parent.width
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
            elide:          Text.ElideRight
            text: {
                const hand = (athleteData.handedness || "R")[0] + "H"
                const hcp  = Theme.formatHandicap(athleteData.handicap)
                const sess = (athleteData.sessionCount || 0) + " sessions"
                return hand + " · " + hcp + " · " + sess
            }
        }

        // Last seen
        Text {
            width:          parent.width
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
            text: {
                const ts = athleteData.lastSessionAt || 0
                if (ts === 0) return qsTr("no sessions yet")
                const days = Math.floor((Date.now() / 1000 - ts) / 86400)
                if (days === 0) return qsTr("last seen today")
                if (days === 1) return qsTr("last seen yesterday")
                return qsTr("last %1 days ago").arg(days)
            }
        }
    }
}
