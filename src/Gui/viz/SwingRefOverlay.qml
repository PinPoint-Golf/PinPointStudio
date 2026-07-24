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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import PinPointStudio

// SwingRefOverlay — WP5a swing-reference overlay (T7, restyled to a static
// P1-P8 strobe per Mark's request). A transparent-background View3D drawing
// the idealised reference shaft at ALL EIGHT canonical P positions at once —
// white, high-contrast against the measured-shaft warm/cool gradient trace
// drawn elsewhere in PpCameraFrame. Pure display component — no shotReplay
// coupling; T8 wires `reference`/`positions`/`playheadUs`/`contentX/Y/W/H`
// from PpCameraFrame. `playheadUs` is retained for binding compatibility with
// the host (PpCameraFrame still passes it) but is NOT used for rendering any
// more — the strobe is static, not phase-animated. The single animated ghost
// cylinder and the RuledSurfaceGeometry swept-surface pass this file used to
// render are gone; RuledSurfaceGeometry itself stays in the tree/build
// (SwingRefOverlayModel::surfacePoses is still computed) in case a future
// pass wants it back — it is simply not instantiated here any more.
//
// Compositing precedent: the T7 spike (transparent View3D over a video-like
// layer, scrub-driven redraw via an externally-pushed property, exactly like
// `playheadUs` here) passed on the real windowing system — see the T7 report
// for pixel evidence. SceneEnvironment settings below (Transparent + MSAA +
// TonemapModeNone) mirror what the spike proved, following BodyVizView.qml's
// existing Transparent-View3D precedent (tonemapping/clearColor mismatch
// comment ~line 227 there — the SAME linear/sRGB characteristic was measured
// during the T7 spike; Theme colours used below will render slightly darker
// through this pipeline than the same hex would via a 2D Rectangle — a known,
// pre-existing Quick3D characteristic, not something new here).
//
// CONTENT RECT CONTRACT: the host is expected to size/position THIS ITEM to
// the video content rect (mirroring PpCameraFrame's existing aspect-locked
// video frame item — `width: Math.min(slotW, slotH * videoAspect)`), so the
// View3D (anchors.fill: parent) ends up aspect-locked to
// reference.projection.width:height — required for the 3D overlay to align
// with the video pixel-for-pixel (see swing_ref_overlay_model.h's CONTENT
// RECT note). contentX/Y/W/H are threaded through to the model as plain
// passthrough inputs (future callout-chip placement, T8).
Item {
    id: root

    // ── Public API ───────────────────────────────────────────────────────────
    property var  reference:  ({})   // analysisDetail.reference (QVariantMap)
    property var  positions:  []     // analysisDetail.club.positions (QVariantList)
    property int  playheadUs: 0      // window-relative microseconds

    property real contentX: 0
    property real contentY: 0
    property real contentW: width
    property real contentH: height
    property real residualWarnPx: 8.0

    // Read-only passthrough for a host Canvas colour-mapped pass (T8) — the
    // projected reference polyline for the CURRENT segment, points already
    // normalized 0..1 image coords: [{s,buttX,buttY,headX,headY}, ...].
    readonly property var  ghostPolyline2D: _model.ghostPolyline2D
    readonly property bool valid:           _model.valid
    readonly property bool residualWarning: _model.residualWarning

    // Golf-shaft visual thickness — fixed, not tunable (a display constant,
    // not an analysis parameter). Quick3D's built-in "#Cylinder" primitive
    // uses the legacy 100-unit convention (diameter 100 at scale 1) — see
    // BodyVizView.qml's own highlight-overlay cylinders for the same
    // divide-by-100 pattern.
    readonly property real _shaftDiaM: 0.016   // ~16 mm, a driver-shaft-ish thickness
    readonly property real _shaftDiaScale: _shaftDiaM / 100.0

    SwingRefOverlayModel {
        id: _model
        reference:      root.reference
        positions:      root.positions
        playheadUs:     root.playheadUs
        contentX:       root.contentX
        contentY:       root.contentY
        contentW:       root.contentW
        contentH:       root.contentH
        residualWarnPx: root.residualWarnPx
    }

    // Dense butt/head trajectory curves (P1->P8), rendered as thin
    // GPU-rasterized line strips (PolylineGeometry — see that header's
    // comment for why this is the "thin trace" approach: Quick3D has no
    // per-segment line-width control, and a native line-primitive strip is
    // inherently ~1px, which is exactly what's wanted here).
    PolylineGeometry {
        id: _buttTraceGeom
        points: _model.buttTrace
    }
    PolylineGeometry {
        id: _headTraceGeom
        points: _model.headTrace
    }

    View3D {
        id: view3d
        anchors.fill: parent
        visible: _model.valid

        environment: SceneEnvironment {
            backgroundMode:      SceneEnvironment.Transparent
            antialiasingMode:    SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            tonemapMode:         SceneEnvironment.TonemapModeNone
        }

        // CustomCamera has no settable raw view matrix — position/rotation
        // (world pose) + its own `projection` matrix, per
        // swing_ref_overlay_model.h's CAMERA MATRICES note.
        // Near/far are already baked into cameraProjectionMatrix (kNearM/kFarM
        // in swing_ref_overlay_math.h) — CustomCamera has no separate
        // clipNear/clipFar properties (unlike PerspectiveCamera/
        // FrustumCamera): the projection matrix IS the full frustum.
        CustomCamera {
            id: camera
            position:   _model.cameraPosition
            rotation:   _model.cameraRotation
            projection: _model.cameraProjectionMatrix
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-45, 45, 0)
            brightness:    1.1
            color:         "#FFFFFF"
        }

        // Butt (grip) trajectory — thin white trace across the whole
        // modelled swing. Drawn first so the P1-P8 shafts below sit visually
        // on top of it.
        Model {
            visible:  _model.valid
            geometry: _buttTraceGeom
            materials: PrincipledMaterial {
                // Literal white, not a Theme token — deliberate high-contrast-
                // over-video exception (see the Repeater3D comment below for
                // why no Theme token qualifies).
                baseColor: "#ffffff"
                opacity:   0.45
                alphaMode: PrincipledMaterial.Blend
                lighting:  PrincipledMaterial.NoLighting
                cullMode:  PrincipledMaterial.NoCulling
            }
        }

        // Clubhead trajectory — same treatment.
        Model {
            visible:  _model.valid
            geometry: _headTraceGeom
            materials: PrincipledMaterial {
                baseColor: "#ffffff"
                opacity:   0.45
                alphaMode: PrincipledMaterial.Blend
                lighting:  PrincipledMaterial.NoLighting
                cullMode:  PrincipledMaterial.NoCulling
            }
        }

        // P1-P8 static strobe — one thin white cylinder per canonical P
        // position (butt->head), replacing the old single phase-animated
        // ghost. midpoint/rotation/lengthM are precomputed in
        // SwingRefOverlayModel (rotationTo(+Y, direction) has no QML
        // equivalent), so this delegate is a plain bind. All 8 stay clearly
        // visible via a subtle opacity ramp (P1 dimmest -> P8 full strength);
        // white is chosen for maximum contrast against the measured club
        // shaft's warm/cool gradient hero pen drawn elsewhere in
        // PpCameraFrame — no Theme colour token is a true/near-true white
        // (colorText flips dark<->light per aesthetic, see Theme.qml), and
        // this is display-over-video, not chrome, so the design system's
        // token rule doesn't apply the same way (half-alpha-over-media is an
        // established pattern in this codebase).
        Repeater3D {
            model: _model.pShaftPoses

            delegate: Model {
                id: pShaft
                required property var modelData
                source:   "#Cylinder"
                position: pShaft.modelData.midpoint
                rotation: pShaft.modelData.rotation
                scale:    Qt.vector3d(root._shaftDiaScale, pShaft.modelData.lengthM / 100.0, root._shaftDiaScale)
                materials: PrincipledMaterial {
                    baseColor: "#ffffff"
                    opacity:   0.55 + 0.35 * (pShaft.modelData.p - 1) / 7.0
                    alphaMode: PrincipledMaterial.Blend
                    lighting:  PrincipledMaterial.NoLighting
                }
            }
        }
    }

    // ── Residual-warning chip (top-right of the tile) ───────────────────────
    Rectangle {
        id: warnChip
        visible: root.residualWarning
        anchors { top: parent.top; right: parent.right; margins: Theme.sp(10) }
        radius:  Theme.sp(4)
        color:   Theme.colorWarnLight
        border.color: Theme.colorWarn
        border.width: 1
        width:  warnLabel.width  + Theme.sp(16)
        height: warnLabel.height + Theme.sp(8)

        Text {
            id: warnLabel
            anchors.centerIn: parent
            text:           qsTr("Low camera-fit confidence")
            color:          Theme.colorWarn
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzLabel
        }
    }
}
