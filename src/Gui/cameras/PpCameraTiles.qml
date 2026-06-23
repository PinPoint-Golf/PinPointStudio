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

// Camera stage-panel — its video source follows the session DATA SOURCE, not the
// mode. Capture is the only live-video mode: it shows one live tile per session-
// enabled camera (plus the degraded-shot ¼× auto-replay metric graph). Review AND
// Analyse both show the LOADED shot's video — one tile per stream of that swing,
// enumerated from the swing's own swing.json (shotReplay.streams), never the local
// camera rig, so a swing recorded on a different setup still plays and the playback
// follows the Review↔Analyse toggle. Hosted by PpModeStage as the "camera" panel.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property int  sessionType: -1
    property bool showHittingArea: false

    // Live cameras show ONLY in Capture; Review and Analyse both render the loaded
    // shot's disk streams. The fork is data-source-driven (not Review-specific), so
    // one loaded shotReplay carries the video across the Review↔Analyse mode toggle.
    readonly property bool _replay: SessionMode.mode !== SessionMode.capture

    // All session screens stay instantiated in the StackLayout, but a disk-replay
    // stream has ONE QMediaPlayer with ONE sink — so only the ACTIVE screen may
    // create replay tiles (else hidden screens steal the sink and the visible one
    // shows nothing). The session screen sits at StackLayout index sessionType + 1.
    readonly property bool _screenActive: navController.currentIndex === root.sessionType + 1
    readonly property bool _replayHere: _replay && _screenActive

    // Replay tiles are instantiated one event-loop turn AFTER _replayHere goes true.
    // Entering Replay can recreate this whole panel (Capture's "stage" arrangement →
    // Replay's "split" rebuilds PpModeStage's camera Loader), and the panel being torn
    // down would otherwise spawn a transient replay tile — which binds, then dies,
    // churning the disk-replay video sink. Loader destroys the dying panel
    // synchronously during the arrangement change, so deferring arming via Qt.callLater
    // means only the SURVIVING instance ever reaches _replayArmed and creates a tile.
    property bool _replayArmed: false
    function _syncReplayArmed() { root._replayArmed = root._replayHere }
    on_ReplayHereChanged: Qt.callLater(root._syncReplayArmed)
    Component.onCompleted:  Qt.callLater(root._syncReplayArmed)

    readonly property var _liveCameras:
        cameraManager.cameraList.filter(function(c) { return c.sessionEnabled })

    readonly property var _replaySeries:
        (shotProcessor.replayAnalysisDetail && shotProcessor.replayAnalysisDetail.series)
        ? shotProcessor.replayAnalysisDetail.series : []

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(12)
        spacing: Theme.sp(8)

        // ── Live camera tiles (Capture only) ───────────────────────────────
        Repeater {
            model: root._replay ? [] : root._liveCameras
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

        // ── Replay stream tiles (Review / Analyse) — the loaded swing's OWN ─
        // streams. Only the active screen creates these (one sink per stream), and
        // only once arming has settled (see _replayArmed) so a torn-down panel during
        // the Capture→Replay transition never spawns a transient tile.
        Repeater {
            model: root._replayArmed ? shotReplay.streams : []
            delegate: PpCameraFrame {
                required property var modelData
                Layout.fillHeight: true
                Layout.fillWidth: false
                Layout.preferredWidth: height * videoAspect
                replayStreamIndex: modelData.index
                placeholderAspect: modelData.aspect > 0 ? modelData.aspect : 16.0 / 9.0
                displayName: qsTr("Replay")
                showHittingArea: false
                showHittingAreaHint: false
                showPoseOverlay: false        // live-pose canvas — replay uses replayOverlay
                showStatsOverlay: false
                showPerspectiveBadge: false
                // Telestrator is an Analyse-only affordance (not plain Replay).
                annotationsEnabled: SessionMode.mode === SessionMode.analyse
            }
        }

        // In-replay metric graph — the Capture-mode ¼× auto-replay transient
        // (shotProcessor; degraded shots only). Review/Analyse draw their curves
        // in the charts panel instead.
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: !root._replay
            PpMetricChart {
                anchors.fill: parent; anchors.margins: Theme.sp(8)
                compact:    true        // plot only — no toolbar / brush / summary chrome
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

    // Capture empty-state: no cameras enabled.
    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(6)
        visible: !root._replay && root._liveCameras.length === 0
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

    // Review / Analyse empty-state: no swing focused, or the swing has no video.
    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(6)
        visible: root._replayHere && shotReplay.streams.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: SessionMode.focusedShotId >= 0 ? qsTr("No video for this swing")
                                                 : qsTr("Select a swing to review")
            color: Theme.colorText2; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Pick a shot from the filmstrip below")
            color: Theme.colorText3; font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
        }
    }

    // ── Telestrator palette (Analyse only) ────────────────────────────────────
    // One shared bar over the camera tiles; drives the AnnotationTool state every
    // tile's PpAnnotationLayer reads. Shown only with a swing loaded for analysis.
    PpAnnotationToolbar {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.sp(12)
        z: 50
        visible: SessionMode.mode === SessionMode.analyse
                 && shotReplay.active && shotReplay.streams.length > 0
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
}
