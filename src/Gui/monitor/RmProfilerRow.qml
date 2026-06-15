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

// One scope row: NAME / CALLS / TOTAL / AVG / MAX / CPU. All values are
// pre-formatted in ProfilerController — pure bindings here. Deep-only scopes
// (those carrying per-scope CPU) grey out while the deep toggle is off.
Rectangle {
    id: root

    property var  scopeData
    property bool isAlternate: false
    property bool deepOn: false

    height: Theme.sp(28)
    color: isAlternate ? Theme.colorBg : Theme.colorSurface

    readonly property color primaryColor:
        (root.scopeData.deep && !root.deepOn) ? Theme.colorText3 : Theme.colorText2

    Row {
        anchors { fill: parent; leftMargin: Theme.sp(10); rightMargin: Theme.sp(10) }
        visible: root.scopeData !== null && root.scopeData !== undefined

        // Scope name (fills remaining width)
        Item {
            width: parent.width - Theme.sp(64) - Theme.sp(76) - Theme.sp(76) - Theme.sp(76) - Theme.sp(76)
            height: parent.height
            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: parent.width
                text: root.scopeData.name
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.primaryColor
                elide: Text.ElideRight
            }
        }

        // Calls — 64px
        Item {
            width: Theme.sp(64); height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.scopeData.callsStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.primaryColor
            }
        }

        // Total — 76px
        Item {
            width: Theme.sp(76); height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.scopeData.totalStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.primaryColor
            }
        }

        // Avg — 76px
        Item {
            width: Theme.sp(76); height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.scopeData.avgStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.primaryColor
            }
        }

        // Max — 76px
        Item {
            width: Theme.sp(76); height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.scopeData.maxStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.primaryColor
            }
        }

        // CPU — 76px (deep tier; greyed/blank when the toggle is off)
        Item {
            width: Theme.sp(76); height: parent.height
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.scopeData.cpuStr.length > 0 ? root.scopeData.cpuStr : "—"
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: root.deepOn ? Theme.colorText2 : Theme.colorText3
            }
        }
    }
}
