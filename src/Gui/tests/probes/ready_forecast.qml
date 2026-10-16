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

// THE SETUP PAGE'S FORECAST (docs/design/trust_and_gates_design.md §7.4, stage 5): the real ReadyPage, fed
// "What this session will record" from the real catalogue (MetricCatalog.setupSummary) worded by
// setup_capability_rows.js, for two setups side by side — face-on camera + calibrated arm sensors (the
// 18 August rig) and two cameras with no sensors (the 4 July rig). Fakes stand in for the flow and the
// context; only `capability` is the product under test.
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path> [--probe-png <abs path>]

import QtQuick
import PinPointStudio
import "../../setup/setup_capability_rows.js" as CapRows

Item {
    id: probe
    anchors.fill: parent
    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    function log(s) { console.warn("PROBE: " + s) }
    MetricCatalog { id: catalog }

    function rowsFor(facts, ctx) { return CapRows.rows(catalog.setupSummary(facts), ctx, null) }
    readonly property var setups: [
        { title: "Face-on camera + arm sensors", facts: { faceOn: true, dtl: false, imuRoles: ["leadForearm", "leadHand"], hackMotion: false, launchMonitor: false },
          ctx: { armInSession: true, armCalibrated: true, upperArmHeld: false, cameraCount: 1, faceOn: "connected", dtl: "absent", lmName: "" } },
        { title: "Two cameras, no sensors", facts: { faceOn: true, dtl: true, imuRoles: [], hackMotion: false, launchMonitor: false },
          ctx: { armInSession: false, armCalibrated: false, upperArmHeld: false, cameraCount: 2, faceOn: "connected", dtl: "connected", lmName: "" } }
    ]

    Rectangle { id: canvas; anchors.fill: parent; color: Theme.colorBg
        Row {
            anchors.fill: parent; anchors.margins: 24; spacing: 24
            Repeater {
                model: probe.setups
                delegate: ReadyPage {
                    required property var modelData
                    width: (canvas.width - 72) / 2
                    height: canvas.height - 48
                    stepLabel: modelData.title.toUpperCase()
                    flow: QtObject { property var issues: []; property var summaryRows: [] }
                    ctx: QtObject {
                        property var capability: probe.rowsFor(modelData.facts, modelData.ctx)
                        property bool launchMonitorConfigured: false
                        property string launchMonitorName: ""
                    }
                    Component.onCompleted: {
                        for (const r of ctx.capability)
                            probe.log(modelData.title + " | " + r.text + (r.gateLine ? "  ⟶ " + r.gateLine : ""))
                    }
                }
            }
        }
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            const png = probe.arg("--probe-png", "")
            if (png === "") { Qt.exit(0); return }
            canvas.grabToImage(function (res) { res.saveToFile(png); Qt.exit(0) })
        }
    }
}
