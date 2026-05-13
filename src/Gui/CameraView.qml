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
import QtMultimedia

Item {
    id: root

    property QtObject controller

    // ROI selection state
    property bool  roiSelecting:  false
    property point roiDragStart:  Qt.point(0, 0)
    property point roiDragEnd:    Qt.point(0, 0)
    property bool  roiDragging:   false

    // Whether any ROI is currently set.
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
        spacing: 6

        // ── Camera frame ──────────────────────────────────────────────────────
        Rectangle {
            id: frameRect
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#181825"
            radius: 6

            // Standard Qt Multimedia path for webcams and pre-decoded streams.
            VideoOutput {
                id: videoOut
                anchors.fill: parent
                anchors.margins: 2
                fillMode: VideoOutput.PreserveAspectFit
                visible: !root.controller.needsDebayer
            }

            // GPU Bayer demosaic path for industrial Bayer cameras (Spinnaker).
            BayerVideoItem {
                id: bayerView
                anchors.fill: parent
                anchors.margins: 2
                visible: root.controller.needsDebayer
            }

            Label {
                anchors.centerIn: parent
                visible: !root.controller.isRecording
                text: qsTr("No camera feed")
                color: "#6c7086"
                font.pixelSize: 14
            }

            // ── Perspective badge (top-left overlay) ──────────────────────────
            Rectangle {
                visible: root.controller.perspective > 0
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 8
                width: perspBadgeText.implicitWidth + 10
                height: 20
                radius: 4
                color: "#89b4fa"

                Text {
                    id: perspBadgeText
                    anchors.centerIn: parent
                    text: root.controller.perspective === 1 ? "DTL"
                        : root.controller.perspective === 2 ? "Face On"
                        : "Other"
                    color: "#1e1e2e"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            // ── Skeleton overlay ──────────────────────────────────────────────
            Canvas {
                id: skeletonCanvas
                anchors.fill: parent
                visible: root.controller.isRecording

                readonly property var kEdges: [
                    {a:0,  b:1,  color:"#a6e3a1"}, {a:0,  b:2,  color:"#89b4fa"},
                    {a:1,  b:3,  color:"#a6e3a1"}, {a:2,  b:4,  color:"#89b4fa"},
                    {a:0,  b:5,  color:"#a6e3a1"}, {a:0,  b:6,  color:"#89b4fa"},
                    {a:5,  b:6,  color:"#f9e2af"},
                    {a:5,  b:7,  color:"#a6e3a1"}, {a:7,  b:9,  color:"#a6e3a1"},
                    {a:6,  b:8,  color:"#89b4fa"}, {a:8,  b:10, color:"#89b4fa"},
                    {a:5,  b:11, color:"#a6e3a1"}, {a:6,  b:12, color:"#89b4fa"},
                    {a:11, b:12, color:"#f9e2af"},
                    {a:11, b:13, color:"#a6e3a1"}, {a:13, b:15, color:"#a6e3a1"},
                    {a:12, b:14, color:"#89b4fa"}, {a:14, b:16, color:"#89b4fa"}
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

                    // For Spinnaker (BayerVideoItem), use the item's full bounds.
                    // For standard VideoOutput, use the letterboxed contentRect.
                    var cr = root.controller.needsDebayer
                        ? Qt.rect(0, 0, bayerView.width, bayerView.height)
                        : videoOut.contentRect
                    if (cr.width <= 0 || cr.height <= 0)
                        return

                    var kMinScore = 0.25

                    for (var i = 0; i < kEdges.length; ++i) {
                        var e = kEdges[i]
                        var ka = kps[e.a], kb = kps[e.b]
                        if (ka.score < kMinScore || kb.score < kMinScore)
                            continue
                        ctx.globalAlpha = 0.4 + 0.6 * Math.min(ka.score, kb.score)
                        ctx.strokeStyle = e.color
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
                        ctx.fillStyle   = s >= 0.6 ? "#cdd6f4" : s >= 0.4 ? "#f9e2af" : "#f38ba8"
                        ctx.globalAlpha = 0.5 + 0.5 * s
                        ctx.beginPath()
                        ctx.arc(kp.x * cr.width + cr.x, kp.y * cr.height + cr.y, 4, 0, Math.PI * 2)
                        ctx.fill()
                    }

                    ctx.globalAlpha = 1.0
                }
            }

            // ── Persistent ROI overlay ────────────────────────────────────────
            // Shows the confirmed ROI as a labelled rectangle mapped back from
            // normalized [0,1] coords to the actual video content bounds.
            Rectangle {
                id: roiOverlay
                visible: root.roiIsSet && !root.roiSelecting
                z: 20
                color: "transparent"
                border.color: "#fab387"
                border.width: 2

                // Content bounds in frameRect coordinates.
                // videoOut sits at x=2, y=2 (anchors.margins: 2) and
                // contentRect is relative to videoOut.
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
                    anchors.margins: 3
                    text: "Hitting Area"
                    color: "#fab387"
                    font.pixelSize: 10
                    font.bold: true
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
                color: "#22fab387"
                border.color: "#fab387"
                border.width: 2
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

                    // Map drag rect to normalized frame coords.
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
            spacing: 8

            Label {
                visible: root.controller.deviceDescription !== ""
                text: root.controller.deviceDescription
                color: "#6c7086"
                font.pixelSize: 11
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
                        height: 20
                        width: perspLabel.implicitWidth + 10
                        topLeftRadius:     modelData.leftR  ? 4 : 0
                        bottomLeftRadius:  modelData.leftR  ? 4 : 0
                        topRightRadius:    modelData.rightR ? 4 : 0
                        bottomRightRadius: modelData.rightR ? 4 : 0
                        color: active ? "#89b4fa" : "#313244"
                        Text {
                            id: perspLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            color: active ? "#1e1e2e" : "#cdd6f4"
                            font.pixelSize: 11
                            font.bold: active
                        }
                        TapHandler {
                            onTapped: cameraManager.setPerspective(
                                root.controller,
                                active ? 0 : modelData.value)
                        }
                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }

            // ── ROI button ────────────────────────────────────────────────────
            Rectangle {
                height: 20
                width: roiBtnLabel.implicitWidth + 10
                radius: 4
                color: root.roiSelecting ? "#fab387"
                     : root.roiIsSet     ? "#f9e2af"
                     :                     "#313244"

                Text {
                    id: roiBtnLabel
                    anchors.centerIn: parent
                    text: qsTr("Hitting Area")
                    color: (root.roiSelecting || root.roiIsSet) ? "#1e1e2e" : "#cdd6f4"
                    font.pixelSize: 11
                    font.bold: root.roiSelecting || root.roiIsSet
                }
                TapHandler {
                    onTapped: {
                        if (root.roiSelecting) {
                            // Cancel active selection.
                            root.roiSelecting = false
                            root.roiDragging  = false
                        } else {
                            // Clear any existing ROI and enter selection mode.
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

            Item { Layout.fillWidth: true }

            Label {
                visible: root.controller.isRecording && root.controller.preprocessAvgMs > 0
                text: "Pre: " + root.controller.preprocessAvgMs.toFixed(1) + " ms"
                color: "#6c7086"
                font.pixelSize: 11
                font.family: "Courier New"
            }

            Label {
                visible: root.controller.isRecording && root.controller.cameraFps > 0
                text: "Cam: " + root.controller.cameraFps.toFixed(1) + " fps"
                color: "#6c7086"
                font.pixelSize: 11
                font.family: "Courier New"
            }

            Label {
                visible: root.controller.isRecording && root.controller.poseFps > 0
                text: "Pose: " + root.controller.poseAvgMs.toFixed(1) + " ms  "
                    + root.controller.poseFps.toFixed(1) + " fps"
                color: "#6c7086"
                font.pixelSize: 11
                font.family: "Courier New"
            }

            // ── MoveNet model selector ────────────────────────────────────────
            Row {
                visible: root.controller.moveNetThunderAvailable
                spacing: 0

                Rectangle {
                    id: lightningBtn
                    height: 20
                    width: lightningLabel.implicitWidth + 10
                    topLeftRadius: 4; bottomLeftRadius: 4
                    color: root.controller.moveNetModel === 0 ? "#cba6f7" : "#313244"
                    Text {
                        id: lightningLabel
                        anchors.centerIn: parent
                        text: qsTr("Lightning")
                        color: root.controller.moveNetModel === 0 ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 11
                        font.bold: root.controller.moveNetModel === 0
                    }
                    TapHandler { onTapped: root.controller.selectMoveNetModel(0) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Rectangle {
                    id: thunderBtn
                    height: 20
                    width: thunderLabel.implicitWidth + 10
                    topRightRadius: 4; bottomRightRadius: 4
                    color: root.controller.moveNetModel === 1 ? "#cba6f7" : "#313244"
                    Text {
                        id: thunderLabel
                        anchors.centerIn: parent
                        text: qsTr("Thunder")
                        color: root.controller.moveNetModel === 1 ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 11
                        font.bold: root.controller.moveNetModel === 1
                    }
                    TapHandler { onTapped: root.controller.selectMoveNetModel(1) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ── ORT backend badge ─────────────────────────────────────────────
            Rectangle {
                visible: root.controller.poseBackendLabel !== ""
                         || root.controller.poseFps > 0
                width: poseBackendText.implicitWidth + 10
                height: 20
                radius: 4
                color: root.controller.poseBackendLabel !== "" ? "#a6e3a1" : "#6c7086"

                HoverHandler { id: poseBackendHover }
                ToolTip.visible: poseBackendHover.hovered
                ToolTip.text: root.controller.poseBackendLabel !== ""
                              ? qsTr("MoveNet ORT: ") + root.controller.poseBackendLabel
                              : qsTr("MoveNet ORT: CPU")
                ToolTip.delay: 500

                Text {
                    id: poseBackendText
                    anchors.centerIn: parent
                    text: root.controller.poseBackendLabel !== ""
                          ? root.controller.poseBackendLabel
                          : qsTr("CPU")
                    color: "#1e1e2e"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }
    }
}
