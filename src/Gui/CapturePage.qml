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

            Rectangle {
                visible: imuController.imuConnected && imuController.batteryPercent >= 0
                radius: 3
                color: imuController.batteryPercent > 60 ? "#1a3a2a"
                     : imuController.batteryPercent > 20 ? "#3a3a1a"
                     :                                     "#3a1a1a"
                implicitWidth:  batteryBadge.implicitWidth  + 10
                implicitHeight: batteryBadge.implicitHeight + 4

                Label {
                    id: batteryBadge
                    anchors.centerIn: parent
                    text: "BAT: " + imuController.batteryPercent + "%"
                    color: imuController.batteryPercent > 60 ? "#a6e3a1"
                         : imuController.batteryPercent > 20 ? "#f9e2af"
                         :                                     "#f38ba8"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            Label {
                visible: imuController.imuConnected && imuController.dataRateHz > 0
                text: imuController.dataRateHz.toFixed(1) + " Hz"
                color: "#a6e3a1"
                font.pixelSize: 13
                font.family: "Courier New"
                verticalAlignment: Text.AlignVCenter
            }

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
                id: zeroBtn
                text: qsTr("Zero")
                enabled: imuController.imuConnected
                onClicked: imuController.zeroOrientation()
                contentItem: Text {
                    text: zeroBtn.text
                    color: zeroBtn.enabled ? "#1e1e2e" : "#6c7086"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: zeroBtn.enabled
                           ? (zeroBtn.pressed ? "#7287fd" : "#89b4fa")
                           : "#313244"
                    radius: 6
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

        // ── Buffer status ─────────────────────────────────────────────────────
        RowLayout {
            visible: imuController.imuConnected
            spacing: 8

            Item { Layout.fillWidth: true }

            Label {
                visible: cameraManager.bufferState !== "idle"
                         && cameraManager.bufferState !== "unavailable"
                text: bufferController.totalEvents + " events"
                color: "#6c7086"
                font.pixelSize: 12
                font.family: "Courier New"
                verticalAlignment: Text.AlignVCenter
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
            View3D {
                environment: SceneEnvironment {
                    clearColor: "#1e1e2e"
                    backgroundMode: SceneEnvironment.Color
                }

                PerspectiveCamera { position: Qt.vector3d(0, 0, 480) }

                DirectionalLight {
                    eulerRotation: Qt.vector3d(-45, -30, 0)
                    brightness: 1.2
                    ambientColor: Qt.rgba(0.3, 0.3, 0.3, 1)
                }

                Node {
                    rotation: Qt.quaternion(imuController.quatW, imuController.quatX,
                                            imuController.quatY, imuController.quatZ)

                            // Helper component — a labelled face texture
                            component FaceTex: Texture {
                                required property string label
                                required property color faceColor
                                property bool flip: false
                                sourceItem: Item {
                                    width: 256; height: 256
                                    Rectangle { anchors.fill: parent; color: faceColor }
                                    Item {
                                        anchors.fill: parent
                                        transform: Scale { xScale: flip ? -1 : 1; origin.x: 128 }
                                        Text {
                                            anchors.centerIn: parent
                                            text: label
                                            color: "white"
                                            font.pixelSize: 52
                                            font.bold: true
                                        }
                                    }
                                }
                            }

                            // Top face — red
                            Model {
                                source: "#Rectangle"; position: Qt.vector3d(0, 100, 0)
                                eulerRotation.x: -90; scale: Qt.vector3d(2, 2, 1)
                                materials: DefaultMaterial {
                                    cullMode: Material.NoCulling
                                    diffuseMap: FaceTex { label: "Top"; faceColor: "#e64553" }
                                }
                            }
                            // Bottom face — red
                            Model {
                                source: "#Rectangle"; position: Qt.vector3d(0, -100, 0)
                                eulerRotation.x: 90; scale: Qt.vector3d(2, 2, 1)
                                materials: DefaultMaterial {
                                    cullMode: Material.NoCulling
                                    diffuseMap: FaceTex { label: "Bottom"; faceColor: "#e64553" }
                                }
                            }
                            // Front face — orange
                            Model {
                                source: "#Rectangle"; position: Qt.vector3d(0, 0, 100)
                                scale: Qt.vector3d(2, 2, 1)
                                materials: DefaultMaterial {
                                    cullMode: Material.NoCulling
                                    diffuseMap: FaceTex { label: "Front"; faceColor: "#fe640b" }
                                }
                            }
                            // Back face — orange
                            Model {
                                source: "#Rectangle"; position: Qt.vector3d(0, 0, -100)
                                eulerRotation.y: 180; scale: Qt.vector3d(2, 2, 1)
                                materials: DefaultMaterial {
                                    cullMode: Material.NoCulling
                                    diffuseMap: FaceTex { label: "Back"; faceColor: "#fe640b" }
                                }
                            }
                            // Right face — orange
                            Model {
                                source: "#Rectangle"; position: Qt.vector3d(100, 0, 0)
                                eulerRotation.y: -90; scale: Qt.vector3d(2, 2, 1)
                                materials: DefaultMaterial {
                                    cullMode: Material.NoCulling
                                    diffuseMap: FaceTex { label: "Left"; faceColor: "#fe640b"; flip: true }
                                }
                            }
                            // Left face — orange
                            Model {
                                source: "#Rectangle"; position: Qt.vector3d(-100, 0, 0)
                                eulerRotation.y: 90; scale: Qt.vector3d(2, 2, 1)
                                materials: DefaultMaterial {
                                    cullMode: Material.NoCulling
                                    diffuseMap: FaceTex { label: "Right"; faceColor: "#fe640b"; flip: true }
                                }
                            }
                            // Up arrow — shaft
                            Model {
                                source: "#Cylinder"; position: Qt.vector3d(0, 160, 0)
                                scale: Qt.vector3d(0.15, 0.6, 0.15)
                                materials: DefaultMaterial { diffuseColor: "#cdd6f4" }
                            }
                            // Up arrow — head (base at cone origin = cylinder top at 160 + 100×0.6/2)
                            Model {
                                source: "#Cone"; position: Qt.vector3d(0, 190, 0)
                                scale: Qt.vector3d(0.4, 0.4, 0.4)
                                materials: DefaultMaterial { diffuseColor: "#cdd6f4" }
                            }

                            // Acceleration arrow — grows from cube centre in body-frame accel direction.
                            // Hidden when mag ≈ 0; capped at 3 g so the arrow stays on screen.
                            Node {
                                id: accelNode
                                property real ax: imuController.accelX
                                property real ay: imuController.accelY
                                property real az: imuController.accelZ
                                property real mag: Math.sqrt(ax*ax + ay*ay + az*az)
                                property real s: Math.min(mag, 3.0)   // display scale, capped at 3 g
                                visible: mag > 0.05
                                rotation: {
                                    if (mag < 0.001) return Qt.quaternion(1, 0, 0, 0)
                                    var dx = ax/mag, dy = ay/mag, dz = az/mag
                                    // Rotate +Y axis toward (dx,dy,dz): q = normalize(1+dy, dz, 0, -dx)
                                    if (dy < -0.9999) return Qt.quaternion(0, 1, 0, 0)
                                    var qw = 1.0 + dy, qx = dz, qz = -dx
                                    var n = Math.sqrt(qw*qw + qx*qx + qz*qz)
                                    return Qt.quaternion(qw/n, qx/n, 0.0, qz/n)
                                }
                                // Shaft: base at cube centre (y=0), tip at y=75*s
                                // Cylinder is centred, so position = half-height = 37.5*s
                                Model {
                                    source: "#Cylinder"
                                    position: Qt.vector3d(0, 37.5 * accelNode.s, 0)
                                    scale: Qt.vector3d(0.12, 0.75 * accelNode.s, 0.12)
                                    materials: DefaultMaterial { diffuseColor: "#f9e2af" }
                                }
                                // Head: cone base (origin) at shaft tip y=75*s; tip = 75*s + 100*(0.25*s) = 100*s
                                // At 1 g the tip sits exactly at the cube face; beyond 1 g it emerges
                                Model {
                                    source: "#Cone"
                                    position: Qt.vector3d(0, 75 * accelNode.s, 0)
                                    scale: Qt.vector3d(0.25 * accelNode.s, 0.25 * accelNode.s, 0.25 * accelNode.s)
                                    materials: DefaultMaterial { diffuseColor: "#f9e2af" }
                                }
                            }
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
        function onImuConnectedChanged() {
            if (!imuController.imuConnected && imuTabBar.currentIndex === 1)
                imuTabBar.currentIndex = 0
        }
    }
}
