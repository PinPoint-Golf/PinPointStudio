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

// The app's on/off switch: a 34 × 18 pill, accent when on, with a 12 px knob that slides across. It
// does not change itself — a press emits toggled(!checked) and the caller writes the setting, so
// `checked` stays bound to the value it shows (the way every settings toggle works):
//
//     PpSwitch {
//         checked: appSettings.archiveKeepRaw
//         onToggled: (v) => appSettings.archiveKeepRaw = v
//     }
//
// `knobColor` is white, as nearly every switch has it; the toolbar camera panel (cameras/PpCameraPanel.qml)
// and the toolbar IMU panel (imu/PpImuPanel.qml) take a theme colour instead
// (Theme.dark ? Theme.colorText : Theme.colorSurface) and pass it here.
// `largeKnob` gives the session View and Motion panels' look: the same pill with a round end, a
// 14 px knob 2 px from the edge, surface-coloured when on and grey (colorText3) when off — knobColor
// is not used then.
// Used by every Settings panel, the toolbar camera and IMU panels, the shot filter, the athlete's clubs
// section, the session View and Motion panels (largeKnob) and the setup check row (SetupCheckRow).

import QtQuick
import PinPointStudio

Rectangle {
    id: pill

    property bool  checked:   false
    property color knobColor: "white"
    property bool  largeKnob: false
    signal toggled(bool value)

    width:  Theme.sp(34)
    height: Theme.sp(18)
    radius: pill.largeKnob ? height / 2 : Theme.sp(9)
    color:  pill.checked ? Theme.colorAccent : Theme.colorBg3
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    Rectangle {
        readonly property real inset: pill.largeKnob ? Theme.sp(2) : Theme.sp(3)
        width:  pill.largeKnob ? Theme.sp(14) : Theme.sp(12)
        height: width
        radius: pill.largeKnob ? width / 2 : Theme.sp(6)
        color:  !pill.largeKnob ? pill.knobColor
              : pill.checked    ? (Theme.dark ? Theme.colorBg : Theme.colorSurface)
              :                   Theme.colorText3
        // Centred, or (largeKnob) pinned 2 px from the top as the session panels drew it — the two
        // differ by half a pixel at some font scales.
        anchors.verticalCenter: pill.largeKnob ? undefined : parent.verticalCenter
        anchors.top:            pill.largeKnob ? parent.top : undefined
        anchors.topMargin:      inset
        x: pill.checked ? parent.width - width - inset : inset
        Behavior on x { NumberAnimation { duration: pill.largeKnob ? Theme.durationFast : 120 } }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: pill.largeKnob
        cursorShape:  Qt.PointingHandCursor
        onClicked:    pill.toggled(!pill.checked)
    }
}
