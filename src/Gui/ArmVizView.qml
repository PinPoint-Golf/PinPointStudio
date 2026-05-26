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
import QtQuick3D.Helpers
import QtQuick3D.AssetUtils
import PinPoint

// ArmVizView — rigid Y-bot arm segments driven by IMU quaternions.
//
// Each arm (upper arm / forearm / hand) is extracted from ybot.glb as a
// standalone bone-local GLB.  Bone-local means the joint sits at the segment's
// origin, so rotating a Node around that origin produces the correct pivot.
//
// Kinematic chain (per arm):
//   armNode  (positioned at shoulder joint)
//     └─ arm segment Model
//     └─ elbowNode  (at Qt.vector3d(0, 0.274, 0) in arm-local space)
//          └─ forearm segment Model
//          └─ wristNode  (at Qt.vector3d(0, 0.2761, 0) in forearm-local space)
//               └─ hand segment Model
//
// Rotation math (all quaternions world-space from IMUs):
//   armNode.rotation     = armRestQuat * imuUpperArm
//   elbowNode.rotation   = imuUpperArm.inv * imuForearm
//   wristNode.rotation   = imuForearm.inv  * imuHand
//
// IMU assignment (by index in imuManager.instances):
//   index 0 → hand      (IMU on back of hand)
//   index 1 → forearm   (IMU on forearm near wrist)
//   index 2 → upper arm (IMU on upper arm)
//
// Handedness — derived from athleteController.currentHandedness:
//   "Right" (default) → Left arm driven  (lead arm for right-handed golfer)
//   "Left"            → Right arm driven (lead arm for left-handed golfer)

