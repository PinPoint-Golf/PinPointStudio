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
import PinPoint

Rectangle {
    id: root

    property string label:       ""
    property bool   primary:     false
    property bool   destructive: false

    signal clicked()

    implicitWidth:  btnLabel.implicitWidth + Theme.sp(24)
    implicitHeight: Theme.sp(34)
    radius:         Theme.radius
    opacity:        root.enabled ? 1.0 : 0.4

    color:        primary ? Theme.colorAccent : (destructive ? Theme.colorWarnLight : "transparent")
    border.width: primary ? 0 : 1
    border.color: destructive ? Theme.colorWarn : Theme.colorBorderStrong

    Text {
        id: btnLabel
        anchors.centerIn: parent
        text:           root.label
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        color:          primary
                            ? (Theme.dark ? Theme.colorBg : "#FFFFFF")
                            : (destructive ? Theme.colorWarn : Theme.colorText)
    }

    MouseArea {
        anchors.fill: parent
        cursorShape:  root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled:      root.enabled
        onClicked:    root.clicked()
        onPressed:    root.opacity = 0.7
        onReleased:   root.opacity = root.enabled ? 1.0 : 0.4
    }
}
