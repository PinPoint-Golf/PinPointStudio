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

// Camera device panel for the session toolbar. Same shell + mode pattern as the
// IMU panel:
//   "list"      — Scan / Connect / Calibrate actions + camera rows. Each connected
//                 camera shows a live preview thumbnail and a per-camera enable
//                 toggle (session-local, mirrors the IMU panel). Calibrate is
//                 attention-framed once a camera is connected that is not "fixed in
//                 place" (appSettings.cameraFixedInPlace) — i.e. needs stereo cal.
//   "calibrate" — hosts CameraCalibrationFlow (a STUB for now) compactly in-panel.
// The attention frame wraps the WHOLE panel while calibrating.

import QtQuick
import QtQuick.Layouts
import QtMultimedia
import PinPointStudio

Item {
    id: root
    property string mode:   "list"        // "list" | "calibrate"
    // True while the hosting popup is open — gates the live preview thumbnails so
    // they only stream (via the controller's settings sink) when visible.
    property bool   active: false

    implicitWidth:  Theme.sp(380)
    implicitHeight: mode === "calibrate"
        ? Theme.sp(46) + 1 + Theme.sp(420)
        : Theme.sp(46) + 1 + listCol.implicitHeight

    // Attention once a connected camera is not "fixed in place" — a non-fixed camera
    // needs (stereo) calibration this session. Fixed cameras keep their calibration.
    readonly property bool needsCalibration: {
        var _dep  = cameraManager.instances
        var list  = cameraManager.cameraList
        var fixed = appSettings.cameraFixedInPlace
        for (var i = 0; i < list.length; ++i)
            if (list[i].selected && fixed[list[i].cameraKey] !== true) return true
        return false
    }

    // ── Per-session camera enablement (mirrors the IMU panel) ──────────────────
    // Seeded from appSettings.cameraExcluded but the toggle edits THIS local list
    // only — never writes back to settings (global enablement is owned by Settings).
    // Keyed by cameraKey (the same id cameraExcluded uses).
    // TODO: persist a real per-session selection carried over from the start wizard.
    property var sessionCameraExcluded: []
    Component.onCompleted: sessionCameraExcluded = appSettings.cameraExcluded.slice()

    function setCameraEnabled(camKey, camIndex, on) {
        var list = sessionCameraExcluded.slice()
        var idx  = list.indexOf(camKey)
        if (!on && idx < 0)  list.push(camKey)
        if ( on && idx >= 0) list.splice(idx, 1)
        sessionCameraExcluded = list
        if (!on) cameraManager.setSelected(camIndex, false)
    }

    // Connect every enabled, not-yet-selected camera.
    function startConnect() {
        var list = cameraManager.cameraList
        var excluded = sessionCameraExcluded
        for (var i = 0; i < list.length; ++i)
            if (excluded.indexOf(list[i].cameraKey) < 0 && !list[i].selected)
                cameraManager.setSelected(list[i].index, true)
    }

    // ── Header ──────────────────────────────────────────────────────────────
    Item {
        id: hdr
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.sp(46)

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.sp(15); rightMargin: Theme.sp(15) }
            visible: root.mode === "list"
            Text {
                Layout.fillWidth: true
                text: qsTr("CAMERAS")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                font.letterSpacing: Theme.trackingLabel; color: Theme.colorText2
            }
            Text {
                text: cameraManager.instances.length + " / " + cameraManager.cameraList.length
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                color: Theme.colorText3
            }
        }

        // Calibrate-mode title — no top navigation (the flow's Cancel returns to
        // the list); just a non-interactive heading for context.
        Text {
            anchors { fill: parent; leftMargin: Theme.sp(15); rightMargin: Theme.sp(15) }
            verticalAlignment: Text.AlignVCenter
            visible: root.mode === "calibrate"
            text: qsTr("Calibrate cameras")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
            font.letterSpacing: Theme.trackingLabel; color: Theme.colorText2
        }
    }
    Rectangle {
        id: hairline
        anchors { left: parent.left; right: parent.right; top: hdr.bottom }
        height: 1; color: Theme.colorBorderMid
    }

    // ── LIST view ───────────────────────────────────────────────────────────
    Column {
        id: listCol
        visible: root.mode === "list"
        anchors { left: parent.left; right: parent.right; top: hairline.bottom }

        Row {
            width: parent.width; padding: Theme.sp(12); spacing: Theme.sp(8)
            ScopedAction { glyph: "⟳"; label: qsTr("Scan");    onTriggered: cameraManager.enumerate() }
            ScopedAction {
                glyph: "⇄"; label: qsTr("Connect")
                onTriggered: root.startConnect()
            }
            ScopedAction {
                glyph: "◳"; label: qsTr("Calibrate")
                primary: root.needsCalibration
                onTriggered: root.mode = "calibrate"
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

        Repeater {
            model: cameraManager.cameraList
            delegate: CamRow {
                required property var modelData
                width: listCol.width
                camKey:   modelData.cameraKey
                camIndex: modelData.index
                camName: modelData.alias && modelData.alias !== "" ? modelData.alias
                                                                   : modelData.description
                serial: modelData.serialNumber
                perspective: modelData.perspective    // 2 = face-on, 1 = down-the-line
                iface:  modelData.interface
                selected: modelData.selected
            }
        }
    }

    // ── CALIBRATE view — compact in-panel stub ──────────────────────────────
    CameraCalibrationFlow {
        id: calibFlow
        visible: root.mode === "calibrate"
        anchors { left: parent.left; right: parent.right; top: hairline.bottom; bottom: parent.bottom }
        layoutMode: "compact"
        showHeader: false
        onCompleted: root.mode = "list"
        onCancelled: root.mode = "list"
    }

    // Attention frame around the WHOLE panel while calibrating — drawn on top,
    // inset slightly from the popup edge with a thin border.
    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.sp(6)
        visible: root.mode === "calibrate"
        color: "transparent"
        radius: Theme.radius
        border.width: Theme.sp(1)
        border.color: Theme.colorAttention
        z: 10
    }

    component ScopedAction: Rectangle {
        property string glyph: ""; property string label: ""; property bool primary: false
        signal triggered()
        width: (root.width - Theme.sp(24) - Theme.sp(16)) / 3
        height: Theme.sp(50); radius: Theme.radius
        color:        primary ? Theme.colorAttentionLight : "transparent"
        border.width: 1
        border.color: primary ? Theme.colorAttention : Theme.colorBorderStrong
        Column {
            anchors.centerIn: parent; spacing: Theme.sp(4)
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: glyph
                   font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(16)
                   color: primary ? Theme.colorAttention : Theme.colorText2 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: label
                   font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                   color: primary ? Theme.colorAttention : Theme.colorText }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: parent.triggered() }
    }

    component CamRow: Item {
        id: camRow
        property string camKey:   ""
        property int    camIndex: -1
        property string camName:  ""
        property int    perspective: 0
        property string serial: ""; property string iface: ""; property bool selected: false

        // Live controller for this camera (reactive on cameraManager.instances).
        readonly property var realController: {
            var insts = cameraManager.instances
            for (var i = 0; i < insts.length; ++i)
                if (insts[i].deviceSerialNumber === serial) return insts[i]
            return null
        }
        readonly property bool connected:     realController !== null
        readonly property bool deviceEnabled: root.sessionCameraExcluded.indexOf(camKey) < 0

        readonly property string perspLabel: perspective === 2 ? qsTr("Face-on")
                                            : perspective === 1 ? qsTr("Down-the-line")
                                            : qsTr("Unassigned")
        height: Theme.sp(60)

        // ── Live preview lifecycle ──────────────────────────────────────────
        // The controller forwards frames to its settings sink (works while it is
        // recording — startPreview() no-ops the device open in that case). Bound
        // only while the panel is active (popup open) and the camera is connected.
        property var _boundCtrl: null
        function _syncPreview() {
            var c = (realController !== null && root.active) ? realController : null
            if (_boundCtrl === c) return
            if (_boundCtrl) { _boundCtrl.setSettingsSink(null); _boundCtrl.stopPreview() }
            _boundCtrl = c
            if (c) { c.setSettingsSink(thumbOut.videoSink); c.startPreview() }
        }
        onRealControllerChanged: _syncPreview()
        Component.onCompleted:   _syncPreview()
        Component.onDestruction: { if (_boundCtrl) { _boundCtrl.setSettingsSink(null); _boundCtrl.stopPreview() } }
        Connections { target: root; function onActiveChanged() { camRow._syncPreview() } }

        Rectangle {  // row hairline
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1; color: Theme.colorBorder
        }

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.sp(15); rightMargin: Theme.sp(15) }
            spacing: Theme.sp(11)

            // Status dot when not connected …
            Rectangle {
                visible: !connected
                Layout.preferredWidth: Theme.sp(8); Layout.preferredHeight: Theme.sp(8)
                radius: Theme.sp(4)
                opacity: deviceEnabled ? 1.0 : 0.45
                color: Theme.colorText3
            }
            // … live preview thumbnail when connected.
            Rectangle {
                visible: connected
                opacity: deviceEnabled ? 1.0 : 0.45
                Layout.preferredWidth:  Theme.sp(72)
                Layout.preferredHeight: Theme.sp(40)
                radius: Theme.sp(4); clip: true
                color: Theme.colorBg
                border.width: 1; border.color: Theme.colorBorderStrong
                VideoOutput {
                    id: thumbOut
                    anchors.fill: parent; anchors.margins: 1
                    fillMode: VideoOutput.PreserveAspectFit
                }
            }

            Column {
                Layout.fillWidth: true; spacing: Theme.sp(2)
                opacity: deviceEnabled ? 1.0 : 0.45
                Text {
                    text: perspLabel + " · " + camName
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText; elide: Text.ElideRight
                }
                Text {
                    text: !deviceEnabled ? qsTr("disabled — won't connect")
                        : [serial !== "" ? "SN " + serial : "", iface]
                              .filter(function(s){ return s !== "" }).join(" · ")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData; color: Theme.colorText3
                }
            }

            // Enable toggle — session-local; matches the standard app toggle.
            Rectangle {
                id: enableToggle
                Layout.alignment: Qt.AlignVCenter
                width:  Theme.sp(34)
                height: Theme.sp(18)
                radius: Theme.sp(9)
                color:  deviceEnabled ? Theme.colorAccent : Theme.colorBg3
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                Rectangle {
                    width:  Theme.sp(12)
                    height: Theme.sp(12)
                    radius: Theme.sp(6)
                    color:  "white"
                    anchors.verticalCenter: parent.verticalCenter
                    x: deviceEnabled ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                    Behavior on x { NumberAnimation { duration: 120 } }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.setCameraEnabled(camKey, camIndex, !deviceEnabled)
                }
            }
        }
    }
}
