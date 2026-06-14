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

// Post-shot ANALYSING badge — label + elapsed time on the top line, a filling
// progress bar (sweeping sheen + leading-edge twinkles) beneath. Lifted out of
// PpSessionToolbar so the title bar (PpHeader) can host it in the centre slot,
// taking the DETECT cluster's place while a shot is processed (post-roll +
// analysis + export; it hides again once the on-screen replay runs, which has
// its own REPLAY badge on the camera frames). Pure presentation: the PLACEMENT
// SITE owns the visibility gate. All motion is gated on Theme.reduceMotion; the
// plain filling bar remains. Reads the global shotProcessor context property.

import QtQuick
import PinPointStudio

Rectangle {
    id: analysingBox
    implicitWidth: Theme.sp(128)
    implicitHeight: Theme.sp(40)
    radius: Theme.radius
    color: Theme.colorBg2
    border.width: 1
    border.color: Theme.colorBorderMid

    readonly property bool sparkling: visible && !Theme.reduceMotion

    // Elapsed wall-time since the shot started processing (post-roll included).
    // Hidden for the first second so instant analyses never flash a "0s".
    property int elapsedS: 0
    readonly property string elapsedLabel: elapsedS >= 60
        ? Math.floor(elapsedS / 60) + ":" + String(elapsedS % 60).padStart(2, "0")
        : elapsedS + "s"
    onVisibleChanged: if (visible) elapsedS = 0
    Timer {
        running: analysingBox.visible
        interval: 1000; repeat: true
        onTriggered: analysingBox.elapsedS++
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - Theme.sp(24)
        spacing: Theme.sp(5)

        Item {   // label left, elapsed time right
            width: parent.width
            height: analysingLbl.implicitHeight

            Text {
                id: analysingLbl
                anchors.left: parent.left
                text: qsTr("ANALYSING")
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            Text {
                anchors.right: parent.right
                visible: analysingBox.elapsedS > 0
                text: analysingBox.elapsedLabel
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color: Theme.colorText3
                opacity: 0.7
            }
        }

        Item {
            id: analyseBar
            width: parent.width
            height: Theme.sp(4)

            Rectangle {   // track
                anchors.fill: parent
                radius: height / 2
                color: Theme.colorBg3
            }

            Rectangle {   // fill — never narrower than its own end caps
                id: analyseFill
                width: Math.max(height, parent.width * shotProcessor.analysisProgress)
                height: parent.height
                radius: height / 2
                color: Theme.colorAccent
                clip: true
                Behavior on width { NumberAnimation { duration: Theme.durationNormal } }

                Rectangle {   // sheen sweeping the filled portion only (parent clips)
                    id: analyseSheen
                    width: Theme.sp(22); height: parent.height
                    visible: analysingBox.sparkling
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.55) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                    NumberAnimation on x {
                        running: analysingBox.sparkling
                        loops: Animation.Infinite
                        from: -analyseSheen.width
                        to: analyseBar.width
                        duration: 1100
                    }
                }
            }

            // Twinkles riding the fill's leading edge, staggered so they read as
            // sparkle rather than a blinking cluster — offsets tightened to stay
            // inside the chip.
            Repeater {
                model: [ { dx: -2, dy: -5, period: 900  },
                         { dx: -9, dy:  4, period: 1300 },
                         { dx:  4, dy: -1, period: 700  } ]
                delegate: Text {
                    required property var modelData
                    x: analyseFill.width + modelData.dx - implicitWidth / 2
                    y: analyseBar.height / 2 + modelData.dy - implicitHeight / 2
                    text: "✦"
                    font.family: Theme.fontSymbol
                    font.pixelSize: Theme.fontSzMicro - 1
                    color: Theme.colorAccent
                    opacity: 0
                    scale: 0.6
                    SequentialAnimation on opacity {
                        running: analysingBox.sparkling
                        loops: Animation.Infinite
                        PauseAnimation  { duration: modelData.period * 0.4 }
                        NumberAnimation { to: 1.0; duration: modelData.period * 0.3
                                          easing.type: Easing.OutQuad }
                        NumberAnimation { to: 0.0; duration: modelData.period * 0.3
                                          easing.type: Easing.InQuad }
                    }
                    SequentialAnimation on scale {
                        running: analysingBox.sparkling
                        loops: Animation.Infinite
                        PauseAnimation  { duration: modelData.period * 0.4 }
                        NumberAnimation { to: 1.15; duration: modelData.period * 0.3 }
                        NumberAnimation { to: 0.6;  duration: modelData.period * 0.3 }
                    }
                }
            }
        }
    }
}
