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

// A setting's words: its title in body type and, under it, an optional subtitle in quiet micro type
// that wraps. It fills the width it is given, with or without a subtitle, so beside a control in a
// RowLayout it takes the room and pushes the control to the right edge:
//
//     SettingsRowTitle {
//         title:    qsTr("Keep raw sensor frames in the archive")
//         subtitle: qsTr("Off discards them when a session is archived.")   // "" = no second line
//     }
//
// Used inside SettingsRow (see there for the whole row), and on its own for rows that are not search
// targets (Archiving's session list).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

ColumnLayout {
    id: rt

    property string title
    property string subtitle

    Layout.fillWidth: true
    spacing: Theme.gap(3)

    // Both lines fill, not just the subtitle: a layout's maximum width is its children's, so with only
    // the subtitle filling, a row with no subtitle (or an empty one) would shrink the column to the
    // title's own width and leave the control sitting just after it instead of at the right edge.
    Text {
        text: rt.title; Layout.fillWidth: true
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; color: Theme.colorText
    }
    Text {
        text: rt.subtitle; visible: text.length > 0
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
        wrapMode: Text.WordWrap; Layout.fillWidth: true
    }
}
