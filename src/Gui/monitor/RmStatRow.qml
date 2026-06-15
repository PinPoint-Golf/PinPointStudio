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

// One STATS HISTORY row: TIME / CATEGORY badge / MESSAGE. Values come
// pre-formatted from ProfilerController — pure bindings here.
Rectangle {
    id: root

    property var  statData
    property bool isAlternate: false

    width: parent ? parent.width : 0
    height: msg.implicitHeight + Theme.sp(10)
    color: isAlternate ? Theme.colorBg : Theme.colorSurface

    readonly property color catColor:
        statData.category === "GAUGE"  ? Theme.colorAccent :
        statData.category === "THREAD" ? Theme.colorGood   :
        statData.category === "MEM"    ? Theme.colorWarn    : Theme.colorText2

    // Timestamp
    Text {
        id: ts
        anchors { left: parent.left; top: parent.top
                  leftMargin: Theme.sp(10); topMargin: Theme.sp(5) }
        width: Theme.sp(52)
        text: root.statData.timestamp
        font.family: Theme.fontData
        font.pixelSize: Theme.sp(10)
        color: Theme.colorText3
    }

    // Category badge
    Rectangle {
        id: catBadge
        anchors { left: ts.right; top: parent.top
                  leftMargin: Theme.sp(4); topMargin: Theme.sp(4) }
        width: Theme.sp(52)
        height: Theme.sp(16)
        radius: Theme.sp(3)
        color: Qt.rgba(root.catColor.r, root.catColor.g, root.catColor.b, 0.12)
        border.width: 1
        border.color: Qt.rgba(root.catColor.r, root.catColor.g, root.catColor.b, 0.35)

        Text {
            anchors.centerIn: parent
            text: root.statData.category
            font.family: Theme.fontData
            font.pixelSize: Theme.sp(9)
            font.letterSpacing: Theme.trackingMicro
            color: root.catColor
        }
    }

    // Message
    Text {
        id: msg
        anchors { left: catBadge.right; right: parent.right; top: parent.top
                  leftMargin: Theme.sp(8); rightMargin: Theme.sp(10); topMargin: Theme.sp(5) }
        text: root.statData.message
        wrapMode: Text.WordWrap
        font.family: Theme.fontData
        font.pixelSize: Theme.fontSzDataSm
        color: Theme.colorText2
    }
}
