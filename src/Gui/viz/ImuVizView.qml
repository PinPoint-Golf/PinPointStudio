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
import QtQuick3D
import PinPointStudio

View3D {
    id: root

    // The data source. Bind to an ImuInstance (or any QtObject with quat*/accel* properties).
    // Null-safe: all bindings guard against null so the view can persist with no active instance.
    property QtObject controller: null

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

    // World→scene basis change (Rx(-90°)). The fused IMU quaternion is expressed in
    // the Z-up IMU world frame; the Qt Quick3D scene is Y-up, -Z forward. Without this
    // the cube's rotation axes are scrambled — a device yaw (about world Z) renders as
    // an on-screen roll. Living on this PARENT Node makes it a left-multiply (C · q),
    // matching the composition ArmVizView applies via pinpoint::viz::worldToScene();
    // a conjugation (C · q · C⁻¹) would be wrong for a label-attached rigid body.
    // docs/design/imu_frame_contract.md §6, src/Gui/cameras/viz_frame.h.
    Node {
        rotation: Qt.quaternion(0.70710678, -0.70710678, 0, 0)

        Node {
            // Diagnostic cube: binds the RAW sensor quaternion DIRECTLY in its body frame.
            // The viewing-frame (world→scene) transform belongs on the parent Node above,
            // never here — applying it here would rotate the cube relative to its labelled
            // faces.
            // Null-guard: controller may be null when no IMU is selected.
            rotation: root.controller
                ? Qt.quaternion(root.controller.quatW, root.controller.quatX,
                                root.controller.quatY, root.controller.quatZ)
                : Qt.quaternion(1, 0, 0, 0)

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
                property real ax: root.controller ? root.controller.accelX : 0
                property real ay: root.controller ? root.controller.accelY : 0
                property real az: root.controller ? root.controller.accelZ : 0
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
