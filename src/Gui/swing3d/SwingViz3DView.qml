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

// SwingViz3DView — the swing as the fitted skeleton, the club and the ball, from any side
// (docs/design/swing_3d_viz_design.md §6). SwingRigDriver does the arithmetic; this file only
// places what it answers. The figure is a smooth, simplified MANNEQUIN on the Y-bot's skeleton
// (qrc:/assets/swing3d/man_*.glb, tools/generate_swing3d_mannequin.py) — the Y-bot's own shells
// read as a robot at swing speed. NOTHING here is shared with the calibration views.
//
// Scene frame (the driver's): metres, +Y up, the floor at y = 0, the ball at address at the
// origin, +X along the stance toward the lead heel, +Z toward the face-on camera's side.
// Presets orbit the golfer's mid-hip at address.
//
// ⚠ Lives inside SwingViz3DHost, which is created once and REPARENTED between panel slots —
// never destroyed. Destroying View3D instances corrupted the Quick 3D render thread before
// (CapturePage.qml), and the session stage rebuilds its delegates on every mode/tab switch.

import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick3D.AssetUtils
import PinPointStudio

Item {
    id: root

    property string swingDir: ""
    property real   positionUs: -1          // < 0: nothing playing — the figure rests at address
    property string preset: "faceOn"          // faceOn | dtl | top | target | behind | free
    // "hull": ONE smooth skinned body (tools/generate_swing3d_hull.py) — no joins to show.
    // "segments": the rigid-piece mannequin, kept as a fallback should skinning misbehave on a GPU.
    property string figure: "hull"
    readonly property bool hullShown: figure === "hull" && hullGeom.ready
    readonly property var hullGeometry: hullGeom
    readonly property alias driver: drv
    readonly property int segmentsLoaded: _loaded

    property int _loaded: 0
    readonly property int rev: drv.revision

    SwingRigDriver {
        id: drv
        swingDir: root.swingDir
        positionUs: root.positionUs
    }

    // The view background is a QML rectangle behind a transparent View3D: a clearColor is
    // tonemapped and never matches the surrounding UI.
    Rectangle { anchors.fill: parent; color: Theme.colorBg2; radius: Theme.radius }

    // ── one bone: the node carries the parent-local offset/rotation, the mesh its own ─────────
    component Bone: Node {
        id: bone
        property int j: 0
        property string mesh: ""
        readonly property int tierNow: drv.tier(j, root.rev)
        position: drv.offset(j, root.rev)
        rotation: drv.localRotation(j, root.rev)
        RuntimeLoader {
            visible: !root.hullShown && bone.mesh !== "" && (!drv.available || bone.tierNow > 0)
            opacity: !drv.available ? 0.35 : bone.tierNow >= 2 ? 1.0 : 0.4
            source: bone.mesh === "" ? "" : "qrc:/assets/swing3d/man_" + bone.mesh + ".glb"
            scale: Qt.vector3d(drv.meshScale(bone.j, root.rev), drv.meshScale(bone.j, root.rev),
                               drv.meshScale(bone.j, root.rev))
            onStatusChanged: if (status === RuntimeLoader.Success) root._loaded += 1
        }
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: cam
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // Soft, even light: a key from above, a fill from behind, and a headlight that rides the
        // camera so whichever side is on show is never in black shade.
        DirectionalLight { eulerRotation: Qt.vector3d(-50, -30, 0); brightness: 0.8 }
        DirectionalLight { eulerRotation: Qt.vector3d(-20, 150, 0); brightness: 0.35 }

        // Orbit pivot: the mid-hip at address. The camera sits on the pivot's +Z at `dist`.
        Node {
            id: pivot
            position: drv.centre
            eulerRotation: Qt.vector3d(-8, 0, 0)
            PerspectiveCamera {
                id: cam
                position: Qt.vector3d(0, 0, 4.2)
                clipNear: 0.02
                clipFar: 60
                fieldOfView: 38
                DirectionalLight { brightness: 0.55 }     // the headlight: looks where the camera looks
            }
        }

        // ── floor: a grid (0.5 m) and the stance line ──
        Node {
            id: floor
            Repeater3D {
                model: 13
                Model {
                    source: "#Cube"
                    position: Qt.vector3d(0, 0, (index - 6) * 0.5)
                    scale: Qt.vector3d(0.06, 0.00004, 0.00006)
                    opacity: 0.22
                    materials: PrincipledMaterial { baseColor: Theme.colorText3; lighting: PrincipledMaterial.NoLighting }
                }
            }
            Repeater3D {
                model: 13
                Model {
                    source: "#Cube"
                    position: Qt.vector3d((index - 6) * 0.5, 0, 0)
                    scale: Qt.vector3d(0.00006, 0.00004, 0.06)
                    opacity: 0.22
                    materials: PrincipledMaterial { baseColor: Theme.colorText3; lighting: PrincipledMaterial.NoLighting }
                }
            }
            // The stance axis (heels at address, trail → lead). Not a target line: the cameras
            // are not calibrated, so the heading is the golfer's own.
            Model {
                source: "#Cube"
                position: Qt.vector3d(0, 0.001, 0)
                scale: Qt.vector3d(0.03, 0.00008, 0.0003)
                materials: PrincipledMaterial { baseColor: Theme.colorAccent; lighting: PrincipledMaterial.NoLighting }
            }
        }

        // ── the ball ──
        Model {
            source: "#Sphere"
            visible: drv.available && drv.ballVisible
            position: drv.ballPosition
            scale: Qt.vector3d(0.000427, 0.000427, 0.000427)
            materials: PrincipledMaterial { baseColor: "#f4f4f0"; roughness: 0.35 }
        }

        // ── the club: butt at shaftButt, +Y along the shaft ──
        Node {
            visible: drv.available && drv.shaftTier > 0
            position: drv.shaftButt
            rotation: drv.shaftRotation
            opacity: drv.shaftTier >= 2 ? 1.0 : 0.45
            Model {
                source: "#Cylinder"
                position: Qt.vector3d(0, drv.clubLengthM / 2, 0)
                scale: Qt.vector3d(0.00012, drv.clubLengthM / 100, 0.00012)
                materials: PrincipledMaterial { baseColor: "#b9bec6"; metalness: 0.8; roughness: 0.3 }
            }
            // A NEUTRAL, symmetric head: face angle is not measured, so the head must not look as
            // if it shows one.
            Model {
                source: "#Sphere"
                position: Qt.vector3d(0, drv.clubLengthM, 0)
                scale: Qt.vector3d(0.0005, 0.0003, 0.0005)
                materials: PrincipledMaterial { baseColor: "#8a9098"; metalness: 0.7; roughness: 0.35 }
            }
        }

        // ── the hull: one skinned body over the rig's bones (joint order = ybot_rig.h) ──
        Model {
            id: hull
            visible: root.hullShown && drv.available
            geometry: SwingHullGeometry { id: hullGeom; source: ":/assets/swing3d/hull.glb" }
            skin: Skin {
                joints: [hips, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16,
                         b17, b18, b19, b20, b21, b22, b23, b24, b25, b26]
                inverseBindPoses: hullGeom.inverseBindPoses
            }
            materials: PrincipledMaterial {
                baseColor: "#cdd1d8"
                roughness: 0.55
                metalness: 0.0
            }
        }

        // ── the rig (ybot_rig.h joint order) ──
        Node {
            id: hips
            visible: drv.available
            position: drv.rootPosition
            rotation: drv.rootRotation
            RuntimeLoader {
                visible: !root.hullShown
                source: "qrc:/assets/swing3d/man_Hips.glb"
                scale: Qt.vector3d(drv.meshScale(0, root.rev), drv.meshScale(0, root.rev), drv.meshScale(0, root.rev))
                onStatusChanged: if (status === RuntimeLoader.Success) root._loaded += 1
            }
            Bone { id: b1; j: 1; mesh: "Spine"
                Bone { id: b2; j: 2; mesh: "Spine1"
                    Bone { id: b3; j: 3; mesh: "Spine2"
                        Bone { id: b4; j: 4; mesh: "Neck"
                            Bone { id: b5; j: 5; mesh: "Head"
                                Bone { id: b6; j: 6 } } }
                        Bone { id: b7; j: 7; mesh: "LeftShoulder"
                            Bone { id: b8; j: 8; mesh: "LeftArm"
                                Bone { id: b9; j: 9; mesh: "LeftForeArm"
                                    Bone { id: b10; j: 10; mesh: "LeftHand"
                                        Bone { id: b11; j: 11 } } } } }
                        Bone { id: b12; j: 12; mesh: "RightShoulder"
                            Bone { id: b13; j: 13; mesh: "RightArm"
                                Bone { id: b14; j: 14; mesh: "RightForeArm"
                                    Bone { id: b15; j: 15; mesh: "RightHand"
                                        Bone { id: b16; j: 16 } } } } } } } }
            Bone { id: b17; j: 17; mesh: "LeftUpLeg"
                Bone { id: b18; j: 18; mesh: "LeftLeg"
                    Bone { id: b19; j: 19; mesh: "LeftFoot"
                        Bone { id: b20; j: 20
                            Bone { id: b21; j: 21 } } } } }
            Bone { id: b22; j: 22; mesh: "RightUpLeg"
                Bone { id: b23; j: 23; mesh: "RightLeg"
                    Bone { id: b24; j: 24; mesh: "RightFoot"
                        Bone { id: b25; j: 25
                            Bone { id: b26; j: 26 } } } } }
        }
    }

    // Drag to orbit, wheel to dolly — the camera, never the scene.
    OrbitCameraController {
        anchors.fill: view
        origin: pivot
        camera: cam
    }

    Vector3dAnimation {
        id: presetAnim
        target: pivot
        property: "eulerRotation"
        duration: Theme.durationNormal
        easing.type: Easing.InOutQuad
    }
    function presetRotation(p) {
        switch (p) {
        case "dtl":    return Qt.vector3d(-8, -90, 0)
        case "top":    return Qt.vector3d(-89, 0, 0)
        case "target": return Qt.vector3d(-8, 90, 0)
        case "behind": return Qt.vector3d(-8, 180, 0)
        default:       return Qt.vector3d(-8, 0, 0)
        }
    }
    function applyPreset(p) {
        preset = p
        presetAnim.stop()
        cam.position = Qt.vector3d(0, 0, 4.2)
        if (Theme.reduceMotion) { pivot.eulerRotation = presetRotation(p); return }
        presetAnim.from = pivot.eulerRotation
        presetAnim.to = presetRotation(p)
        presetAnim.start()
    }

    // ── overlay: presets, the tier chip, the frame's honesty ──
    PpSegmentedControl {
        id: presets
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: Theme.sp(10) }
        solid: false
        readonly property var keys: ["faceOn", "dtl", "top", "target", "behind"]
        options: [qsTr("Face-on"), qsTr("Down the line"), qsTr("Top"), qsTr("Target side"), qsTr("Behind")]
        selected: keys.indexOf(root.preset) >= 0 ? options[keys.indexOf(root.preset)] : ""
        onActivated: (v) => root.applyPreset(keys[options.indexOf(v)])
    }

    Rectangle {
        id: tierChip
        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.sp(10) }
        visible: drv.available
        radius: height / 2
        height: Theme.sp(22)
        width: tierText.implicitWidth + Theme.sp(18)
        color: Theme.colorBg
        border.width: 1; border.color: Theme.colorBorderMid
        Text {
            id: tierText
            anchors.centerIn: parent
            text: drv.frameTierText + (drv.twoViews ? "" : qsTr(" · face-on only"))
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText2
        }
    }
    Text {
        anchors { right: parent.right; bottom: parent.bottom; margins: Theme.sp(12) }
        visible: drv.available && root.preset === "top"
        text: qsTr("Square to your stance at address")
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
        color: Theme.colorText3
    }
    Text {
        anchors.centerIn: parent
        visible: !drv.available
        width: parent.width * 0.7
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: drv.loading ? qsTr("Loading the 3-D swing…") : drv.reason
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        color: Theme.colorText3
    }
}
