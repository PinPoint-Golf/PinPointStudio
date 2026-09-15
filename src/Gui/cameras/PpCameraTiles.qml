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

    // Analyse-only telestrator gutter: a reserved LHS margin that hosts the
    // annotation palette's open/collapse button, so the affordance lives beside
    // the footage rather than on top of it. Reserved whenever the palette can show
    // (regardless of open/closed) so the tiles never reflow as you toggle it.
    readonly property bool _annotate: SessionMode.mode === SessionMode.analyse
                                      && shotReplay.active && shotReplay.streams.length > 0
    property real _leftGutter: _annotate ? Theme.sp(54) : Theme.sp(12)
    Behavior on _leftGutter {
        enabled: !Theme.reduceMotion
        NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
    }

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

    // Every session-enabled camera, and the same list without the impact camera
    // — the row shows the latter; the impact camera floats over it (see the
    // picture-in-picture Item below).
    readonly property var _liveAll:
        cameraManager.cameraList.filter(function(c) { return c.sessionEnabled })
    readonly property var _liveCameras:
        root._liveAll.filter(function(c) { return c.perspective !== CameraInstance.Impact })
    readonly property var _liveImpact: {
        var l = root._liveAll
        for (var i = 0; i < l.length; ++i)
            if (l[i].perspective === CameraInstance.Impact) return l[i]
        return null
    }
    // Replay streams likewise: the loaded swing's non-impact streams fill the
    // row; its impact stream (setup.perspective 4), if any, floats.
    readonly property var _replayStreams:
        shotReplay.streams.filter(function(s) { return s.perspective !== CameraInstance.Impact })
    readonly property var _replayImpact: {
        var s = shotReplay.streams
        for (var i = 0; i < s.length; ++i)
            if (s[i].perspective === CameraInstance.Impact) return s[i]
        return null
    }

    readonly property var _replaySeries:
        (shotProcessor.replayAnalysisDetail && shotProcessor.replayAnalysisDetail.series)
        ? shotProcessor.replayAnalysisDetail.series : []

    RowLayout {
        id: tilesRow
        anchors.fill: parent
        anchors.topMargin: Theme.sp(12)
        anchors.bottomMargin: Theme.sp(12)
        anchors.rightMargin: Theme.sp(12)
        anchors.leftMargin: root._leftGutter   // reserve the telestrator gutter
        spacing: Theme.sp(8)

        // ── Live camera tiles (Capture only) ───────────────────────────────
        Repeater {
            model: root._replay ? [] : root._liveCameras
            delegate: PpCameraFrame {
                required property var modelData
                Layout.fillHeight: true
                Layout.fillWidth: false
                Layout.preferredWidth: height * videoAspect
                // ⚠ READ FROM THE SETTING, NOT FROM THE LIST ROW, AND THAT IS
                // WHAT LETS A CROP EDIT LEAVE THE CAMERA LIST ALONE.
                // `cameraList()` carries crop-derived initialWidth/initialHeight,
                // so keeping a disconnected tile's aspect honest used to mean
                // re-emitting `cameraListChanged` on every crop write — which
                // rebuilt every delegate in every camera Repeater in the
                // application, including the one holding the mouse grab in the
                // crop editor (see CamerasPanel.qml's cropRoiChanged handler) and
                // the Settings tiles whose teardown closed their Streams.  This
                // binding depends on the setting itself, so the tile follows a
                // crop with no list refresh at all.  The row's own values remain
                // the fallback for a camera with no crop stored.
                placeholderAspect: {
                    const roi = appSettings.cameraRoi[modelData.cameraKey]
                    if (roi && roi.w > 0 && roi.h > 0
                        && modelData.maxWidth > 0 && modelData.maxHeight > 0)
                        return (modelData.maxWidth * roi.w) / (modelData.maxHeight * roi.h)
                    return (modelData.initialWidth > 0 && modelData.initialHeight > 0)
                           ? modelData.initialWidth / modelData.initialHeight : 16.0 / 9.0
                }
                instance: {
                    var insts = cameraManager.instances
                    for (var i = 0; i < insts.length; ++i)
                        if (insts[i].cameraKey === modelData.cameraKey) return insts[i]
                    return null
                }
                displayName: modelData.alias !== "" ? modelData.alias : modelData.description
                showHittingArea: root.showHittingArea
                showHittingAreaHint: cameraManager.livePoseEnabled && sessionController.running
                // Live ball circle rides the same live-pose gate as the ROI hint.
                showBallOverlay: cameraManager.livePoseEnabled && sessionController.running
                // Capture-mode ¼× post-shot auto-replay overlay follows the view's
                // Pose overlay toggle (Capture setting == current mode here). Chrome
                // bindings above are left EXACTLY as-is (capture chrome unchanged v1).
                showReplayOverlay: ViewLayout.overlaysOn(SessionMode.mode)
                // Per-element motion modes for that ¼× auto-replay overlay. The
                // Capture guard inside ViewLayout forces every body/shaft element to
                // "off" (ball only), so the live skeleton is untouched by this.
                motionOn:          ViewLayout.motionOn(SessionMode.mode)
                motionModes:       ViewLayout.motionFor(SessionMode.mode).modes
                motionTraceTarget: ViewLayout.motionTraceTarget(SessionMode.mode)
                leadIsLeft:        ViewLayout.leadIsLeft()
            }
        }

        // ── Replay stream tiles (Review / Analyse) — the loaded swing's OWN ─
        // streams. Only the active screen creates these (one sink per stream), and
        // only once arming has settled (see _replayArmed) so a torn-down panel during
        // the Capture→Replay transition never spawns a transient tile.
        Repeater {
            model: root._replayArmed ? root._replayStreams : []
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
                // Analyzed pose+shaft+ball overlay follows the view's motion master
                // switch — covers both Replay and Analyse (this Repeater serves both).
                // showReplayOverlay gates the Canvas visibility; motionOn is the same
                // value (master off ⇒ nothing draws), with per-element modes below.
                showReplayOverlay: ViewLayout.motionOn(SessionMode.mode)
                motionOn:          ViewLayout.motionOn(SessionMode.mode)
                motionModes:       ViewLayout.motionFor(SessionMode.mode).modes
                motionTraceTarget: ViewLayout.motionTraceTarget(SessionMode.mode)
                leadIsLeft:        ViewLayout.leadIsLeft()
                // Telestrator is an Analyse-only affordance (not plain Replay).
                annotationsEnabled: SessionMode.mode === SessionMode.analyse
            }
        }

        // Trailing fill absorbs the dead space to the RIGHT of the left-aligned
        // replay tiles (their original layout). Its left edge marks the camera
        // cluster's right boundary, so the telestrator bar can centre over the
        // cluster without altering the tiles. (Capture uses the metric graph as
        // its fillWidth element instead.)
        Item { id: tilesEnd; Layout.fillWidth: true; Layout.fillHeight: true; visible: root._replay }

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

    // ── Impact camera — picture-in-picture over the other tiles ─────────────
    // A 640×240 strip at ~600 fps (impact_camera_design.md §10.2) is 2.7:1: in
    // the row it would claim a whole tile's height for a few frames of content
    // and squeeze the cameras that carry the swing. It floats over the cluster
    // instead — movable, corner-resizable with its aspect locked, and never
    // closable: like every tile it follows the selected cameras / the loaded
    // swing. Its place and size are remembered (appSettings.impactPipRect).
    Item {
        id: pip
        readonly property var  liveData:   root._replay ? null : root._liveImpact
        readonly property var  replayData: root._replayArmed ? root._replayImpact : null
        readonly property bool active:     root._replay ? replayData !== null : liveData !== null
        visible: active
        z: 40   // over the tiles, under the telestrator chrome (50)

        // The stage area the box lives in — the tile row's own box.
        readonly property real areaX: root._leftGutter
        readonly property real areaY: Theme.sp(12)
        readonly property real areaW: Math.max(1, root.width  - root._leftGutter - Theme.sp(12))
        readonly property real areaH: Math.max(1, root.height - Theme.sp(24))

        // Remembered geometry, normalised so it survives any window size: w is
        // the width as a fraction of the area width; x and y are the position as
        // a fraction of the FREE range (0 = left/top edge, 1 = right/bottom
        // edge), so the box can never end up off-stage. Default: 38 % wide,
        // bottom-right — a picture-in-picture that leaves the swing cameras
        // the room they need, big enough that a 240-row strip is readable.
        readonly property var  stored:   appSettings.impactPipRect
        readonly property real minFracW: 0.18
        readonly property real maxFracW: 0.75
        property real fracW: (stored && stored.w > 0) ? stored.w : 0.38
        property real fracX: (stored && stored.x !== undefined) ? stored.x : 1.0
        property real fracY: (stored && stored.y !== undefined) ? stored.y : 1.0

        readonly property real aspect: (pipLoader.item && pipLoader.item.videoAspect > 0)
                                       ? pipLoader.item.videoAspect : 8.0 / 3.0
        readonly property real wantW: Math.max(minFracW, Math.min(maxFracW, fracW)) * areaW
        width:  Math.max(1, Math.min(wantW, areaH * aspect))   // never taller than the stage
        height: width / aspect
        x: areaX + Math.max(0, Math.min(1, fracX)) * Math.max(0, areaW - width)
        y: areaY + Math.max(0, Math.min(1, fracY)) * Math.max(0, areaH - height)

        function persist() {
            appSettings.impactPipRect = { x: pip.fracX, y: pip.fracY, w: pip.fracW }
        }

        // Halo so the box reads as floating over the footage beneath.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.sp(3)
            radius: Theme.radius
            color: "black"
            opacity: 0.55
        }

        Loader {
            id: pipLoader
            anchors.fill: parent
            active: pip.active
            sourceComponent: root._replay ? pipReplayFrame : pipLiveFrame
        }

        Component {
            id: pipLiveFrame
            PpCameraFrame {
                readonly property var camData: pip.liveData
                placeholderAspect: {
                    if (!camData) return 8.0 / 3.0
                    const roi = appSettings.cameraRoi[camData.cameraKey]
                    if (roi && roi.w > 0 && roi.h > 0 && camData.maxWidth > 0 && camData.maxHeight > 0)
                        return (camData.maxWidth * roi.w) / (camData.maxHeight * roi.h)
                    return (camData.initialWidth > 0 && camData.initialHeight > 0)
                           ? camData.initialWidth / camData.initialHeight : 8.0 / 3.0
                }
                instance: {
                    if (!camData) return null
                    var insts = cameraManager.instances
                    for (var i = 0; i < insts.length; ++i)
                        if (insts[i].cameraKey === camData.cameraKey) return insts[i]
                    return null
                }
                displayName: camData ? (camData.alias !== "" ? camData.alias : camData.description) : ""
                // No body in a 240-row strip: no hitting area, pose or ball chrome.
                showHittingArea:     false
                showHittingAreaHint: false
                showBallOverlay:     false
                showPoseOverlay:     false
                showReplayOverlay:   false
            }
        }

        Component {
            id: pipReplayFrame
            PpCameraFrame {
                replayStreamIndex: pip.replayData ? pip.replayData.index : -1
                placeholderAspect: (pip.replayData && pip.replayData.aspect > 0)
                                   ? pip.replayData.aspect : 8.0 / 3.0
                displayName: qsTr("Impact")
                showHittingArea:      false
                showHittingAreaHint:  false
                showPoseOverlay:      false
                showStatsOverlay:     false
                showPerspectiveBadge: false
                showReplayOverlay:    false
                annotationsEnabled:   false
            }
        }

        // Outline on top of the frame's own chrome.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: "transparent"
            border.width: 1
            border.color: pipMouse.containsMouse ? Theme.colorAccent : Theme.colorAccentMid
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
        }

        // Move anywhere on the box; resize from a corner, aspect locked, the
        // opposite corner held still. Same zone scheme as the crop editor.
        MouseArea {
            id: pipMouse
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            z: 10

            readonly property real hr: Theme.sp(18)
            property string mode: "none"
            property real   pressRootX: 0
            property real   pressRootY: 0
            property real   startX: 0
            property real   startY: 0
            property real   startW: 0
            property real   startH: 0

            function zone(mx, my) {
                const w = pip.width, h = pip.height
                if (mx < hr     && my < hr)     return "tl"
                if (mx > w - hr && my < hr)     return "tr"
                if (mx < hr     && my > h - hr) return "bl"
                if (mx > w - hr && my > h - hr) return "br"
                return "move"
            }
            cursorShape: {
                switch (mode !== "none" ? mode : zone(mouseX, mouseY)) {
                case "tl": case "br": return Qt.SizeFDiagCursor
                case "tr": case "bl": return Qt.SizeBDiagCursor
                default:              return Qt.SizeAllCursor
                }
            }

            // Place the box's top-left at root coords (px, py) with width w,
            // clamped to the stage; writes the normalised fractions the
            // geometry bindings read.
            function place(px, py, w) {
                w = Math.max(pip.minFracW * pip.areaW, Math.min(pip.maxFracW * pip.areaW, w))
                w = Math.min(w, pip.areaH * pip.aspect)
                const h = w / pip.aspect
                const freeW = Math.max(0, pip.areaW - w)
                const freeH = Math.max(0, pip.areaH - h)
                pip.fracW = w / pip.areaW
                pip.fracX = freeW > 0 ? Math.max(0, Math.min(1, (px - pip.areaX) / freeW)) : 0
                pip.fracY = freeH > 0 ? Math.max(0, Math.min(1, (py - pip.areaY) / freeH)) : 0
            }

            onPressed: (m) => {
                mode = zone(m.x, m.y)
                const p = mapToItem(root, m.x, m.y)
                pressRootX = p.x; pressRootY = p.y
                startX = pip.x; startY = pip.y; startW = pip.width; startH = pip.height
            }
            onPositionChanged: (m) => {
                if (mode === "none") return
                const p  = mapToItem(root, m.x, m.y)
                const dx = p.x - pressRootX
                const dy = p.y - pressRootY
                switch (mode) {
                case "move":
                    place(startX + dx, startY + dy, startW)
                    break
                case "br": {                       // top-left held
                    const w = Math.max(startW + dx, startH + dy > 0 ? (startH + dy) * pip.aspect : 0)
                    place(startX, startY, w)
                    break
                }
                case "tl": {                       // bottom-right held
                    const w = Math.max(startW - dx, (startH - dy) * pip.aspect)
                    const wc = Math.max(pip.minFracW * pip.areaW, Math.min(pip.maxFracW * pip.areaW, w))
                    place(startX + startW - wc, startY + startH - wc / pip.aspect, wc)
                    break
                }
                case "tr": {                       // bottom-left held
                    const w = Math.max(startW + dx, (startH - dy) * pip.aspect)
                    const wc = Math.max(pip.minFracW * pip.areaW, Math.min(pip.maxFracW * pip.areaW, w))
                    place(startX, startY + startH - wc / pip.aspect, wc)
                    break
                }
                case "bl": {                       // top-right held
                    const w = Math.max(startW - dx, (startH + dy) * pip.aspect)
                    const wc = Math.max(pip.minFracW * pip.areaW, Math.min(pip.maxFracW * pip.areaW, w))
                    place(startX + startW - wc, startY, wc)
                    break
                }
                }
            }
            onReleased: {
                if (mode !== "none") pip.persist()
                mode = "none"
            }
            onCanceled: { mode = "none" }
        }
    }

    // Capture empty-state: no cameras enabled.
    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(6)
        visible: !root._replay && root._liveCameras.length === 0 && root._liveImpact === null
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

    // ── Telestrator open button (in the gutter, off the footage) ──────────────
    // A ">" chevron that opens the palette; shown only while collapsed (the
    // matching "<" collapse button lives on the palette itself). Docked top-left
    // in the reserved gutter so the affordance never sits on the video.
    Rectangle {
        id: annoOpen
        visible: root._annotate && !AnnotationTool.paletteOpen
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Math.max(Theme.sp(4), (root._leftGutter - width) / 2)
        anchors.topMargin: Theme.sp(10)
        width: Theme.sp(32); height: Theme.sp(32)
        radius: Theme.radius
        z: 50
        color: annoOpenMa.containsMouse ? Theme.colorBg2 : "transparent"
        border.width: 1
        border.color: Theme.colorBorderMid
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        PpAnnotationIcon {
            anchors.centerIn: parent
            width: Theme.sp(17); height: Theme.sp(17)
            kind: "chevronRight"
            iconColor: Theme.colorText2
        }
        PpPressable {
            id: annoOpenMa
            onClicked: AnnotationTool.paletteOpen = true
        }
    }

    // ── Telestrator palette (Analyse only) ────────────────────────────────────
    // Centred over the camera cluster (gutter edge → tilesEnd's left edge) so its
    // position reads as "applies to all cameras" — without moving the left-aligned
    // tiles. Drives the AnnotationTool state every tile's PpAnnotationLayer reads.
    PpAnnotationToolbar {
        id: annoBar
        // cluster spans root-x [_leftGutter, _leftGutter + tilesEnd.x - spacing];
        // place this bar's centre at the cluster centre.
        x: root._leftGutter + (tilesEnd.x - tilesRow.spacing - width) / 2
        anchors.top: parent.top
        anchors.topMargin: Theme.sp(10)
        z: 50
        visible: root._annotate && AnnotationTool.paletteOpen
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
}
