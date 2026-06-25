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

Item {
    id: root
    property string iconText:  ""
    property string titleText: ""
    // Opt-in ambient topo background — on for the Swing/GRF/Coach rail holding
    // screens, off for the inert Settings sub-panel placeholders. Hidden when
    // off, so its shader never renders or animates there.
    property bool ambientBackground: false

    // Theme-reactive contour field over the window's Theme.colorBg. Pauses when
    // off-screen / backgrounded; respects reduced-motion. Hover lifts the terrain
    // under the cursor; click/tap sends a ripple.
    PpTopoBackground {
        id: topo
        anchors.fill: parent
        z: -1
        visible:  root.ambientBackground
        animated: root.ambientBackground && !appSettings.reduceMotion
        colorLow:  Theme.gradientWarm
        colorMid:  Theme.gradientWarmLit
        colorHigh: Theme.gradientCool
        hoverPoint: topoHover.hovered
            ? Qt.point(topoHover.point.position.x / width,
                       topoHover.point.position.y / height)
            : Qt.point(-1, -1)
    }

    // Passive pointer handlers — inert on the Settings sub-panel placeholders
    // (topo.animated is false there), live on the Swing/GRF/Coach holding screens.
    HoverHandler {
        id: topoHover
        enabled: topo.animated
    }
    TapHandler {
        enabled: topo.animated
        gesturePolicy: TapHandler.DragThreshold
        onTapped: (ep) => topo.ripple(ep.position.x / topo.width,
                                      ep.position.y / topo.height)
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(12)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:           iconText
            font.pixelSize: Theme.sp(32)
            color:          Theme.colorText3
        }

        PpDisplayText {
            anchors.horizontalCenter: parent.horizontalCenter
            text:           titleText
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:               qsTr("COMING SOON")
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              Theme.colorText3
        }
    }
}
