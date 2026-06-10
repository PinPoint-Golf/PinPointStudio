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
//   "list"      — Scan / Connect / Calibrate actions, an all-cameras "Live pose"
//                 toggle, and the camera rows with a per-camera enable toggle
//                 (session-local, owned by CameraManager so the per-screen video
//                 tiles share it). Connect starts the full capture pipeline on
//                 every enabled camera — the screens' video tiles stream from it.
//                 Calibrate is attention-framed once a camera is connected that is
//                 not "fixed in place" (appSettings.cameraFixedInPlace) — i.e.
//                 needs stereo cal.
//   "calibrate" — hosts CameraCalibrationFlow (a STUB for now) compactly in-panel.
// The attention frame wraps the WHOLE panel while calibrating.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property string mode:   "list"        // "list" | "calibrate"

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

    // Per-session camera enablement lives in CameraManager
    // (cameraManager.sessionCameraExcluded) so the per-screen video tiles,
    // every toolbar instance AND the start-session wizard share one list.
    // Seeded from appSettings.cameraExcluded at startup, re-seeded by the
    // wizard on open; never written back to settings (global enablement is
    // owned by Settings).

    // True when at least one camera is session-enabled and every enabled one is
    // connected — drives the Connect ⇄ Disconnect action toggle.
    readonly property bool allConnected: {
        var list = cameraManager.cameraList
        var enabled = 0
        for (var i = 0; i < list.length; ++i)
            if (list[i].sessionEnabled) { ++enabled; if (!list[i].selected) return false }
        return enabled > 0
    }

    // Connect every session-enabled, not-yet-selected camera, then start the
    // capture pipeline so the visible video tiles stream (same path as the Play
    // capture tab — pose/ball pipeline, ring buffer, swing replay).
    function startConnect() {
        var list = cameraManager.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].sessionEnabled && !list[i].selected)
                cameraManager.setSelected(list[i].index, true)
        if (!cameraManager.isRecording && cameraManager.anySelected)
            cameraManager.startAll()
    }

    // Disconnect every connected camera (enabled or not). Stop the recording
    // session first so isRecording doesn't stay true with zero instances.
    function disconnectAll() {
        if (cameraManager.isRecording) cameraManager.stopAll()
        var list = cameraManager.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].selected) cameraManager.setSelected(list[i].index, false)
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
                glyph: "⇄"
                label: root.allConnected ? qsTr("Disconnect") : qsTr("Connect")
                onTriggered: root.allConnected ? root.disconnectAll() : root.startConnect()
            }
            ScopedAction {
                glyph: "◳"; label: qsTr("Calibrate")
                primary: root.needsCalibration
                onTriggered: root.mode = "calibrate"
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

        // All-cameras live pose-estimation toggle. Session-wide: gates pose
        // inference itself (not just the overlay) on every connected camera.
        Item {
            width: parent.width; height: Theme.sp(44)
            RowLayout {
                anchors { fill: parent; leftMargin: Theme.sp(15); rightMargin: Theme.sp(15) }
                spacing: Theme.sp(11)
                Column {
                    Layout.fillWidth: true; spacing: Theme.sp(2)
                    Text {
                        text: qsTr("Live pose detection")
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText
                    }
                    Text {
                        text: qsTr("all cameras")
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData; color: Theme.colorText3
                    }
                }
                TogglePill {
                    Layout.alignment: Qt.AlignVCenter
                    checked: cameraManager.livePoseEnabled
                    onToggled: (v) => cameraManager.livePoseEnabled = v
                }
            }
            Rectangle {
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                height: 1; color: Theme.colorBorder
            }
        }

        Repeater {
            model: cameraManager.cameraList
            delegate: CamRow {
                required property var modelData
                width: listCol.width
                camKey:   modelData.cameraKey
                camName: modelData.alias && modelData.alias !== "" ? modelData.alias
                                                                   : modelData.description
                serial: modelData.serialNumber
                perspective: modelData.perspective    // CameraInstance.Perspective value
                iface:  modelData.interface
                selected: modelData.selected
                deviceEnabled: modelData.sessionEnabled
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

    component TogglePill: Rectangle {
        property bool checked: false
        signal toggled(bool value)
        width:  Theme.sp(34)
        height: Theme.sp(18)
        radius: Theme.sp(9)
        color:  checked ? Theme.colorAccent : Theme.colorBg3
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle {
            width:  Theme.sp(12)
            height: Theme.sp(12)
            radius: Theme.sp(6)
            color:  "white"
            anchors.verticalCenter: parent.verticalCenter
            x: parent.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
            Behavior on x { NumberAnimation { duration: 120 } }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.toggled(!parent.checked)
        }
    }

    component CamRow: Item {
        id: camRow
        property string camKey:   ""
        property string camName:  ""
        property int    perspective: 0
        property string serial: ""; property string iface: ""; property bool selected: false
        property bool   deviceEnabled: true   // session enablement, from cameraList

        // Live controller for this camera (reactive on cameraManager.instances).
        readonly property var realInstance: {
            var insts = cameraManager.instances
            for (var i = 0; i < insts.length; ++i)
                if (insts[i].cameraKey === camKey) return insts[i]
            return null
        }
        readonly property bool connected: realInstance !== null

        readonly property string perspLabel: perspective === CameraInstance.FaceOn ? qsTr("Face-on")
                                            : perspective === CameraInstance.DownTheLine ? qsTr("Down-the-line")
                                            : qsTr("Unassigned")
        height: Theme.sp(60)

        Rectangle {  // row hairline
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1; color: Theme.colorBorder
        }

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.sp(15); rightMargin: Theme.sp(15) }
            spacing: Theme.sp(11)

            // Status dot — good when connected, muted otherwise.
            Rectangle {
                Layout.preferredWidth: Theme.sp(8); Layout.preferredHeight: Theme.sp(8)
                radius: Theme.sp(4)
                opacity: deviceEnabled ? 1.0 : 0.45
                color: connected ? Theme.colorGood : Theme.colorText3
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

            // Enable toggle — session-local; disabling also disconnects.
            TogglePill {
                Layout.alignment: Qt.AlignVCenter
                checked: camRow.deviceEnabled
                onToggled: (v) => cameraManager.setSessionCameraEnabled(camRow.camKey, v)
            }
        }
    }
}
