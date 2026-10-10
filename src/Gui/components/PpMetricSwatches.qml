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

// The metric palette as a row of swatches — one per NAME, drawn in the current theme's colour for
// it (Theme.paletteColor, so a user's retune shows here too). Picking one emits its name; what the
// name is written to is the caller's business (a metric's colour in the inspector, the name being
// retuned in Appearance).
Flow {
    id: root

    // The name ringed as current. "" rings nothing.
    property string selected: ""
    property real   swatchSize: Theme.sp(20)

    signal picked(string name)

    spacing: Theme.gap(6)

    Repeater {
        model: Theme.metricColorNames
        delegate: Rectangle {
            id: sw
            required property string modelData
            required property int    index

            objectName: "metricSwatch_" + modelData
            width:  root.swatchSize
            height: root.swatchSize
            radius: width / 2
            color:  Theme.paletteColor(modelData)
            // The current one is ringed in the text colour with a surface gap, so the ring reads on
            // every swatch rather than vanishing into a dark one.
            border.width: modelData === root.selected ? Theme.sp(2) : 0
            border.color: Theme.colorSurface

            Rectangle {
                anchors.centerIn: parent
                width:  parent.width + Theme.sp(4)
                height: parent.height + Theme.sp(4)
                radius: width / 2
                color:  "transparent"
                border.width: 1.5
                border.color: Theme.colorText
                visible: sw.modelData === root.selected
            }

            ToolTip.visible: swMa.containsMouse
            ToolTip.text:    sw.modelData.charAt(0).toUpperCase() + sw.modelData.slice(1)
            ToolTip.delay:   300

            PpPressable { id: swMa; hoverScale: 1.12; onClicked: root.picked(sw.modelData) }
        }
    }
}
