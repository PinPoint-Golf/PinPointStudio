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
// IMU slot assignment (Wrist Motion session):
//   Slot A → Wrist  (forearm near wrist) — the calibrated IMU
//   Slot B → Hand   (back of hand)
//   Slot C → Upper arm (optional)
//
// Instances are resolved by slot assignment, not by position in
// imuManager.instances, because instances() iterates a QMap in device-ID order.
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

    // ── Per-slot IMU bindings ─────────────────────────────────────────────────
    // Resolved by slot assignment (A/B/C from appSettings.imuPlacement) so the
    // correct ImuInstance drives each segment regardless of device-ID ordering.
    // The `var _dep = imuManager.instances` forces re-evaluation when any
    // instance is created or destroyed (instanceFor() is a Q_INVOKABLE, not a
    // reactive property, so the explicit dep is required).
    readonly property QtObject imuSlotA: {   // Wrist (forearm) — calibrated
        var _dep      = imuManager.instances
        var placement = appSettings.imuPlacement
        var list      = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i)
            if (placement[list[i].id] === "A")
                return imuManager.instanceFor(list[i].id)
        return null
    }
    readonly property QtObject imuSlotB: {   // Hand
        var _dep      = imuManager.instances
        var placement = appSettings.imuPlacement
        var list      = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i)
            if (placement[list[i].id] === "B")
                return imuManager.instanceFor(list[i].id)
        return null
    }
    readonly property QtObject imuSlotC: {   // Upper arm (optional)
        var _dep      = imuManager.instances
        var placement = appSettings.imuPlacement
        var list      = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i)
            if (placement[list[i].id] === "C")
                return imuManager.instanceFor(list[i].id)
        return null
    }

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

    // Apply calibration transform to a raw IMU quaternion if the instance is calibrated.
    // q_segment = calibTransform * q_raw
    // Returns q_raw unchanged if uncalibrated or instance is null.
    // IMPORTANT: QML quaternion uses .scalar for W — .w returns undefined.
    function quatApplyCalib(imuInst, raw) {
        if (!imuInst || !imuInst.calibrated) return raw
        var cal = Qt.quaternion(imuInst.calibTransform.scalar,
                                imuInst.calibTransform.x,
                                imuInst.calibTransform.y,
                                imuInst.calibTransform.z)
        return quatMul(cal, raw)
    }

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
        // Each rotation binding accesses imuSlot*.quatW/X/Y/Z directly so QML's
        // binding engine registers them as live dependencies.  Intermediate
        // `property quaternion` values are not used — see comment above.
        Node {
            id: armNode
            position: root.rightHanded ? Qt.vector3d( 0.1876, 1.4357, -0.0617)
                                       : Qt.vector3d(-0.1876, 1.4356, -0.0617)
            // rotation = armRestQuat * slotC(upperArm)
            rotation: {
                var raw = root.imuSlotC
                    ? Qt.quaternion(root.imuSlotC.quatW, root.imuSlotC.quatX, root.imuSlotC.quatY, root.imuSlotC.quatZ)
                    : Qt.quaternion(1, 0, 0, 0)
                var ua = root.quatApplyCalib(root.imuSlotC, raw)
                return root.quatMul(root.rightHanded ? root.leftRestQuat : root.rightRestQuat, ua)
            }

            RuntimeLoader {
                source: root.rightHanded ? "qrc:/assets/body/arm_LeftArm.glb"
                                         : "qrc:/assets/body/arm_RightArm.glb"
            }

            Node {
                position: Qt.vector3d(0, 0.274, 0)
                // rotation = slotC(upperArm).inv * slotA(wrist/forearm, calibrated)
                rotation: {
                    var rawUa = root.imuSlotC
                        ? Qt.quaternion(root.imuSlotC.quatW, root.imuSlotC.quatX, root.imuSlotC.quatY, root.imuSlotC.quatZ)
                        : Qt.quaternion(1, 0, 0, 0)
                    var rawFa = root.imuSlotA
                        ? Qt.quaternion(root.imuSlotA.quatW, root.imuSlotA.quatX, root.imuSlotA.quatY, root.imuSlotA.quatZ)
                        : Qt.quaternion(1, 0, 0, 0)
                    var ua = root.quatApplyCalib(root.imuSlotC, rawUa)
                    var fa = root.quatApplyCalib(root.imuSlotA, rawFa)
                    return root.quatMul(root.quatInv(ua), fa)
                }

                RuntimeLoader {
                    source: root.rightHanded ? "qrc:/assets/body/arm_LeftForeArm.glb"
                                             : "qrc:/assets/body/arm_RightForeArm.glb"
                }

                Node {
                    position: Qt.vector3d(0, 0.2761, 0)
                    // rotation = slotA(wrist/forearm).inv * slotB(hand)
                    rotation: {
                        var rawFa = root.imuSlotA
                            ? Qt.quaternion(root.imuSlotA.quatW, root.imuSlotA.quatX, root.imuSlotA.quatY, root.imuSlotA.quatZ)
                            : Qt.quaternion(1, 0, 0, 0)
                        var rawHa = root.imuSlotB
                            ? Qt.quaternion(root.imuSlotB.quatW, root.imuSlotB.quatX, root.imuSlotB.quatY, root.imuSlotB.quatZ)
                            : Qt.quaternion(1, 0, 0, 0)
                        var fa = root.quatApplyCalib(root.imuSlotA, rawFa)
                        var ha = root.quatApplyCalib(root.imuSlotB, rawHa)
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
                var w = root.imuSlotA ? root.imuSlotA.quatW.toFixed(4) : "—"
                var x = root.imuSlotA ? root.imuSlotA.quatX.toFixed(4) : "—"
                var y = root.imuSlotA ? root.imuSlotA.quatY.toFixed(4) : "—"
                var z = root.imuSlotA ? root.imuSlotA.quatZ.toFixed(4) : "—"
                var nr = armNode.rotation
                var nw = nr.scalar.toFixed(4)
                var nx = nr.x.toFixed(4)
                return "instances: " + n + "  slotA: " + (root.imuSlotA ? "ok" : "NULL") +
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
                qsTr("Slot A · Wrist"),
                qsTr("Slot B · Hand"),
                qsTr("Slot C · Upper arm")
            ]
            delegate: Row {
                spacing: Theme.sp(6)
                property bool live: {
                    var _dep = imuManager.instances
                    var inst = index === 0 ? root.imuSlotA
                             : index === 1 ? root.imuSlotB
                             : root.imuSlotC
                    return inst !== null && inst.imuConnected
                }
                Rectangle {
                    width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                    anchors.verticalCenter: parent.verticalCenter
                    color: parent.live ? Theme.colorGood : Theme.colorText3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
                Text {
                    text:           modelData
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
