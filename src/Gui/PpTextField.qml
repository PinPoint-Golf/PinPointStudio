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
import QtQuick.Controls.Basic
import PinPointStudio

TextField {
    id: root

    property bool hasError: false

    implicitHeight: Theme.sp(34)
    leftPadding:    Theme.sp(10)
    rightPadding:   Theme.sp(10)
    topPadding:     0
    bottomPadding:  0
    verticalAlignment: TextInput.AlignVCenter

    font.family:    Theme.fontBody
    font.pixelSize: Theme.fontSzBody
    font.weight:    Theme.fontBodyWeight
    color:          Theme.colorText

    placeholderTextColor: Theme.colorText3

    // Commit the current text whenever this field loses keyboard focus —
    // covers the common case where the user navigates away from the settings
    // screen without pressing Enter or clicking elsewhere first.
    onActiveFocusChanged: if (!activeFocus) root.editingFinished()

    background: Rectangle {
        radius:       Theme.radius
        color:        Theme.colorSurface
        border.width: 1
        border.color: root.activeFocus
                          ? Theme.colorAccent
                          : (root.hasError ? Theme.colorWarn : Theme.colorBorderStrong)
    }
}
