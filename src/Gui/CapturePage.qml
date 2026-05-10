import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick3D

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

            Label {
                text: "Rate:"
                color: "#6c7086"
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }

            ComboBox {
                id: rateCombo
                enabled: imuController.imuConnected
                model: [10, 50, 100, 200]
                currentIndex: {
                    var idx = model.indexOf(imuController.outputRateHz)
                    return idx >= 0 ? idx : 2
                }
                onActivated: imuController.setOutputRateHz(model[currentIndex])
                implicitWidth: 90
                contentItem: Text {
                    leftPadding: 8
                    text: rateCombo.displayText + " Hz"
                    color: rateCombo.enabled ? "#cdd6f4" : "#6c7086"
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: rateCombo.enabled ? "#313244" : "#1e1e2e"
                    border.color: rateCombo.enabled ? "#45475a" : "#313244"
                    border.width: 1
                    radius: 6
                }
                popup: Popup {
                    y: rateCombo.height + 2
                    width: rateCombo.width
                    padding: 4
                    background: Rectangle { color: "#313244"; radius: 6 }
                    contentItem: ListView {
                        implicitHeight: contentHeight
                        model: rateCombo.delegateModel
                        clip: true
                    }
                }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: rateCombo.width
                    contentItem: Text {
                        text: modelData + " Hz"
                        color: rateCombo.currentIndex === index ? "#cdd6f4" : "#6c7086"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: hovered ? "#45475a" : "transparent"
                        radius: 4
                    }
                }
            }

            Button {
                id: saveLogBtn
                text: qsTr("Save Log")
                onClicked: imuController.saveLog()
                contentItem: Text {
                    text: saveLogBtn.text
                    color: "#1e1e2e"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: saveLogBtn.pressed ? "#89b4fa" : "#585b70"
                    radius: 6
                }
            }
        }

        TabBar {
            id: imuTabBar
            Layout.fillWidth: true

            TabButton {
                text: "Log"
                contentItem: Text {
                    text: parent.text
                    color: imuTabBar.currentIndex === 0 ? "#cdd6f4" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: imuTabBar.currentIndex === 0 ? "#45475a" : "transparent"
                    radius: 4
                }
            }

            TabButton {
                text: "Viz"
                enabled: imuController.imuConnected
                contentItem: Text {
                    text: parent.text
                    color: imuTabBar.currentIndex === 1 ? "#cdd6f4"
                         : imuController.imuConnected   ? "#6c7086"
                         :                               "#45475a"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: imuTabBar.currentIndex === 1 ? "#45475a" : "transparent"
                    radius: 4
                }
            }

            background: Rectangle { color: "#313244"; radius: 6 }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: imuTabBar.currentIndex

            // Log tab
            Rectangle {
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

            // Viz tab
            Item {
                id: vizTab

            // ── Orientation debug controls ────────────────────────────────────
            // Axis order confirmed: Roll→X, Yaw→Y, Pitch→Z (RYP)
            // Sign flips — toggle to find correct polarity for each axis
            property bool negRoll:  false
            property bool negPitch: false
            property bool negYaw:   false

            // Helpers ────────────────────────────────────────────────────────


            // Build ZYX quaternion: q_Z(c) * q_Y(b) * q_X(a)
            // a=X-axis angle, b=Y-axis angle, c=Z-axis angle (degrees)
            function zyx(a, b, c) {
                const ha = a * Math.PI / 360  // half angle in radians
                const hb = b * Math.PI / 360
                const hc = c * Math.PI / 360
                const ca = Math.cos(ha), sa = Math.sin(ha)
                const cb = Math.cos(hb), sb = Math.sin(hb)
                const cc = Math.cos(hc), sc = Math.sin(hc)
                return Qt.quaternion(
                    ca*cb*cc + sa*sb*sc,
                    sa*cb*cc - ca*sb*sc,
                    ca*sb*cc + sa*cb*sc,
                    ca*cb*sc - sa*sb*cc
                )
            }

            // RYP + Y180 confirmed. Negate toggles still live for polarity tuning.
            function deviceQuat() {
                const r = imuController.imuRoll  * (vizTab.negRoll  ? -1 : 1)
                const p = imuController.imuPitch * (vizTab.negPitch ? -1 : 1)
                const y = imuController.imuYaw   * (vizTab.negYaw   ? -1 : 1)
                const d = vizTab.zyx(r, y, p)
                // Y180: [0,0,1,0] * d
                return Qt.quaternion(-d.y, d.z, d.scalar, -d.x)
            }

            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                spacing: 3

                // ── Sign flips ────────────────────────────────────────────────
                RowLayout {
                    spacing: 4; Layout.leftMargin: 4
                    Label { text: "Negate:"; color: "#6c7086"; font.pixelSize: 10; Layout.preferredWidth: 38 }
                    Repeater {
                        model: [["Roll", "negRoll"], ["Pitch", "negPitch"], ["Yaw", "negYaw"]]
                        delegate: Rectangle {
                            required property var modelData
                            width: sLbl.implicitWidth+10; height: 20; radius: 3
                            color: vizTab[modelData[1]] ? "#f9e2af" : "#313244"
                            Text { id: sLbl; anchors.centerIn: parent; text: modelData[0]; font.pixelSize: 10
                                   color: parent.color === "#f9e2af" ? "#1e1e2e" : "#cdd6f4" }
                            TapHandler { onTapped: vizTab[modelData[1]] = !vizTab[modelData[1]] }
                        }
                    }
                }


            View3D {
                Layout.fillWidth: true
                Layout.fillHeight: true
                environment: SceneEnvironment {
                    clearColor: "#1e1e2e"
                    backgroundMode: SceneEnvironment.Color
                }

                PerspectiveCamera {
                    position: Qt.vector3d(0, 0, 480)
                }

                DirectionalLight {
                    eulerRotation: Qt.vector3d(-45, -30, 0)
                    brightness: 1.2
                    ambientColor: Qt.rgba(0.3, 0.3, 0.3, 1)
                }

                Node {
                    rotation: vizTab.deviceQuat()

                    // Top face — red
                    Model {
                        source: "#Rectangle"
                        position: Qt.vector3d(0, 100, 0)
                        eulerRotation.x: -90
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial { diffuseColor: "#e64553"; cullMode: Material.NoCulling }
                    }
                    // Bottom face — red
                    Model {
                        source: "#Rectangle"
                        position: Qt.vector3d(0, -100, 0)
                        eulerRotation.x: 90
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial { diffuseColor: "#e64553"; cullMode: Material.NoCulling }
                    }
                    // Front face — orange
                    Model {
                        source: "#Rectangle"
                        position: Qt.vector3d(0, 0, 100)
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial { diffuseColor: "#fe640b"; cullMode: Material.NoCulling }
                    }
                    // Back face — orange
                    Model {
                        source: "#Rectangle"
                        position: Qt.vector3d(0, 0, -100)
                        eulerRotation.y: 180
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial { diffuseColor: "#fe640b"; cullMode: Material.NoCulling }
                    }
                    // Right face — orange
                    Model {
                        source: "#Rectangle"
                        position: Qt.vector3d(100, 0, 0)
                        eulerRotation.y: -90
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial { diffuseColor: "#fe640b"; cullMode: Material.NoCulling }
                    }
                    // Left face — orange
                    Model {
                        source: "#Rectangle"
                        position: Qt.vector3d(-100, 0, 0)
                        eulerRotation.y: 90
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial { diffuseColor: "#fe640b"; cullMode: Material.NoCulling }
                    }

                    // ── Up arrow ──────────────────────────────────────────────
                    // Shaft — thin cylinder rising above the top face
                    Model {
                        source: "#Cylinder"
                        position: Qt.vector3d(0, 160, 0)
                        scale: Qt.vector3d(0.15, 0.6, 0.15)
                        materials: DefaultMaterial { diffuseColor: "#cdd6f4" }
                    }
                    // Arrowhead — cone at the top of the shaft
                    Model {
                        source: "#Cone"
                        position: Qt.vector3d(0, 220, 0)
                        scale: Qt.vector3d(0.4, 0.4, 0.4)
                        materials: DefaultMaterial { diffuseColor: "#cdd6f4" }
                    }
                }
            }           // View3D
            }           // ColumnLayout
        }               // Item (viz tab)
        }
    }

    Connections {
        target: imuController
        function onLogEntryAdded(entry) {
            logModel.append({ "entry": entry })
            logView.positionViewAtEnd()
        }
        function onImuConnectedChanged() {
            if (!imuController.imuConnected && imuTabBar.currentIndex === 1)
                imuTabBar.currentIndex = 0
        }
    }
}
