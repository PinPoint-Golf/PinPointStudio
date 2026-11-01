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

// "RECORDED WITH" — the device filter over a list of sessions: one chip per device any saved session
// used, on whichever DeviceFilterModel it is given (the carousel's session drawer; the golfer's sessions
// on Home and the golfer page). Multi-select, AND: two chips on = sessions that had both. Each chip's
// count is what adding it would leave. Tap again to drop one; Clear drops them all. Hidden when no saved
// session says what recorded it.
//
// showCount puts "3 of 12" after Clear while the filter is on, for a list with no header of its own to
// say it in.

import QtQuick
import PinPointStudio

Flow {
    id: bar
    objectName: "deviceFilterBar"

    property var  model: null
    property bool showCount: false

    readonly property bool active: !!model && model.deviceFilter.length > 0

    visible: !!model && model.availableDevices.length > 0
    spacing: Theme.gap(6)

    PpMicro {
        height: Theme.sp(22)
        verticalAlignment: Text.AlignVCenter
        text: Theme.caps(qsTr("Recorded with"))
    }

    Repeater {
        model: bar.model ? bar.model.availableDevices : []

        PpChoiceChip {
            id: devChip
            required property var modelData

            objectName: "deviceChip_" + modelData.id
            style:    "filter"
            label:    modelData.label
            count:    String(modelData.count)     // sessions left if this chip were on too (AND)
            selected: bar.model.deviceFilter.indexOf(modelData.id) >= 0
            // Nothing would be left if this were added — quietened, still tappable.
            dimmed:   !selected && modelData.count === 0
            onClicked: bar.model.toggleDevice(devChip.modelData.id)
        }
    }

    PpLink {
        visible: bar.active
        height: Theme.sp(22)
        verticalAlignment: Text.AlignVCenter
        text: qsTr("Clear")
        pressMargin: Theme.sp(4)
        onClicked: bar.model.clearDeviceFilter()
    }

    Text {   // how much of the library the filter is showing
        objectName: "deviceFilterCount"
        visible: bar.showCount && bar.active
        height: Theme.sp(22)
        verticalAlignment: Text.AlignVCenter
        text: bar.model ? qsTr("%1 of %2").arg(bar.model.shownSavedCount).arg(bar.model.savedCount) : ""
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
        color: Theme.colorText3
    }
}
