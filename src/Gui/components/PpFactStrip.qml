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

// A strip of read-only facts across the foot of a Settings device card (cameras, IMUs, phones):
// equal-width cells on the raised background, each a micro caps key over its value, with a
// hairline between neighbours and none after the last. `cells` is the model, one entry per cell:
//   { key: "Battery", val: "82%", tint: "good" }
// `tint` colours the value: "accent", "good", "warn", "error", or absent/"" for ordinary text.
// Give it Layout.fillWidth from the card's column; it sizes its own height from its cells.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

RowLayout {
    id: strip

    property var  cells: []
    // Fade a value when its tint changes (the IMU battery reading does); off, the colour snaps.
    property bool animateTint: false

    spacing: 0

    Repeater {
        id: fsRepeater
        model: strip.cells

        delegate: Rectangle {
            required property var modelData
            required property int index

            Layout.fillWidth: true
            implicitHeight: fsCol.implicitHeight + Theme.gap(18)
            color: Theme.colorBg2

            // Vertical separator (except last)
            Rectangle {
                anchors.right:  parent.right
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                width:   1
                color:   Theme.colorBorderMid
                opacity: Theme.borderOpacityNormal
                visible: index < fsRepeater.count - 1
            }

            ColumnLayout {
                id: fsCol
                anchors {
                    left:   parent.left
                    right:  parent.right
                    top:    parent.top
                    leftMargin:  Theme.gap(14)
                    rightMargin: Theme.gap(14)
                    topMargin:   Theme.gap(9)
                }
                spacing: Theme.gap(3)

                PpMicro {
                    kind: "caps"
                    text: modelData.key
                }
                Text {
                    text:           modelData.val
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody2
                    color: Theme.toneColor(modelData.tint, Theme.colorText)
                    elide:          Text.ElideRight
                    Layout.fillWidth: true
                    Behavior on color {
                        enabled: strip.animateTint
                        ColorAnimation { duration: Theme.durationFast }
                    }
                }
            }
        }
    }
}
