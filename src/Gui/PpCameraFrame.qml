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

// Reusable camera video frame — the one video view used by every rail screen
// (Wrist, swing analysis, the Play capture tab, …). Just the aspect-locked
// frame and its in-frame overlays; NO surrounding controls. Which overlays
// are active is configured per screen via the show* flags below — the same
// frame shows skeleton-only on the Wrist screen and the full hitting-area +
// ball chrome on capture screens.
//
// instance is nullable: a tile can be shown for a session-enabled camera
// before it is connected (dim "Not connected" placeholder) and subscribes to
// the instance's frame feed as soon as the instance arrives.

import QtQuick
import PinPointStudio
import QtMultimedia

Item {
    id: root

    // Live CameraInstance, or null while the camera is not connected.
    property QtObject instance: null

    // Replay-stream mode: when >= 0 this tile renders shotReplay stream N — the
    // reviewed swing's OWN video (disk-backed, source-agnostic) — instead of a
    // live CameraInstance. Default -1 = live-camera tile (instance path).
    property int replayStreamIndex: -1
    readonly property bool _isReplay: replayStreamIndex >= 0

    // Replay-overlay source — resolves to the disk replay (Review tile) or the
    // live in-window transient (Capture tile), so the analyzed skeleton/club
    // overlay reads ONE set of values regardless of which surface is driving it.
    readonly property bool _replayActive:     _isReplay ? shotReplay.active        : shotProcessor.isReplaying
    readonly property var  _replayDetail:     _isReplay ? shotReplay.analysisDetail : shotProcessor.replayAnalysisDetail
    readonly property real _replayPlayheadUs: _isReplay ? shotReplay.positionUs     : shotProcessor.replayPositionUs
    readonly property int  _replayPerspective: _isReplay
            ? (shotReplay.streams[replayStreamIndex] !== undefined
               ? shotReplay.streams[replayStreamIndex].perspective : -1)
            : (instance ? instance.perspective : -1)

    // Camera name shown in-frame (muted overlay; also the placeholder title).
    property string displayName: ""

    // ── Per-screen overlay configuration ────────────────────────────────────
    property bool showPoseOverlay:      true   // skeleton canvas
    property bool showHittingArea:      true   // ROI overlay + detected ball circle
    property bool showHittingAreaHint:  false  // faint label-free ROI outline (live pose)
    property bool roiEditable:          false  // enables the ROI drag-select interaction
    property bool showPerspectiveBadge: true
    property bool showStatsOverlay:     true   // resolution / fps
    property bool showReplayBadge:      true
    property bool showReplayOverlay:    true   // analyzed skeleton + club shaft during replay

    // Skeleton edge definitions, shared by the live pose canvas and the
    // replay overlay — colours mapped to theme tokens. Left-body edges use
    // colorGood; right-body use colorAccent; mid-line connections use colorWarn.
    readonly property var kSkeletonEdges: [
        {a:0,  b:1,  side:"good"}, {a:0,  b:2,  side:"accent"},
        {a:1,  b:3,  side:"good"}, {a:2,  b:4,  side:"accent"},
        {a:0,  b:5,  side:"good"}, {a:0,  b:6,  side:"accent"},
        {a:5,  b:6,  side:"warn"},
        {a:5,  b:7,  side:"good"}, {a:7,  b:9,  side:"good"},
        {a:6,  b:8,  side:"accent"}, {a:8,  b:10, side:"accent"},
        {a:5,  b:11, side:"good"}, {a:6,  b:12, side:"accent"},
        {a:11, b:12, side:"warn"},
        {a:11, b:13, side:"good"}, {a:13, b:15, side:"good"},
        {a:12, b:14, side:"accent"}, {a:14, b:16, side:"accent"}
    ]

    // ROI editor drag state (internal — driven by the unified MouseArea
    // below). roiDragging is true only while a NEW rectangle is being drawn
    // (rubber band); move/resize manipulate the committed rect live.
    property point roiDragStart:  Qt.point(0, 0)
    property point roiDragEnd:    Qt.point(0, 0)
    property bool  roiDragging:   false

    readonly property bool roiIsSet: instance !== null
                                     && instance.roi.width > 0 && instance.roi.height > 0

    // The video item's inset inside the frame border. Every overlay maps
    // video coordinates through this ONE value — hardcoded 2s drift from
    // Theme.sp(2) as soon as the font scale leaves 1.0.
    readonly property real videoInset: Theme.sp(2)

    // Pre-connect placeholder aspect — hosts with a camera-list entry pass
    // its crop-aware initialWidth/initialHeight so a disconnected tile
    // already opens at the aspect the stream will have once connected.
    property real placeholderAspect: 16.0 / 9.0

    // Video aspect ratio — used to centre the frame rect within the allocated
    // slot. Falls back to placeholderAspect while no instance/frame exists.
    readonly property real videoAspect: (instance && instance.frameWidth > 0
                                                     && instance.frameHeight > 0)
                                         ? instance.frameWidth / instance.frameHeight
                                         : placeholderAspect

    // ── Frame subscription ───────────────────────────────────────────────────
    // The CameraInstance publishes every display/replay frame to all subscribed
    // views, so any number of frames can show the same camera at once (screens
    // live side by side in a StackLayout, all instantiated). Subscribe when the
    // instance arrives, unsubscribe from the old one on reassignment and on
    // destruction. _subscribed tracks the instance we registered with; it goes
    // null automatically if that instance is destroyed (QML QObject tracking),
    // and the C++ side's QPointer list prunes our sinks in that case too.
    property QtObject _subscribed: null
    function _syncSubscription() {
        if (root._subscribed === root.instance)
            return
        if (root._subscribed) {
            root._subscribed.removeVideoSink(videoOut.videoSink)
            root._subscribed.removeBayerItem(bayerView)
        }
        root._subscribed = root.instance
        if (root._subscribed) {
            root._subscribed.addVideoSink(videoOut.videoSink)
            root._subscribed.addBayerItem(bayerView)
        }
    }
    Component.onCompleted: {
        _syncSubscription()
        // Replay tiles bind their VideoOutput sink to the disk-replay stream
        // (persists across loads; rebinds the live player immediately).
        if (root._isReplay)
            shotReplay.setVideoSink(root.replayStreamIndex, videoOut.videoSink)
    }
    onInstanceChanged:     _syncSubscription()
    Component.onDestruction: {
        if (root._subscribed) {
            root._subscribed.removeVideoSink(videoOut.videoSink)
            root._subscribed.removeBayerItem(bayerView)
        }
    }

    Rectangle {
        id: frameRect
        anchors.centerIn: parent
        width:  Math.min(parent.width, parent.height * root.videoAspect)
        height: width / root.videoAspect
        color: Theme.colorBg2
        radius: Theme.radius
        border.width: 1
        border.color: Theme.colorBorderMid

        VideoOutput {
            id: videoOut
            anchors.fill: parent
            anchors.margins: root.videoInset
            fillMode: VideoOutput.PreserveAspectFit
            visible: root._isReplay || (root.instance !== null && !root.instance.needsDebayer)
        }

        BayerVideoItem {
            id: bayerView
            anchors.fill: parent
            anchors.margins: root.videoInset
            visible: root.instance !== null && root.instance.needsDebayer
        }

        // ── Placeholder states ────────────────────────────────────────────
        // Not connected: dim frame with the camera name; the layout doesn't
        // jump when video starts. Connected but idle: "No camera feed".
        // (Replay tiles never show this — their video is disk-backed.)
        Column {
            anchors.centerIn: parent
            spacing: Theme.sp(4)
            visible: root.instance === null && !root._isReplay
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.displayName !== ""
                text: root.displayName
                color: Theme.colorText2
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Not connected")
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
            }
        }

        Text {
            anchors.centerIn: parent
            visible: root.instance !== null && !root.instance.isRecording
            text: qsTr("No camera feed")
            color: Theme.colorText3
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzBody
        }

        // ── Camera name (muted, bottom-left, only while connected) ─────────
        Text {
            visible: root.instance !== null && root.displayName !== ""
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.margins: Theme.sp(8)
            text: root.displayName
            color: Theme.colorText3
            font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
        }

        // ── Perspective badge (top-left overlay) ──────────────────────────
        Rectangle {
            visible: root.showPerspectiveBadge
                     && root.instance !== null
                     && root.instance.perspective !== CameraInstance.None
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Theme.sp(8)
            width: perspBadgeText.implicitWidth + Theme.sp(10)
            height: Theme.sp(20)
            radius: Theme.radius
            color: Theme.colorAccentMid
            border.width: 1
            border.color: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.4)

            Text {
                id: perspBadgeText
                anchors.centerIn: parent
                text: !root.instance ? ""
                    : root.instance.perspective === CameraInstance.DownTheLine ? "DTL"
                    : root.instance.perspective === CameraInstance.FaceOn ? "Face On"
                    : "Other"
                color: Theme.colorAccent
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.weight: Font.Normal
                font.letterSpacing: Theme.trackingData
            }
        }

        // ── Resolution / FPS overlay (bottom-right) ──────────────────────
        Rectangle {
            visible: root.showStatsOverlay && root.instance !== null
                     && root.instance.isRecording && root.instance.frameWidth > 0
            anchors.bottom: parent.bottom
            anchors.right:  parent.right
            anchors.margins: Theme.sp(8)
            width:  resLabel.implicitWidth + Theme.sp(10)
            height: Theme.sp(18)
            radius: Theme.radius - 1
            color: Qt.rgba(0, 0, 0, 0.55)

            Text {
                id: resLabel
                anchors.centerIn: parent
                text: !root.instance ? ""
                    : root.instance.frameWidth + "x"
                      + root.instance.frameHeight + "  "
                      + (root.instance.configuredFps > 0
                             ? root.instance.configuredFps
                             : root.instance.cameraFps).toFixed(0) + " fps"
                color: Theme.colorText
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
            }
        }

        // ── Skeleton overlay ──────────────────────────────────────────────
        Canvas {
            id: skeletonCanvas
            anchors.fill: parent
            // poseEnabled: hide immediately when live pose detection is toggled
            // off — don't wait for the cleared-keypoints repaint.
            visible: root.showPoseOverlay
                     && root.instance !== null && root.instance.isRecording
                     && root.instance.poseEnabled

            Connections {
                target: root.instance
                function onPoseKeypointsChanged() { skeletonCanvas.requestPaint() }
            }
            onVisibleChanged: if (visible) requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                if (!root.instance)
                    return
                var kps = root.instance.poseKeypoints
                if (!kps || kps.length < 17)
                    return

                // Both branches include the video item's inset inside the
                // frame: the canvas fills the FRAME, while contentRect /
                // bayerView dims are in the inset video item's space.
                var cr = root.instance.needsDebayer
                    ? Qt.rect(root.videoInset, root.videoInset,
                              bayerView.width, bayerView.height)
                    : Qt.rect(root.videoInset + videoOut.contentRect.x,
                              root.videoInset + videoOut.contentRect.y,
                              videoOut.contentRect.width,
                              videoOut.contentRect.height)
                if (cr.width <= 0 || cr.height <= 0)
                    return

                var kMinScore = 0.25
                var cGood   = Qt.rgba(Theme.colorGood.r,   Theme.colorGood.g,   Theme.colorGood.b,   1)
                var cAccent = Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 1)
                var cWarn   = Qt.rgba(Theme.colorWarn.r,   Theme.colorWarn.g,   Theme.colorWarn.b,   1)
                var cText   = Qt.rgba(Theme.colorText.r,   Theme.colorText.g,   Theme.colorText.b,   1)

                for (var i = 0; i < root.kSkeletonEdges.length; ++i) {
                    var e = root.kSkeletonEdges[i]
                    var ka = kps[e.a], kb = kps[e.b]
                    if (ka.score < kMinScore || kb.score < kMinScore)
                        continue
                    ctx.globalAlpha = 0.4 + 0.6 * Math.min(ka.score, kb.score)
                    ctx.strokeStyle = e.side === "good"   ? cGood
                                    : e.side === "accent" ? cAccent
                                    :                       cWarn
                    ctx.lineWidth   = 2
                    ctx.lineCap     = "round"
                    ctx.beginPath()
                    ctx.moveTo(ka.x * cr.width + cr.x, ka.y * cr.height + cr.y)
                    ctx.lineTo(kb.x * cr.width + cr.x, kb.y * cr.height + cr.y)
                    ctx.stroke()
                }

                for (var j = 0; j < kps.length; ++j) {
                    var kp = kps[j]
                    if (kp.score < kMinScore)
                        continue
                    var s = kp.score
                    ctx.fillStyle   = s >= 0.6 ? cText : cWarn
                    ctx.globalAlpha = 0.5 + 0.5 * s
                    ctx.beginPath()
                    ctx.arc(kp.x * cr.width + cr.x, kp.y * cr.height + cr.y, 4, 0, Math.PI * 2)
                    ctx.fill()
                }

                ctx.globalAlpha = 1.0
            }
        }

        // ── Replay overlay: analyzed skeleton + club shaft ────────────────
        // Drawn from the shot's analyzed detail (offline ViTPose pose2d + the
        // ShaftTracker club track), scrubbing with the replay playhead. The
        // live skeletonCanvas is recording-gated, so the two never co-draw.
        // Face-on tiles only — the tracks were measured on that camera.
        Canvas {
            id: replayOverlay
            anchors.fill: parent
            z: 21
            visible: root.showReplayOverlay
                     && root._replayActive
                     && root._replayPerspective === 2
                     && (_poseFrames.length > 0 || _clubSamples.length > 0)

            // Bound from the active replay's detail — pose kp flat [x,y,c]×17 and
            // club samples with normalized grip/head (toAnalysisDetail shapes).
            // root._replayDetail resolves to disk (Review) or in-window (Capture).
            readonly property var _poseFrames: {
                var d = root._replayDetail
                return (d && d.pose2d && d.pose2d.frames) ? d.pose2d.frames : []
            }
            readonly property var _clubSamples: {
                var d = root._replayDetail
                return (d && d.club && d.club.valid && d.club.samples) ? d.club.samples : []
            }
            readonly property int kTrail: 10

            // Greatest index with t_us <= t (−1 when empty).
            function _indexFor(arr, t) {
                var hi = arr.length - 1
                if (hi < 0 || t < arr[0].t_us) return hi < 0 ? -1 : 0
                if (t >= arr[hi].t_us) return hi
                var lo = 0
                while (hi - lo > 1) {
                    var mid = (lo + hi) >> 1
                    if (arr[mid].t_us <= t) lo = mid; else hi = mid
                }
                return lo
            }

            // Repaint as the playhead advances — from whichever surface drives it.
            Connections {
                target: shotProcessor
                enabled: !root._isReplay
                function onReplayPositionChanged() {
                    if (replayOverlay.visible) replayOverlay.requestPaint()
                }
            }
            Connections {
                target: shotReplay
                enabled: root._isReplay
                function onPositionChanged() {
                    if (replayOverlay.visible) replayOverlay.requestPaint()
                }
            }
            onVisibleChanged: if (visible) requestPaint()
            on_PoseFramesChanged:  if (visible) requestPaint()
            on_ClubSamplesChanged: if (visible) requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                // Live tile needs an instance; replay tiles paint over videoOut.
                if (!root._isReplay && !root.instance)
                    return
                var cr = (!root._isReplay && root.instance.needsDebayer)
                    ? Qt.rect(root.videoInset, root.videoInset,
                              bayerView.width, bayerView.height)
                    : Qt.rect(root.videoInset + videoOut.contentRect.x,
                              root.videoInset + videoOut.contentRect.y,
                              videoOut.contentRect.width,
                              videoOut.contentRect.height)
                if (cr.width <= 0 || cr.height <= 0)
                    return
                var t = root._replayPlayheadUs
                var kMinScore = 0.25
                var cGood   = Qt.rgba(Theme.colorGood.r,   Theme.colorGood.g,   Theme.colorGood.b,   1)
                var cAccent = Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 1)
                var cWarn   = Qt.rgba(Theme.colorWarn.r,   Theme.colorWarn.g,   Theme.colorWarn.b,   1)

                // Analyzed skeleton — live style, muted (it sits over replay
                // footage; half-alpha chrome).
                var pi = _indexFor(_poseFrames, t)
                if (pi >= 0) {
                    var kp = _poseFrames[pi].kp
                    ctx.lineWidth = 2
                    ctx.lineCap   = "round"
                    for (var i = 0; i < root.kSkeletonEdges.length; ++i) {
                        var e = root.kSkeletonEdges[i]
                        var ca = kp[e.a * 3 + 2], cb = kp[e.b * 3 + 2]
                        if (ca < kMinScore || cb < kMinScore)
                            continue
                        ctx.globalAlpha = 0.55 * (0.4 + 0.6 * Math.min(ca, cb))
                        ctx.strokeStyle = e.side === "good"   ? cGood
                                        : e.side === "accent" ? cAccent
                                        :                       cWarn
                        ctx.beginPath()
                        ctx.moveTo(kp[e.a * 3] * cr.width + cr.x, kp[e.a * 3 + 1] * cr.height + cr.y)
                        ctx.lineTo(kp[e.b * 3] * cr.width + cr.x, kp[e.b * 3 + 1] * cr.height + cr.y)
                        ctx.stroke()
                    }
                }

                // Club: fading head trail, then the current shaft line.
                var ci = _indexFor(_clubSamples, t)
                if (ci >= 0) {
                    ctx.strokeStyle = cAccent
                    ctx.lineCap     = "round"
                    var k0 = Math.max(0, ci - kTrail)
                    for (var k = k0; k < ci; ++k) {
                        var h0 = _clubSamples[k].head, h1 = _clubSamples[k + 1].head
                        ctx.globalAlpha = 0.45 * (k + 1 - k0) / (ci - k0 + 1)
                        ctx.lineWidth   = 2
                        ctx.beginPath()
                        ctx.moveTo(h0[0] * cr.width + cr.x, h0[1] * cr.height + cr.y)
                        ctx.lineTo(h1[0] * cr.width + cr.x, h1[1] * cr.height + cr.y)
                        ctx.stroke()
                    }
                    var s  = _clubSamples[ci]
                    var gx = s.grip[0] * cr.width + cr.x, gy = s.grip[1] * cr.height + cr.y
                    var hx = s.head[0] * cr.width + cr.x, hy = s.head[1] * cr.height + cr.y
                    ctx.globalAlpha = 0.35 + 0.5 * s.conf
                    ctx.lineWidth   = 2
                    ctx.beginPath()
                    ctx.moveTo(gx, gy)
                    ctx.lineTo(hx, hy)
                    ctx.stroke()
                    ctx.fillStyle = cAccent
                    ctx.beginPath()
                    ctx.arc(hx, hy, 4, 0, Math.PI * 2)
                    ctx.fill()
                }
                ctx.globalAlpha = 1.0
            }
        }

        // ── Faint hitting-area hint ───────────────────────────────────────
        // Label-free, low-alpha outline of the ball detector ROI — enough to
        // see where the detector is looking while live pose runs, without the
        // full editor chrome (which showHittingArea owns; hint yields to it).
        Rectangle {
            id: roiHint
            visible: root.showHittingAreaHint && !root.showHittingArea
                     && root.roiIsSet
            z: 19
            color: Theme.colorWarnLight
            border.color: Qt.alpha(Theme.colorWarn, 0.35)
            border.width: 1

            x: roiOverlay.crX + (root.instance ? root.instance.roi.x * roiOverlay.crW : 0)
            y: roiOverlay.crY + (root.instance ? root.instance.roi.y * roiOverlay.crH : 0)
            width:  root.instance ? root.instance.roi.width  * roiOverlay.crW : 0
            height: root.instance ? root.instance.roi.height * roiOverlay.crH : 0
        }

        // ── Persistent ROI overlay ────────────────────────────────────────
        Rectangle {
            id: roiOverlay
            visible: root.showHittingArea && root.roiIsSet && !root.roiDragging
            z: 20
            color: "transparent"
            border.color: Theme.colorWarn
            border.width: 2

            property real crX: root.videoInset + (root.instance && root.instance.needsDebayer
                               ? 0 : videoOut.contentRect.x)
            property real crY: root.videoInset + (root.instance && root.instance.needsDebayer
                               ? 0 : videoOut.contentRect.y)
            property real crW: root.instance && root.instance.needsDebayer
                               ? bayerView.width : videoOut.contentRect.width
            property real crH: root.instance && root.instance.needsDebayer
                               ? bayerView.height : videoOut.contentRect.height

            x: crX + (root.instance ? root.instance.roi.x * crW : 0)
            y: crY + (root.instance ? root.instance.roi.y * crH : 0)
            width:  root.instance ? root.instance.roi.width  * crW : 0
            height: root.instance ? root.instance.roi.height * crH : 0

            Text {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: Theme.sp(3)
                text: qsTr("Hitting Area")
                color: Theme.colorWarn
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.weight: Font.Normal
                font.letterSpacing: Theme.trackingData
            }

            // Corner handle squares — editor affordance only (drag handled by
            // the unified MouseArea below; same pattern as the crop editor).
            readonly property int hs: Theme.sp(10)
            Rectangle { visible: root.roiEditable; width: roiOverlay.hs; height: roiOverlay.hs; x: -roiOverlay.hs/2;                    y: -roiOverlay.hs/2;                     color: Theme.colorWarn; border.width: 1; border.color: "black" }
            Rectangle { visible: root.roiEditable; width: roiOverlay.hs; height: roiOverlay.hs; x: roiOverlay.width-roiOverlay.hs/2;    y: -roiOverlay.hs/2;                     color: Theme.colorWarn; border.width: 1; border.color: "black" }
            Rectangle { visible: root.roiEditable; width: roiOverlay.hs; height: roiOverlay.hs; x: -roiOverlay.hs/2;                    y: roiOverlay.height-roiOverlay.hs/2;    color: Theme.colorWarn; border.width: 1; border.color: "black" }
            Rectangle { visible: root.roiEditable; width: roiOverlay.hs; height: roiOverlay.hs; x: roiOverlay.width-roiOverlay.hs/2;    y: roiOverlay.height-roiOverlay.hs/2;    color: Theme.colorWarn; border.width: 1; border.color: "black" }
        }

        // ── Rubber-band while dragging ────────────────────────────────────
        Rectangle {
            id: rubberBand
            visible: root.roiDragging
            z: 25
            x: Math.min(root.roiDragStart.x, root.roiDragEnd.x)
            y: Math.min(root.roiDragStart.y, root.roiDragEnd.y)
            width:  Math.abs(root.roiDragEnd.x - root.roiDragStart.x)
            height: Math.abs(root.roiDragEnd.y - root.roiDragStart.y)
            color: Theme.colorWarnLight
            border.color: Theme.colorWarn
            border.width: 2
        }

        // ── Detected ball circle ──────────────────────────────────────────
        Rectangle {
            id: ballCircle
            visible: root.showHittingArea && root.roiIsSet
                     && root.instance !== null && root.instance.ballDetected
            z: 22
            color: "transparent"
            border.color: Theme.colorGood
            border.width: 2

            property real crX: root.videoInset + (root.instance && root.instance.needsDebayer
                               ? 0 : videoOut.contentRect.x)
            property real crY: root.videoInset + (root.instance && root.instance.needsDebayer
                               ? 0 : videoOut.contentRect.y)
            property real crW: root.instance && root.instance.needsDebayer
                               ? bayerView.width : videoOut.contentRect.width
            property real crH: root.instance && root.instance.needsDebayer
                               ? bayerView.height : videoOut.contentRect.height

            property real screenR: Math.max(4, (root.instance ? root.instance.ballRadius : 0) * crW)

            x: crX + (root.instance ? root.instance.ballX : 0) * crW - screenR
            y: crY + (root.instance ? root.instance.ballY : 0) * crH - screenR
            width:  screenR * 2
            height: screenR * 2
            radius: screenR
        }

        // ── Replay badge ──────────────────────────────────────────────────
        Rectangle {
            id: replayBadge
            visible: root.showReplayBadge
                     && ((root.instance !== null && root.instance.isReplaying)
                         || (root._isReplay && shotReplay.active))
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.sp(12)
            width: replayRow.implicitWidth + Theme.sp(24)
            height: Theme.sp(28)
            radius: Theme.sp(14)
            color: Qt.rgba(Theme.colorBg.r, Theme.colorBg.g, Theme.colorBg.b, 0.8)
            border.width: 1
            border.color: Theme.colorBorderMid
            z: 30

            Row {
                id: replayRow
                anchors.centerIn: parent
                spacing: Theme.sp(6)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("REPLAY")
                    color: Theme.colorWarn
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    font.letterSpacing: Theme.trackingMicro
                    font.capitalization: Font.AllUppercase
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "¼×"
                    color: Theme.colorAccent
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzLabel
                }

                // Muted skip hint — ESC cancels the replay (Main.qml Shortcut).
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("ESC")
                    color: Theme.colorText3
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                }
            }

            SequentialAnimation on opacity {
                running: replayBadge.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.5; duration: 800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
            }
        }

        // ── Unified ROI editor MouseArea (crop-editor interaction model) ──
        // Always-on while roiEditable: drag inside the rect to move it, drag
        // a corner handle to resize, drag anywhere else to draw a new rect.
        // Move/resize update the instance live (no persistence churn); the
        // committed rect persists once on release via the manager.
        MouseArea {
            id: roiEditArea
            anchors.fill: parent
            enabled: root.roiEditable && root.instance !== null
            visible: enabled
            hoverEnabled: true
            preventStealing: true
            z: 30

            readonly property real hr: Theme.sp(16)   // handle hit radius
            property string dragMode: "none"          // none|new|move|tl|tr|bl|br
            property real origX: 0
            property real origY: 0
            property real origW: 0
            property real origH: 0

            function hitZone(mx, my) {
                if (!root.roiIsSet) return "new"
                var rx = roiOverlay.x,     ry = roiOverlay.y
                var rw = roiOverlay.width, rh = roiOverlay.height
                var h  = hr
                if (Math.abs(mx - rx)      < h && Math.abs(my - ry)      < h) return "tl"
                if (Math.abs(mx - (rx+rw)) < h && Math.abs(my - ry)      < h) return "tr"
                if (Math.abs(mx - rx)      < h && Math.abs(my - (ry+rh)) < h) return "bl"
                if (Math.abs(mx - (rx+rw)) < h && Math.abs(my - (ry+rh)) < h) return "br"
                if (mx > rx && mx < rx+rw && my > ry && my < ry+rh)           return "move"
                return "new"                          // outside = draw a replacement
            }

            cursorShape: {
                switch (hitZone(mouseX, mouseY)) {
                case "tl": case "br": return Qt.SizeFDiagCursor
                case "tr": case "bl": return Qt.SizeBDiagCursor
                case "move":          return Qt.SizeAllCursor
                default:              return Qt.CrossCursor
                }
            }

            onPressed: (mouse) => {
                dragMode = hitZone(mouse.x, mouse.y)
                root.roiDragStart = Qt.point(mouse.x, mouse.y)
                if (dragMode === "new") {
                    root.roiDragEnd  = Qt.point(mouse.x, mouse.y)
                    root.roiDragging = true
                } else if (root.instance) {
                    var r = root.instance.roi
                    origX = r.x; origY = r.y; origW = r.width; origH = r.height
                }
            }

            onPositionChanged: (mouse) => {
                if (dragMode === "none" || !root.instance) return
                if (dragMode === "new") {
                    root.roiDragEnd = Qt.point(mouse.x, mouse.y)
                    return
                }
                var crW = roiOverlay.crW, crH = roiOverlay.crH
                if (crW <= 0 || crH <= 0) return
                var dx = (mouse.x - root.roiDragStart.x) / crW
                var dy = (mouse.y - root.roiDragStart.y) / crH
                var ox = origX, oy = origY, ow = origW, oh = origH
                var nx, ny, nw, nh
                switch (dragMode) {
                case "move":
                    nx = Math.max(0, Math.min(1.0 - ow, ox + dx))
                    ny = Math.max(0, Math.min(1.0 - oh, oy + dy))
                    root.instance.setRoi(Qt.rect(nx, ny, ow, oh))
                    break
                case "tl":
                    nx = Math.max(0,    Math.min(ox + ow - 0.02, ox + dx))
                    ny = Math.max(0,    Math.min(oy + oh - 0.02, oy + dy))
                    nw = Math.max(0.02, ow - (nx - ox))
                    nh = Math.max(0.02, oh - (ny - oy))
                    root.instance.setRoi(Qt.rect(nx, ny, nw, nh))
                    break
                case "tr":
                    ny = Math.max(0,    Math.min(oy + oh - 0.02, oy + dy))
                    nw = Math.max(0.02, Math.min(1.0 - ox, ow + dx))
                    nh = Math.max(0.02, oh - (ny - oy))
                    root.instance.setRoi(Qt.rect(ox, ny, nw, nh))
                    break
                case "bl":
                    nx = Math.max(0,    Math.min(ox + ow - 0.02, ox + dx))
                    nw = Math.max(0.02, ow - (nx - ox))
                    nh = Math.max(0.02, Math.min(1.0 - oy, oh + dy))
                    root.instance.setRoi(Qt.rect(nx, oy, nw, nh))
                    break
                case "br":
                    nw = Math.max(0.02, Math.min(1.0 - ox, ow + dx))
                    nh = Math.max(0.02, Math.min(1.0 - oy, oh + dy))
                    root.instance.setRoi(Qt.rect(ox, oy, nw, nh))
                    break
                }
            }

            onReleased: (mouse) => {
                if (!root.instance) { dragMode = "none"; root.roiDragging = false; return }

                if (dragMode === "new") {
                    root.roiDragging = false
                    var crX = roiOverlay.crX, crY = roiOverlay.crY
                    var crW = roiOverlay.crW, crH = roiOverlay.crH
                    if (crW > 0 && crH > 0) {
                        var x1  = Math.min(root.roiDragStart.x, mouse.x)
                        var y1  = Math.min(root.roiDragStart.y, mouse.y)
                        var x2  = Math.max(root.roiDragStart.x, mouse.x)
                        var y2  = Math.max(root.roiDragStart.y, mouse.y)
                        var nx  = Math.max(0, Math.min(1, (x1 - crX) / crW))
                        var ny  = Math.max(0, Math.min(1, (y1 - crY) / crH))
                        var nx2 = Math.max(0, Math.min(1, (x2 - crX) / crW))
                        var ny2 = Math.max(0, Math.min(1, (y2 - crY) / crH))
                        // Via the manager so the hitting area persists per
                        // camera (restored on connect when fixed in place).
                        cameraManager.setBallRoi(root.instance, Qt.rect(nx, ny, nx2 - nx, ny2 - ny))
                    }
                } else if (dragMode !== "none") {
                    // Move/resize updated the instance live — persist once.
                    cameraManager.setBallRoi(root.instance, root.instance.roi)
                }
                dragMode = "none"
            }
        }
    }
}