Item {
    id: root

    readonly property bool rightHanded: athleteController.currentHandedness !== "Left"

    onRightHandedChanged: {
        camera.position      = Qt.vector3d(rightHanded ? 0.60 : -0.60, 1.5, 1.2)
        camera.eulerRotation = Qt.vector3d(-8, 0, 0)
    }

    // ── Direct QtObject bindings to each IMU slot ─────────────────────────────
    // Hold the ImuInstance in a named QtObject property so rotation bindings can
    // access imu*.quatW/X/Y/Z directly — the same pattern used in ImuVizView.
    // Intermediate `property quaternion` values are intentionally avoided: value-
    // type properties do not reliably propagate change notifications through a
    // function-argument boundary, so they can silently break the binding chain.
    readonly property QtObject imu0: imuManager.instances.length > 0 ? imuManager.instances[0] : null
    readonly property QtObject imu1: imuManager.instances.length > 1 ? imuManager.instances[1] : null
    readonly property QtObject imu2: imuManager.instances.length > 2 ? imuManager.instances[2] : null

    // ── Quaternion helpers ────────────────────────────────────────────────────

    // Hamilton product of two quaternions (a × b).
    // QML quaternion value type uses .scalar (not .w) for the W component.
    function quatMul(a, b) {
        return Qt.quaternion(
            a.scalar*b.scalar - a.x*b.x - a.y*b.y - a.z*b.z,
            a.scalar*b.x + a.x*b.scalar + a.y*b.z - a.z*b.y,
            a.scalar*b.y - a.x*b.z + a.y*b.scalar + a.z*b.x,
            a.scalar*b.z + a.x*b.y - a.y*b.x + a.z*b.scalar
        )
    }

    // Conjugate = inverse for unit quaternions.
    function quatInv(q) { return Qt.quaternion(q.scalar, -q.x, -q.y, -q.z) }

    // Bone rest-pose world quaternion derived from the ybot.glb inverse-bind
    // matrices.  Maps bone-local Y (= arm length direction) to world ±X so the
    // segment appears in T-pose when the IMU reports identity.
    //   Left arm:  Qt.quaternion( 0.5,  0.5,  0.5, -0.5)  → Y maps to +X
    //   Right arm: Qt.quaternion( 0.5,  0.5, -0.5,  0.5)  → Y maps to -X
    readonly property quaternion leftRestQuat:  Qt.quaternion( 0.5,  0.5,  0.5, -0.5)
    readonly property quaternion rightRestQuat: Qt.quaternion( 0.5,  0.5, -0.5,  0.5)

    // ─────────────────────────────────────────────────────────────────────────
    // 3D Scene
    // ─────────────────────────────────────────────────────────────────────────
    View3D {
        id: view3D
        anchors.fill: parent

        environment: SceneEnvironment {
            clearColor:          Theme.colorBg
            backgroundMode:      SceneEnvironment.Color
            antialiasingMode:    SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // Camera framed on the single lead arm.
        PerspectiveCamera {
            id: camera
            position:      Qt.vector3d(root.rightHanded ? 0.60 : -0.60, 1.5, 1.2)
            eulerRotation: Qt.vector3d(-8, 0, 0)
            fieldOfView:   45
            clipNear:      0.01
            clipFar:       50.0
        }

        Node { id: orbitOrigin; position: Qt.vector3d(root.rightHanded ? 0.60 : -0.60, 1.43, 0) }

        OrbitCameraController {
            anchors.fill: parent
            origin: orbitOrigin
            camera: camera
        }

        // ── Lighting ──────────────────────────────────────────────────────────
        DirectionalLight {
            eulerRotation: Qt.vector3d(-45, 45, 0)
            brightness:    1.2
            color:         "#FFFFFF"
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(30, -60, 0)
            brightness:    0.4
            color:         Theme.colorAccentLight
        }
        PointLight {
            position:      Qt.vector3d(0, 1.8, 1.2)
            brightness:    0.5
            color:         Theme.colorAccent
            quadraticFade: 0.8
        }

        // ── Active (lead) arm — driven by IMUs ────────────────────────────────
        //
        // Each rotation binding accesses imu*.quatW/X/Y/Z directly so QML's
        // binding engine registers them as live dependencies.  Intermediate
        // `property quaternion` values are not used — see comment above.
        Node {
            id: armNode
            position: root.rightHanded ? Qt.vector3d( 0.1876, 1.4357, -0.0617)
                                       : Qt.vector3d(-0.1876, 1.4356, -0.0617)
            // rotation = armRestQuat * imuUpperArm
            rotation: {
                var ua = root.imu2
                    ? Qt.quaternion(root.imu2.quatW, root.imu2.quatX, root.imu2.quatY, root.imu2.quatZ)
                    : Qt.quaternion(1, 0, 0, 0)
                return root.quatMul(root.rightHanded ? root.leftRestQuat : root.rightRestQuat, ua)
            }

            RuntimeLoader {
                source: root.rightHanded ? "qrc:/assets/body/arm_LeftArm.glb"
                                         : "qrc:/assets/body/arm_RightArm.glb"
            }

            Node {
                position: Qt.vector3d(0, 0.274, 0)
                // rotation = imuUpperArm.inv * imuForearm
                rotation: {
                    var ua = root.imu2
                        ? Qt.quaternion(root.imu2.quatW, root.imu2.quatX, root.imu2.quatY, root.imu2.quatZ)
                        : Qt.quaternion(1, 0, 0, 0)
                    var fa = root.imu1
                        ? Qt.quaternion(root.imu1.quatW, root.imu1.quatX, root.imu1.quatY, root.imu1.quatZ)
                        : Qt.quaternion(1, 0, 0, 0)
                    return root.quatMul(root.quatInv(ua), fa)
                }

                RuntimeLoader {
                    source: root.rightHanded ? "qrc:/assets/body/arm_LeftForeArm.glb"
                                             : "qrc:/assets/body/arm_RightForeArm.glb"
                }

                Node {
                    position: Qt.vector3d(0, 0.2761, 0)
                    // rotation = imuForearm.inv * imuHand
                    rotation: {
                        var fa = root.imu1
                            ? Qt.quaternion(root.imu1.quatW, root.imu1.quatX, root.imu1.quatY, root.imu1.quatZ)
                            : Qt.quaternion(1, 0, 0, 0)
                        var ha = root.imu0
                            ? Qt.quaternion(root.imu0.quatW, root.imu0.quatX, root.imu0.quatY, root.imu0.quatZ)
                            : Qt.quaternion(1, 0, 0, 0)
                        return root.quatMul(root.quatInv(fa), ha)
                    }

                    RuntimeLoader {
                        source: root.rightHanded ? "qrc:/assets/body/arm_LeftHand.glb"
                                                 : "qrc:/assets/body/arm_RightHand.glb"
                    }
                }
            }
        }

    }

    // ── Diagnostic overlay ── remove once animation is confirmed working ──────
    Rectangle {
        anchors { top: parent.top; left: parent.left; margins: Theme.sp(6) }
        width: diagText.width + Theme.sp(12); height: diagText.height + Theme.sp(8)
        color: "#AA000000"; radius: Theme.sp(4)
        Text {
            id: diagText
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 11; font.family: Theme.fontBody
            text: {
                var n = imuManager.instances.length
                var w = root.imu2 ? root.imu2.quatW.toFixed(4) : "—"
                var x = root.imu2 ? root.imu2.quatX.toFixed(4) : "—"
                var y = root.imu2 ? root.imu2.quatY.toFixed(4) : "—"
                var z = root.imu2 ? root.imu2.quatZ.toFixed(4) : "—"
                var nr = armNode.rotation
                var nw = nr.scalar.toFixed(4)
                var nx = nr.x.toFixed(4)
                return "instances: " + n + "  imu2: " + (root.imu2 ? "ok" : "NULL") +
                       "\nimu  W:" + w + " X:" + x + " Y:" + y + " Z:" + z +
                       "\nnode W:" + nw + " X:" + nx + " …"
            }
        }
    }

    // ── IMU assignment legend ─────────────────────────────────────────────────
    Column {
        anchors { bottom: parent.bottom; left: parent.left; margins: Theme.sp(10) }
        spacing: Theme.sp(4)

        Repeater {
            model: [
                { label: qsTr("IMU 0 → Hand"),      idx: 0 },
                { label: qsTr("IMU 1 → Wrist"),     idx: 1 },
                { label: qsTr("IMU 2 → Upper arm"), idx: 2 }
            ]
            delegate: Row {
                spacing: Theme.sp(6)
                property bool live: {
                    var inst = imuManager.instances
                    return inst && modelData.idx < inst.length &&
                           inst[modelData.idx] && inst[modelData.idx].imuConnected
                }
                Rectangle {
                    width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                    anchors.verticalCenter: parent.verticalCenter
                    color: parent.live ? Theme.colorGood : Theme.colorText3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
                Text {
                    text:           modelData.label
                    color:          parent.live ? Theme.colorText2 : Theme.colorText3
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzLabel
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
            }
        }
    }

    // ── Orbit hint ────────────────────────────────────────────────────────────
    Text {
        anchors { bottom: parent.bottom; right: parent.right; margins: Theme.sp(10) }
        text:           qsTr("Drag · Scroll to zoom")
        color:          Theme.colorText3
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzLabel
    }
}
