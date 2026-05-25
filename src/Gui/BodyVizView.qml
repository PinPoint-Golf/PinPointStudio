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

// BodyVizView — Y Bot full-body visualisation loaded from ybot.glb.
// Drag to orbit, scroll to zoom. IMU linkage to be added later.

Item {
    id: root

    View3D {
        anchors.fill: parent

        environment: SceneEnvironment {
            clearColor:          Theme.colorBg
            backgroundMode:      SceneEnvironment.Color
            antialiasingMode:    SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        // ── Camera ────────────────────────────────────────────────────────────
        // Y Bot is ~1.7 world units tall (0.01 scale × 170 cm export).
        // Orbit origin sits at chest height so the full body stays centred.
        PerspectiveCamera {
            id: camera
            position:      Qt.vector3d(0, 0.9, 3.5)
            eulerRotation: Qt.vector3d(-5, 0, 0)
            clipNear: 0.01
            clipFar:  100.0
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

        // ── Body model ────────────────────────────────────────────────────────
        RuntimeLoader {
            id: bodyLoader
            source: "qrc:/assets/body/ybot.glb"
        }
    }

    // ── Status overlay — shown while loading or on error ──────────────────────
    Text {
        anchors.centerIn: parent
        visible: bodyLoader.status !== RuntimeLoader.Success
        text: bodyLoader.status === RuntimeLoader.Loading ? qsTr("Loading…")
            : bodyLoader.status === RuntimeLoader.Error   ? qsTr("Load error: ") + bodyLoader.errorString
            : ""
        color:          Theme.colorText3
        font.family:    Theme.fontData
        font.pixelSize: Theme.fontSzBody2
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
