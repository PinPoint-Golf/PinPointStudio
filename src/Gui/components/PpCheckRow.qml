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

// A square checkbox and its words, the whole row pressable: the export sheet's camera and JSON rows
// (PpExportOptionsSheet), the Sessions screen's "Show archived (N)". Press-dip only, no hover-grow:
// the row is the target, and a growing full-width row would nudge its neighbours' edges.

import QtQuick
import PinPointStudio

Item {
    property string label:   ""
    property bool   checked: false
    signal toggled()

    implicitHeight: Theme.sp(26)
    implicitWidth:  crRow.implicitWidth

    Row {
        id: crRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gap(10)
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.sp(16); height: Theme.sp(16); radius: Theme.sp(4)
            color:        checked ? Theme.colorAccent : "transparent"
            border.width: 1
            border.color: checked ? Theme.colorAccent : Theme.colorBorderStrong
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            Text {
                anchors.centerIn: parent
                visible:        checked
                text:           "✓"
                font.family:    Theme.fontSymbol
                font.pixelSize: Theme.fontSzLabel
                color:          Theme.dark ? Theme.colorBg : Theme.colorSurface
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text:           label
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            color:          Theme.colorText
        }
    }
    PpPressable {
        id: crMa
        hoverScale: 1.0       // full-width row — press-dip only
        onClicked:  toggled()
    }
}
