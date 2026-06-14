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

// Read-only grouped key/value list over `metadata` — a list of groups
// [{ group: <title>, rows: [{ k, v }, ...] }, ...]. Plain provenance; no editing.

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

Item {
    id: panel
    property var metadata: []

    Text {
        anchors { left: parent.left; top: parent.top; leftMargin: Theme.sp(14); topMargin: Theme.sp(12) }
        text: qsTr("PROPERTIES"); font.family: Theme.fontData
        font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
        color: Theme.colorText3
        id: title
    }

    Flickable {
        anchors { left: parent.left; right: parent.right; top: title.bottom; bottom: parent.bottom
                  topMargin: Theme.sp(8) }
        clip: true
        contentHeight: col.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: col
            width: parent.width
            spacing: Theme.sp(12)
            leftPadding: Theme.sp(14); rightPadding: Theme.sp(14); bottomPadding: Theme.sp(14)

            Repeater {
                model: panel.metadata
                delegate: Column {
                    required property var modelData
                    width: col.width - Theme.sp(28)
                    spacing: Theme.sp(4)

                    Text {
                        text: (modelData.group || "").toUpperCase()
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro; color: Theme.colorText2
                    }
                    Repeater {
                        model: modelData.rows
                        delegate: Item {
                            required property var modelData
                            width: parent.width
                            height: Theme.sp(20)
                            Text {
                                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                                width: parent.width * 0.42; elide: Text.ElideRight
                                text: modelData.k
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                color: Theme.colorText3
                            }
                            Text {
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                width: parent.width * 0.55; elide: Text.ElideLeft
                                horizontalAlignment: Text.AlignRight
                                text: modelData.v
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                                color: Theme.colorText
                            }
                        }
                    }
                }
            }
        }
    }
}
