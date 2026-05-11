import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── Header row: title + camera selector ───────────────────────────────
        RowLayout {
            spacing: 12

            Label {
                text: "Camera"
                color: "#cdd6f4"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // One toggle chip per discovered camera.
            Repeater {
                model: cameraManager.cameraList
                delegate: Rectangle {
                    width: camChipLabel.implicitWidth + 16
                    height: 24
                    radius: 4
                    color: modelData.selected ? "#cba6f7" : "#313244"
                    opacity: cameraManager.isRecording ? 0.5 : 1.0

                    Text {
                        id: camChipLabel
                        anchors.centerIn: parent
                        text: modelData.description
                        color: modelData.selected ? "#1e1e2e" : "#cdd6f4"
                        font.pixelSize: 12
                        font.bold: modelData.selected
                    }

                    TapHandler {
                        enabled: !cameraManager.isRecording
                        onTapped: cameraManager.setSelected(modelData.index, !modelData.selected)
                    }
                    HoverHandler {
                        enabled: !cameraManager.isRecording
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // ── Camera views (side-by-side, equal width) ──────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Repeater {
                model: cameraManager.instances
                delegate: CameraView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: modelData
                }
            }

            // Placeholder shown when no camera is selected.
            Rectangle {
                visible: cameraManager.instances.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#181825"
                radius: 6

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Select a camera above")
                    color: "#6c7086"
                    font.pixelSize: 14
                }
            }
        }

        // ── Shared controls ───────────────────────────────────────────────────
        RowLayout {
            spacing: 8

            Button {
                id: startButton
                text: qsTr("Start ●")
                enabled: !cameraManager.isRecording && cameraManager.anySelected
                onClicked: cameraManager.startAll()
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
                enabled: cameraManager.isRecording
                onClicked: cameraManager.stopAll()
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
                visible: cameraManager.isRecording
                width: 10; height: 10; radius: 5
                color: "#f38ba8"
                SequentialAnimation on opacity {
                    running: cameraManager.isRecording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 900; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                }
            }

            Label {
                visible: cameraManager.isRecording
                text: qsTr("Recording…")
                color: "#f38ba8"
                font.pixelSize: 13
            }

            Item { Layout.fillWidth: true }
        }
    }
}
