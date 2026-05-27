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

// BodyVizView — full Y-bot body assembled from individual bone-local GLBs.
//
// Each segment is extracted from ybot.glb by tools/extract_body_segments.py,
// which uses the GLTF inverse-bind matrices to place the joint at the origin
// of each segment's local coordinate system.
//
// Kinematic chain (all positions are parent-local, rotations are rest-pose):
//
//   hipsNode       ← pelvis, world y≈1.0
//     spineNode      (0, 0.099, -0.012)
//       spine1Node   (0, 0.117, 0)
//         spine2Node (0, 0.135, 0)
//           neckNode   (0, 0.150, 0.009)  [no mesh — no distinct Y-bot neck]
//             headNode (0, 0.103, 0.031)
//           leftShoulderNode  (+0.061, 0.091, 0.008)
//             leftArmNode     (0, 0.129, 0)
//               leftForeArmNode  (0, 0.274, 0)
//                 leftHandNode   (0, 0.276, 0)
//           rightShoulderNode (mirror)
//     leftUpLegNode  (+0.091, -0.067, 0)
//       leftLegNode  (0, 0.406, 0)   [+Y is bone-local down — UpLeg is 180° around Z]
//         leftFootNode (0, 0.421, 0)
//     rightUpLegNode (mirror)
//
// Rest-pose quaternions are the local-space rotations read from the bind matrices
// (see extract_body_segments.py diagnostic output for derivation).
// Near-identity rotations (<0.1° deviation) are left at default (identity) to
// avoid unnecessary computation.
//
// Phase 2 will add:
//   property QtObject poseSource  — drives bone rotations from a VideoController
//   BodyPoseAdapter               — 2D keypoints → per-bone quaternions
//   60 Hz Timer                   — animates rotations via slerp

