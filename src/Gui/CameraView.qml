import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtMultimedia

Item {
    id: root

    property QtObject controller

    Component.onCompleted: controller.setVideoSink(videoOut.videoSink)
    onControllerChanged: if (controller) controller.setVideoSink(videoOut.videoSink)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // ── Camera frame ──────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#181825"
            radius: 6

            VideoOutput {
                id: videoOut
                anchors.fill: parent
                anchors.margins: 2
                fillMode: VideoOutput.PreserveAspectFit
                visible: !root.controller.needsDebayer
            }

            ShaderEffectSource {
                id: videoSource
                sourceItem: videoOut
                live: true
                hideSource: false
            }

            Loader {
                anchors.fill: parent
                active: root.controller.needsDebayer
                visible: active
                sourceComponent: ShaderEffect {
                    property variant source: videoSource
                    vertexShader: "qrc:/shaders/src/Gui/debayer.vert.qsb"
                    fragmentShader: "qrc:/shaders/src/Gui/debayer.frag.qsb"
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !root.controller.isRecording
                text: qsTr("No camera feed")
                color: "#6c7086"
                font.pixelSize: 14
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

                    var cr = videoOut.contentRect
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
                Layout.maximumWidth: 160
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
