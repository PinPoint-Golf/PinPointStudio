import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtMultimedia

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: "Camera"
            color: "#cdd6f4"
            font.pixelSize: 20
            font.bold: true
        }

        // ── Camera frame ─────────────────────────────────────────────────────
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
                Component.onCompleted: videoController.setVideoSink(videoOut.videoSink)
                // Hide the raw VideoOutput when debayering is active
                visible: !videoController.needsDebayer
            }

            ShaderEffectSource {
                id: videoSource
                sourceItem: videoOut
                live: true
                hideSource: false // We control visibility via videoOut.visible
            }

            Loader {
                id: debayerLoader
                anchors.fill: parent
                active: videoController.needsDebayer
                visible: active
                sourceComponent: ShaderEffect {
                    property variant source: videoSource
                    vertexShader: "qrc:/shaders/src/Gui/debayer.vert.qsb"
                    fragmentShader: "qrc:/shaders/src/Gui/debayer.frag.qsb"
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !videoController.isRecording
                text: qsTr("No camera feed")
                color: "#6c7086"
                font.pixelSize: 14
            }

            // ── Skeleton overlay (drawn independently of frame capture) ────────
            Canvas {
                id: skeletonCanvas
                anchors.fill: parent
                visible: videoController.isRecording

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
                    target: videoController
                    function onPoseKeypointsChanged() { skeletonCanvas.requestPaint() }
                }

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var kps = videoController.poseKeypoints
                    if (!kps || kps.length < 17)
                        return

                    // Map normalised [0,1] coords into the letterboxed video rect.
                    var cr = videoOut.contentRect
                    if (cr.width <= 0 || cr.height <= 0)
                        return

                    var kMinScore = 0.25

                    // Draw bones.
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

                    // Draw joints on top.
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

        // ── Controls ─────────────────────────────────────────────────────────
        RowLayout {
            spacing: 8

            Button {
                id: startButton
                text: qsTr("Start ●")
                enabled: !videoController.isRecording
                onClicked: videoController.startRecording()
                contentItem: Text {
                    text: startButton.text
                    color: startButton.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: startButton.enabled
                           ? (startButton.pressed ? "#a6e3a1" : "#40a02b")
                           : "#313244"
                    radius: 6
                }
            }

            Button {
                id: stopButton
                text: qsTr("Stop ■")
                enabled: videoController.isRecording
                onClicked: videoController.stopRecording()
                contentItem: Text {
                    text: stopButton.text
                    color: stopButton.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: stopButton.enabled
                           ? (stopButton.pressed ? "#f38ba8" : "#e64553")
                           : "#313244"
                    radius: 6
                }
            }

            Rectangle {
                visible: videoController.isRecording
                width: 10; height: 10; radius: 5
                color: "#f38ba8"
                SequentialAnimation on opacity {
                    running: videoController.isRecording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                }
            }

            Label {
                visible: videoController.isRecording
                text: qsTr("Recording…")
                color: "#f38ba8"
                font.pixelSize: 13
            }

            Item { Layout.fillWidth: true }

            // ── Pipeline stats ────────────────────────────────────────────────
            Label {
                visible: videoController.isRecording && videoController.preprocessAvgMs > 0
                text: "Pre: " + videoController.preprocessAvgMs.toFixed(1) + " ms"
                color: "#6c7086"
                font.pixelSize: 12
                font.family: "Courier New"
            }

            Label {
                visible: videoController.isRecording && videoController.cameraFps > 0
                text: "Cam: " + videoController.cameraFps.toFixed(1) + " fps"
                color: "#6c7086"
                font.pixelSize: 12
                font.family: "Courier New"
            }

            Label {
                visible: videoController.isRecording && videoController.poseFps > 0
                text: "Pose: " + videoController.poseAvgMs.toFixed(1) + " ms  "
                    + videoController.poseFps.toFixed(1) + " fps"
                color: "#6c7086"
                font.pixelSize: 12
                font.family: "Courier New"
            }

            // ── MoveNet model selector ────────────────────────────────────────
            Row {
                visible: videoController.moveNetThunderAvailable
                spacing: 0

                Rectangle {
                    id: lightningBtn
                    height: 20
                    width: lightningLabel.implicitWidth + 10
                    topLeftRadius: 4; bottomLeftRadius: 4
                    color: videoController.moveNetModel === 0 ? "#cba6f7" : "#313244"
                    Text {
                        id: lightningLabel
                        anchors.centerIn: parent
                        text: qsTr("Lightning")
                        color: videoController.moveNetModel === 0 ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 11
                        font.bold: videoController.moveNetModel === 0
                    }
                    TapHandler { onTapped: videoController.selectMoveNetModel(0) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Rectangle {
                    id: thunderBtn
                    height: 20
                    width: thunderLabel.implicitWidth + 10
                    topRightRadius: 4; bottomRightRadius: 4
                    color: videoController.moveNetModel === 1 ? "#cba6f7" : "#313244"
                    Text {
                        id: thunderLabel
                        anchors.centerIn: parent
                        text: qsTr("Thunder")
                        color: videoController.moveNetModel === 1 ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 11
                        font.bold: videoController.moveNetModel === 1
                    }
                    TapHandler { onTapped: videoController.selectMoveNetModel(1) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ── ORT backend badge ─────────────────────────────────────────────
            Rectangle {
                visible: videoController.poseBackendLabel !== ""
                         || videoController.poseFps > 0
                width: poseBackendText.implicitWidth + 10
                height: 20
                radius: 4
                color: videoController.poseBackendLabel !== "" ? "#a6e3a1" : "#6c7086"

                HoverHandler { id: poseBackendHover }
                ToolTip.visible: poseBackendHover.hovered
                ToolTip.text: videoController.poseBackendLabel !== ""
                              ? qsTr("MoveNet ORT: ") + videoController.poseBackendLabel
                              : qsTr("MoveNet ORT: CPU")
                ToolTip.delay: 500

                Text {
                    id: poseBackendText
                    anchors.centerIn: parent
                    text: videoController.poseBackendLabel !== ""
                          ? videoController.poseBackendLabel
                          : qsTr("CPU")
                    color: "#1e1e2e"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }
    }
}