Item {
    id: root

    // Face-on VideoController — drives per-bone animation when non-null.
    property QtObject poseSource: null

    // true when the camera delivers a horizontally mirrored image (typical webcam).
    // false for industrial cameras that deliver non-mirrored frames.
    property bool mirroredSource: true

    // BodyPoseAdapter converts keypoints → slerped per-bone quaternions at 60 Hz.
    BodyPoseAdapter {
        id: adapter
        poseSource:     root.poseSource
        mirroredSource: root.mirroredSource
    }

    // Set true to show a small sphere at every joint pivot — useful for
    // verifying that parent-local offsets are correct.
    property bool showJoints: false

    // ── Arm override properties — used by the calibration wizard step ─────────
    // When false (default), arm rotations are driven by the BodyPoseAdapter.
    // When true, the lead/trail arm rotations are overridden directly so the
    // calibration screen can pose the body independently of camera input.

    // true = left arm is lead arm (right-handed golfer), false = right arm is lead
    property bool rightHanded: true

    // Highlight the lead arm with a coloured overlay — used to indicate which
    // arm the user should position during calibration.
    property bool  highlightLeadArm: false
    property color leadArmColor:     "#5B9BD5"

    // Lead arm override (calibration phase 0 = arm-down, phase 2 = T-pose guide)
    property bool       useLeadArmOverride:          false
    property quaternion leadArmOverrideRotation:     Qt.quaternion(1, 0, 0, 0)
    property quaternion leadForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

    // Trail arm override — keeps the trail arm in a natural relaxed hang
    // regardless of what the BodyPoseAdapter would otherwise compute.
    property bool       useTrailArmOverride:          false
    property quaternion trailArmOverrideRotation:     Qt.quaternion(1, 0, 0, 0)
    property quaternion trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

    // When true, animate leadArmOverrideRotation changes via slerp (1.5 s).
    // Used by the calibration wizard to animate the guide from arm-down to T-pose.
    // Implemented with NumberAnimation + JS slerp to avoid QuaternionAnimation,
    // whose from/to properties shadow base-class members and generate Qt warnings.
    property bool animateLeadArm:     false
    property int  leadArmAnimDuration: 1500

    // Internal slerp state — not part of the public API.
    property quaternion _leadArmFrom: Qt.quaternion(1, 0, 0, 0)
    property quaternion _leadArmTo:   Qt.quaternion(1, 0, 0, 0)
    property real       _leadArmT:    1.0   // 0 → 1 driven by NumberAnimation

    function _slerp(a, b, t) {
        var dot = a.scalar*b.scalar + a.x*b.x + a.y*b.y + a.z*b.z
        if (dot < 0) { b = Qt.quaternion(-b.scalar, -b.x, -b.y, -b.z); dot = -dot }
        dot = Math.min(dot, 1.0)
        if (dot > 0.9995) {
            return Qt.quaternion(a.scalar + t*(b.scalar - a.scalar),
                                 a.x      + t*(b.x      - a.x),
                                 a.y      + t*(b.y      - a.y),
                                 a.z      + t*(b.z      - a.z))
        }
        var theta0    = Math.acos(dot)
        var theta     = theta0 * t
        var sinTheta  = Math.sin(theta)
        var sinTheta0 = Math.sin(theta0)
        var s0 = Math.cos(theta) - dot * sinTheta / sinTheta0
        var s1 = sinTheta / sinTheta0
        return Qt.quaternion(s0*a.scalar + s1*b.scalar,
                             s0*a.x      + s1*b.x,
                             s0*a.y      + s1*b.y,
                             s0*a.z      + s1*b.z)
    }

    function resetArmAnimation(fromQ) {
        _leadArmAnim.stop()
        _leadArmFrom = fromQ
        _leadArmT    = 1.0
    }

    NumberAnimation {
        id: _leadArmAnim
        target:   root
        property: "_leadArmT"
        from:     0.0; to: 1.0
        duration: root.leadArmAnimDuration
        easing.type: Easing.InOutCubic
    }

    onLeadArmOverrideRotationChanged: {
        if (!root.animateLeadArm) return
        root._leadArmFrom = _leadArmT < 1.0
            ? root._slerp(root._leadArmFrom, root._leadArmTo, root._leadArmT)
            : root._leadArmFrom
        root._leadArmTo = root.leadArmOverrideRotation
        root._leadArmT  = 0.0
        _leadArmAnim.restart()
    }

    // ── Loading state ─────────────────────────────────────────────────────────
    // Each RuntimeLoader reports Success (2), Loading (1), or Error (3).
    // Tally successes so we can show a progress indicator until all 19 segments load.
    property int loadedCount:  0
    property int totalSegments: 19   // 13 body + 6 arm (LeftArm/ForeArm/Hand ×2)
    readonly property bool fullyLoaded: loadedCount >= totalSegments

    function onSegmentLoaded(status) {
        if (status === RuntimeLoader.Success) loadedCount++
    }

    // ── Debug joint sphere component ──────────────────────────────────────────
    component JointMarker: Model {
        visible: root.showJoints
        source:  "#Sphere"
        scale:   Qt.vector3d(0.008, 0.008, 0.008)
        materials: PrincipledMaterial { baseColor: "#FF4444"; metalness: 0; roughness: 0.5 }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3D Scene
    // ─────────────────────────────────────────────────────────────────────────
    // QML Rectangle provides the background so its color exactly matches the
    // surrounding UI — View3D's clearColor undergoes tonemapping that causes a
    // visible mismatch when the 3D viewport is large (e.g. calibration panel).
    Rectangle {
        anchors.fill: parent
        color: Theme.colorBg
    }

    View3D {
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode:      SceneEnvironment.Transparent
            antialiasingMode:    SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // ── Camera ────────────────────────────────────────────────────────────
        // Character is ~1.8 world units tall; orbit origin at mid-chest height.
        PerspectiveCamera {
            id: camera
            position:    Qt.vector3d(0, 0.9, 3.5)
            fieldOfView: 45
            clipNear:    0.01
            clipFar:     100.0
        }

        Node { id: orbitOrigin; position: Qt.vector3d(0, 0.9, 0) }

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
            position:      Qt.vector3d(0, 2.0, 1.5)
            brightness:    0.5
            color:         Theme.colorAccent
            quadraticFade: 0.8
        }

        // ═════════════════════════════════════════════════════════════════════
        // Kinematic chain root — Hips (pelvis)
        // World position extracted from ybot.glb bind matrix.
        // ═════════════════════════════════════════════════════════════════════
        Node {
            id: hipsNode
            position: Qt.vector3d(0, 0.9979, 0)
            rotation: adapter.hipsRotation

            JointMarker {}
            RuntimeLoader {
                source: "qrc:/assets/body/body_Hips.glb"
                onStatusChanged: root.onSegmentLoaded(status)
            }

            // ── Spine chain ───────────────────────────────────────────────────
            Node {
                id: spineNode
                position: Qt.vector3d(0, 0.0992, -0.0123)
                rotation: Qt.quaternion(0.9982, -0.0607, 0, 0)

                JointMarker {}
                RuntimeLoader {
                    source: "qrc:/assets/body/body_Spine.glb"
                    onStatusChanged: root.onSegmentLoaded(status)
                }

                Node {
                    id: spine1Node
                    position: Qt.vector3d(0, 0.1173, 0)

                    JointMarker {}
                    RuntimeLoader {
                        source: "qrc:/assets/body/body_Spine1.glb"
                        onStatusChanged: root.onSegmentLoaded(status)
                    }

                    Node {
                        id: spine2Node
                        position: Qt.vector3d(0, 0.1346, 0)
                        rotation: Qt.quaternion(0.9983, 0.0577, 0, 0)

                        JointMarker {}
                        RuntimeLoader {
                            source: "qrc:/assets/body/body_Spine2.glb"
                            onStatusChanged: root.onSegmentLoaded(status)
                        }

                        // ── Neck → Head ───────────────────────────────────────
                        // No neck mesh — Y-bot has no distinct neck skin geometry.
                        // The Node is kept for Phase 2 head animation.
                        Node {
                            id: neckNode
                            position: Qt.vector3d(0, 0.1503, 0.0088)

                            JointMarker {}

                            Node {
                                id: headNode
                                position: Qt.vector3d(0, 0.1032, 0.0314)
                                visible:  adapter.headVisible
                                rotation: adapter.headRotation

                                JointMarker {}
                                RuntimeLoader {
                                    source: "qrc:/assets/body/body_Head.glb"
                                    onStatusChanged: root.onSegmentLoaded(status)
                                }
                            }
                        }

                        // ── Left shoulder → arm chain ─────────────────────────
                        Node {
                            id: leftShoulderNode
                            position: Qt.vector3d(0.0611, 0.0911, 0.0076)
                            rotation: Qt.quaternion(-0.4398, -0.4538, -0.5448, 0.5511)

                            JointMarker {}
                            RuntimeLoader {
                                source: "qrc:/assets/body/body_LeftShoulder.glb"
                                onStatusChanged: root.onSegmentLoaded(status)
                            }

                            Node {
                                id: leftArmNode
                                position: Qt.vector3d(0, 0.1292, 0)
                                visible:  adapter.leftArmVisible
                                rotation: {
                                    if (root.useLeadArmOverride && root.rightHanded) {
                                        return root.animateLeadArm && root._leadArmT < 1.0
                                            ? root._slerp(root._leadArmFrom, root._leadArmTo, root._leadArmT)
                                            : root.leadArmOverrideRotation
                                    }
                                    if (root.useTrailArmOverride && !root.rightHanded) return root.trailArmOverrideRotation
                                    return adapter.leftArmRotation
                                }

                                JointMarker {}
                                RuntimeLoader {
                                    source: "qrc:/assets/body/arm_LeftArm.glb"
                                    onStatusChanged: root.onSegmentLoaded(status)
                                }
                                // Highlight overlay — upper arm segment (right-handed lead arm)
                                Model {
                                    visible: root.highlightLeadArm && root.rightHanded
                                    source: "#Cylinder"
                                    position: Qt.vector3d(0, 0.137, 0)
                                    scale: Qt.vector3d(0.00025, 0.00274, 0.00025)
                                    materials: PrincipledMaterial {
                                        baseColor: root.leadArmColor
                                        opacity: 0.45
                                        alphaMode: PrincipledMaterial.Blend
                                        metalness: 0.0
                                        roughness: 0.5
                                    }
                                }

                                Node {
                                    id: leftForeArmNode
                                    position: Qt.vector3d(0, 0.274, 0)
                                    visible:  adapter.leftForeArmVisible
                                    rotation: {
                                        if (root.useLeadArmOverride  &&  root.rightHanded) return root.leadForeArmOverrideRotation
                                        if (root.useTrailArmOverride && !root.rightHanded) return root.trailForeArmOverrideRotation
                                        return adapter.leftForeArmRotation
                                    }

                                    JointMarker {}
                                    RuntimeLoader {
                                        source: "qrc:/assets/body/arm_LeftForeArm.glb"
                                        onStatusChanged: root.onSegmentLoaded(status)
                                    }
                                    // Highlight overlay — forearm segment
                                    Model {
                                        visible: root.highlightLeadArm && root.rightHanded
                                        source: "#Cylinder"
                                        position: Qt.vector3d(0, 0.138, 0)
                                        scale: Qt.vector3d(0.00022, 0.002761, 0.00022)
                                        materials: PrincipledMaterial {
                                            baseColor: root.leadArmColor
                                            opacity: 0.45
                                            alphaMode: PrincipledMaterial.Blend
                                            metalness: 0.0
                                            roughness: 0.5
                                        }
                                    }

                                    Node {
                                        id: leftHandNode
                                        position: Qt.vector3d(0, 0.2761, 0)
                                        visible:  adapter.leftForeArmVisible

                                        JointMarker {}
                                        RuntimeLoader {
                                            source: "qrc:/assets/body/arm_LeftHand.glb"
                                            onStatusChanged: root.onSegmentLoaded(status)
                                        }
                                        // Highlight overlay — hand segment
                                        Model {
                                            visible: root.highlightLeadArm && root.rightHanded
                                            source: "#Sphere"
                                            position: Qt.vector3d(0, 0.05, 0)
                                            scale: Qt.vector3d(0.0005, 0.0007, 0.0004)
                                            materials: PrincipledMaterial {
                                                baseColor: root.leadArmColor
                                                opacity: 0.45
                                                alphaMode: PrincipledMaterial.Blend
                                                metalness: 0.0
                                                roughness: 0.5
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ── Right shoulder → arm chain ────────────────────────
                        Node {
                            id: rightShoulderNode
                            position: Qt.vector3d(-0.0611, 0.0911, 0.0076)
                            rotation: Qt.quaternion(0.4398, 0.4538, -0.5448, 0.5511)

                            JointMarker {}
                            RuntimeLoader {
                                source: "qrc:/assets/body/body_RightShoulder.glb"
                                onStatusChanged: root.onSegmentLoaded(status)
                            }

                            Node {
                                id: rightArmNode
                                position: Qt.vector3d(0, 0.1292, 0)
                                visible:  adapter.rightArmVisible
                                rotation: {
                                    if (root.useLeadArmOverride && !root.rightHanded) {
                                        return root.animateLeadArm && root._leadArmT < 1.0
                                            ? root._slerp(root._leadArmFrom, root._leadArmTo, root._leadArmT)
                                            : root.leadArmOverrideRotation
                                    }
                                    if (root.useTrailArmOverride && root.rightHanded) return root.trailArmOverrideRotation
                                    return adapter.rightArmRotation
                                }

                                JointMarker {}
                                RuntimeLoader {
                                    source: "qrc:/assets/body/arm_RightArm.glb"
                                    onStatusChanged: root.onSegmentLoaded(status)
                                }
                                // Highlight overlay — upper arm segment (left-handed lead arm)
                                Model {
                                    visible: root.highlightLeadArm && !root.rightHanded
                                    source: "#Cylinder"
                                    position: Qt.vector3d(0, 0.137, 0)
                                    scale: Qt.vector3d(0.00025, 0.00274, 0.00025)
                                    materials: PrincipledMaterial {
                                        baseColor: root.leadArmColor
                                        opacity: 0.45
                                        alphaMode: PrincipledMaterial.Blend
                                        metalness: 0.0
                                        roughness: 0.5
                                    }
                                }

                                Node {
                                    id: rightForeArmNode
                                    position: Qt.vector3d(0, 0.2741, 0)
                                    visible:  adapter.rightForeArmVisible
                                    rotation: {
                                        if (root.useLeadArmOverride  && !root.rightHanded) return root.leadForeArmOverrideRotation
                                        if (root.useTrailArmOverride &&  root.rightHanded) return root.trailForeArmOverrideRotation
                                        return adapter.rightForeArmRotation
                                    }

                                    JointMarker {}
                                    RuntimeLoader {
                                        source: "qrc:/assets/body/arm_RightForeArm.glb"
                                        onStatusChanged: root.onSegmentLoaded(status)
                                    }
                                    // Highlight overlay — forearm segment
                                    Model {
                                        visible: root.highlightLeadArm && !root.rightHanded
                                        source: "#Cylinder"
                                        position: Qt.vector3d(0, 0.138, 0)
                                        scale: Qt.vector3d(0.00022, 0.002741, 0.00022)
                                        materials: PrincipledMaterial {
                                            baseColor: root.leadArmColor
                                            opacity: 0.45
                                            alphaMode: PrincipledMaterial.Blend
                                            metalness: 0.0
                                            roughness: 0.5
                                        }
                                    }

                                    Node {
                                        id: rightHandNode
                                        position: Qt.vector3d(0, 0.2761, 0)
                                        visible:  adapter.rightForeArmVisible

                                        JointMarker {}
                                        RuntimeLoader {
                                            source: "qrc:/assets/body/arm_RightHand.glb"
                                            onStatusChanged: root.onSegmentLoaded(status)
                                        }
                                        // Highlight overlay — hand segment
                                        Model {
                                            visible: root.highlightLeadArm && !root.rightHanded
                                            source: "#Sphere"
                                            position: Qt.vector3d(0, 0.05, 0)
                                            scale: Qt.vector3d(0.0005, 0.0007, 0.0004)
                                            materials: PrincipledMaterial {
                                                baseColor: root.leadArmColor
                                                opacity: 0.45
                                                alphaMode: PrincipledMaterial.Blend
                                                metalness: 0.0
                                                roughness: 0.5
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Left leg chain ────────────────────────────────────────────────
            // UpLeg rotation ≈ 180° around Z: bone-local +Y points world-down.
            // Child positions are therefore along local +Y (= world -Y = downward).
            Node {
                id: leftUpLegNode
                position: Qt.vector3d(0.0912, -0.0666, -0.0006)
                visible:  adapter.leftUpLegVisible
                rotation: adapter.leftUpLegRotation

                JointMarker {}
                RuntimeLoader {
                    source: "qrc:/assets/body/body_LeftUpLeg.glb"
                    onStatusChanged: root.onSegmentLoaded(status)
                }

                Node {
                    id: leftLegNode
                    position: Qt.vector3d(0, 0.4060, 0)
                    visible:  adapter.leftLegVisible
                    rotation: adapter.leftLegRotation

                    JointMarker {}
                    RuntimeLoader {
                        source: "qrc:/assets/body/body_LeftLeg.glb"
                        onStatusChanged: root.onSegmentLoaded(status)
                    }

                    Node {
                        id: leftFootNode
                        position: Qt.vector3d(0, 0.4210, 0)
                        visible:  adapter.poseSource === null
                        rotation: Qt.quaternion(0.8408, 0.5405, 0.0144, 0.0250)

                        JointMarker {}
                        RuntimeLoader {
                            source: "qrc:/assets/body/body_LeftFoot.glb"
                            onStatusChanged: root.onSegmentLoaded(status)
                        }
                    }
                }
            }

            // ── Right leg chain ───────────────────────────────────────────────
            Node {
                id: rightUpLegNode
                position: Qt.vector3d(-0.0913, -0.0666, -0.0006)
                visible:  adapter.rightUpLegVisible
                rotation: adapter.rightUpLegRotation

                JointMarker {}
                RuntimeLoader {
                    source: "qrc:/assets/body/body_RightUpLeg.glb"
                    onStatusChanged: root.onSegmentLoaded(status)
                }

                Node {
                    id: rightLegNode
                    position: Qt.vector3d(0, 0.4060, 0)
                    visible:  adapter.rightLegVisible
                    rotation: adapter.rightLegRotation

                    JointMarker {}
                    RuntimeLoader {
                        source: "qrc:/assets/body/body_RightLeg.glb"
                        onStatusChanged: root.onSegmentLoaded(status)
                    }

                    Node {
                        id: rightFootNode
                        position: Qt.vector3d(0, 0.4210, 0)
                        visible:  adapter.poseSource === null
                        rotation: Qt.quaternion(0.8408, 0.5406, -0.0144, -0.0250)

                        JointMarker {}
                        RuntimeLoader {
                            source: "qrc:/assets/body/body_RightFoot.glb"
                            onStatusChanged: root.onSegmentLoaded(status)
                        }
                    }
                }
            }
        }
    }

    // ── Loading overlay ───────────────────────────────────────────────────────
    Rectangle {
        anchors.centerIn: parent
        visible:  !root.fullyLoaded
        width:    loadText.width + Theme.sp(24)
        height:   loadText.height + Theme.sp(16)
        color:    "#CC000000"
        radius:   Theme.sp(6)

        Text {
            id: loadText
            anchors.centerIn: parent
            color:          Theme.colorText2
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            text:           qsTr("Loading… %1 / %2").arg(root.loadedCount).arg(root.totalSegments)
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
