import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: "IMU"
            color: "#cdd6f4"
            font.pixelSize: 20
            font.bold: true
        }

        RowLayout {
            spacing: 8

            Button {
                id: connectBtn
                text: qsTr("Connect")
                enabled: !imuController.busy && !imuController.imuConnected
                onClicked: imuController.connectImu()
                contentItem: Text {
                    text: connectBtn.text
                    color: connectBtn.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: connectBtn.enabled
                           ? (connectBtn.pressed ? "#a6e3a1" : "#40a02b")
                           : "#313244"
                    radius: 6
                }
            }

            Button {
                id: disconnectBtn
                text: qsTr("Disconnect")
                enabled: imuController.busy || imuController.imuConnected
                onClicked: imuController.disconnectImu()
                contentItem: Text {
                    text: disconnectBtn.text
                    color: disconnectBtn.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: disconnectBtn.enabled
                           ? (disconnectBtn.pressed ? "#f38ba8" : "#e64553")
                           : "#313244"
                    radius: 6
                }
            }

            Rectangle {
                visible: imuController.busy
                width: 10; height: 10; radius: 5
                color: "#f9e2af"
                SequentialAnimation on opacity {
                    running: imuController.busy
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                }
            }

            Label {
                text: imuController.stateLabel
                color: imuController.imuConnected ? "#a6e3a1"
                     : imuController.busy          ? "#f9e2af"
                     :                               "#6c7086"
                font.pixelSize: 13
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#313244"
            radius: 6

            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                model: ListModel { id: logModel }
                spacing: 1

                delegate: Text {
                    required property string entry
                    width: logView.width
                    text: entry
                    color: "#cdd6f4"
                    font.family: "Courier New"
                    font.pixelSize: 11
                    wrapMode: Text.NoWrap
                }
            }
        }
    }

    Connections {
        target: imuController
        function onLogEntryAdded(entry) {
            logModel.append({ "entry": entry })
            logView.positionViewAtEnd()
        }
    }
}
