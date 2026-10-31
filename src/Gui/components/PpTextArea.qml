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

// A multi-line text field in PpTextField's look: the surface, the radius, a 1 px border that is
// colorBorderStrong at rest, colorWarn on `hasError` and the accent while it has focus; body type;
// colorText3 placeholder; word wrap; 10 px in from the sides and 8 px from the top and bottom.
//
// Two ways to use it:
//   - plain: read and write `text` yourself (the report sheet's note, which is read when Make is
//     pressed).
//   - committing, `commitOnBlur: true`: `text` starts as `value` (what the setting holds). When the field
//     loses focus it emits commit(text), then goes back to showing `value`, so a value the owner
//     normalised (trimmed, say) shows as it is stored — PpTextField's commit-on-blur, for a TextArea,
//     where Return is a new line rather than "done":
//
//         PpTextArea {
//             Layout.fillWidth: true
//             commitOnBlur: true
//             value: studioProfile.booking
//             onCommit: (t) => studioProfile.booking = t
//         }
//
// Settings → Studio (the booking sentence, the practice steps, the email message), the report sheet's
// note (PpReportSheet).

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

TextArea {
    id: root

    property bool   hasError:     false
    property bool   commitOnBlur: false
    property string value:        ""
    signal commit(string text)

    text: root.value
    wrapMode: TextArea.Wrap
    leftPadding:   Theme.gap(10)
    rightPadding:  Theme.gap(10)
    topPadding:    Theme.gap(8)
    bottomPadding: Theme.gap(8)

    font.family:    Theme.fontBody
    font.pixelSize: Theme.fontSzBody
    font.weight:    Theme.fontBodyWeight
    color:          Theme.colorText

    placeholderTextColor: Theme.colorText3
    selectByMouse: true

    onActiveFocusChanged: if (!activeFocus && root.commitOnBlur) {
        root.commit(text)
        text = Qt.binding(function () { return root.value })
    }

    background: Rectangle {
        radius:       Theme.radius
        color:        Theme.colorSurface
        border.width: 1
        border.color: root.activeFocus
                          ? Theme.colorAccent
                          : (root.hasError ? Theme.colorWarn : Theme.colorBorderStrong)
    }
}
