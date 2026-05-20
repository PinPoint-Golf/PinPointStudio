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
import QtQuick3D

Item {

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(16)
        spacing: Theme.sp(12)

        Label {
            text: qsTr("IMU")
            color: Theme.colorText
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSzHeading
            font.weight: Font.Normal
        }

        RowLayout {
            spacing: Theme.sp(8)

            Button {
                id: connectBtn
                text: qsTr("Connect")
                enabled: !imuController.busy && !imuController.imuConnected
                onClicked: imuController.connectImu()
                contentItem: Text {
                    text: connectBtn.text
                    color: connectBtn.enabled ? Theme.colorBg : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: connectBtn.enabled
                           ? (connectBtn.pressed ? Qt.darker(Theme.colorAccent, 1.1) : Theme.colorAccent)
                           : Theme.colorBg3
                    radius: Theme.radius
                }
            }

            Button {
                id: disconnectBtn
                text: qsTr("Disconnect")
                enabled: imuController.busy || imuController.imuConnected
                onClicked: imuController.disconnectImu()
                contentItem: Text {
                    text: disconnectBtn.text
                    color: disconnectBtn.enabled ? Theme.colorWarn : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: disconnectBtn.enabled ? Theme.colorWarnLight : Theme.colorBg3
                    border.width: disconnectBtn.enabled ? 1 : 0
                    border.color: Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.5)
                    radius: Theme.radius
                }
            }

            Rectangle {
                visible: imuController.busy
                width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                color: Theme.colorWarn
                SequentialAnimation on opacity {
                    running: imuController.busy
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 600; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                }
            }

            Label {
                text: imuController.stateLabel
                color: imuController.imuConnected ? Theme.colorGood
                     : imuController.busy          ? Theme.colorWarn
                     :                               Theme.colorText3
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
            }

            Item { Layout.fillWidth: true }

            // Battery badge
            Rectangle {
                visible: imuController.imuConnected && imuController.batteryPercent >= 0
                radius: Theme.radius
                color: imuController.batteryPercent > 60 ? Theme.colorGoodLight
                     : imuController.batteryPercent > 20 ? Theme.colorWarnLight
                     :                                     Theme.colorWarnLight
                border.width: 1
                border.color: imuController.batteryPercent > 60
                              ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.25)
                              : Qt.rgba(Theme.colorWarn.r, Theme.colorWarn.g, Theme.colorWarn.b, 0.25)
                implicitWidth:  batteryBadge.implicitWidth  + Theme.sp(10)
                implicitHeight: batteryBadge.implicitHeight + Theme.sp(4)

                Label {
                    id: batteryBadge
                    anchors.centerIn: parent
                    text: "BAT: " + imuController.batteryPercent + "%"
                    color: imuController.batteryPercent > 60 ? Theme.colorGood
                         : imuController.batteryPercent > 20 ? Theme.colorWarn
                         :                                     Theme.colorWarn
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.weight: Font.Normal
                }
            }

            Label {
                visible: imuController.imuConnected && imuController.dataRateHz > 0
                text: imuController.dataRateHz.toFixed(1) + " Hz"
                color: Theme.colorGood
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody
                verticalAlignment: Text.AlignVCenter
            }

            Label {
                text: qsTr("Rate:")
                color: Theme.colorText3
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
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
                implicitWidth: Theme.sp(90)
                contentItem: Text {
                    leftPadding: 8
                    text: rateCombo.displayText + " Hz"
                    color: rateCombo.enabled ? Theme.colorText : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: Theme.colorSurface
                    border.color: rateCombo.enabled ? Theme.colorBorderMid : Theme.colorBorder
                    border.width: 1
                    radius: Theme.radius
                }
                popup: Popup {
                    y: rateCombo.height + 2
                    width: rateCombo.width
                    padding: Theme.sp(4)
                    background: Rectangle {
                        color: Theme.colorSurface
                        radius: Theme.radius
                        border.width: 1
                        border.color: Theme.colorBorderMid
                    }
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
                        color: rateCombo.currentIndex === index ? Theme.colorText : Theme.colorText3
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: hovered ? Theme.colorBg3 : "transparent"
                        radius: Theme.radius - 1
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
                    color: zeroBtn.enabled ? Theme.colorText2 : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    border.width: 1
                    border.color: zeroBtn.enabled ? Theme.colorBorderStrong : Theme.colorBorderMid
                    radius: Theme.radius
                }
            }

            Button {
                id: saveLogBtn
                text: qsTr("Save Log")
                onClicked: imuController.saveLog()
                contentItem: Text {
                    text: saveLogBtn.text
                    color: Theme.colorText2
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.colorBorderStrong
                    radius: Theme.radius
                }
            }
        }

        // ── Buffer status ─────────────────────────────────────────────────────
        RowLayout {
            visible: imuController.imuConnected
            spacing: Theme.sp(8)

            Item { Layout.fillWidth: true }

            Label {
                visible: cameraManager.bufferState !== "idle"
                         && cameraManager.bufferState !== "unavailable"
                text: bufferController.totalEvents + " events"
                color: Theme.colorText3
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzBody2
                verticalAlignment: Text.AlignVCenter
            }
        }

        TabBar {
            id: imuTabBar
            Layout.fillWidth: true
            background: Rectangle {
                color: Theme.colorSurface
                radius: Theme.radius
                border.width: 1
                border.color: Theme.colorBorderMid
            }

            TabButton {
                text: qsTr("Log")
                contentItem: Text {
                    text: parent.text
                    color: imuTabBar.currentIndex === 0 ? Theme.colorText : Theme.colorText3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: imuTabBar.currentIndex === 0 ? Theme.colorBg3 : "transparent"
                    radius: Theme.radius - 1
                }
            }

            TabButton {
                text: qsTr("Viz")
                enabled: imuController.imuConnected
                contentItem: Text {
                    text: parent.text
                    color: imuTabBar.currentIndex === 1 ? Theme.colorText
                         : imuController.imuConnected   ? Theme.colorText3
                         :                                Theme.colorBg3
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight: Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: imuTabBar.currentIndex === 1 ? Theme.colorBg3 : "transparent"
                    radius: Theme.radius - 1
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: imuTabBar.currentIndex

            // Log tab
            Rectangle {
                color: Theme.colorSurface
                radius: Theme.radius
                border.width: 1
                border.color: Theme.colorBorderMid

                ListView {
                    id: logView
                    anchors.fill: parent
                    anchors.margins: Theme.sp(8)
                    clip: true
                    model: ListModel { id: logModel }
                    spacing: 1

                    delegate: Text {
                        required property string entry
                        width: logView.width
                        text: entry
                        color: Theme.colorText
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzLabel
                        wrapMode: Text.NoWrap
                    }
                }
            }

            // Viz tab
            View3D {
                environment: SceneEnvironment {
                    clearColor: Theme.colorBg
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
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }

                    // Top / Bottom faces
                    Model {
                        source: "#Rectangle"; position: Qt.vector3d(0, 100, 0)
                        eulerRotation.x: -90; scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial {
                            cullMode: Material.NoCulling
                            diffuseMap: FaceTex { label: qsTr("Top"); faceColor: Theme.colorWarn }
                        }
                    }
                    Model {
                        source: "#Rectangle"; position: Qt.vector3d(0, -100, 0)
                        eulerRotation.x: 90; scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial {
                            cullMode: Material.NoCulling
                            diffuseMap: FaceTex { label: qsTr("Bottom"); faceColor: Theme.colorWarn }
                        }
                    }
                    // Front / Back / Left / Right faces
                    Model {
                        source: "#Rectangle"; position: Qt.vector3d(0, 0, 100)
                        scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial {
                            cullMode: Material.NoCulling
                            diffuseMap: FaceTex { label: qsTr("Front"); faceColor: Theme.colorAccent }
                        }
                    }
                    Model {
                        source: "#Rectangle"; position: Qt.vector3d(0, 0, -100)
                        eulerRotation.y: 180; scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial {
                            cullMode: Material.NoCulling
                            diffuseMap: FaceTex { label: qsTr("Back"); faceColor: Theme.colorAccent }
                        }
                    }
                    Model {
                        source: "#Rectangle"; position: Qt.vector3d(100, 0, 0)
                        eulerRotation.y: -90; scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial {
                            cullMode: Material.NoCulling
                            diffuseMap: FaceTex { label: qsTr("Left"); faceColor: Theme.colorAccent; flip: true }
                        }
                    }
                    Model {
                        source: "#Rectangle"; position: Qt.vector3d(-100, 0, 0)
                        eulerRotation.y: 90; scale: Qt.vector3d(2, 2, 1)
                        materials: DefaultMaterial {
                            cullMode: Material.NoCulling
                            diffuseMap: FaceTex { label: qsTr("Right"); faceColor: Theme.colorAccent; flip: true }
                        }
                    }
                    // Up-direction arrow
                    Model {
                        source: "#Cylinder"; position: Qt.vector3d(0, 160, 0)
                        scale: Qt.vector3d(0.15, 0.6, 0.15)
                        materials: DefaultMaterial { diffuseColor: Theme.colorText }
                    }
                    Model {
                        source: "#Cone"; position: Qt.vector3d(0, 190, 0)
                        scale: Qt.vector3d(0.4, 0.4, 0.4)
                        materials: DefaultMaterial { diffuseColor: Theme.colorText }
                    }

                    // Acceleration arrow — grows from cube centre in body-frame accel direction.
                    Node {
                        id: accelNode
                        property real ax: imuController.accelX
                        property real ay: imuController.accelY
                        property real az: imuController.accelZ
                        property real mag: Math.sqrt(ax*ax + ay*ay + az*az)
                        property real s: Math.min(mag, 3.0)
                        visible: mag > 0.05
                        rotation: {
                            if (mag < 0.001) return Qt.quaternion(1, 0, 0, 0)
                            var dx = ax/mag, dy = ay/mag, dz = az/mag
                            if (dy < -0.9999) return Qt.quaternion(0, 1, 0, 0)
                            var qw = 1.0 + dy, qx = dz, qz = -dx
                            var n = Math.sqrt(qw*qw + qx*qx + qz*qz)
                            return Qt.quaternion(qw/n, qx/n, 0.0, qz/n)
                        }
                        Model {
                            source: "#Cylinder"
                            position: Qt.vector3d(0, 37.5 * accelNode.s, 0)
                            scale: Qt.vector3d(0.12, 0.75 * accelNode.s, 0.12)
                            materials: DefaultMaterial { diffuseColor: Theme.colorWarn }
                        }
                        Model {
                            source: "#Cone"
                            position: Qt.vector3d(0, 75 * accelNode.s, 0)
                            scale: Qt.vector3d(0.25 * accelNode.s, 0.25 * accelNode.s, 0.25 * accelNode.s)
                            materials: DefaultMaterial { diffuseColor: Theme.colorWarn }
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
