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

// Settings → Motion Sensors: every enumerated IMU as a device card (alias, enable switch, read-only
// mount, output rate, capabilities strip, and a live Test panel), then the global motion-sensor
// switches and the orientation-fusion choice. Stands on SettingsPanelBase (scroll + search contract).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

SettingsPanelBase {
    id: root
    contentSpacing: Theme.gap(16)

    // Id of the IMU device whose test panel is currently open ("" = none).
    property string openTestId: ""

    // ─────────────────────────────────────────────────────────────────────────
    // Inline component — IMU device row
    // ─────────────────────────────────────────────────────────────────────────
    component ImuDeviceRow: PpDeviceCard {
        id: imuRow

        property var imuData: ({})

        // Live ImuInstance for this device; null when not selected/connected.
        // Rebinds whenever instances change (selection or connection-state change).
        readonly property QtObject inst: {
            // Read as a VALUE, not as a bare statement. `imuManager.instances` on a line of its own
            // is dead code: the compiler drops it and takes the dependency with it, so this would
            // resolve once and never rebind — exactly the bug that froze the diagnostic model's
            // inspector (see the note on `_revision` in DiagnosticModel.qml).
            if (imuManager.instances.length < 0) return null
            return imuManager.instanceFor(imuData.id)
        }

        readonly property bool isConnected: inst !== null && inst.imuConnected
        readonly property bool isExcluded:  appSettings.imuExcluded.indexOf(imuData.id) >= 0
        readonly property bool testOpen:    root.openTestId === imuData.id

        // A HackMotion wG3 is a different device KIND, not a Witmotion variant:
        // it has two sensor units instead of one, an adaptive (not settable)
        // output rate, on-device (not host-side) fusion, and no host-side zero.
        // Every branch below on this flag exists because one of those is true.
        readonly property bool isHackMotion: imuData.vendor === "hackmotion"

        // The mount this device holds, by name ("" when it has none) — shown truthfully whatever
        // the pickers offer. roleForDevice() is Q_INVOKABLE and not reactive: the roles map is
        // READ AS A VALUE first (a bare statement would be dropped with its dependency).
        readonly property string mountLabel: {
            var roles = appSettings.imuRoles
            return roles ? ImuMounts.deviceMountLabel(isHackMotion, imuManager.roleForDevice(imuData.id)) : ""
        }
        // The row's read-only mount line. Mounts are set in session setup only (Mark, 26 Oct).
        readonly property string mountLine: mountLabel !== ""
                                            ? qsTr("Worn as: %1 (set in session setup)").arg(mountLabel)
                                            : qsTr("Not worn (set in session setup)")

        // Capabilities strip cell model — reactive to live battery data.
        readonly property var capsCells: {
            var sensors = []
            if (imuData.hasAccelerometer) sensors.push(qsTr("Accel"))
            if (imuData.hasGyroscope)     sensors.push(qsTr("Gyro"))
            if (imuData.hasMagnetometer)  sensors.push(qsTr("Mag"))
            var rates = imuData.supportedRatesHz || []
            var maxRate = rates.length > 0 ? Math.max.apply(null, rates) : 0
            var batPct = (imuRow.isConnected && imuRow.inst && imuRow.inst.batteryPercent >= 0)
                             ? imuRow.inst.batteryPercent : -1
            // The battery reads green above 60%, amber above 20%, red at or below.
            var batTint = batPct < 0 ? "" : batPct > 60 ? "good" : batPct > 20 ? "warn" : "error"
            return [
                { key: qsTr("Transport"),   val: imuData.transport || "—" },
                { key: qsTr("Sensors"),     val: sensors.length > 0 ? sensors.join(" · ") : "—" },
                { key: qsTr("Accel range"), val: (imuData.accelRangeMax > 0)
                                                    ? ("±" + imuData.accelRangeMax.toFixed(0) + " g") : "—" },
                { key: qsTr("Gyro range"),  val: (imuData.gyroRangeMax > 0)
                                                    ? ("±" + imuData.gyroRangeMax.toFixed(0) + " °/s") : "—" },
                { key: qsTr("Max rate"),    val: maxRate > 0 ? (maxRate + " Hz") : "—", tint: "accent" },
                { key: qsTr("Battery"),     val: batPct >= 0 ? (batPct + "%") : "—",    tint: batTint }
            ]
        }

        implicitHeight: testOpen && !isExcluded
            ? imuRow.headerRow.height + bodyWrap.height + testPanel.height
            : imuRow.headerRow.height + (isExcluded ? excludedNote.height : bodyWrap.height)

        Behavior on implicitHeight { NumberAnimation { duration: Theme.durationFast } }

        dimmed: isExcluded
        borderColor: imuRow.testOpen    ? Theme.colorAccent
                   : imuRow.isConnected ? Theme.colorGood
                   : imuRow.isExcluded  ? Theme.colorBorderMid
                   :                      Theme.colorBorderStrong
        statusColor: imuRow.isConnected ? Theme.colorGood
                   : !imuRow.isExcluded  ? Theme.colorWarn
                   :                       Theme.colorText3

        // ── Header row (after the card's status dot) ─────────────────────────
        headerItems: [
            // Alias (editable) + meta
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.gap(2)

                PpTextField {
                    Layout.fillWidth: true
                    placeholderText:  qsTr("Device alias…")
                    text:             imuData.alias
                    onEditingFinished: imuManager.setImuAlias(imuData.imuKey, text)
                }

                Row {
                    spacing: Theme.gap(10)
                    Text { text: imuData.description || "";    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3 }
                    Text { text: imuData.transport || "";      font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3 }
                    Text { text: imuData.id || "";             font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3 }
                    Text { text: imuData.firmwareVersion || ""; font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3 }
                }
            },

            // Enable toggle
            Row {
                spacing: Theme.gap(6)
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text:           qsTr("Enable")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                    anchors.verticalCenter: parent.verticalCenter
                }

                PpSwitch {
                    checked: !imuRow.isExcluded
                    onToggled: (v) => {
                        var list = appSettings.imuExcluded
                        var idx  = list.indexOf(imuData.id)
                        if (!v && idx < 0)  list.push(imuData.id)
                        if ( v && idx >= 0) list.splice(idx, 1)
                        appSettings.imuExcluded = list
                        if (!v && imuRow.testOpen) root.openTestId = ""
                    }
                }
            }
        ]

        // ── Excluded note ────────────────────────────────────────────────────
        Item {
            id: excludedNote
            anchors.top:   imuRow.headerRow.bottom
            anchors.left:  parent.left
            anchors.right: parent.right
            height:  imuRow.isExcluded ? (excText.implicitHeight + Theme.sp(18)) : 0
            visible: imuRow.isExcluded
            clip:    true

            Text {
                id: excText
                anchors {
                    left:        parent.left
                    right:       parent.right
                    top:         parent.top
                    leftMargin:  Theme.gap(14)
                    rightMargin: Theme.gap(14)
                    topMargin:   Theme.gap(4)
                }
                text:        qsTr("Excluded — will not be connected at session start.")
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.italic: true
                color:       Theme.colorText3
                wrapMode:    Text.WordWrap
            }
        }

        // ── Body wrap — chips + capabilities strip ───────────────────────────
        Item {
            id: bodyWrap
            anchors.top:   imuRow.headerRow.bottom
            anchors.left:  parent.left
            anchors.right: parent.right
            height:  !imuRow.isExcluded ? (bodyCol.implicitHeight + Theme.sp(20)) : 0
            visible: !imuRow.isExcluded
            clip:    true

            ColumnLayout {
                id: bodyCol
                anchors.left:  parent.left
                anchors.right: parent.right
                anchors.top:   parent.top
                spacing: 0

                // Config chips row
                RowLayout {
                    Layout.fillWidth:    true
                    Layout.leftMargin:   Theme.gap(14)
                    Layout.rightMargin:  Theme.gap(14)
                    Layout.topMargin:    Theme.gap(4)
                    Layout.bottomMargin: Theme.gap(12)
                    spacing: Theme.gap(16)

                    // ── The mount: read-only (Mark, 26 Oct; trunk design §16.2 W2.2) ──
                    // Where the sensor is worn is set in ONE place, session setup's Sensors page
                    // (pages/ImusPage.qml), where the golfer confirms or changes it before each
                    // session. Here it is shown, never edited.
                    ColumnLayout {
                        spacing: Theme.gap(4)
                        Layout.alignment: Qt.AlignTop

                        PpMicro {
                            kind: "caps"
                            text: qsTr("Mount")
                        }

                        Text {
                            objectName: "imuMountLine"
                            text:       imuRow.mountLine
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color:          imuRow.mountLabel !== "" ? Theme.colorText2 : Theme.colorText3
                            wrapMode:       Text.WordWrap
                            Layout.maximumWidth: Theme.sp(260)
                        }
                    }

                    // ── Output rate chips ────────────────────────────────────
                    // Witmotion only: the wG3's output rate is adaptive, not a
                    // host-settable register (hm_instance.h) — offering a chip
                    // row that silently does nothing would be worse than
                    // offering none, so a HackMotion shows the MEASURED rate
                    // instead (the readout that already exists at :744-745,
                    // driven by inst.dataRateHz).
                    ColumnLayout {
                        visible: !imuRow.isHackMotion
                        spacing: Theme.gap(4)
                        Layout.alignment: Qt.AlignTop

                        PpMicro {
                            kind: "caps"
                            text: qsTr("Output rate")
                        }

                        Row {
                            id: rateChipRow
                            spacing: Theme.gap(4)

                            readonly property var rateOptions: {
                                var rates = imuData.supportedRatesHz
                                return (rates && rates.length > 0) ? rates : [50, 100, 200, 500]
                            }

                            readonly property int selectedRate: {
                                var v = appSettings.imuOutputRateHz[imuData.id]
                                return (v !== undefined && v > 0) ? v : (imuData.defaultRateHz || 100)
                            }

                            Repeater {
                                model: rateChipRow.rateOptions

                                delegate: PpChoiceChip {
                                    id: rateChip
                                    required property var modelData
                                    style:    "mono"; moves: false
                                    label:    modelData + qsTr(" Hz")
                                    selected: rateChipRow.selectedRate === modelData
                                    onClicked: {
                                        var map = appSettings.imuOutputRateHz
                                        map[imuData.id] = modelData
                                        appSettings.imuOutputRateHz = map
                                        if (imuRow.isConnected && imuRow.inst)
                                            imuRow.inst.setOutputRateHz(modelData)
                                    }
                                }
                            }
                        }
                    }

                    // ── Measured rate (HackMotion only) ──────────────────────
                    // No chips: there is nothing here to SET, only to read. The
                    // wG3 adapts its own rate up to ~799.2 Hz internally
                    // (hm_instance.h §6.6); this is the same measured dataRateHz
                    // the live-test readout shows once the test panel is open.
                    ColumnLayout {
                        visible: imuRow.isHackMotion
                        spacing: Theme.gap(4)
                        Layout.alignment: Qt.AlignTop

                        PpMicro {
                            kind: "caps"
                            text: qsTr("Output rate")
                        }

                        Text {
                            text: (imuRow.isConnected && imuRow.inst && imuRow.inst.dataRateHz > 0)
                                      ? (imuRow.inst.dataRateHz.toFixed(1) + qsTr(" Hz (adaptive)"))
                                      : qsTr("Adaptive — not set")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorText2
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // ── Test button ──────────────────────────────────────────
                    PpButton {
                        id: testBtn
                        style: "wash"; size: "compact"
                        implicitHeight: Theme.sp(26)
                        horizontalPadding: Theme.sp(24)
                        active: imuRow.testOpen
                        Layout.alignment: Qt.AlignVCenter
                        label: qsTr("Test")

                        onClicked: {
                            if (imuRow.testOpen) {
                                root.openTestId = ""
                            } else {
                                root.openTestId = imuData.id
                                if (!imuRow.isConnected)
                                    imuManager.setSelected(imuData.index, true)
                            }
                        }
                    }
                }

                // ── Capabilities strip ───────────────────────────────────────
                PpFactStrip {
                    Layout.fillWidth: true
                    cells:       imuRow.capsCells
                    animateTint: true
                }
            }
        }

        // ── Test panel ───────────────────────────────────────────────────────
        Item {
            id: testPanel
            anchors.top:   bodyWrap.bottom
            anchors.left:  parent.left
            anchors.right: parent.right
            height: imuRow.testOpen && !imuRow.isExcluded ? testPanelContent.implicitHeight : 0
            clip:   true

            Behavior on height { NumberAnimation { duration: Theme.durationFast } }

            ColumnLayout {
                id: testPanelContent
                anchors.left:  parent.left
                anchors.right: parent.right
                spacing: 0

                // Top separator
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color:  Theme.colorBorderMid
                    opacity: Theme.borderOpacityNormal
                }

                // ── Panel header ──────────────────────────────────────────────
                RowLayout {
                    Layout.fillWidth:    true
                    Layout.leftMargin:   Theme.gap(14)
                    Layout.rightMargin:  Theme.gap(14)
                    Layout.topMargin:    Theme.gap(12)
                    Layout.bottomMargin: Theme.gap(12)
                    spacing: Theme.gap(8)

                    PpMicro {
                        kind: "caps"
                        // The mount by name (imuRow.mountLabel — a wG3's pair reads
                        // "Lead forearm + hand"), then the device; just the device
                        // while it has no mount.
                        text: qsTr("Live test — ") + (imuRow.mountLabel !== ""
                                                          ? imuRow.mountLabel + " — " + imuData.description
                                                          : imuData.description)
                        Layout.fillWidth: true
                    }

                    // Connect / Disconnect button — fixed width sized for the longer label.
                    PpButton {
                        id: connBtn
                        readonly property bool connected: imuRow.isConnected
                        style: "wash"; size: "compact"
                        implicitHeight: Theme.sp(26)
                        horizontalPadding: Theme.sp(24)
                        tone: connected ? "warn" : "accent"
                        // Always measures the longer string so the button never shrinks when switching
                        // between "Connect" and "Disconnect".
                        sizeLabel: qsTr("Disconnect")
                        label: connBtn.connected ? qsTr("Disconnect") : qsTr("Connect")

                        onClicked: {
                            if (imuRow.isConnected)
                                imuManager.setSelected(imuData.index, false)
                            else
                                imuManager.setSelected(imuData.index, true)
                        }

                        // Traveling-light frame while this device's BLE connect
                        // is in flight (busy spans the retry/backoff/watchdog).
                        PpConnectingFrame {
                            anchors.fill: parent
                            radius:  parent.radius
                            running: imuRow.inst !== null && imuRow.inst.busy && !connBtn.connected
                        }
                    }

                    // Done button — closes the test panel; does not disconnect
                    PpButton {
                        id: testDoneBtn
                        style: "tonal"; tone: "accent"; size: "compact"
                        implicitHeight: Theme.sp(26)
                        horizontalPadding: Theme.sp(24)
                        label: qsTr("Done")

                        onClicked:    root.openTestId = ""
                    }
                }

                // ── Body — viz left, live data right ─────────────────────────
                RowLayout {
                    Layout.fillWidth:    true
                    Layout.leftMargin:   Theme.gap(14)
                    Layout.rightMargin:  Theme.gap(14)
                    Layout.bottomMargin: Theme.gap(24)
                    spacing: Theme.gap(16)

                    // ImuVizView — loaded only when test panel is open to avoid
                    // creating GPU contexts for every row at startup.
                    //
                    // ImuVizView is duck-typed: it documents itself as taking
                    // "any QtObject with quat*/accel* properties" and resolves
                    // them by NAME through the metaobject, so it needs no change
                    // to drive either an ImuInstance or an HmUnit.
                    Item {
                        visible: !imuRow.isHackMotion
                        width:  Theme.sp(220)
                        height: Theme.sp(220)
                        Layout.alignment: Qt.AlignTop

                        Loader {
                            anchors.fill: parent
                            // ⚠ GATED ON active, NOT JUST THE PARENT'S visible.
                            // A hidden item still instantiates its children and
                            // still evaluates their bindings, so leaving this
                            // active for a HackMotion bound `controller` to an
                            // HmInstance — which has no quat*/accel* properties
                            // of its own (they live per HmUnit) — and every
                            // frame produced "Unable to assign [undefined] to
                            // double" from inside ImuVizView.
                            active:  imuRow.testOpen && imuRow.inst !== null
                                     && !imuRow.isHackMotion
                            visible: imuRow.isConnected
                            sourceComponent: Component {
                                ImuVizView {
                                    anchors.fill: parent
                                    controller:   imuRow.inst
                                }
                            }
                        }

                        // Placeholder when not connected
                        Rectangle {
                            anchors.fill:  parent
                            color:         Theme.colorBg
                            border.width:  1
                            border.color:  Theme.colorBorderMid
                            radius:        Theme.radius
                            visible:       !imuRow.isConnected

                            Text {
                                anchors.centerIn: parent
                                text:           qsTr("Not connected")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                            }
                        }
                    }

                    // ── Two orientation cubes (HackMotion only) ───────────────
                    // One wG3 peripheral, two sensor units on a fixed cable
                    // order (hm_instance.h) — a single viz would have to pick
                    // one and hide the other, so this drives ImuVizView twice,
                    // once per HmUnit, side by side.
                    Row {
                        visible: imuRow.isHackMotion
                        spacing: Theme.gap(10)
                        Layout.alignment: Qt.AlignTop

                        Repeater {
                            model: imuRow.isHackMotion && imuRow.inst
                                       ? [ { obj: imuRow.inst.unitLowerArm, label: qsTr("Lower arm") },
                                           { obj: imuRow.inst.unitPalm,     label: qsTr("Palm") } ]
                                       : []

                            delegate: ColumnLayout {
                                required property var modelData
                                spacing: Theme.gap(4)

                                PpMicro {
                                    kind: "caps"
                                    text: modelData.label
                                }

                                Item {
                                    width:  Theme.sp(105)
                                    height: Theme.sp(105)

                                    Loader {
                                        anchors.fill: parent
                                        active:  imuRow.testOpen && modelData.obj !== null
                                        visible: imuRow.isConnected
                                        sourceComponent: Component {
                                            ImuVizView {
                                                anchors.fill: parent
                                                controller:   modelData.obj
                                            }
                                        }
                                    }

                                    Rectangle {
                                        anchors.fill:  parent
                                        color:         Theme.colorBg
                                        border.width:  1
                                        border.color:  Theme.colorBorderMid
                                        radius:        Theme.radius
                                        visible:       !imuRow.isConnected

                                        Text {
                                            anchors.centerIn: parent
                                            text:           qsTr("Not connected")
                                            font.family:    Theme.fontData
                                            font.pixelSize: Theme.fontSzMicro
                                            color:          Theme.colorText3
                                            wrapMode:       Text.WordWrap
                                            horizontalAlignment: Text.AlignHCenter
                                            width: parent.width - Theme.sp(12)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Live data ─────────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.gap(12)

                        // Connection status + battery
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap(8)

                            // Status dot
                            PpBadge {
                                kind:     "dot"
                                plain:    true
                                size:     Theme.sp(6)
                                tone: imuRow.isConnected                     ? Theme.colorGood
                                    : (imuRow.inst && imuRow.inst.busy)      ? Theme.colorAccent
                                    :                                           Theme.colorText3
                                animated: true
                            }

                            // State label + data rate
                            Text {
                                text: {
                                    var s = imuRow.inst ? imuRow.inst.stateLabel : qsTr("Disconnected")
                                    if (imuRow.isConnected && imuRow.inst && imuRow.inst.dataRateHz > 0)
                                        return s + qsTr(" · ") + imuRow.inst.dataRateHz.toFixed(1) + qsTr(" Hz")
                                    return s
                                }
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzBody2
                                color: imuRow.isConnected                     ? Theme.colorGood
                                     : (imuRow.inst && imuRow.inst.busy)      ? Theme.colorWarn
                                     :                                           Theme.colorText3
                                Layout.fillWidth: true
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            // Battery indicator
                            RowLayout {
                                visible: imuRow.isConnected
                                         && imuRow.inst !== null
                                         && imuRow.inst.batteryPercent >= 0
                                spacing: Theme.gap(4)

                                Text {
                                    text:           qsTr("BAT")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorText3
                                }

                                Rectangle {
                                    width:  Theme.sp(40)
                                    height: Theme.sp(8)
                                    radius: Theme.sp(2)
                                    color:  Theme.colorBg3
                                    border.width: 1
                                    border.color: Theme.colorBorderStrong

                                    Rectangle {
                                        property int pct: imuRow.inst ? imuRow.inst.batteryPercent : 0
                                        width:  parent.width * Math.max(0, Math.min(1, pct / 100.0))
                                        height: parent.height
                                        radius: parent.radius
                                        color:  pct > 60 ? Theme.colorGood
                                              : pct > 20 ? Theme.colorWarn
                                              :             Theme.colorError
                                        Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                    }
                                }

                                Text {
                                    property int pct: imuRow.inst ? imuRow.inst.batteryPercent : 0
                                    text:  pct + qsTr("%")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: pct > 60 ? Theme.colorGood
                                         : pct > 20 ? Theme.colorWarn
                                         :             Theme.colorError
                                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                }
                            }
                        }

                        // ── Gimbal drop counter ───────────────────────────────
                        RowLayout {
                            Layout.fillWidth: true
                            // ⚠ The isHackMotion term is load-bearing, not
                            // belt-and-braces: gimbalDropCount is a Witmotion
                            // Euler-gimbal artefact and does not exist on an
                            // HmInstance, so without it the child Text below
                            // still binds to undefined even while this row is
                            // hidden — a hidden item evaluates its bindings.
                            visible: imuRow.isConnected && imuRow.inst !== null
                                     && !imuRow.isHackMotion
                                     && imuRow.inst.gimbalDropCount > 0
                            spacing: Theme.gap(6)

                            Text {
                                text:           Theme.caps(qsTr("Gimbal drops"))
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                            }
                            Text {
                                text:           (imuRow.inst && !imuRow.isHackMotion)
                                                    ? imuRow.inst.gimbalDropCount : 0
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorWarn
                            }
                            Text {
                                text:           qsTr("packets dropped this session")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                Layout.fillWidth: true
                            }
                        }

                        // ── Euler angles ──────────────────────────────────────
                        // Witmotion: quatW..Z live directly on inst. A
                        // HackMotion has no such properties on the instance
                        // itself — they live per unit (HmUnit) — so binding
                        // this block to inst.quatW would resolve to undefined
                        // rather than a real quantity; show one tile row PER
                        // UNIT instead of leaving it bound to nothing.
                        ColumnLayout {
                            visible: !imuRow.isHackMotion
                            Layout.fillWidth:    true
                            Layout.bottomMargin: Theme.gap(8)
                            spacing: Theme.gap(4)

                            PpMicro {
                                kind: "caps"
                                text: qsTr("Quaternion")
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.gap(6)

                                Repeater {
                                    // ⚠ 0 rather than 4 for a HackMotion. The
                                    // enclosing ColumnLayout is already hidden
                                    // for it, but hiding does not stop a
                                    // delegate instantiating or its bindings
                                    // running — and these read inst.quatW..Z,
                                    // which an HmInstance does not have (they
                                    // live per HmUnit), so .toFixed() was being
                                    // called on undefined four times a row.
                                    model: imuRow.isHackMotion ? 0 : 4

                                    delegate: Rectangle {
                                        required property int index

                                        Layout.fillWidth: true
                                        height: Theme.sp(60)
                                        color:  Theme.colorBg2
                                        radius: Theme.radius
                                        border.width: 1
                                        border.color: Theme.colorBorderMid

                                        ColumnLayout {
                                            anchors.fill:          parent
                                            anchors.topMargin:     Theme.gap(8)
                                            anchors.bottomMargin:  Theme.gap(12)
                                            anchors.leftMargin:    Theme.gap(8)
                                            anchors.rightMargin:   Theme.gap(8)
                                            spacing: Theme.gap(2)

                                            PpMicro {
                                                kind: "caps"
                                                text: ["W", "X", "Y", "Z"][index]
                                            }
                                            Text {
                                                text: [
                                                    (imuRow.inst ? imuRow.inst.quatW.toFixed(4) : "1.0000"),
                                                    (imuRow.inst ? imuRow.inst.quatX.toFixed(4) : "0.0000"),
                                                    (imuRow.inst ? imuRow.inst.quatY.toFixed(4) : "0.0000"),
                                                    (imuRow.inst ? imuRow.inst.quatZ.toFixed(4) : "0.0000")
                                                ][index]
                                                font.family:    Theme.fontData
                                                font.pixelSize: Theme.fontSzHeading
                                                color:          Theme.colorText
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // HackMotion: one QUATERNION row per unit, reading the
                        // HmUnit directly rather than a non-existent inst.quat*.
                        ColumnLayout {
                            visible: imuRow.isHackMotion
                            Layout.fillWidth:    true
                            Layout.bottomMargin: Theme.gap(8)
                            spacing: Theme.gap(8)

                            Repeater {
                                model: imuRow.isHackMotion && imuRow.inst
                                           ? [ { obj: imuRow.inst.unitLowerArm, label: qsTr("Lower arm") },
                                               { obj: imuRow.inst.unitPalm,     label: qsTr("Palm") } ]
                                           : []

                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: Theme.gap(4)

                                    PpMicro {
                                        kind: "caps"
                                        text: qsTr("Quaternion — ") + modelData.label
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.gap(6)

                                        Repeater {
                                            model: 4

                                            delegate: Rectangle {
                                                required property int index

                                                Layout.fillWidth: true
                                                height: Theme.sp(60)
                                                color:  Theme.colorBg2
                                                radius: Theme.radius
                                                border.width: 1
                                                border.color: Theme.colorBorderMid

                                                ColumnLayout {
                                                    anchors.fill:          parent
                                                    anchors.topMargin:     Theme.gap(8)
                                                    anchors.bottomMargin:  Theme.gap(12)
                                                    anchors.leftMargin:    Theme.gap(8)
                                                    anchors.rightMargin:   Theme.gap(8)
                                                    spacing: Theme.gap(2)

                                                    PpMicro {
                                                        kind: "caps"
                                                        text: ["W", "X", "Y", "Z"][index]
                                                    }
                                                    Text {
                                                        text: [
                                                            (modelData.obj ? modelData.obj.quatW.toFixed(4) : "1.0000"),
                                                            (modelData.obj ? modelData.obj.quatX.toFixed(4) : "0.0000"),
                                                            (modelData.obj ? modelData.obj.quatY.toFixed(4) : "0.0000"),
                                                            (modelData.obj ? modelData.obj.quatZ.toFixed(4) : "0.0000")
                                                        ][index]
                                                        font.family:    Theme.fontData
                                                        font.pixelSize: Theme.fontSzHeading
                                                        color:          Theme.colorText
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ── Calibration tools ─────────────────────────────────
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap(4)
                            enabled: imuRow.isConnected && imuRow.inst !== null

                            PpMicro {
                                kind: "caps"
                                text: qsTr("Calibration")
                            }

                            Row {
                                spacing: Theme.gap(6)

                                // Zero orientation — Witmotion only. There is no
                                // host-side zero on a HackMotion (imu_device.h);
                                // a control that silently does nothing is worse
                                // than no control.
                                PpButton {
                                    id: zeroBtn
                                    visible: !imuRow.isHackMotion
                                    style: "wash"; size: "compact"
                                    implicitHeight: Theme.sp(24)
                                    label: qsTr("Zero orientation")

                                    onClicked:    if (imuRow.inst) imuRow.inst.zeroOrientation()
                                }

                                // Calibrate magnetometer (placeholder — no API yet)
                                Rectangle {
                                    visible: imuData.supportsMagCalibration
                                    width:   magCalLabel.implicitWidth + Theme.sp(20)
                                    height:  Theme.sp(24)
                                    radius:  Theme.radius
                                    color:   "transparent"
                                    border.width: 1
                                    border.color: Theme.colorBorderMid
                                    opacity: 0.5

                                    Text {
                                        id: magCalLabel
                                        anchors.centerIn: parent
                                        text:           qsTr("Calibrate mag")
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Theme.fontBodyWeight
                                        color:          Theme.colorText3
                                    }
                                    // TODO: imuManager.calibrateMagnetometer() — not implemented yet
                                }

                                // Save to flash (placeholder — no API yet)
                                Rectangle {
                                    visible: imuData.supportsConfigPersistence
                                    width:   saveFlashLabel.implicitWidth + Theme.sp(20)
                                    height:  Theme.sp(24)
                                    radius:  Theme.radius
                                    color:   "transparent"
                                    border.width: 1
                                    border.color: Theme.colorBorderMid
                                    opacity: 0.5

                                    Text {
                                        id: saveFlashLabel
                                        anchors.centerIn: parent
                                        text:           qsTr("Save to flash")
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Theme.fontBodyWeight
                                        color:          Theme.colorText3
                                    }
                                    // TODO: imuManager.saveConfigToFlash() — not implemented yet
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Page header ────────────────────────────────────────────────
    PpPageHeader {
        Layout.fillWidth: true
        spacing:     root.contentSpacing
        eyebrow:     Theme.caps(qsTr("Hardware"))
        title:       qsTr("Motion Sensors")
        description: qsTr("All enumerated motion sensors are listed below. Each shows where it is worn — set in session setup, on the Sensors step. Configure the output rate here, and use the Test panel to verify orientation before a session.")
    }

    // ── Enumerated devices header ──────────────────────────────────
    RowLayout {
        Layout.fillWidth: true

        PpMicro {
            kind: "body"
            text:             Theme.caps(qsTr("Enumerated devices"))
            Layout.fillWidth: true
        }

        // Scan button — fixed width sized for the longer "Scanning…" label.
        PpButton {
            id: scanBtn
            // Bound to the manager's authoritative scan state — NOT a
            // local 30 s timer. The discovery window is 90 s with
            // HackMotion enabled, and a local clock that ran out early
            // re-offered a Scan tap that the enumerator's re-entry guard
            // silently swallowed for the rest of the real window.
            readonly property bool scanning: imuManager.imuScanActive

            style: "wash"; size: "compact"
            implicitHeight: Theme.sp(26)
            horizontalPadding: Theme.sp(24)
            active: scanning
            sizeLabel: qsTr("Scanning…")
            label: scanBtn.scanning ? qsTr("Scanning…") : qsTr("Scan")
            onClicked: imuManager.rescanImu()
        }
    }

    // ── Device rows ────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.gap(8)

        Repeater {
            model: imuManager.imuDeviceList

            delegate: ImuDeviceRow {
                required property var modelData
                imuData:          modelData
                Layout.fillWidth: true
                // Dim (but keep listed + interactive) when absent from the
                // latest scan, so a known device can still be renamed/forgotten.
                opacity: modelData.present ? 1.0 : 0.45
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            }
        }

        // Empty state — distinguishes "discovery couldn't run" (e.g.
        // Bluetooth off / no adapter, surfaced via imuScanError) from a
        // genuine "no devices in range".
        PpCardNote {
            kind:             "aside"
            visible:          imuManager.imuDeviceList.length === 0
            text:             imuManager.imuScanError !== ""
                                  ? qsTr("Bluetooth discovery error: %1").arg(imuManager.imuScanError)
                                  : qsTr("No motion sensors found. Click Scan to search for BLE devices.")
            color:            imuManager.imuScanError !== "" ? Theme.colorWarn : Theme.colorText3
            Layout.fillWidth: true
        }
    }

    // ── Status summary ─────────────────────────────────────────────
    Rectangle {
        id: summaryRect
        Layout.fillWidth: true
        // One line normally; taller only when a narrow window wraps a list.
        Layout.preferredHeight: Math.max(Theme.sp(40), summaryRow.implicitHeight + 2 * Theme.gap(12))
        color:   Theme.colorBg2
        radius:  Theme.radius
        border.width: 1
        border.color: Theme.colorBorderMid

        readonly property int enabledCount: {
            var list = imuManager.imuDeviceList
            var ex   = appSettings.imuExcluded
            return list.filter(function(d) { return ex.indexOf(d.id) < 0 }).length
        }
        readonly property int connectedCount: imuManager.imuCount

        // Mounts BY NAME, in picker order. Reads only the roles map's VALUES,
        // never its keys, so a wG3's two unit keys count as the two mounts they
        // fill. "Assigned" is every role claimed, offered or not; "Not worn" is the
        // OFFERED mounts nobody claims (ImuMounts.offeredRoles). Not a warning: no
        // mount is required (session_wizard_refactor_design.md §4.12), and since the
        // trunk mounts are offered (W2.2) a wrist-only setup leaves two of them unworn.
        readonly property var assignedRoles: {
            var held = Object.values(appSettings.imuRoles)
            return ImuMounts.allRoles.filter(function(r) { return held.indexOf(r) >= 0 })
        }
        // The SHORT names (ImuMounts.shortLabel): a one-line summary has no room for the
        // trunk mounts' placement hints, which the Sensors page shows where they are chosen.
        readonly property var assignedPlacements: assignedRoles.map(function(r) { return ImuMounts.shortLabel(r) })
        readonly property var missingPlacements: ImuMounts.offeredRoles
            .filter(function(r) { return summaryRect.assignedRoles.indexOf(r) < 0 })
            .map(function(r) { return ImuMounts.shortLabel(r) })

        RowLayout {
            id: summaryRow
            anchors.fill:    parent
            anchors.margins: Theme.gap(12)
            spacing:         Theme.gap(16)

            Text {
                text:  summaryRect.connectedCount + qsTr(" of ") + summaryRect.enabledCount + qsTr(" connected")
                color: (summaryRect.connectedCount === summaryRect.enabledCount && summaryRect.enabledCount > 0)
                            ? Theme.colorGood : Theme.colorWarn
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }

            Rectangle { width: 1; height: Theme.sp(14); color: Theme.colorBorderStrong; opacity: 0.4 }

            // The two lists SHARE the row, each in proportion to its length, and wrap
            // within their share rather than run into each other: both stay readable at
            // any window width (the box grows to the taller list).
            Text {
                text:  summaryRect.assignedPlacements.length > 0
                           ? qsTr("Assigned: ") + summaryRect.assignedPlacements.join(qsTr(" · "))
                           : qsTr("No placements assigned")
                color:          Theme.colorText2
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                wrapMode:       Text.WordWrap
                Layout.fillWidth:               true
                Layout.preferredWidth:          0
                Layout.minimumWidth:            0
                Layout.horizontalStretchFactor: Math.max(1, text.length)
            }

            Text {
                visible:        summaryRect.missingPlacements.length > 0
                text:           qsTr("Not worn: ") + summaryRect.missingPlacements.join(qsTr(" · "))
                color:          Theme.colorText3
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                wrapMode:       Text.WordWrap
                horizontalAlignment: Text.AlignRight
                Layout.fillWidth:               true
                Layout.preferredWidth:          0
                Layout.minimumWidth:            0
                Layout.horizontalStretchFactor: Math.max(1, text.length)
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Global IMU settings ────────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Global motion sensor settings")) }

    // Auto-connect on session start
    SettingsRow {
        objectName: "setting_imuAutoConnect"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Auto-connect on session start")
                subtitle: qsTr("Connects all enabled devices before recording begins")
            }
            PpSwitch {
                checked: appSettings.imuAutoConnect
                onToggled: (v) => appSettings.imuAutoConnect = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Auto-reconnect on signal loss
    SettingsRow {
        objectName: "setting_imuAutoReconnect"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Auto-reconnect on signal loss")
                subtitle: qsTr("Attempts reconnect if the BLE link drops during a session")
            }
            PpSwitch {
                checked: appSettings.imuAutoReconnect
                onToggled: (v) => appSettings.imuAutoReconnect = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Look for a HackMotion wrist sensor
    SettingsRow {
        objectName: "setting_hackmotionEnabled"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Look for a HackMotion wrist sensor")
                // The wG3 advertises only for a few seconds after a button
                // press, which is why discovery uses a 90 s scan window
                // instead of the usual 30 s — turning this off also drops
                // back to the shorter window, since that budget exists
                // only for the wG3's advertising burst.
                subtitle: qsTr("Off restores the shorter Bluetooth scan window used for other sensors")
            }
            PpSwitch {
                checked: appSettings.hackmotionEnabled
                onToggled: (v) => appSettings.hackmotionEnabled = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Save calibration to device flash
    SettingsRow {
        objectName: "setting_imuFlash"
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title:    qsTr("Save calibration to device flash")
                subtitle: qsTr("Persists zero-orientation and mag calibration across power cycles")
            }
            PpSwitch {
                checked: appSettings.imuSaveCalibrationToFlash
                onToggled: (v) => appSettings.imuSaveCalibrationToFlash = v
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Orientation fusion (global) ────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Orientation fusion")) }

    ColumnLayout {
        Layout.fillWidth:  true
        spacing:           Theme.gap(6)

        RowLayout {
            spacing: Theme.gap(8)

            Repeater {
                model: [ { key: "Madgwick", label: qsTr("Madgwick Filter") },
                         { key: "ESKF",     label: qsTr("Error State Kalman Filter (ESKF)") } ]

                delegate: PpChoiceChip {
                    id: fusionSeg
                    required property var modelData
                    style:     "body"; moves: false
                    label:     fusionSeg.modelData.label
                    selected:  appSettings.imuOrientationFilter === modelData.key
                    onClicked: imuManager.setOrientationFilter(fusionSeg.modelData.key)
                }
            }
        }

        PpCardNote {
            kind: "plain"
            text: qsTr("Software sensor-fusion algorithm used to derive orientation from the raw gyroscope + accelerometer. Applies to all motion sensors and takes effect immediately.")
            Layout.fillWidth: true
        }
    }
}
