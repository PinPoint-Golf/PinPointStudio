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

// WHAT THIS RIG CAN MEASURE — the right pane of Home's Start card (ux_refactor_away_from_4_modes
// §2.3, A.1). One row per capability family, from the kit connected NOW (D10: cameras enabled and
// connected, sensor roles assigned, enabled and connected, the HackMotion connected, the launch
// monitor configured; no calibration gate). The rule is capability_facts.js in "home" mode — the
// wizard's closing page uses the same helper in "wizard" mode — and the rows and their words are
// setup_capability_rows.js, the ONE place that wording lives: describe, never instruct.
//
// Below a hairline, what today's DEVICES section carried that still belongs on Home: the PPCP pair
// entry (pairPhoneButton), the sessions a connected phone is offering (PpcpOfferList) and the
// first resource warning (RmWarningNotice).
//
// The device readings come through a SetupContext with no draft: it is the one object that reads
// cameraManager / imuManager / appSettings / launchMonitor with their reactive dependencies
// written once, so Home does not re-derive the role ladder.

import QtQuick
import PinPointStudio
import "../setup/capability_facts.js" as CapFacts
import "../setup/setup_capability_rows.js" as CapRows

Column {
    id: root
    objectName: "rigCapability"

    // "Rig ▸": Settings → Studio.
    signal studioSettingsRequested()

    spacing: 0

    // ── The facts ────────────────────────────────────────────────────────────
    SetupContext { id: kit }
    property MetricCatalog _catalog: MetricCatalog {}

    readonly property var _input: ({
        faceOn: kit.faceOn, dtl: kit.dtl, roles: kit.roles, groups: kit.groups,
        groupDefs: kit.groupDefs, launchMonitor: kit.launchMonitorConfigured,
        lmName: kit.launchMonitorName, draft: null,
        cameraExcluded: appSettings.cameraExcluded, imuExcluded: appSettings.imuExcluded
    })
    readonly property var facts: CapFacts.facts(root._input, "home")
    readonly property var rows: {
        const f = root.facts
        const noLm = CapFacts.factsWithoutLm(f)
        return CapRows.rows(root._catalog.setupSummary(f),
                            CapFacts.rowContext(root._input, f, "home"),
                            noLm === null ? null : root._catalog.setupSummary(noLm))
    }

    // A family's dot: measured (filled accent), estimated or partly (the accent at half strength),
    // nothing recorded (an outline in the quiet ink).
    function dotKind(state) {
        if (state === "measured" || state === "measuredByLm" || state === "recorded") return "measured"
        if (state === "none") return "unavailable"
        return "estimated"
    }
    // The words beside the family: CapRows' own, minus the family label it leads `text` with.
    function rowWords(r) {
        return r.reason !== "" ? r.stateText + " · " + r.reason : r.stateText
    }

    // ── Title ────────────────────────────────────────────────────────────────
    Item {
        width:  root.width
        height: Theme.sp(30)
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter
                      right: rigLink.left; rightMargin: Theme.gap(12) }
            text:           qsTr("What this rig can measure")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Font.DemiBold
            color:          Theme.colorText
            elide:          Text.ElideRight
        }
        PpLink {
            id: rigLink
            objectName: "rigSettingsLink"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text:        qsTr("Rig ▸")
            pressMargin: Theme.sp(6)
            onClicked:   root.studioSettingsRequested()
        }
    }
    Item { width: 1; height: Theme.sp(4) }

    // ── The families ─────────────────────────────────────────────────────────
    Repeater {
        model: root.rows

        Item {
            id: capRow
            required property var modelData
            readonly property string kind: root.dotKind(modelData.state)
            objectName: "capabilityRow"
            width:  root.width
            height: Math.max(Theme.sp(30), Math.max(capLabel.implicitHeight, capWords.implicitHeight) + Theme.sp(10))

            Accessible.role: Accessible.StaticText
            Accessible.name: modelData.text

            Rectangle {
                id: dot
                x: 0
                anchors.verticalCenter: capLabel.verticalCenter
                width:  Theme.sp(Theme.capDot); height: width; radius: width / 2
                color:  capRow.kind === "measured"  ? Theme.colorAccent
                      : capRow.kind === "estimated" ? Theme.colorAccentMid
                      :                               "transparent"
                border.width: capRow.kind === "measured" ? 0 : 1
                border.color: capRow.kind === "estimated" ? Theme.colorAccent : Theme.colorText3
            }
            Text {
                id: capLabel
                x: dot.width + Theme.gap(12)
                y: Theme.sp(5)
                width: Math.min(implicitWidth, Math.round(root.width * 0.45))
                text:           capRow.modelData.label
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    Theme.fontBodyWeight
                color:          capRow.kind === "unavailable" ? Theme.colorText3 : Theme.colorText
                elide:          Text.ElideRight
            }
            Text {
                id: capWords
                objectName: "capabilityWords"
                anchors { left: capLabel.right; leftMargin: Theme.gap(12); right: parent.right }
                anchors.baseline: capLabel.baseline
                text:                root.rowWords(capRow.modelData)
                horizontalAlignment: Text.AlignRight
                wrapMode:            Text.WordWrap
                maximumLineCount:    2
                elide:               Text.ElideRight
                font.family:         Theme.fontData
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingData
                color:               Theme.colorText3
            }
        }
    }

    // ── Pair, offers, warnings ───────────────────────────────────────────────
    Item { width: 1; height: Theme.sp(10); visible: pairRow.visible }
    Rectangle { width: root.width; height: 1; color: Theme.colorHairline; visible: pairRow.visible }

    // The pairing panel. A Popup parented to Overlay.overlay, so it covers the whole window from
    // here without Main.qml having to host it.
    PpcpPairDialog { id: pairDialog }

    // ⛔ H10 — RV-6 guided pairing (PPCP-RV §11) was instantiated beside the pair dialog and is
    // withdrawn with its button until RT-20c has run. Restoring it is:
    //
    //     PpcpGuidedPairDialog {
    //         id: guidedDialog
    //         onPairingCodeRequested: pairDialog.open()
    //     }
    //
    // and, beside "Add iPhone as a camera", a "Pair without a code" entry opening it. RV-6 is
    // built; the dialog, the controller and `guidedAvailable` are intact. What is missing is the one
    // test that proves the path does what it exists to do (RT-20c needs PinPointCapture's acceptor
    // and a relay), and 9g forbids claiming otherwise, so it does not belong on the home screen.

    // ── Pairing, PPCP-RV §4 ──────────────────────────────────────────────────
    // One entry, opening PpcpPairDialog: pair a phone and its cameras join the rig. Hidden entirely
    // on a build without libppcp and OpenSSL, where there is no `ppcpHost` context property at all
    // (H0 — a build with no PPCP must still be a working application).
    Item {
        id: pairRow
        visible: (typeof ppcpHost !== "undefined") && ppcpHost !== null && ppcpHost.listening
        width:   root.width
        height:  visible ? Theme.sp(40) : 0

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gap(8)
            // Drawn, not a glyph: neither Apple Symbols nor Segoe UI Symbol has a QR or a phone.
            PpQrGlyph {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.sp(13); height: Theme.sp(13)
                color: pairButton.hovered ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent
            }
            PpLink {
                id: pairButton
                objectName:  "pairPhoneButton"
                anchors.verticalCenter: parent.verticalCenter
                text:        qsTr("Add iPhone as a camera")
                font.pixelSize: Theme.fontSzBody
                pressMargin: Theme.sp(6)
                onClicked:   pairDialog.open()
            }
        }
    }

    // ── The sessions a connected capture device is offering ──────────────────
    // Already silent when it has nothing to say (visible: offerCount > 0), and the only route to a
    // device's recorded sessions. On a build without libppcp there is no controller and it never
    // appears (H0).
    PpcpOfferList {
        width: root.width
    }

    // ── The first resource warning ───────────────────────────────────────────
    Item {
        visible: resourceMonitor.warnings.length > 0
        height:  visible ? (warnNotice.implicitHeight + Theme.sp(12)) : 0
        width:   root.width

        RmWarningNotice {
            id: warnNotice
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            message: resourceMonitor.warnings.length > 0 ? resourceMonitor.warnings[0] : ""
        }
    }
}
