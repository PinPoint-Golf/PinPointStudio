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
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPoint
import QtMultimedia

Item {
    id: root

    property QtObject controller

    // ROI selection state
    property bool  roiSelecting:  false
    property point roiDragStart:  Qt.point(0, 0)
    property point roiDragEnd:    Qt.point(0, 0)
    property bool  roiDragging:   false

    readonly property bool roiIsSet: controller && controller.roi.width > 0 && controller.roi.height > 0

    Component.onCompleted: {
        controller.setVideoSink(videoOut.videoSink)
        controller.setBayerItem(bayerView)
    }
    onControllerChanged: if (controller) {
        controller.setVideoSink(videoOut.videoSink)
        controller.setBayerItem(bayerView)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.sp(6)

        // ── Camera frame ──────────────────────────────────────────────────────
        Rectangle {
            id: frameRect
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.colorBg2
            radius: Theme.radius
            border.width: 1
            border.color: Theme.colorBorderMid

            VideoOutput {
                id: videoOut
                anchors.fill: parent
                anchors.margins: Theme.sp(2)
                fillMode: VideoOutput.PreserveAspectFit
                visible: !root.controller.needsDebayer
            }

            BayerVideoItem {
                id: bayerView
                anchors.fill: parent
                anchors.margins: Theme.sp(2)
                visible: root.controller.needsDebayer
            }

            Label {
                anchors.centerIn: parent
                visible: !root.controller.isRecording
                text: qsTr("No camera feed")
                color: Theme.colorText3
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
            }

            // ── Perspective badge (top-left overlay) ──────────────────────────
            Rectangle {
                visible: root.controller.perspective > 0
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
                    text: root.controller.perspective === 1 ? "DTL"
                        : root.controller.perspective === 2 ? "Face On"
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
                visible: root.controller.isRecording && root.controller.frameWidth > 0
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
                    text: root.controller.frameWidth + "x"
                        + root.controller.frameHeight + "  "
                        + (root.controller.configuredFps > 0
                               ? root.controller.configuredFps
                               : root.controller.cameraFps).toFixed(0) + " fps"
                    color: Theme.colorText
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                }
            }

            // ── Skeleton overlay ──────────────────────────────────────────────
            Canvas {
                id: skeletonCanvas
                anchors.fill: parent
                visible: root.controller.isRecording

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
                    target: root.controller
                    function onPoseKeypointsChanged() { skeletonCanvas.requestPaint() }
                }

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var kps = root.controller.poseKeypoints
                    if (!kps || kps.length < 17)
                        return

                    var cr = root.controller.needsDebayer
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
                visible: root.roiIsSet && !root.roiSelecting
                z: 20
                color: "transparent"
                border.color: Theme.colorWarn
                border.width: 2

                property real crX: root.controller && root.controller.needsDebayer
                                   ? 2 : (2 + videoOut.contentRect.x)
                property real crY: root.controller && root.controller.needsDebayer
                                   ? 2 : (2 + videoOut.contentRect.y)
                property real crW: root.controller && root.controller.needsDebayer
                                   ? bayerView.width : videoOut.contentRect.width
                property real crH: root.controller && root.controller.needsDebayer
                                   ? bayerView.height : videoOut.contentRect.height

                x: crX + (root.controller ? root.controller.roi.x * crW : 0)
                y: crY + (root.controller ? root.controller.roi.y * crH : 0)
                width:  root.controller ? root.controller.roi.width  * crW : 0
                height: root.controller ? root.controller.roi.height * crH : 0

                Text {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Theme.sp(3)
                    text: "Hitting Area"
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
                visible: root.controller.ballDetected && root.roiIsSet
                z: 22
                color: "transparent"
                border.color: Theme.colorGood
                border.width: 2

                property real crX: root.controller && root.controller.needsDebayer
                                   ? 2 : (2 + videoOut.contentRect.x)
                property real crY: root.controller && root.controller.needsDebayer
                                   ? 2 : (2 + videoOut.contentRect.y)
                property real crW: root.controller && root.controller.needsDebayer
                                   ? bayerView.width : videoOut.contentRect.width
                property real crH: root.controller && root.controller.needsDebayer
                                   ? bayerView.height : videoOut.contentRect.height

                property real screenR: Math.max(4, root.controller.ballRadius * crW)

                x: crX + root.controller.ballX * crW - screenR
                y: crY + root.controller.ballY * crH - screenR
                width:  screenR * 2
                height: screenR * 2
                radius: screenR
            }

            // ── Replay badge ──────────────────────────────────────────────────
            Rectangle {
                id: replayBadge
                visible: root.controller.isReplaying
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
                        text: "REPLAY"
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
                    running: root.controller.isReplaying
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.5; duration: 800; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
                }
            }

            // ── ROI drag-select MouseArea ─────────────────────────────────────
            MouseArea {
                anchors.fill: parent
                enabled: root.roiSelecting
                visible: root.roiSelecting
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

                    var crX = (root.controller && root.controller.needsDebayer)
                              ? 2 : (2 + videoOut.contentRect.x)
                    var crY = (root.controller && root.controller.needsDebayer)
                              ? 2 : (2 + videoOut.contentRect.y)
                    var crW = (root.controller && root.controller.needsDebayer)
                              ? bayerView.width : videoOut.contentRect.width
                    var crH = (root.controller && root.controller.needsDebayer)
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
                    root.controller.setRoi(Qt.rect(nx, ny, nx2 - nx, ny2 - ny))
                }
            }
        }

        // ── Per-camera stats + controls ───────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(8)

            Label {
                visible: root.controller.deviceDescription !== ""
                text: root.controller.deviceDescription
                color: Theme.colorText3
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzLabel
                elide: Text.ElideRight
                Layout.maximumWidth: 140
            }

            // ── Perspective selector ──────────────────────────────────────────
            Row {
                spacing: 0

                Repeater {
                    model: [
                        { value: 1, label: "DTL",     leftR: true,  rightR: false },
                        { value: 2, label: "Face On",  leftR: false, rightR: false },
                        { value: 3, label: "Other",    leftR: false, rightR: true  }
                    ]
                    delegate: Rectangle {
                        readonly property bool active: root.controller.perspective === modelData.value
                        height: Theme.sp(20)
                        width: perspLabel.implicitWidth + Theme.sp(10)
                        topLeftRadius:     modelData.leftR  ? Theme.radius : 0
                        bottomLeftRadius:  modelData.leftR  ? Theme.radius : 0
                        topRightRadius:    modelData.rightR ? Theme.radius : 0
                        bottomRightRadius: modelData.rightR ? Theme.radius : 0
                        color: active ? Theme.colorAccent : Theme.colorSurface
                        border.width: 1
                        border.color: active ? Theme.colorAccent : Theme.colorBorderMid
                        Text {
                            id: perspLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            color: active ? Theme.colorBg : Theme.colorText2
                            font.family: Theme.fontBody
                            font.pixelSize: Theme.fontSzLabel
                            font.weight: Font.Normal
                        }
                        TapHandler {
                            onTapped: cameraManager.setPerspective(
                                root.controller,
                                active ? 0 : modelData.value)
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // ── ROI button ────────────────────────────────────────────────────
            Rectangle {
                height: Theme.sp(20)
                width: roiBtnLabel.implicitWidth + Theme.sp(10)
                radius: Theme.radius
                color: root.roiSelecting ? Theme.colorWarn
                     : root.roiIsSet     ? Theme.colorWarnLight
                     :                     Theme.colorSurface
                border.width: 1
                border.color: root.roiSelecting || root.roiIsSet
                              ? Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.5)
                              : Theme.colorBorderMid

                Text {
                    id: roiBtnLabel
                    anchors.centerIn: parent
                    text: qsTr("Hitting Area")
                    color: root.roiSelecting ? Theme.colorBg
                         : root.roiIsSet     ? Theme.colorWarn
                         :                     Theme.colorText2
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzLabel
                    font.weight: Font.Normal
                }
                TapHandler {
                    onTapped: {
                        if (root.roiSelecting) {
                            root.roiSelecting = false
                            root.roiDragging  = false
                        } else {
                            root.controller.clearRoi()
                            root.roiSelecting = true
                        }
                    }
                }
                HoverHandler { id: roiBtnHover; cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: roiBtnHover.hovered
                ToolTip.text: root.roiSelecting ? qsTr("Click to cancel — drag on video to define hitting area")
                            : root.roiIsSet     ? qsTr("Hitting area set — click to redefine")
                            :                     qsTr("Click then drag on video to define hitting area")
                ToolTip.delay: 600
            }

            // ── Ball detection badge ──────────────────────────────────────────
            Rectangle {
                visible: root.roiIsSet && root.controller.isRecording
                width: ballBadgeLabel.implicitWidth + Theme.sp(10)
                height: Theme.sp(20)
                radius: Theme.radius

                readonly property bool ballPresent: root.controller.ballPresencePercent > 30

                color: ballPresent ? Theme.colorGoodLight : Theme.colorSurface
                border.width: 1
                border.color: ballPresent
                              ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.3)
                              : Theme.colorBorderMid

                Text {
                    id: ballBadgeLabel
                    anchors.centerIn: parent
                    text: parent.ballPresent ? qsTr("Ball") : qsTr("No Ball")
                    color: parent.ballPresent ? Theme.colorGood : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzLabel
                    font.weight: Font.Normal
                }
            }

            // ── Rolling ball-presence percentage ─────────────────────────────
            Label {
                visible: root.roiIsSet && root.controller.isRecording
                text: root.controller.ballPresencePercent.toFixed(0) + "%"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
            }

            Item { Layout.fillWidth: true }

            Label {
                visible: root.controller.isRecording && root.controller.preprocessAvgMs > 0
                text: "Pre: " + root.controller.preprocessAvgMs.toFixed(1) + " ms"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
            }

            Label {
                visible: root.controller.isRecording && root.controller.cameraFps > 0
                text: "Cam: " + root.controller.cameraFps.toFixed(1) + " fps"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
            }

            Label {
                visible: root.controller.isRecording && root.controller.poseFps > 0
                text: "Pose: " + root.controller.poseAvgMs.toFixed(1) + " ms  "
                    + root.controller.poseFps.toFixed(1) + " fps"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
            }

            Label {
                visible: root.controller.isRecording && root.controller.ballAvgMs > 0
                text: "Ball: " + root.controller.ballAvgMs.toFixed(1) + " ms"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzLabel
            }

            // ── Pose model selector (Lightning / Thunder) ────────────────────
            Row {
                visible: root.controller.moveNetThunderAvailable
                spacing: 0

                Rectangle {
                    id: lightningBtn
                    height: Theme.sp(20)
                    width: lightningLabel.implicitWidth + Theme.sp(10)
                    topLeftRadius: Theme.radius; bottomLeftRadius: Theme.radius
                    topRightRadius: 0; bottomRightRadius: 0
                    color: root.controller.moveNetModel === 0 ? Theme.colorAccentMid : Theme.colorSurface
                    border.width: 1
                    border.color: root.controller.moveNetModel === 0
                                  ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.4)
                                  : Theme.colorBorderMid
                    Text {
                        id: lightningLabel
                        anchors.centerIn: parent
                        text: qsTr("Lightning")
                        color: root.controller.moveNetModel === 0 ? Theme.colorAccent : Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzLabel
                        font.weight: Font.Normal
                    }
                    TapHandler { onTapped: root.controller.selectMoveNetModel(0) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Rectangle {
                    id: thunderBtn
                    visible: root.controller.moveNetThunderAvailable
                    height: Theme.sp(20)
                    width: thunderLabel.implicitWidth + Theme.sp(10)
                    topRightRadius: Theme.radius; bottomRightRadius: Theme.radius
                    topLeftRadius: 0; bottomLeftRadius: 0
                    color: root.controller.moveNetModel === 1 ? Theme.colorAccentMid : Theme.colorSurface
                    border.width: 1
                    border.color: root.controller.moveNetModel === 1
                                  ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.4)
                                  : Theme.colorBorderMid
                    Text {
                        id: thunderLabel
                        anchors.centerIn: parent
                        text: qsTr("Thunder")
                        color: root.controller.moveNetModel === 1 ? Theme.colorAccent : Theme.colorText2
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzLabel
                        font.weight: Font.Normal
                    }
                    TapHandler { onTapped: root.controller.selectMoveNetModel(1) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ── ORT backend badge ─────────────────────────────────────────────
            Rectangle {
                visible: root.controller.poseBackendLabel !== "" || root.controller.poseFps > 0
                width: poseBackendText.implicitWidth + Theme.sp(10)
                height: Theme.sp(20)
                radius: Theme.radius
                color: root.controller.poseBackendLabel !== "" ? Theme.colorGoodLight : Theme.colorSurface
                border.width: 1
                border.color: root.controller.poseBackendLabel !== ""
                              ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.3)
                              : Theme.colorBorderMid

                HoverHandler { id: poseBackendHover }
                ToolTip.visible: poseBackendHover.hovered
                ToolTip.text: {
                    const ep = root.controller.poseBackendLabel !== ""
                               ? root.controller.poseBackendLabel : "CPU"
                    return "MoveNet ORT: " + ep
                }
                ToolTip.delay: 500

                Text {
                    id: poseBackendText
                    anchors.centerIn: parent
                    text: root.controller.poseBackendLabel !== ""
                          ? root.controller.poseBackendLabel
                          : qsTr("CPU")
                    color: root.controller.poseBackendLabel !== "" ? Theme.colorGood : Theme.colorText3
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzLabel
                    font.weight: Font.Normal
                }
            }
        }
    }
}
