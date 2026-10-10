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

// Session setup — Cameras (the retired ScreenSessionWizard.qml Panel 1, l.917–1092, with its footer hint,
// Skip, Connect and traveling-light frame, l.2075–2084, 2130, 2154–2158, 2200–2204, 2232),
// ported with every string, colour and size unchanged. Hardware through `ctx` only (lint W4).
//
// Connect is the footer's `primary` until nothing enabled is left to connect; while it is, the
// page does not offer Continue, so the header › (Continue only) cannot mark the step done without
// connecting (F5, D2).
//
// DECISION(stage5a): the two-camera branches of today's panel are not carried: the "Down-the-line
// camera" placeholder row and the "Down-the-line camera not assigned" hint (l.964–965, 2081–2082)
// and the two-view intro text (l.982) applied only when the session type required two cameras —
// the three comingSoon types, which Home never opens. Nothing is "required" any more (§4.12), and
// SetupContext.camsAllConnected already dropped the same clause in Stage 4. For every session
// that can be opened, the page is today's.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: camsCol.implicitHeight + Theme.sp(32)

    // Connect ↔ Continue: the primary is Connect while an enabled camera is not yet connected.
    // Cameras connect synchronously, so there is no "Connecting…" label (today's camConnectMode).
    primary: page.ctx.anyCameraToConnect
             ? ({ label: qsTr("Connect"), busy: page.ctx.camerasConnecting,
                  run: function() { page.ctx.connectCameras() } })
             : null
    canContinue: !page.ctx.anyCameraToConnect
    canSkip:     !page.ctx.camsAllConnected
    busy:        page.ctx.camerasConnecting

    hint: {
        if (page.ctx.camsAllConnected)
            return qsTr("All cameras connected")
        if (page.ctx.anyCameraToConnect)
            return qsTr("Cameras assigned — tap Connect to start them")
        return page.ctx.faceOn.length === 0 ? qsTr("Face-on camera not assigned")
                                             : qsTr("Camera disabled — enable it to capture video this session")
    }
    hintTone: page.ctx.camsAllConnected ? "good" : "warn"

    function camDetail(d) {
        if (!d) return ""
        var sn  = d.serialNumber ? qsTr("SN: ") + d.serialNumber : ""
        var res = (d.maxWidth && d.maxHeight) ? d.maxWidth + " × " + d.maxHeight : ""
        var ifc = d.interface || ""
        return [d.alias || d.description, sn, ifc, res].filter(function(s){ return s !== "" }).join(" · ")
    }

    Column {
        id: camsCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.gap(32) }
        spacing: Theme.gap(16)

        // One row per available camera — capture is not limited by
        // view assignment. Grouped face-on, down-the-line, then
        // unassigned/other. A placeholder row appears for a missing
        // face-on view so the "ASSIGN IN SETTINGS" failure state still shows.
        readonly property var camRows: {
            var rows = []
            var fo = page.ctx.faceOn, dtl = page.ctx.dtl, others = page.ctx.others
            if (fo.length === 0) rows.push({ persp: CameraInstance.FaceOn, data: null })
            for (var i = 0; i < fo.length; ++i)
                rows.push({ persp: CameraInstance.FaceOn, data: fo[i] })
            for (var j = 0; j < dtl.length; ++j)
                rows.push({ persp: CameraInstance.DownTheLine, data: dtl[j] })
            for (var k = 0; k < others.length; ++k)
                rows.push({ persp: others[k].perspective, data: others[k] })
            return rows
        }

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("Connecting your cameras")
            body:    qsTr("Every connected camera records this session alongside your IMU data. Enable and connect the cameras below — analysis uses the face-on view, so make sure at least one camera is assigned to it.")
        }

        Column {
            width: parent.width
            spacing: 0

            // Per-camera rows — same pattern as the IMU slot rows:
            // enable toggle (manager-owned session enablement), live
            // status chip, and a streaming thumbnail once connected.
            // One row per assigned camera; perspectives can repeat.
            Repeater {
                model: camsCol.camRows

                delegate: SetupCheckRow {
                    required property var modelData

                    width: parent.width

                    // cameraList entry for this row (null = placeholder
                    // for a missing face-on view). Reactive:
                    // cameraListChanged fires on selection and
                    // session-enablement changes.
                    readonly property var _data: modelData.data

                    label: {
                        if (!_data)
                            return qsTr("Face-on camera")
                        var name = _data.alias || _data.description
                        if (modelData.persp === CameraInstance.FaceOn)
                            return qsTr("Face-on camera — %1").arg(name)
                        if (modelData.persp === CameraInstance.DownTheLine)
                            return qsTr("Down-the-line camera — %1").arg(name)
                        if (modelData.persp === CameraInstance.Impact)
                            return qsTr("Impact camera — %1").arg(name)
                        return qsTr("Camera — %1").arg(name)
                    }

                    // Live instance for the thumbnail. cameraInstanceFor() is not reactive on
                    // its own: the read of ctx.cameraInstances is the dependency (PpCameraPanel
                    // pattern — reactive on cameraManager.instances).
                    readonly property QtObject _inst: {
                        var _dep = page.ctx.cameraInstances
                        if (!_data) return null
                        return page.ctx.cameraInstanceFor(_data.cameraKey)
                    }

                    readonly property bool _enabled:
                        _data !== null && _data.sessionEnabled
                    readonly property bool _connected:
                        _data !== null && _data.selected

                    // Session-local enable toggle — manager-owned so the
                    // toolbar panel and video tiles share it. Disabling a
                    // connected camera also disconnects it (C++ side).
                    showToggle:    _data !== null
                    toggleChecked: _enabled
                    onToggled: (v) => {
                        if (_data) page.ctx.setCameraEnabled(_data.cameraKey, v)
                    }

                    disabled:    _data !== null && !_enabled
                    subDisabled: _data
                        ? qsTr("%1 · DISABLED — WON'T CONNECT").arg(_data.alias || _data.description)
                        : ""

                    ok:   _enabled && _connected
                    warn: _enabled && !_connected

                    // Unassigned cameras record fine — nudge that analysis
                    // won't use them until a view is assigned ("Other" is
                    // a deliberate assignment, no nudge).
                    subOk:   modelData.persp === CameraInstance.None
                             ? page.camDetail(_data) + qsTr(" · NO VIEW ASSIGNED")
                             : page.camDetail(_data)
                    subWarn: _data
                        ? (_data.alias || _data.description) + qsTr(" — PRESS CONNECT")
                        : ""
                    subFail: modelData.persp === CameraInstance.FaceOn
                        ? qsTr("NOT FOUND — ASSIGN FACE-ON PERSPECTIVE IN SETTINGS → CAMERAS")
                        : qsTr("NOT FOUND — ASSIGN DOWN-THE-LINE PERSPECTIVE IN SETTINGS → CAMERAS")

                    chipText:      !_enabled ? "" : (_connected ? qsTr("Connected") : "")
                    chipColor:     Theme.colorGoodLight
                    chipTextColor: Theme.colorGood

                    // Streaming thumbnail once the instance exists.
                    thumbInstance: _inst
                }
            }

        }


        // Settings deep-link
        Text {
            text:           qsTr("→ Open camera settings")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorAccent
            MouseArea {
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                onClicked:    page.flow.openSettings(page.ctx.settingsPanelCameras)
            }
        }
    }
}
