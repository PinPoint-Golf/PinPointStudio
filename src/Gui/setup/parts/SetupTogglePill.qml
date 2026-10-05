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

// Small on/off toggle — mirrors the TogglePill in ImusPanel.qml so the
// wizard's per-IMU enable switch looks and behaves identically to Settings.
// (Ported verbatim from the retired ScreenSessionWizard.qml's inline `component TogglePill`, l.2279–2307.)
import QtQuick
import PinPointStudio

Rectangle {
    id: pill
    property bool checked: false
    signal toggled(bool value)

    width:  Theme.sp(34)
    height: Theme.sp(18)
    radius: Theme.sp(9)
    color:  pill.checked ? Theme.colorAccent : Theme.colorBg3
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    Rectangle {
        width:  Theme.sp(12)
        height: Theme.sp(12)
        radius: Theme.sp(6)
        color:  "white"
        anchors.verticalCenter: parent.verticalCenter
        x: pill.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
        Behavior on x { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape:  Qt.PointingHandCursor
        onClicked:    pill.toggled(!pill.checked)
    }
}
