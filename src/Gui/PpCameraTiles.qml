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

// Camera stage-panel: one video tile per session-enabled camera, the in-replay
// metric graph, and the pop-out replay stage. Extracted from ScreenWrist so the
// stage arranger (PpModeStage) can host it as the "camera" panel on every mode.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property int  sessionType: -1
    property bool showHittingArea: false

    readonly property var _replaySeries:
        (shotProcessor.replayAnalysisDetail && shotProcessor.replayAnalysisDetail.series)
        ? shotProcessor.replayAnalysisDetail.series : []

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(12)
        spacing: Theme.sp(8)

        Repeater {
            model: cameraManager.cameraList.filter(function(c) { return c.sessionEnabled })
            delegate: PpCameraFrame {
                required property var modelData
                Layout.fillHeight: true
                Layout.fillWidth: false
                Layout.preferredWidth: height * videoAspect
                placeholderAspect: (modelData.initialWidth > 0 && modelData.initialHeight > 0)
                                   ? modelData.initialWidth / modelData.initialHeight : 16.0 / 9.0
                instance: {
                    var insts = cameraManager.instances
                    for (var i = 0; i < insts.length; ++i)
                        if (insts[i].cameraKey === modelData.cameraKey) return insts[i]
                    return null
                }
                displayName: modelData.alias !== "" ? modelData.alias : modelData.description
                showHittingArea: root.showHittingArea
                showHittingAreaHint: cameraManager.livePoseEnabled && sessionController.running
            }
        }

        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            PpMetricGraph {
                anchors.fill: parent; anchors.margins: Theme.sp(8)
                visible:    shotProcessor.isReplaying && root._replaySeries.length > 0
                seriesList: root._replaySeries
                phases:     (shotProcessor.replayAnalysisDetail && shotProcessor.replayAnalysisDetail.phases)
                                ? shotProcessor.replayAnalysisDetail.phases : []
                startUs:    shotProcessor.replayStartUs
                endUs:      shotProcessor.replayEndUs
                impactUs:   shotProcessor.replayImpactUs
                playheadUs: shotProcessor.replayPositionUs
                showPlayhead: true
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(6)
        visible: cameraManager.cameraList.filter(function(c) { return c.sessionEnabled }).length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("No cameras enabled")
            color: Theme.colorText2; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Enable cameras in the toolbar's Cameras panel")
            color: Theme.colorText3; font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
        }
    }

    PpReplayStage {
        anchors.fill: parent; z: 10
        visible: shotReplay.active && shotReplay.target === "screen"
    }
}
