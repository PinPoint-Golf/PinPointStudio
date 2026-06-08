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

// Wrist Motion screen (contentStack index 2). Hosts the persistent session
// toolbar at the top and one video tile per session-enabled camera (toggle a
// camera off in the toolbar's camera panel and its tile disappears; Connect
// in the panel starts the streams). The toolbar's device panels run
// calibration entirely in-panel, so this screen neither exposes nor routes
// any calibrate request.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PpSessionToolbar {
            Layout.fillWidth: true
            sessionType: 1   // SessionController::Type::Wrist
        }

        // Body — one video tile per session-enabled camera, side by side.
        // Tiles appear on entry as dim "Not connected" placeholders and stream
        // once the toolbar panel's Connect starts the capture pipeline.
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp(12)
                spacing: Theme.sp(8)

                Repeater {
                    model: cameraManager.cameraList.filter(function(c) { return c.sessionEnabled })
                    delegate: PpCameraFrame {
                        required property var modelData
                        Layout.fillHeight: true
                        // Hug the video: width follows the (cropped) frame
                        // aspect at full row height, so a narrow crop frees
                        // horizontal space instead of letterboxing. The
                        // layout may shrink below preferred when several
                        // cameras compete — the inner frameRect aspect-fits
                        // within whatever it gets.
                        Layout.fillWidth: false
                        Layout.preferredWidth: height * videoAspect
                        // Disconnected placeholders open at the crop-aware
                        // aspect (persisted crop × sensor) instead of 16:9,
                        // so the tile doesn't resize when the stream starts.
                        placeholderAspect: (modelData.initialWidth > 0
                                            && modelData.initialHeight > 0)
                                           ? modelData.initialWidth / modelData.initialHeight
                                           : 16.0 / 9.0
                        // Reactive instance lookup (same pattern as the panel's
                        // CamRow) — non-null once the camera is connected.
                        instance: {
                            var insts = cameraManager.instances
                            for (var i = 0; i < insts.length; ++i)
                                if (insts[i].deviceSerialNumber === modelData.serialNumber)
                                    return insts[i]
                            return null
                        }
                        displayName: modelData.alias !== "" ? modelData.alias
                                                            : modelData.description
                        // Wrist screen: skeleton overlay only — no hitting-area
                        // / ball chrome here (other rail screens enable theirs).
                        showHittingArea: false
                    }
                }

                // Tiles pack left; the remainder is reserved for future
                // charts/graphs so adding them won't reflow the videos.
                Item { Layout.fillWidth: true }
            }

            // Muted empty state when every camera is toggled off (or none found).
            Column {
                anchors.centerIn: parent
                spacing: Theme.sp(6)
                visible: cameraManager.cameraList.filter(function(c) { return c.sessionEnabled }).length === 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("No cameras enabled")
                    color: Theme.colorText2
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Enable cameras in the toolbar's Cameras panel")
                    color: Theme.colorText3
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                }
            }
        }

        // Session-shot carousel — keys mirror the Wrist goal vocabulary
        // (goalDefsByType[1]); the stub model supplies placeholder values.
        PpShotCarousel {
            Layout.fillWidth: true
            metricKeys: ["leadWristFlexExt", "leadWristRadUln", "forearmPronation", "leadArmFlexion"]
            traceLabel: qsTr("LEAD-WRIST FLEXION · ADDRESS → IMPACT")
        }
    }
}
