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

// Persistent session toolbar (Command-Bar direction): session clock + the one
// session-global Capture verb + two count-badged device pills. Each pill opens a
// Popup hosting the relevant device panel. Reusable across all four mode screens
// (Wrist first). Calibration is handled entirely INSIDE the panels — the toolbar
// no longer routes a calibrate request anywhere.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    implicitHeight: Theme.sp(60)

    // Live capture truth — mirrors the actual EventBuffer state (same source as
    // the Resource Monitor). cameraManager.bufferState notifies on every net
    // transition, including those caused by IMU register/deregister (forwarded
    // via applyCaptureIntent in main.cpp).
    readonly property bool captureLive: cameraManager.bufferState === "capturing"

    // ── Aggregate device state (drives pill badges + colours) ───────────────
    readonly property int  camTotal:     cameraManager.cameraList.length
    readonly property int  camConnected: cameraManager.instances.length
    // True when a connected camera is not "fixed in place" — i.e. it still needs
    // (stereo) calibration this session. Mirrors PpCameraPanel.needsCalibration;
    // stays lit until the camera is calibrated/fixed.
    readonly property bool camNeedsAttention: {
        var _dep  = cameraManager.instances
        var list  = cameraManager.cameraList
        var fixed = appSettings.cameraFixedInPlace
        for (var i = 0; i < list.length; ++i)
            if (list[i].selected && fixed[list[i].cameraKey] !== true) return true
        return false
    }

    readonly property int  imuTotal:     imuManager.imuDeviceList.length
    readonly property int  imuConnected: imuManager.imuCount   // connected instance count
    // True when at least one connected IMU is not yet *successfully* calibrated →
    // attention. A sensor counts as calibrated only when anatCalibrated AND the
    // mount check passes (same thresholds the calibration flow uses: deviation
    // ≤ 15°, gravity ≤ 25°). anatCalibrated alone flips true at the phase-1 arm-down
    // capture, before phase-2 mount validation — so a failed calibration must keep
    // the attention indicator lit, not clear it.
    function _imuCalibratedOk(inst) {
        return inst && inst.anatCalibrated
            && inst.mountDeviationDeg    <= 15.0
            && inst.mountGravityErrorDeg <= 25.0
    }
    readonly property bool imuNeedsAttention: {
        var _dep = imuManager.instances
        var list = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i) {
            var inst = imuManager.instanceFor(list[i].id)
            if (inst && inst.imuConnected && !_imuCalibratedOk(inst)) return true
        }
        return false
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.aesthetic === "instrument" ? Theme.colorBg2 : Theme.colorSurface
    }
    Rectangle {  // bottom hairline
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
    }

    RowLayout {
        anchors { fill: parent; leftMargin: Theme.sp(16); rightMargin: Theme.sp(14) }
        spacing: Theme.sp(12)

        // ── Capture (session-global) — far left, always first ───────────────
        Rectangle {
            id: captureBtn
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: captureLbl.implicitWidth + Theme.sp(28)
            implicitHeight: Theme.sp(40)
            radius: Theme.radius
            color: root.captureLive ? Theme.colorErrorLight : Theme.colorAccent
            border.width: root.captureLive ? 1 : 0
            border.color: Theme.colorError
            Row {
                id: captureLbl
                anchors.centerIn: parent
                spacing: Theme.sp(8)
                Rectangle {
                    width: Theme.sp(9); height: Theme.sp(9); radius: Theme.sp(4.5)
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.captureLive ? Theme.colorError
                                            : (Theme.dark ? Theme.colorBg : "#FFFFFF")
                }
                Text {
                    text: root.captureLive ? qsTr("Stop") : qsTr("Capture")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                    color: root.captureLive ? Theme.colorError
                                            : (Theme.dark ? Theme.colorBg : "#FFFFFF")
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (root.captureLive) {
                        cameraManager.stopCapture()
                    } else {
                        if (!sessionController.running) sessionController.start()
                        cameraManager.startCapture()
                    }
                }
            }
        }

        // ── Session clock (alongside Capture, to its right) ─────────────────
        Column {
            spacing: Theme.sp(2)
            Layout.alignment: Qt.AlignVCenter
            Text {
                text: qsTr("SESSION")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
            }
            Row {
                spacing: Theme.sp(8)
                Rectangle {
                    width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(4)
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.captureLive ? Theme.colorError : Theme.colorText3
                    SequentialAnimation on opacity {
                        running: root.captureLive && !Theme.reduceMotion
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: Theme.durationSlow }
                        NumberAnimation { to: 1.0; duration: Theme.durationSlow }
                    }
                }
                Text {
                    text: sessionController.elapsedLabel
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzData
                    color: Theme.colorText
                }
            }
        }

        Item { Layout.fillWidth: true }   // push device pills to the right

        // ── Cameras pill ────────────────────────────────────────────────────
        DevicePill {
            id: camPill
            glyph: "◫"                 // ◫
            title: qsTr("Cameras")
            count: root.camConnected
            valueText: root.camTotal === 0 ? qsTr("none")
                        : root.camNeedsAttention ? qsTr("calibrate")
                        : qsTr("%1 of %2").arg(root.camConnected).arg(root.camTotal)
            ledColor: root.camNeedsAttention ? Theme.colorAttention
                       : root.camConnected > 0 ? Theme.colorGood : Theme.colorText3
            attention: root.camNeedsAttention
            onClicked: { imuPopup.close(); camPopup.opened ? camPopup.close() : camPopup.open() }
        }

        // ── IMUs pill ───────────────────────────────────────────────────────
        DevicePill {
            id: imuPill
            glyph: "⦿"                 // ⦿
            title: qsTr("IMUs")
            count: root.imuConnected
            valueText: root.imuTotal === 0 ? qsTr("none")
                        : root.imuNeedsAttention ? qsTr("calibrate")
                        : qsTr("%1 connected").arg(root.imuConnected)
            ledColor: root.imuNeedsAttention ? Theme.colorAttention
                       : root.imuConnected > 0 ? Theme.colorGood : Theme.colorText3
            attention: root.imuNeedsAttention
            onClicked: { camPopup.close(); imuPopup.opened ? imuPopup.close() : imuPopup.open() }
        }
    }

    // ── Popups host the reusable panels; positioned under their pills ───────
    // margins clamp the popup within the window; the panel's implicitHeight
    // drives the popup height so it grows when a panel enters calibrate mode.
    Popup {
        id: camPopup
        parent: camPill
        y: camPill.height + Theme.sp(10)
        x: camPill.width - width
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpCameraPanel {}
    }

    Popup {
        id: imuPopup
        parent: imuPill
        y: imuPill.height + Theme.sp(10)
        x: imuPill.width - width
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpImuPanel {}
    }

    // ── Inline pill component ───────────────────────────────────────────────
    component DevicePill: Rectangle {
        property string glyph:     ""
        property string title:     ""
        property string valueText: ""
        property int    count:     0
        property color  ledColor:  Theme.colorText3
        property bool   attention: false   // tints only the value text (e.g. "calibrate")
        signal clicked()

        Layout.alignment: Qt.AlignVCenter
        implicitWidth: pillRow.implicitWidth + Theme.sp(24)
        implicitHeight: Theme.sp(44)
        radius: Theme.radius
        color: pillMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
        border.width: 1; border.color: Theme.colorBorderMid
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        RowLayout {
            id: pillRow
            anchors { fill: parent; leftMargin: Theme.sp(11); rightMargin: Theme.sp(13) }
            spacing: Theme.sp(11)

            Item {  // glyph + count badge
                Layout.preferredWidth: Theme.sp(34); Layout.preferredHeight: Theme.sp(34)
                Layout.alignment: Qt.AlignVCenter
                Rectangle {
                    anchors.fill: parent; radius: Theme.radius; color: Theme.colorSurface
                    Text {
                        anchors.centerIn: parent; text: glyph
                        font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(18)
                        color: Theme.colorText2
                    }
                }
                Rectangle {
                    anchors { right: parent.right; top: parent.top
                              rightMargin: -Theme.sp(5); topMargin: -Theme.sp(5) }
                    width: Theme.sp(19); height: Theme.sp(19); radius: width / 2
                    color: ledColor
                    Text {
                        anchors.centerIn: parent; text: count
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        color: Theme.dark ? Theme.colorBg : "#FFFFFF"
                    }
                }
            }
            Column {
                Layout.alignment: Qt.AlignVCenter; spacing: Theme.sp(2)
                Text {
                    text: title; font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                }
                Text {
                    text: valueText; font.family: Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color: attention ? Theme.colorAttention : Theme.colorText
                }
            }
        }
        MouseArea {
            id: pillMa; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked()
        }
    }
}
