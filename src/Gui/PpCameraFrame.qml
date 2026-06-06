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

    // Camera name shown in-frame (muted overlay; also the placeholder title).
    property string displayName: ""

    // ── Per-screen overlay configuration ────────────────────────────────────
    property bool showPoseOverlay:      true   // skeleton canvas
    property bool showHittingArea:      true   // ROI overlay + detected ball circle
    property bool roiEditable:          false  // enables the ROI drag-select interaction
    property bool showPerspectiveBadge: true
    property bool showStatsOverlay:     true   // resolution / fps
    property bool showReplayBadge:      true

    // ROI selection state — driven externally (set roiSelecting true to arm a
    // drag-select; it clears itself when the drag completes). Only honoured
    // when roiEditable is true.
    property bool  roiSelecting:  false
    property point roiDragStart:  Qt.point(0, 0)
    property point roiDragEnd:    Qt.point(0, 0)
    property bool  roiDragging:   false

    readonly property bool roiIsSet: instance !== null
                                     && instance.roi.width > 0 && instance.roi.height > 0

    // Video aspect ratio — used to centre the frame rect within the allocated
    // slot. Defaults to 16:9 before the first frame arrives.
    readonly property real videoAspect: (instance && instance.frameWidth > 0
                                                     && instance.frameHeight > 0)
                                         ? instance.frameWidth / instance.frameHeight
                                         : 16.0 / 9.0

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
    Component.onCompleted: _syncSubscription()
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
            anchors.margins: Theme.sp(2)
            fillMode: VideoOutput.PreserveAspectFit
            visible: root.instance !== null && !root.instance.needsDebayer
        }

        BayerVideoItem {
            id: bayerView
            anchors.fill: parent
            anchors.margins: Theme.sp(2)
            visible: root.instance !== null && root.instance.needsDebayer
        }

        // ── Placeholder states ────────────────────────────────────────────
        // Not connected: dim frame with the camera name; the layout doesn't
        // jump when video starts. Connected but idle: "No camera feed".
        Column {
            anchors.centerIn: parent
            spacing: Theme.sp(4)
            visible: root.instance === null
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
                     && root.instance !== null && root.instance.perspective > 0
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
                    : root.instance.perspective === 1 ? "DTL"
                    : root.instance.perspective === 2 ? "Face On"
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

            // Skeleton edge definitions — colours mapped to theme tokens.
            // Left-body edges use colorGood; right-body use colorAccent;
            // mid-line connections use colorWarn.
            readonly property var kEdges: [
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

                var cr = root.instance.needsDebayer
                    ? Qt.rect(0, 0, bayerView.width, bayerView.height)
                    : videoOut.contentRect
                if (cr.width <= 0 || cr.height <= 0)
                    return

                var kMinScore = 0.25
                var cGood   = Qt.rgba(Theme.colorGood.r,   Theme.colorGood.g,   Theme.colorGood.b,   1)
                var cAccent = Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 1)
                var cWarn   = Qt.rgba(Theme.colorWarn.r,   Theme.colorWarn.g,   Theme.colorWarn.b,   1)
                var cText   = Qt.rgba(Theme.colorText.r,   Theme.colorText.g,   Theme.colorText.b,   1)

                for (var i = 0; i < kEdges.length; ++i) {
                    var e = kEdges[i]
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

        // ── Persistent ROI overlay ────────────────────────────────────────
        Rectangle {
            id: roiOverlay
            visible: root.showHittingArea && root.roiIsSet && !root.roiSelecting
            z: 20
            color: "transparent"
            border.color: Theme.colorWarn
            border.width: 2

            property real crX: root.instance && root.instance.needsDebayer
                               ? 2 : (2 + videoOut.contentRect.x)
            property real crY: root.instance && root.instance.needsDebayer
                               ? 2 : (2 + videoOut.contentRect.y)
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

            property real crX: root.instance && root.instance.needsDebayer
                               ? 2 : (2 + videoOut.contentRect.x)
            property real crY: root.instance && root.instance.needsDebayer
                               ? 2 : (2 + videoOut.contentRect.y)
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
                     && root.instance !== null && root.instance.isReplaying
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
            }

            SequentialAnimation on opacity {
                running: replayBadge.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.5; duration: 800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
            }
        }

        // ── ROI drag-select MouseArea ─────────────────────────────────────
        MouseArea {
            anchors.fill: parent
            enabled: root.roiEditable && root.roiSelecting
            visible: root.roiEditable && root.roiSelecting
            cursorShape: Qt.CrossCursor
            z: 30

            onPressed: (mouse) => {
                root.roiDragStart = Qt.point(mouse.x, mouse.y)
                root.roiDragEnd   = Qt.point(mouse.x, mouse.y)
                root.roiDragging  = true
            }
            onPositionChanged: (mouse) => {
                if (root.roiDragging)
                    root.roiDragEnd = Qt.point(mouse.x, mouse.y)
            }
            onReleased: (mouse) => {
                root.roiDragging  = false
                root.roiSelecting = false

                if (!root.instance) return
                var crX = root.instance.needsDebayer
                          ? 2 : (2 + videoOut.contentRect.x)
                var crY = root.instance.needsDebayer
                          ? 2 : (2 + videoOut.contentRect.y)
                var crW = root.instance.needsDebayer
                          ? bayerView.width : videoOut.contentRect.width
                var crH = root.instance.needsDebayer
                          ? bayerView.height : videoOut.contentRect.height
                if (crW <= 0 || crH <= 0) return

                var x1  = Math.min(root.roiDragStart.x, mouse.x)
                var y1  = Math.min(root.roiDragStart.y, mouse.y)
                var x2  = Math.max(root.roiDragStart.x, mouse.x)
                var y2  = Math.max(root.roiDragStart.y, mouse.y)
                var nx  = Math.max(0, Math.min(1, (x1 - crX) / crW))
                var ny  = Math.max(0, Math.min(1, (y1 - crY) / crH))
                var nx2 = Math.max(0, Math.min(1, (x2 - crX) / crW))
                var ny2 = Math.max(0, Math.min(1, (y2 - crY) / crH))
                root.instance.setRoi(Qt.rect(nx, ny, nx2 - nx, ny2 - ny))
            }
        }
    }
}
