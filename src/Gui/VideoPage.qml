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
                visible: videoController.isRecording && videoController.poseFps > 0
                text: "Pose: " + videoController.poseAvgMs.toFixed(1) + " ms  "
                    + videoController.poseFps.toFixed(1) + " fps"
                color: "#6c7086"
                font.pixelSize: 12
                font.family: "Courier New"
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
