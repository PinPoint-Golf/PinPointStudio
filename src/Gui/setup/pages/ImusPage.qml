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

// Session setup — Sensors (the retired ScreenSessionWizard.qml Panel 4, l.1289–1638, with its footer hint,
// Skip, Connect and traveling-light frame, l.2093–2097, 2133, 2150–2152, 2175, 2187, 2206–2208,
// 2233–2234). Hardware through `ctx` only (lint W4).
//
// What changed from today's panel (design §4.12, "found is not wanted"):
//   - one row per ENUMERATED sensor, not one per slot of a per-session-type table (that table is
//     gone). A wG3 is one row: one peripheral, one enable switch, one mount ("Lead forearm + hand");
//   - each row shows its MOUNT by name. A remembered mount is confirmed, not asked again (mounts
//     are settings and stick); a sensor that is in the session with no mount gets the question
//     "Where is this sensor worn?" in the row, and "change" reopens it for a mounted one. The
//     choices are the Settings ▸ IMUs combo's (ImuMounts, the manager's one-claim rule): a mount
//     another sensor holds is greyed and names its holder; a refusal leaves the mount as it was
//     and says who holds it, in the page;
//   - a mount whose sensor the scan cannot see is its own row, "<Mount> — <name> (not found)",
//     with today's "power it on and Scan" (D6, reworded from slot to mount);
//   - Connect is the paced connect in ImuManager (one call; a wG3 connects once, D4). The primary
//     reads "Connecting…" for as long as the queue or any sensor of the session is still
//     connecting, never "Continue →" (D3). Leaving the page does not stop the queue (D5): it lives
//     in the manager, and only the flow's exit cancels it;
//   - nothing is required: with no sensor in the session the page says so and Continue stays on.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    implicitHeight: imusCol.implicitHeight + Theme.sp(32)

    readonly property bool _connecting: page.ctx.sensorsConnectingInSession
    readonly property bool _toConnect:  page.ctx.devicesToConnect.length > 0
    readonly property bool _noneInSession: page.ctx.inSessionDevices.length === 0

    // Connect ↔ Connecting… ↔ Continue. While a primary is offered Continue is not (the header ›
    // is Continue only — F5, D2).
    primary: _connecting
             ? ({ label: qsTr("Connecting…"), busy: true, run: function() {} })
             : (_toConnect ? ({ label: qsTr("Connect"), busy: false,
                                run: function() { page.ctx.connectSensors() } })
                           : null)
    canContinue: !_connecting && !_toConnect
    canSkip:     !page.ctx.allInSessionConnected
    busy:        _connecting

    // DECISION(stage5b): today's hints spoke of "required" sensors; nothing is required now
    // (§4.12), so they speak of the sensors in the session.
    hint: {
        if (_noneInSession && page.ctx.absentMounts.length === 0)
            return qsTr("No sensors in this session — wrist angles will not be measured")
        if (_connecting)
            return qsTr("Connecting sensors…")
        if (_toConnect)
            return qsTr("Sensors assigned — tap Connect to pair them")
        if (page.ctx.absentMounts.length > 0)
            return qsTr("A sensor was not found — power it on and Scan, or skip")
        return qsTr("All sensors connected")
    }
    hintTone: (_noneInSession && page.ctx.absentMounts.length === 0) ? "neutral"
              : (page.ctx.allInSessionConnected && page.ctx.absentMounts.length === 0 ? "good" : "warn")

    // The last refusal, per device id: { role, text }. Cleared by the next pick and by any change
    // to the mounts (the holder may just have let go) — ImusPanel's rule.
    property var _refusals: ({})
    // The device whose mount the coach is changing ("" = none): its row shows the question.
    property string _changing: ""

    // Pick a mount for a sensor (the question's choices call this). Exactly one write.
    // DECISION(stage5b): the question offers mounts only, no "Unassigned": a sensor not wanted
    // this session is switched off with the row's Enable; a mount is taken away in Settings ▸ IMUs.
    function chooseMount(deviceId, role) {
        var r = {}
        for (var k in _refusals) if (k !== deviceId) r[k] = _refusals[k]
        _refusals = r
        if (page.ctx.setMount(deviceId, role)) _changing = ""
        // A refusal leaves the mount unchanged — nothing to snap back here: the row shows the
        // manager's state, and mountRefused (below) names the holder.
    }

    Connections {
        target:  page.ctx
        enabled: page.active
        function onMountRefused(deviceId, role, holderId) {
            var r = {}
            for (var k in page._refusals) r[k] = page._refusals[k]
            r[deviceId] = { role: role,
                            text: qsTr("%1 is held by %2. Unassign or disable it first.")
                                      .arg(ImuMounts.label(role) || role).arg(page.ctx.sensorName(holderId)) }
            page._refusals = r
        }
    }
    Connections {
        target:  page.ctx.settings
        enabled: page.active
        function onImuRolesChanged() { page._refusals = ({}) }
    }

    Column {
        id: imusCol
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
        spacing: Theme.sp(16)

        SetupStepIntro {
            width:   parent.width
            eyebrow: page.stepLabel
            heading: qsTr("Attaching your motion sensors")
            // DECISION(stage5b): today's body said the positions were "specific to this session
            // type"; they are the sensors' own mounts now, remembered from one session to the next.
            body:    qsTr("Switch each sensor on before stepping up. Each sensor below shows where it is worn — confirm it, or choose a place for a new one — then pair them. Change mounts any time in Settings → IMUs.")
        }

        // ── Scan header ─────────────────────────────────────────────
        RowLayout {
            width: parent.width
            spacing: Theme.sp(8)

            Text {
                Layout.fillWidth: true
                text: {
                    var n = page.ctx.sensorsFound
                    return n === 0
                        ? qsTr("NO DEVICES FOUND")
                        : n === 1 ? qsTr("1 DEVICE FOUND")
                                  : qsTr("%1 DEVICES FOUND").arg(n)
                }
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color: page.ctx.sensorsFound > 0 ? Theme.colorText3 : Theme.colorWarn
            }

            // Scan button — mirrors ImusPanel.qml. Bound to the manager's authoritative scan state
            // (D7), NOT a local timer: the window is 90 s with HackMotion enabled, and a local clock
            // that ran out early re-offered a Scan tap the enumerator's re-entry guard swallowed.
            Rectangle {
                id: scanBtn
                objectName: "sensorScan"
                readonly property bool scanning: page.ctx.sensorScanActive

                implicitWidth:  scanMeasure.implicitWidth + Theme.sp(24)
                implicitHeight: Theme.sp(28)
                radius: Theme.radius
                color:  scanning ? Theme.colorAccentLight
                                 : (scanArea.containsMouse ? Theme.colorBg2
                                        : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0))
                border.width: 1
                border.color: scanning ? Theme.colorAccent
                                       : (scanArea.containsMouse ? Theme.colorAccentMid : Theme.colorBorderStrong)
                Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                Text {
                    id: scanMeasure
                    visible: false
                    text: qsTr("Scanning…")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
                }
                Text {
                    anchors.centerIn: parent
                    text:           scanBtn.scanning ? qsTr("Scanning…") : qsTr("Scan")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          scanBtn.scanning ? Theme.colorAccent : Theme.colorText2
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
                PpPressable {
                    id: scanArea
                    onClicked: page.ctx.rescanSensors()
                }
            }
        }

        Column {
            width: parent.width
            spacing: 0

            // ── One row per enumerated sensor ───────────────────────
            Repeater {
                model: page.ctx.devices.filter(function(d) { return d.present })

                delegate: Column {
                    id: devRow
                    required property var modelData

                    width: parent.width
                    spacing: 0

                    readonly property var  _dev:       modelData
                    readonly property bool _excluded:  !_dev.sessionEnabled
                    readonly property bool _mounted:   _dev.role !== ""
                    readonly property var  _inst:      _dev.instance
                    readonly property string _stateLabel: _dev.stateLabel
                    readonly property bool _connected: _dev.connected
                    readonly property bool _failedConnect: _dev.failed
                    // The question: in the session with no mount, or the coach asked to change it.
                    readonly property bool _asking: !_excluded && (!_mounted || page._changing === _dev.id)
                    readonly property var  _refusal: page._refusals[_dev.id] !== undefined ? page._refusals[_dev.id] : null
                    // The choices, recomputed when the mounts or the sensors change (ctx.devices
                    // depends on both; offeredMounts is not reactive on its own).
                    readonly property var _options: {
                        var _dep = page.ctx.devices
                        return page.ctx.offeredMounts(_dev)
                    }

                    SetupCheckRow {
                        id: sensorRow
                        objectName: "sensorRow"
                        // Which sensor this row is, for the tests and the log.
                        property string deviceId: devRow._dev.id
                        property string mountLabel: devRow._dev.roleLabel
                        width: parent.width

                        label: devRow._mounted ? qsTr("%1 — %2").arg(devRow._dev.roleLabel).arg(devRow._dev.name)
                                               : devRow._dev.name

                        // Excluded = disabled for connection THIS SESSION only. Manager-owned, so
                        // the toolbar sensor panel shares it; never written back to settings.
                        // Disabling a connected sensor also disconnects it (manager side).
                        disabled:      devRow._excluded
                        subDisabled:   qsTr("%1 · DISABLED — WON'T CONNECT").arg(devRow._dev.name)
                        showToggle:    true
                        toggleChecked: !devRow._excluded
                        onToggled: (v) => page.ctx.setSensorEnabled(devRow._dev.id, v)

                        // A sensor with no mount sits out: muted, not an error.
                        optional: !devRow._mounted
                        ok:   !devRow._excluded && devRow._mounted && devRow._connected
                        warn: !devRow._excluded && devRow._mounted && !devRow._connected

                        // RHS chip: as soon as an instance exists, live as the connection goes.
                        chipText: {
                            if (devRow._excluded || !devRow._inst) return ""
                            if (devRow._connected) return qsTr("Connected")
                            if (devRow._failedConnect) return qsTr("Connection Failed")
                            return qsTr("Connecting")
                        }
                        chipColor: {
                            if (!devRow._inst) return "transparent"
                            if (devRow._connected) return Theme.colorGoodLight
                            if (devRow._failedConnect) return Theme.colorErrorLight
                            return Theme.colorWarnLight
                        }
                        chipTextColor: {
                            if (!devRow._inst) return Theme.colorText3
                            if (devRow._connected) return Theme.colorGood
                            if (devRow._failedConnect) return Theme.colorError
                            return Theme.colorWarn
                        }

                        // DECISION(stage5d): name and transport only. Today's row also showed the
                        // device id, which on macOS is a raw "{…}" UUID the golfer must never see.
                        subOk: [devRow._dev.name, devRow._dev.transport]
                                   .filter(function(s) { return s && s !== "" }).join(" · ")
                        subWarn: devRow._dev.name + qsTr(" — PRESS CONNECT")
                        subFail: qsTr("NO MOUNT — IT SITS OUT THIS SESSION")
                    }

                    // ── The mount: confirmed, or asked ──────────────────
                    Item {
                        width:  parent.width
                        height: mountCol.implicitHeight + (mountCol.visible ? Theme.sp(10) : 0)
                        visible: !devRow._excluded

                        Column {
                            id: mountCol
                            visible: !devRow._excluded
                            anchors { left: parent.left; right: parent.right; top: parent.top
                                      leftMargin: Theme.sp(26); topMargin: Theme.sp(6) }
                            spacing: Theme.sp(6)

                            // A remembered mount: confirmed by being shown (in the row's label),
                            // with a small way to change it.
                            Text {
                                objectName: "mountChange"
                                visible:  devRow._mounted && !devRow._asking
                                text:     qsTr("change")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                font.letterSpacing: Theme.trackingData
                                color: changeMa.containsMouse ? Theme.colorText : Theme.colorAccent
                                MouseArea {
                                    id: changeMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape:  Qt.PointingHandCursor
                                    onClicked:    page._changing = devRow._dev.id
                                }
                            }

                            Text {
                                objectName: "mountQuestion"
                                visible: devRow._asking
                                text:    qsTr("Where is this sensor worn?")
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          Theme.colorText2
                            }

                            Flow {
                                visible: devRow._asking
                                width:   parent.width
                                spacing: Theme.sp(6)

                                Repeater {
                                    model: devRow._asking ? devRow._options : []
                                    delegate: Rectangle {
                                        id: opt
                                        required property var modelData
                                        objectName: "mountOption"
                                        // For the tests: which mount, and whether it can be taken.
                                        property string role:      modelData.role
                                        property bool   taken:     modelData.taken
                                        property string optionText: optText.text
                                        readonly property bool current: devRow._dev.role === modelData.role

                                        implicitWidth:  optText.implicitWidth + Theme.sp(20)
                                        implicitHeight: Theme.sp(26)
                                        radius:  Theme.sp(13)
                                        opacity: taken ? 0.45 : 1.0
                                        color:   current ? Theme.colorAccentLight
                                                         : (optMa.containsMouse && !taken ? Theme.colorBg2 : "transparent")
                                        border.width: 1
                                        border.color: current ? Theme.colorAccent : Theme.colorBorderStrong

                                        Text {
                                            id: optText
                                            anchors.centerIn: parent
                                            // A mount another sensor holds says who, so the coach
                                            // knows which sensor to change (ImusPanel's wording).
                                            text: opt.modelData.taken
                                                  ? qsTr("%1 — held by %2").arg(opt.modelData.label).arg(opt.modelData.holderName)
                                                  : opt.modelData.label
                                            font.family:    Theme.fontBody
                                            font.pixelSize: Theme.fontSzBody2
                                            color: opt.current ? Theme.colorAccent : Theme.colorText2
                                        }
                                        MouseArea {
                                            id: optMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled:      !opt.taken
                                            cursorShape:  Qt.PointingHandCursor
                                            onClicked:    page.chooseMount(devRow._dev.id, opt.modelData.role)
                                        }
                                    }
                                }
                            }

                            // Why the last pick did not stick — in the page, never a dialog.
                            Text {
                                objectName: "mountRefusal"
                                visible:  devRow._refusal !== null
                                width:    parent.width
                                text:     devRow._refusal !== null ? devRow._refusal.text : ""
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorWarn
                                wrapMode:       Text.WordWrap
                            }
                        }
                    }
                }
            }

            // ── A mount whose sensor the scan cannot see (D6) ────────
            Repeater {
                model: page.ctx.absentMounts

                delegate: SetupCheckRow {
                    required property var modelData
                    objectName: "absentMountRow"
                    property string deviceId: modelData.deviceId
                    width: parent.width
                    label:   qsTr("%1 — %2 (not found)").arg(modelData.mountLabel).arg(modelData.name)
                    subFail: qsTr("POWER IT ON AND SCAN")
                }
            }
        }

        // Settings deep-link
        Text {
            text:           qsTr("→ Open IMU settings")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorAccent
            MouseArea {
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                onClicked:    page.flow.openSettings(page.ctx.settingsPanelImus)
            }
        }
    }
}
