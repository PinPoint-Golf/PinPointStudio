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
import PinPoint

// ArmVizView — stylised robotic arm driven by three ImuInstance quaternions.
//
// IMU assignment (by index in imuManager.instances):
//   index 0 → upper arm   (IMU on upper arm)
//   index 1 → forearm     (IMU on wrist)
//   index 2 → hand        (IMU on back of hand)
//
// If fewer than three IMUs are connected the unassigned segments remain in
// their default orientation.  The view is purely cosmetic — no C++ changes
// are required beyond what already exists in ImuInstance.

Item {
    id: root

    // ── Segment dimensions (scene units; arm hangs along -Y from shoulder) ─
    readonly property real upperArmLength: 28.0
    readonly property real forearmLength:  25.0
    readonly property real handLength:     10.0
    readonly property real handWidth:       8.0
    readonly property real handDepth:       2.5
    readonly property real segmentRadius:   3.2
    readonly property real jointRadius:     3.8

    // ── Convenience accessors into imuManager.instances ───────────────────
    // Returns null-safe quaternion components from a given instance index.
    function imuQuat(idx) {
        var inst = imuManager.instances
        if (!inst || idx >= inst.length || !inst[idx]) return Qt.quaternion(1, 0, 0, 0)
        var c = inst[idx]
        return Qt.quaternion(c.quatW, c.quatX, c.quatY, c.quatZ)
    }

    // Helper — convert QQuaternion to euler angles (degrees) for Node.eulerRotation.
    // QtQuick3D Nodes accept eulerRotation as vector3d (pitch, yaw, roll).
    // We decompose via the standard trig formulas matching QQuaternion::toEulerAngles.
    function quatToEuler(q) {
        // Clamp to avoid NaN at singularities
        var sinr_cosp = 2.0 * (q.scalar * q.x + q.y * q.z)
        var cosr_cosp = 1.0 - 2.0 * (q.x * q.x + q.y * q.y)
        var roll  = Math.atan2(sinr_cosp, cosr_cosp) * 180.0 / Math.PI

        var sinp = 2.0 * (q.scalar * q.y - q.z * q.x)
        sinp = Math.max(-1.0, Math.min(1.0, sinp))
        var pitch = Math.asin(sinp) * 180.0 / Math.PI

        var siny_cosp = 2.0 * (q.scalar * q.z + q.x * q.y)
        var cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z)
        var yaw   = Math.atan2(siny_cosp, cosy_cosp) * 180.0 / Math.PI

        return Qt.vector3d(pitch, yaw, roll)
    }

    // ─────────────────────────────────────────────────────────────────────
    // 3D Scene
    // ─────────────────────────────────────────────────────────────────────
    View3D {
        id: view3D
        anchors.fill: parent

        environment: SceneEnvironment {
            clearColor: Theme.colorBg
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // ── Camera ────────────────────────────────────────────────────────
        PerspectiveCamera {
            id: camera
            position:      Qt.vector3d(0, 20, 140)
            eulerRotation: Qt.vector3d(-10, 0, 0)
            clipNear: 1.0
            clipFar:  1000.0
        }

        // ── Orbit controls — mouse drag / pinch to zoom ───────────────────
        OrbitCameraController {
            anchors.fill: parent
            origin: Qt.vector3d(0, 0, 0)
            camera: camera
        }

        // ── Lighting ──────────────────────────────────────────────────────
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
            position:      Qt.vector3d(0, 80, 60)
            brightness:    0.5
            color:         Theme.colorAccent
            quadraticFade: 0.01
        }

        // ── Materials ─────────────────────────────────────────────────────
        PrincipledMaterial {
            id: segmentMat
            baseColor: Theme.colorAccent
            metalness: 0.55
            roughness: 0.35
        }
        PrincipledMaterial {
            id: jointMat
            baseColor: Theme.colorWarn
            metalness: 0.75
            roughness: 0.20
        }
        PrincipledMaterial {
            id: palmMat
            baseColor: Theme.colorAccent
            metalness: 0.55
            roughness: 0.35
            cullMode: Material.NoCulling
        }

        // ═════════════════════════════════════════════════════════════════
        // ARM HIERARCHY
        //
        // Coordinate convention (arm hanging at side):
        //   +Y = proximal → distal (up the segment)
        //   The pivot of each Node is at the proximal joint.
        //   Geometry is offset by -Y * (length/2) so it hangs below pivot.
        //
        // Orientation: world-space absolute quaternions from IMU fusion.
        // Position chaining uses invisible helper Nodes at each distal tip
        // so scenePosition can be read without manual vector maths in JS.
        // ═════════════════════════════════════════════════════════════════

        // Shoulder joint sphere (fixed reference)
        Model {
            source: "#Sphere"
            scale:  Qt.vector3d(jointRadius/50, jointRadius/50, jointRadius/50)
            materials: jointMat
        }

        // ── UPPER ARM ─────────────────────────────────────────────────────
        Node {
            id: upperArmNode
            eulerRotation: root.quatToEuler(root.imuQuat(0))

            // Cylinder body
            Model {
                source:   "#Cylinder"
                position: Qt.vector3d(0, -upperArmLength / 2, 0)
                scale:    Qt.vector3d(segmentRadius/50, upperArmLength/2/50, segmentRadius/50)
                materials: segmentMat
            }
            // Elbow cap ring
            Model {
                source:   "#Cylinder"
                position: Qt.vector3d(0, -upperArmLength, 0)
                scale:    Qt.vector3d(segmentRadius*1.1/50, 1.0/50, segmentRadius*1.1/50)
                materials: jointMat
            }
            // Elbow joint sphere
            Model {
                source:   "#Sphere"
                position: Qt.vector3d(0, -upperArmLength, 0)
                scale:    Qt.vector3d(jointRadius/50, jointRadius/50, jointRadius/50)
                materials: jointMat
            }
        }

        // ── FOREARM ───────────────────────────────────────────────────────
        Node {
            id: forearmNode
            position:      elbowHelper.scenePosition
            eulerRotation: root.quatToEuler(root.imuQuat(1))

            Model {
                source:   "#Cylinder"
                position: Qt.vector3d(0, -forearmLength / 2, 0)
                scale:    Qt.vector3d(segmentRadius*0.88/50, forearmLength/2/50, segmentRadius*0.88/50)
                materials: segmentMat
            }
            // Wrist cap ring
            Model {
                source:   "#Cylinder"
                position: Qt.vector3d(0, -forearmLength, 0)
                scale:    Qt.vector3d(segmentRadius/50, 1.0/50, segmentRadius/50)
                materials: jointMat
            }
            // Wrist joint sphere
            Model {
                source:   "#Sphere"
                position: Qt.vector3d(0, -forearmLength, 0)
                scale:    Qt.vector3d(jointRadius*0.9/50, jointRadius*0.9/50, jointRadius*0.9/50)
                materials: jointMat
            }
        }

        // ── HAND + FINGERS ────────────────────────────────────────────────
        Node {
            id: handNode
            position:      wristHelper.scenePosition
            eulerRotation: root.quatToEuler(root.imuQuat(2))

            // Palm plate — trapezoidal prism: narrow at wrist (y=0), wide at finger base (y=-handLength)
            // ProceduralMesh vertices are in scene units; no /50 scaling needed.
            Model {
                materials: palmMat
                geometry: ProceduralMesh {
                    property real wH: root.handWidth * 0.20   // wrist half-width  (1.6 units)
                    property real fH: root.handWidth * 0.32   // finger half-width (2.56 units, matches outermost finger x-offsets)
                    property real pL: root.handLength          // palm length       (10 units)
                    property real hD: root.handDepth * 0.5    // half-depth        (1.25 units)
                    // 6 faces × 4 vertices; NoCulling on palmMat so winding is irrelevant.
                    positions: [
                        // Wrist end (y=0)
                        Qt.vector3d(-wH, 0,  -hD), Qt.vector3d( wH, 0,  -hD),
                        Qt.vector3d( wH, 0,   hD), Qt.vector3d(-wH, 0,   hD),
                        // Finger end (y=-pL)
                        Qt.vector3d(-fH, -pL, -hD), Qt.vector3d( fH, -pL, -hD),
                        Qt.vector3d( fH, -pL,  hD), Qt.vector3d(-fH, -pL,  hD),
                        // Front face (z=-hD)
                        Qt.vector3d(-wH,  0,  -hD), Qt.vector3d( wH,  0,  -hD),
                        Qt.vector3d( fH, -pL, -hD), Qt.vector3d(-fH, -pL, -hD),
                        // Back face (z=+hD)
                        Qt.vector3d( wH,  0,   hD), Qt.vector3d(-wH,  0,   hD),
                        Qt.vector3d(-fH, -pL,  hD), Qt.vector3d( fH, -pL,  hD),
                        // Left face
                        Qt.vector3d(-wH,  0,  -hD), Qt.vector3d(-wH,  0,   hD),
                        Qt.vector3d(-fH, -pL,  hD), Qt.vector3d(-fH, -pL, -hD),
                        // Right face
                        Qt.vector3d( wH,  0,   hD), Qt.vector3d( wH,  0,  -hD),
                        Qt.vector3d( fH, -pL, -hD), Qt.vector3d( fH, -pL,  hD),
                    ]
                    normals: [
                        Qt.vector3d(0, 1,0), Qt.vector3d(0, 1,0), Qt.vector3d(0, 1,0), Qt.vector3d(0, 1,0),
                        Qt.vector3d(0,-1,0), Qt.vector3d(0,-1,0), Qt.vector3d(0,-1,0), Qt.vector3d(0,-1,0),
                        Qt.vector3d(0,0,-1), Qt.vector3d(0,0,-1), Qt.vector3d(0,0,-1), Qt.vector3d(0,0,-1),
                        Qt.vector3d(0,0, 1), Qt.vector3d(0,0, 1), Qt.vector3d(0,0, 1), Qt.vector3d(0,0, 1),
                        Qt.vector3d(-1,0,0), Qt.vector3d(-1,0,0), Qt.vector3d(-1,0,0), Qt.vector3d(-1,0,0),
                        Qt.vector3d( 1,0,0), Qt.vector3d( 1,0,0), Qt.vector3d( 1,0,0), Qt.vector3d( 1,0,0),
                    ]
                    indexes: [
                         0, 1, 2,  0, 2, 3,
                         4, 6, 5,  4, 7, 6,
                         8, 9,10,  8,10,11,
                        12,13,14, 12,14,15,
                        16,17,18, 16,18,19,
                        20,21,22, 20,22,23,
                    ]
                }
            }

            // ── Four fingers ──────────────────────────────────────────────
            // fingerData: [ xOffset, proxLen, midLen, distLen, radius ]
            property var fingerData: [
                [ -handWidth*0.30, handLength*0.42, handLength*0.26, handLength*0.18, segmentRadius*0.48 ], // index
                [ -handWidth*0.10, handLength*0.46, handLength*0.29, handLength*0.20, segmentRadius*0.50 ], // middle
                [  handWidth*0.10, handLength*0.44, handLength*0.27, handLength*0.19, segmentRadius*0.48 ], // ring
                [  handWidth*0.30, handLength*0.36, handLength*0.22, handLength*0.15, segmentRadius*0.40 ]  // pinky
            ]

            Repeater3D {
                model: handNode.fingerData.length
                delegate: Node {
                    required property int index
                    id: fRoot
                    position: Qt.vector3d(handNode.fingerData[index][0], -handLength, 0)

                    property real proxLen: handNode.fingerData[index][1]
                    property real midLen:  handNode.fingerData[index][2]
                    property real distLen: handNode.fingerData[index][3]
                    property real fRad:    handNode.fingerData[index][4]
                    property real kRad:    fRad * 1.25

                    // MCP knuckle
                    Model { source:"#Sphere"; scale: Qt.vector3d(fRoot.kRad/50, fRoot.kRad/50, fRoot.kRad/50); materials: jointMat }

                    // Proximal phalanx
                    Model {
                        source: "#Cylinder"
                        position: Qt.vector3d(0, -fRoot.proxLen/2, 0)
                        scale: Qt.vector3d(fRoot.fRad/50, fRoot.proxLen/2/50, fRoot.fRad/50)
                        materials: segmentMat
                    }
                    // PIP knuckle
                    Model {
                        source: "#Sphere"
                        position: Qt.vector3d(0, -fRoot.proxLen, 0)
                        scale: Qt.vector3d(fRoot.kRad*0.9/50, fRoot.kRad*0.9/50, fRoot.kRad*0.9/50)
                        materials: jointMat
                    }
                    // Middle phalanx
                    Model {
                        source: "#Cylinder"
                        position: Qt.vector3d(0, -(fRoot.proxLen + fRoot.midLen/2), 0)
                        scale: Qt.vector3d(fRoot.fRad*0.9/50, fRoot.midLen/2/50, fRoot.fRad*0.9/50)
                        materials: segmentMat
                    }
                    // DIP knuckle
                    Model {
                        source: "#Sphere"
                        position: Qt.vector3d(0, -(fRoot.proxLen + fRoot.midLen), 0)
                        scale: Qt.vector3d(fRoot.kRad*0.78/50, fRoot.kRad*0.78/50, fRoot.kRad*0.78/50)
                        materials: jointMat
                    }
                    // Distal phalanx
                    Model {
                        source: "#Cylinder"
                        position: Qt.vector3d(0, -(fRoot.proxLen + fRoot.midLen + fRoot.distLen/2), 0)
                        scale: Qt.vector3d(fRoot.fRad*0.78/50, fRoot.distLen/2/50, fRoot.fRad*0.78/50)
                        materials: segmentMat
                    }
                    // Fingertip cap
                    Model {
                        source: "#Sphere"
                        position: Qt.vector3d(0, -(fRoot.proxLen + fRoot.midLen + fRoot.distLen), 0)
                        scale: Qt.vector3d(fRoot.fRad*0.78/50, fRoot.fRad*0.78/50, fRoot.fRad*0.78/50)
                        materials: segmentMat
                    }
                }
            }

            // ── Thumb ─────────────────────────────────────────────────────
            Node {
                id: thumbRoot
                position:      Qt.vector3d(-handWidth * 0.50, -handLength * 0.35, 0)
                eulerRotation: Qt.vector3d(0, 0, -50)

                property real proxLen: handLength * 0.34
                property real distLen: handLength * 0.26
                property real tRad:    segmentRadius * 0.52
                property real kRad:    tRad * 1.25

                Model { source:"#Sphere"; scale: Qt.vector3d(thumbRoot.kRad/50, thumbRoot.kRad/50, thumbRoot.kRad/50); materials: jointMat }
                Model {
                    source: "#Cylinder"
                    position: Qt.vector3d(0, -thumbRoot.proxLen/2, 0)
                    scale: Qt.vector3d(thumbRoot.tRad/50, thumbRoot.proxLen/2/50, thumbRoot.tRad/50)
                    materials: segmentMat
                }
                Model {
                    source: "#Sphere"
                    position: Qt.vector3d(0, -thumbRoot.proxLen, 0)
                    scale: Qt.vector3d(thumbRoot.kRad*0.88/50, thumbRoot.kRad*0.88/50, thumbRoot.kRad*0.88/50)
                    materials: jointMat
                }
                Model {
                    source: "#Cylinder"
                    position: Qt.vector3d(0, -(thumbRoot.proxLen + thumbRoot.distLen/2), 0)
                    scale: Qt.vector3d(thumbRoot.tRad*0.88/50, thumbRoot.distLen/2/50, thumbRoot.tRad*0.88/50)
                    materials: segmentMat
                }
                Model {
                    source: "#Sphere"
                    position: Qt.vector3d(0, -(thumbRoot.proxLen + thumbRoot.distLen), 0)
                    scale: Qt.vector3d(thumbRoot.tRad*0.88/50, thumbRoot.tRad*0.88/50, thumbRoot.tRad*0.88/50)
                    materials: segmentMat
                }
            }
        } // handNode

        // ═════════════════════════════════════════════════════════════════
        // POSITION HELPER NODES
        // Invisible nodes at each segment's distal tip.
        // Their scenePosition drives the next segment's world position,
        // avoiding manual quaternion-vector rotation in JS.
        // ═════════════════════════════════════════════════════════════════
        Node {
            id: elbowHelper
            parent:   upperArmNode
            position: Qt.vector3d(0, -upperArmLength, 0)
        }
        Node {
            id: wristHelper
            parent:   forearmNode
            position: Qt.vector3d(0, -forearmLength, 0)
        }

    } // View3D

    // ── IMU assignment legend ─────────────────────────────────────────────
    Column {
        anchors { bottom: parent.bottom; left: parent.left; margins: Theme.sp(10) }
        spacing: Theme.sp(4)

        Repeater {
            model: [
                { label: qsTr("IMU 0 → Upper arm"), idx: 0 },
                { label: qsTr("IMU 1 → Forearm"),   idx: 1 },
                { label: qsTr("IMU 2 → Hand"),       idx: 2 }
            ]
            delegate: Row {
                spacing: Theme.sp(6)
                property bool live: {
                    var inst = imuManager.instances
                    return inst && modelData.idx < inst.length && inst[modelData.idx] && inst[modelData.idx].imuConnected
                }
                Rectangle {
                    width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                    anchors.verticalCenter: parent.verticalCenter
                    color: parent.live ? Theme.colorGood : Theme.colorText3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
                Text {
                    text:       modelData.label
                    color:      parent.live ? Theme.colorText2 : Theme.colorText3
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzLabel
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
            }
        }
    }

    // Orbit hint
    Text {
        anchors { bottom: parent.bottom; right: parent.right; margins: Theme.sp(10) }
        text:           qsTr("Drag · Scroll to zoom")
        color:          Theme.colorText3
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzLabel
    }

} // root
