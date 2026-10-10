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

// The WORKING WELL section of the Wrist motion panel — positive findings the golfer should protect
// while fixing faults, as check rows (FindingCard) like the home screen's WHAT YOU DO WELL. A
// section, not a card: a Micro heading in colorGood, an italic line on why, hairlines between rows.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Column {
    id: root

    property var strengths: []
    signal seek(real us)

    spacing: 0

    PpMicro {
        bottomPadding: Theme.gap(4)
        text:  Theme.caps(qsTr("Working well"))
        color: Theme.colorGood
    }
    Text {
        visible: root.strengths.length > 0
        width:   parent.width
        bottomPadding: Theme.gap(4)
        text: qsTr("Keep these while you work on the findings — they're correct, and easy to lose by accident when changing something else.")
        wrapMode: Text.WordWrap
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
        font.weight: Theme.fontBodyWeight; font.italic: true
        color: Theme.colorText3
    }
    PpCardNote {
        visible: root.strengths.length === 0
        width:   parent.width
        topPadding: Theme.gap(6)
        text: qsTr("Nothing in this swing stands out as one to keep yet.")
    }

    Repeater {
        model: root.strengths
        delegate: FindingCard {
            required property var modelData
            width:   root.width
            finding: modelData
            onSeek: (u) => root.seek(u)
        }
    }
}
