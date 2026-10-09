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
// Presets orbit the golfer's mid-hip at address; the two "cam" presets stand where the fitted
// face-on / DTL camera stood (docs/design/swing_3d_annotations_design.md §5a).
//
// MOTION ANNOTATIONS (swing_3d_annotations_design.md): the camera tiles' Motion setting, drawn in
// 3-D — the same ViewLayout object the tiles read, passed in by the host. Each element is one
// SwingAnnotationGeometry (trace tube, frame sticks, fan); the P-positions ride the club; the fused
// downswing plane is the "plane" element (3-D only — the tiles ignore it).
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
    property string preset: "faceOn"          // faceOn | dtl | top | target | behind | camFo | camDtl | free
    // Motion annotations — ViewLayout's object for the screen's mode (PpCameraTiles binds the same).
    property bool   motionOn: false
    property var    motionModes: ({})
    property string motionTraceTarget: ""
    property bool   leadIsLeft: true
    function elemMode(key) {
        if (!root.motionOn || !root.motionModes) return "off"
        var m = root.motionModes[key]
        return m ? m : "off"
    }
    readonly property var _bodyElems: ["arms", "spine", "shoulders", "hips", "legs"]
    // Any body or club annotation on ⇒ the figure goes see-through, so a trace inside or behind the
    // body (the pelvis, the club going round the back) is seen (Mark, 28 Sept; design §10).
    readonly property real hullFadeOpacity: 0.5
    readonly property bool hullFaded: {
        var keys = _bodyElems.concat(["shaft", "shaftGrip"])
        for (var i = 0; i < keys.length; ++i)
            if (elemMode(keys[i]) !== "off") return true
        return false
    }
    readonly property bool clubTraced: elemMode("shaft") === "trace" || elemMode("shaftGrip") === "trace"
    // Standing where a fitted camera stood.
    readonly property int matchView: preset === "camFo" ? 0 : preset === "camDtl" ? 1 : -1
    readonly property bool matched: matchView >= 0 && drv.available && drv.cameraAvailable(matchView)
    readonly property var annotations: [annArms, annSpine, annShoulders, annHips, annLegs, annShaft, annGrip, annPlane]
    readonly property bool usingMatchCamera: view.camera === matchCam
    // No DTL / no fused shaft plane ⇒ depth is the fit's guess: the swing is shown face-on only
    // (Mark, 28 Sept) — the Face-on preset (and the face-on camera's), no orbit.
    readonly property bool faceOnOnly: drv.faceOnOnly
    function presetAllowed(p) { return !faceOnOnly || p === "faceOn" || p === "camFo" }
    onFaceOnOnlyChanged: if (!presetAllowed(preset)) applyPreset("faceOn")
    function mapPosition(i) { return view.mapFrom3DScene(drv.positionHead(i, rev)) }
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
    // tonemapped and never matches the surrounding UI. Flat and borderless — the stage's 3-D
    // SWING card is the frame — but opaque, and a step off the card's surface, because the
    // figure's pale grey would wash out on a light theme's surface, and because the well marks
    // the area that orbits under a drag, as a video tile marks its picture.
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

    // ── one motion annotation (design §4): its element's mode from the shared Motion setting ─────
    component Annot: Model {
        id: an
        property string element: ""
        property string mode: root.elemMode(element)
        property color  tint: Theme.colorAccent
        property color  tintB: "transparent"
        property bool   lit: true                 // shaded tubes read as 3-D objects; the plane is flat
        readonly property bool body: root._bodyElems.indexOf(element) >= 0
        readonly property alias geom: g
        visible: drv.available && mode !== "off" && g.vertexCount > 0
        geometry: SwingAnnotationGeometry {
            id: g
            driver: drv
            element: an.element
            mode: an.mode
            target: root.motionTraceTarget
            leadLeft: root.leadIsLeft
            // Metres: thick enough at the orbit distance for the shading to read as a tube.
            radius: an.mode === "trace" ? 0.009 : an.element === "shaft" ? 0.006 : 0.010
            color: an.tint
            colorB: an.tintB
            revision: root.rev
        }
        materials: PrincipledMaterial {
            lighting: an.lit ? PrincipledMaterial.FragmentLighting : PrincipledMaterial.NoLighting
            vertexColorsEnabled: true
            baseColor: "white"
            roughness: 0.45
            // Lit tubes face outward and cull their insides (a lit inside face reads as a dark speck);
            // the flat plane is seen from both sides.
            cullMode: an.lit ? Material.BackFaceCulling : Material.NoCulling
            // Frame-mode body sticks are OPAQUE so they are drawn before the faded hull (§4.2);
            // traces and fans carry their fade in the vertex alpha.
            alphaMode: an.body && an.mode === "frame" ? PrincipledMaterial.Opaque : PrincipledMaterial.Blend
        }
    }

    // ── one status chip in the bottom row: a fact, untinted; a caveat about the data (what this
    // shot cannot show) tinted in colorWarn ──
    component Chip: PpChip {
        property string label: ""
        property bool   caveat: false
        text:   label
        tinted: caveat
        tone:   caveat ? Theme.colorWarn : Theme.colorText2
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: root.matched ? matchCam : cam
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

        // Where a fitted camera stood (the match presets). Scene space, not under the orbit pivot.
        PerspectiveCamera {
            id: matchCam
            position: root.matchView >= 0 && drv.available ? drv.cameraPosition(root.matchView) : Qt.vector3d(0, 1, 4)
            rotation: root.matchView >= 0 && drv.available ? drv.cameraRotation(root.matchView) : Qt.quaternion(1, 0, 0, 0)
            fieldOfView: root.matchView >= 0 && drv.available ? drv.cameraFovDeg(root.matchView) : 38
            clipNear: 0.02
            clipFar: 60
            DirectionalLight { brightness: 0.55 }
        }

        // ── motion annotations ──
        Annot { id: annArms;      element: "arms";      tint: mode === "trace" ? Theme.colorAccent : Theme.poseBone }
        Annot { id: annSpine;     element: "spine";     tint: mode === "trace" ? Theme.colorAccent : Theme.poseSpineTop
                                                         tintB: mode === "trace" ? "transparent" : Theme.poseSpineBottom }
        Annot { id: annShoulders; element: "shoulders"; tint: mode === "trace" ? Theme.colorAccent : Theme.poseBone }
        Annot { id: annHips;      element: "hips";      tint: mode === "trace" ? Theme.colorAccent : Theme.poseBone }
        Annot { id: annLegs;      element: "legs";      tint: mode === "trace" ? Theme.colorAccent : Theme.poseBone }
        Annot { id: annShaft;     element: "shaft" }
        Annot { id: annGrip;      element: "shaftGrip"; tint: Qt.lighter(Theme.colorAccent, 1.7) }
        // The fused downswing plane (design §5b): a property of the swing, drawn whole.
        Annot {
            id: annPlane
            element: "plane"
            lit: false
            tint: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.15)
            visible: drv.available && mode !== "off" && drv.planeAvailable && geom.vertexCount > 0
        }

        // ── P-positions: a dot at each one's clubhead while the shaft is drawn in frame mode, and
        // the club at that instant + its label within ±40 ms of the playhead (the tiles' rule). ──
        Repeater3D {
            id: posDots
            model: root.elemMode("shaft") === "frame" && drv.available ? drv.positionCount : 0
            Node {
                id: pd
                readonly property bool near: root.positionUs >= 0
                                             && Math.abs(drv.positionTimeUs(index) - root.positionUs) <= 40000
                readonly property color tint: drv.positionSource(index) === 1 ? Theme.colorGood : Theme.colorAccent
                Model {
                    source: "#Sphere"
                    position: drv.positionHead(index, root.rev)
                    scale: Qt.vector3d(0.00018, 0.00018, 0.00018)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: pd.tint }
                }
                // The club at that position: butt at the origin, +Y along the shaft (as the club).
                Node {
                    visible: pd.near
                    position: drv.positionButt(index, root.rev)
                    rotation: drv.positionRotation(index, root.rev)
                    Model {
                        source: "#Cylinder"
                        position: Qt.vector3d(0, drv.clubLengthM / 2, 0)
                        scale: Qt.vector3d(0.00008, drv.clubLengthM / 100, 0.00008)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: pd.tint }
                    }
                }
            }
        }

        // ── the ball ──
        Model {
            source: "#Sphere"
            // The ball element's setting, as on the tiles.
            visible: drv.available && drv.ballVisible && root.elemMode("ball") !== "off"
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
                // See-through while annotations are on (design §10). The faded hull writes NO depth, so
                // nothing it covers is hidden — a trace inside the pelvis or behind the back shows through
                // it; back faces stay culled, so the body does not show its own far side.
                opacity: root.hullFaded ? root.hullFadeOpacity : 1.0
                alphaMode: root.hullFaded ? PrincipledMaterial.Blend : PrincipledMaterial.Default
                depthDrawMode: root.hullFaded ? Material.NeverDepthDraw : Material.OpaqueOnlyDepthDraw
                cullMode: Material.BackFaceCulling
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
        // A matched camera stays where the real one stood; a face-on-only swing is not orbited.
        enabled: !root.matched && !root.faceOnOnly
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
        if (!presetAllowed(p)) p = "faceOn"
        preset = p
        if (p === "camFo" || p === "camDtl") return      // the match camera: nothing to animate
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
        // The match chips only when this swing's fitted camera (and its image size) is on file.
        readonly property bool camFo: drv.available && drv.cameraAvailable(0)
        readonly property bool camDtl: drv.available && drv.cameraAvailable(1)
        readonly property bool orbit: !root.faceOnOnly
        readonly property var keys: ["faceOn"].concat(orbit ? ["dtl", "top", "target", "behind"] : [])
                                    .concat(camFo ? ["camFo"] : []).concat(camDtl && orbit ? ["camDtl"] : [])
        options: [qsTr("Face-on")].concat(orbit ? [qsTr("Down the line"), qsTr("Top"), qsTr("Target side"), qsTr("Behind")] : [])
                 .concat(camFo ? [qsTr("Face-on cam")] : []).concat(camDtl && orbit ? [qsTr("DTL cam")] : [])
        selected: keys.indexOf(root.preset) >= 0 ? options[keys.indexOf(root.preset)] : ""
        onActivated: (v) => root.applyPreset(keys[options.indexOf(v)])
    }

    // The chips wrap upward rather than run off a narrow panel.
    Flow {
        id: chipRow
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.sp(10) }
        spacing: Theme.sp(6)
        Chip { id: tierChip; visible: drv.available; label: drv.frameTierText }
        Chip {
            visible: drv.available && root.faceOnOnly
            caveat: true
            label: drv.twoViews ? qsTr("face-on only: no fused shaft plane") : qsTr("face-on only")
        }
        Chip { visible: drv.available && drv.heldAtEnd; label: qsTr("held at P8") }
        Chip {
            visible: drv.available && root.clubTraced
            label: qsTr("club path smoothed %1 ms").arg(drv.clubSmoothingMs.toFixed(0))
        }
        Chip {
            id: planeChip
            // A face-on-only swing has no plane, and its own chip already says so.
            visible: drv.available && root.elemMode("plane") !== "off" && !root.faceOnOnly
            caveat: !drv.planeAvailable
            label: drv.planeAvailable ? qsTr("downswing plane · club3d · %1°").arg(drv.planeInclDeg.toFixed(1))
                                      : qsTr("no downswing plane on this shot")
        }
        // The P-position marks' legend, in words, while the marks are drawn.
        Item {
            visible: posDots.model > 0
            width: posLegend.implicitWidth; height: Theme.sp(18)
            PpMicro {
                id: posLegend
                anchors.verticalCenter: parent.verticalCenter
                font.letterSpacing: Theme.trackingData
                text: qsTr("P  ● FITTED  ○ SAMPLED")
            }
        }
    }
    // Footnotes sit under the preset bar, clear of the chips.
    PpCardNote {
        anchors { right: parent.right; top: presets.bottom; margins: Theme.sp(12) }
        visible: drv.available && root.preset === "top"
        text: qsTr("Square to your stance at address")
    }
    PpCardNote {
        anchors { right: parent.right; top: presets.bottom; margins: Theme.sp(12) }
        visible: root.matched
        text: (root.matchView === 0 ? qsTr("As the face-on camera saw it") : qsTr("As the DTL camera saw it"))
              + (root.matchView === 0 && drv.foMirrored ? qsTr(" · mirrored") : "")
    }

    // P-position labels: 2-D text pinned to the 3-D clubhead — 3-D text would face away from half
    // the presets. Re-placed when the camera or the playhead moves. Each leads with its
    // provenance mark, so fitted and sampled read by shape as well as colour: a solid dot for a
    // milestone fit (positionSource 1), a hollow ring for a raw track sample.
    property int _camTick: 0
    Connections {
        target: cam
        function onScenePositionChanged() { root._camTick += 1 }
        function onSceneRotationChanged() { root._camTick += 1 }
    }
    onMatchedChanged: _camTick += 1
    onWidthChanged: _camTick += 1
    onHeightChanged: _camTick += 1
    function mapToView(p, tick, camNow) { return view.mapFrom3DScene(p) }
    readonly property alias positionLabels: posLabels
    Repeater {
        id: posLabels
        model: posDots.model
        Row {
            id: posLabel
            required property int index
            readonly property bool near: root.positionUs >= 0
                                         && Math.abs(drv.positionTimeUs(index) - root.positionUs) <= 40000
            readonly property vector3d sp: root.mapToView(drv.positionHead(index, root.rev), root._camTick, view.camera)
            readonly property bool  fitted: drv.positionSource(index) === 1
            readonly property color tint: fitted ? Theme.colorGood : Theme.colorAccent
            // The label's words, kept on the item itself (tools/probes/swing3d_panel.qml reads them).
            readonly property string text: "P" + drv.positionP(index)
            visible: near && sp.z > 0 && sp.x >= 0 && sp.y >= 0 && sp.x <= root.width && sp.y <= root.height
            x: sp.x + Theme.sp(6)
            y: sp.y - height - Theme.sp(2)
            spacing: Theme.sp(4)
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.sp(7); height: width; radius: width / 2
                color: posLabel.fitted ? posLabel.tint : "transparent"
                border.width: posLabel.fitted ? 0 : Math.max(1, Theme.sp(1.4))
                border.color: posLabel.tint
            }
            Text {
                text: posLabel.text
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; font.bold: true
                color: posLabel.tint
            }
        }
    }

    PpCardNote {
        anchors.centerIn: parent
        visible: !drv.available
        width: parent.width * 0.7
        horizontalAlignment: Text.AlignHCenter
        text: drv.loading ? qsTr("Loading the 3-D swing…") : drv.reason
    }
}
