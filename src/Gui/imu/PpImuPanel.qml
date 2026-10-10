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

// IMU device panel for the session toolbar. Two modes:
//   "list"      — scoped action row (Scan / Connect / Calibrate) + device rows.
//                 Each row leads with the device vocabulary's badge (a check when
//                 connected and calibrated, a colorAttention target when it needs
//                 calibration, a colorError target when it failed, the dashed ring
//                 when not connected) and says the same in words. The Calibrate
//                 action is drawn in colorAttention while there is work to do.
//   "calibrate" — hosts ImuCalibrationFlow compactly IN-PANEL; the panel grows to
//                 fit it. Calibration NEVER leaves this panel / the Wrist screen.
//                 The heading turns colorAttention, and the toolbar turns the
//                 popover card's rule the same tone.
// The attention marks live ONLY around the flow — never inside it.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property string mode: "list"          // "list" | "calibrate"

    implicitWidth:  Theme.sp(380)
    implicitHeight: mode === "calibrate"
        ? Theme.sp(46) + 1 + Theme.sp(560)        // header + hairline + compact flow (room for fail messages)
        : Theme.sp(46) + 1 + listCol.implicitHeight

    // True when at least one connected IMU is not yet *successfully* calibrated.
    // A sensor counts as calibrated only when anatCalibrated AND the mount check
    // passes (same thresholds the flow uses: deviation ≤ 15°, gravity ≤ 25°).
    // anatCalibrated alone flips true at the phase-1 arm-down capture, before
    // phase-2 mount validation — so a failed calibration keeps Calibrate lit.
    function _imuCalibratedOk(inst) {
        return !!(inst && inst.anatCalibrated
                  && inst.mountDeviationDeg    <= 15.0
                  && inst.mountGravityErrorDeg <= 25.0)
    }
    readonly property bool needsCalibration: {
        var _dep = imuManager.instances
        var list = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i) {
            var inst = imuManager.instanceFor(list[i].id)
            if (inst && inst.imuConnected && !_imuCalibratedOk(inst)) return true
        }
        return false
    }

    // ── Per-session IMU enablement ─────────────────────────────────────────────
    // Manager-owned (imuManager.sessionImuExcluded / setSessionImuEnabled) so the
    // start wizard and every toolbar panel instance share ONE list — same pattern
    // as cameras. Seeded from appSettings.imuExcluded at startup and re-seeded by
    // the wizard on open; never written back to settings (global enablement is
    // owned by the Settings screen). The per-device state is read from the
    // sessionEnabled field on imuDeviceList entries.

    // True when at least one IMU is session-enabled and every enabled one is
    // *actually* connected (live instance with imuConnected — not merely
    // selected/pending) — drives the Connect ⇄ Disconnect action toggle.
    readonly property bool allConnected: {
        var _dep = imuManager.instances
        var list = imuManager.imuDeviceList
        var enabled = 0
        for (var i = 0; i < list.length; ++i) {
            if (!list[i].sessionEnabled) continue
            ++enabled
            var inst = imuManager.instanceFor(list[i].id)
            if (!inst || !inst.imuConnected) return false
        }
        return enabled > 0
    }

    // Disconnect every connected device (enabled or not), cancelling any
    // in-flight paced-connect queue first so it can't reconnect afterwards.
    // Disconnects are immediate — the 2 s BlueZ gap only matters between
    // *connection* attempts.
    function disconnectAll() {
        imuManager.cancelPacedConnect()
        var list = imuManager.imuDeviceList
        for (var i = 0; i < list.length; ++i)
            if (imuManager.instanceFor(list[i].id) !== null)
                imuManager.setSelected(list[i].index, false)
    }

    // Paced connect of every enabled, not-yet-connected device. Sequential with a
    // 2 s gap so BlueZ can reset its GATT state between connections. The queue is the
    // manager's (imuManager.connectPaced), so closing this panel cannot strand a
    // half-run queue.
    function startConnect() {
        var ids = []
        var list = imuManager.imuDeviceList
        for (var j = 0; j < list.length; ++j) {
            if (list[j].sessionEnabled) {
                var inst = imuManager.instanceFor(list[j].id)
                if (!inst || !inst.imuConnected) ids.push(list[j].id)
            }
        }
        if (ids.length === 0) return
        imuManager.connectPaced(ids)
    }

    // ── Header — the card's Micro title, and the count as an aside ─────────────
    // The same heading as the camera panel. Calibrate mode keeps the row with no
    // top navigation (the flow's Cancel returns to the list), titled in the tone.
    Item {
        id: hdr
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.sp(46)

        PpMicro {
            anchors { left: parent.left; leftMargin: Theme.gap(15); verticalCenter: parent.verticalCenter }
            text: root.mode === "calibrate" ? Theme.caps(qsTr("Calibrate sensors")) : Theme.caps(qsTr("IMUs"))
            color: root.mode === "calibrate" ? Theme.colorAttention : Theme.colorText3
        }
        PpMicro {
            anchors { right: parent.right; rightMargin: Theme.gap(15); verticalCenter: parent.verticalCenter }
            visible: root.mode === "list"
            font.letterSpacing: Theme.trackingData
            text: qsTr("%1 of %2 connected").arg(imuManager.imuCount).arg(imuManager.imuDeviceList.length)
        }
    }
    Rectangle {
        id: hairline
        anchors { left: parent.left; right: parent.right; top: hdr.bottom }
        height: 1; color: Theme.colorBorder
    }

    // ── LIST view ──────────────────────────────────────────────────────────────
    Column {
        id: listCol
        visible: root.mode === "list"
        anchors { left: parent.left; right: parent.right; top: hairline.bottom }

        // Scoped actions: Scan / Connect / Calibrate
        Row {
            width: parent.width
            padding: Theme.gap(12); spacing: Theme.gap(8)
            ScopedAction {
                glyph: "⟳"; label: qsTr("Scan")
                onTriggered: imuManager.rescanImu()
            }
            ScopedAction {
                glyph: "⇄"
                label: root.allConnected ? qsTr("Disconnect") : qsTr("Connect")
                connecting: !root.allConnected && (imuManager.pacedConnectActive || imuManager.anyConnecting)
                onTriggered: {
                    if (root.allConnected)          root.disconnectAll()
                    else if (!imuManager.pacedConnectActive) root.startConnect()
                }
            }
            ScopedAction {
                glyph: "◳"; label: qsTr("Calibrate")
                primary: root.needsCalibration                // attention-framed when work to do
                onTriggered: root.mode = "calibrate"
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

        // Device rows
        Repeater {
            model: imuManager.imuDeviceList
            delegate: ImuRow {
                id: imuRow
                required property var modelData
                width: listCol.width
                calibratedOk: root._imuCalibratedOk(imuRow.inst)
                devId:    modelData.id
                devIndex: modelData.index
                devName: modelData.alias && modelData.alias !== "" ? modelData.alias
                                                                   : modelData.description
                deviceEnabled: modelData.sessionEnabled
                // Dim when absent from the latest scan (kept listed so a known
                // device stays manageable); a selected device stays present.
                opacity: modelData.present ? 1.0 : 0.45
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                placement: {
                    // The device's mount BY NAME (ImuMounts), from the role it was
                    // assigned — a wG3 answers "leadForearm" for its pair and reads
                    // "Lead forearm + hand". roleForDevice() is a Q_INVOKABLE and not
                    // reactive on its own, so the roles map is READ here, in the
                    // expression (an unused read is dropped by the compiler).
                    var roles = appSettings.imuRoles
                    return roles ? ImuMounts.deviceMountLabel(modelData.vendor === "hackmotion",
                                                              imuManager.roleForDevice(modelData.id))
                                 : ""
                }
            }
        }

        // Nothing found: one quiet line, so the card keeps its shape.
        Item {
            visible: imuManager.imuDeviceList.length === 0
            width: parent.width
            height: noImus.implicitHeight + Theme.sp(28)
            PpCardNote {
                id: noImus
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: Theme.gap(15); rightMargin: Theme.gap(15) }
                text: qsTr("No IMUs found — Scan to look again.")
            }
        }
    }

    // ── CALIBRATE view — the SAME component, compact, hosted in-panel ───────────
    ImuCalibrationFlow {
        id: calibFlow
        visible: root.mode === "calibrate"
        anchors { left: parent.left; right: parent.right; top: hairline.bottom; bottom: parent.bottom }
        layoutMode: "compact"
        showHeader: false
        // R3: the flow (and its routine) is live only in calibrate mode AND while the panel
        // is shown. Leaving calibrate mode, or closing the toolbar popup (which hides the
        // panel — its contentItem), stops the routine (a HackMotion's device routine is
        // aborted once); reopening in calibrate mode starts a fresh run.
        active: root.mode === "calibrate" && root.visible
        onCompleted: root.mode = "list"
        onCancelled: root.mode = "list"
        onActiveChanged: if (active) calibFlow.begin()   // auto-start on entering calibrate mode
    }

    // ── Scoped action button ────────────────────────────────────────────────
    component ScopedAction: Rectangle {
        property string glyph:   ""
        property string label:   ""
        property bool   primary: false
        // Drives the traveling-light frame while a connect attempt is in flight.
        property bool   connecting: false
        signal triggered()
        width: (root.width - Theme.sp(24) - Theme.sp(16)) / 3
        height: Theme.sp(50); radius: Theme.radius
        // Hover brighten + scale/dip — the PpButton language. Primary (amber)
        // lightens; the outline variant fades a faint fill in (alpha-ramped rest
        // so no colour flash).
        readonly property color _rest: primary ? Theme.colorAttentionLight
                                               : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
        color:        saMa.containsMouse ? (primary ? Qt.lighter(_rest, 1.08) : Theme.colorBg2)
                                         : _rest
        border.width: 1
        border.color: primary ? Theme.colorAttention : Theme.colorBorderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Column {
            anchors.centerIn: parent; spacing: Theme.gap(4)
            Text {
                anchors.horizontalCenter: parent.horizontalCenter; text: glyph
                font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(16)
                color: primary ? Theme.colorAttention : Theme.colorText2
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter; text: label
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: primary ? Theme.colorAttention : Theme.colorText
            }
        }
        PpPressable { id: saMa; onClicked: parent.triggered() }
        PpConnectingFrame { anchors.fill: parent; radius: parent.radius; running: parent.connecting }
    }

    // ── Per-IMU row ─────────────────────────────────────────────────────────
    component ImuRow: Item {
        id: imuRowItem
        property string devId:     ""
        property int    devIndex:  -1
        property string devName:   ""
        property string placement: ""   // the mount by name; "" when unassigned
        // Calibrated AND the mount check passed (root._imuCalibratedOk), set by
        // the list so the row's state follows the same thresholds as Calibrate.
        property bool   calibratedOk: false

        // Live instance (reactive on imuManager.instances).
        property QtObject inst: {
            var _dep = imuManager.instances
            return imuManager.instanceFor(devId)
        }
        readonly property bool connected:     inst ? inst.imuConnected  : false
        // Live connection state (driver labels: Scanning… / Connecting… /
        // Discovering services… / Retrying… / Connected / Error / Not found).
        readonly property string stateLabel: inst ? inst.stateLabel : ""
        readonly property bool failed:  inst !== null && !connected
                                        && (stateLabel === "Error" || stateLabel === "Not found")
        readonly property bool pending: inst !== null && !connected && !failed
        readonly property bool needsCal: connected && !calibratedOk
        // Only once a connected device reports a level.
        readonly property bool hasBattery: connected && !!inst && inst.batteryPercent >= 0
        // Session enablement — set from the imuDeviceList entry (manager-owned;
        // the Repeater model rebinds on imuDeviceListChanged).
        property bool deviceEnabled: true

        height: Theme.sp(56) + (hasBattery || placement !== "" ? Theme.sp(22) : 0)

        Rectangle {  // row hairline
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1; color: Theme.colorBorder
        }

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.gap(15); rightMargin: Theme.gap(15) }
            spacing: Theme.gap(11)

            // State badge: a check (connected, calibrated), a colorAttention
            // target (connected, needs calibration), a colorError target
            // (failed), or the dashed ring (not connected, or disabled and
            // dimmed). The ring pulses grey↔green while a connection is pending:
            // the value source drives the tone only while running; otherwise the
            // binding applies.
            PpBadge {
                id: statusBadge
                Layout.alignment: Qt.AlignVCenter
                opacity: imuRowItem.deviceEnabled ? 1.0 : 0.45
                kind: !imuRowItem.deviceEnabled ? "unconfirmed"
                    : imuRowItem.failed || imuRowItem.needsCal ? "target"
                    : imuRowItem.connected ? "check"
                    :                        "unconfirmed"
                tone: !imuRowItem.deviceEnabled ? Theme.colorText3
                    : imuRowItem.failed    ? Theme.colorError
                    : imuRowItem.needsCal  ? Theme.colorAttention
                    : imuRowItem.connected ? Theme.colorGood
                    :                        Theme.colorText3
                SequentialAnimation on tone {
                    running: imuRowItem.pending && imuRowItem.deviceEnabled && !Theme.reduceMotion
                    loops:   Animation.Infinite
                    ColorAnimation { from: Theme.colorText3; to: Theme.colorGood;  duration: Theme.durationSlow }
                    ColorAnimation { from: Theme.colorGood;  to: Theme.colorText3; duration: Theme.durationSlow }
                }
            }
            // ⚠ THE THREE WIDTH LINES BELOW ARE WHAT MAKES elide WORK. A Text with
            // no width uses its implicitWidth, and elide is inert against an
            // unconstrained width — so `elide: Text.ElideRight` alone did nothing.
            // Worse, a plain Column reports the widest child as its own
            // implicitWidth, and a RowLayout will not shrink a fillWidth item below
            // that, so one long name pushed the chips and the toggle past the right
            // edge and drew them over the text. The default IMU alias is
            // "<description> <device id>", and on macOS that id is a 36-character
            // per-host UUID (CoreBluetooth never exposes a MAC), so a HackMotion row
            // hit it every time while a short-named sensor never did.
            // preferredWidth 0 + minimumWidth 0: take the space that is left after
            // the fixed-size siblings, never demand more.
            Column {
                Layout.fillWidth: true; spacing: Theme.gap(2)
                Layout.preferredWidth: 0
                Layout.minimumWidth: 0
                opacity: deviceEnabled ? 1.0 : 0.45
                Text {
                    width: parent.width
                    text: devName
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText; elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    // Battery has its own chip (below); the subtitle carries the
                    // state in words (what the badge shows) + data rate.
                    text: {
                        if (!deviceEnabled) return qsTr("disabled — won't connect")
                        if (!inst)          return qsTr("not connected")
                        if (failed)         return qsTr("connection failed")
                        if (!connected)     return inst.stateLabel   // Scanning… / Connecting… / Retrying…
                        var state = imuRowItem.needsCal ? qsTr("needs calibration") : qsTr("connected")
                        return inst.dataRateHz > 0 ? state + " · " + Math.round(inst.dataRateHz) + " Hz"
                                                   : state
                    }
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color: failed               ? Theme.colorError
                         : imuRowItem.needsCal ? Theme.colorAttention
                         : pending              ? Theme.colorText2
                         :                        Theme.colorText3
                }
                // The chips, on their own line so the name keeps the row's width.
                Row {
                    id: chipRow
                    visible: imuRowItem.hasBattery || imuRowItem.placement !== ""
                    topPadding: Theme.gap(4)
                    spacing: Theme.gap(6)
                    // Battery, toned by charge (good >60%, attention >20%, error
                    // ≤20%), with the level in words.
                    PpChip {
                        id: batChip
                        visible: imuRowItem.hasBattery
                        readonly property int pct: inst ? inst.batteryPercent : 0
                        text: Theme.caps(qsTr("Bat %1%")).arg(batChip.pct)
                        tone: pct > 60 ? Theme.colorGood
                            : pct > 20 ? Theme.colorAttention
                            :            Theme.colorError
                    }
                    // The mount by name (`placement`, from the roles map), an
                    // untinted chip with no handler: stated, not judged.
                    // TODO: per-session IMU location override (defaults to appSettings.imuRoles)
                    PpChip {
                        id: placementChip
                        text: placement
                        tone: Theme.colorText2
                        tinted: false
                    }
                }
            }

            // Enable toggle — session-local. IMUs are calibrated collectively (the
            // T-pose flow calibrates every connected sensor at once), so there is no
            // per-row calibrate affordance. Toggling on marks the sensor for Connect;
            // toggling off disconnects it if connected. Mirrors the wizard. Styled to
            // match the standard app toggle (GeneralPanel): accent track, white knob.
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
                    color:  Theme.dark ? Theme.colorText : Theme.colorSurface
                    anchors.verticalCenter: parent.verticalCenter
                    x: deviceEnabled ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                    Behavior on x { NumberAnimation { duration: 120 } }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // Manager-owned; disabling a connected device also disconnects it.
                    onClicked: imuManager.setSessionImuEnabled(devId, !deviceEnabled)
                }
            }
        }
    }
}
