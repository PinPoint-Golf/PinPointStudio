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
// analysis + export; it hides again once the on-screen replay runs). Pure
// presentation: the PLACEMENT
// SITE owns the visibility gate. All motion is gated on Theme.reduceMotion; the
// plain filling bar remains. Reads the global shotProcessor context property.
//
// NOT READY (#281's stopgap): while the shot trigger is disarmed — and it is for
// as long as a shot is in the pipeline — the label says so in words, after a ⊘
// (the shape the "shot refused" notice uses), so a golfer who swings now knows the
// swing will not be recorded. Bound to app.shotController.armed, not to the
// badge being shown, so it stops saying it the day capture overlaps analysis.

import QtQuick
import PinPointStudio

Rectangle {
    id: analysingBox
    // Wide enough for the words and the longest elapsed time ("0:00"), so the badge
    // does not resize as the seconds tick.
    implicitWidth: Math.max(Theme.sp(128),
                            labelRow.implicitWidth + Theme.gap(10) + elapsedRoom.width + Theme.gap(24))
    implicitHeight: Theme.sp(40)
    radius: Theme.radius
    color: Theme.colorBg2
    border.width: 1
    border.color: Theme.colorBorderMid

    readonly property bool sparkling: visible && !Theme.reduceMotion
    // The next swing cannot be detected or recorded now.
    readonly property bool notReady: !app.shotController.armed

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
        width: parent.width - Theme.gap(24)
        spacing: Theme.gap(5)

        Item {   // label left, elapsed time right
            width: parent.width
            height: labelRow.implicitHeight

            Row {
                id: labelRow
                anchors.left: parent.left
                spacing: Theme.gap(5)

                Text {   // the not-ready mark: a shape beside the words, never a colour alone
                    anchors.verticalCenter: parent.verticalCenter
                    visible: analysingBox.notReady
                    text: "\u2298"   // ⊘
                    font.family: Theme.fontSymbol
                    font.pixelSize: Math.round(Theme.fontSzMicro * Theme.symbolScale("\u2298"))
                    color: Theme.colorText3
                }

                PpMicro {
                    id: analysingLbl
                    text: analysingBox.notReady
                          ? Theme.caps(qsTr("Analysing: not ready for the next swing"))
                          : Theme.caps(qsTr("Analysing"))
                }
            }

            TextMetrics {
                id: elapsedRoom
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                text: "0:00"
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
                width: Math.max(height, parent.width * app.shotProcessor.analysisProgress)
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
