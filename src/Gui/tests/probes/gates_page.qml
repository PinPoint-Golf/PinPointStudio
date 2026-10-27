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

// THE GATES PAGE (docs/design/trust_and_gates_design.md §7.5, trust stage 7): Settings → Swing model →
// Gates. Opens the type, lets the library count run, logs the first rows, then opens one gate's details
// and grabs the window.
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path> [--probe-gate sequence.sightedBand] [--probe-png <abs path>]

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent
    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    function log(s) { console.warn("PROBE: " + s) }
    function find(item, test) {
        if (!item) return null
        if (test(item)) return item
        for (const k of (item.children || [])) { const r = find(k, test); if (r) return r }
        return null
    }
    function texts(item, out) {
        if (!item) return
        if (item.text !== undefined && item.visible && String(item.text) !== "") out.push(String(item.text).replace(/<[^>]*>/g, ""))
        for (const k of (item.children || [])) texts(k, out)
    }
    property int step: 0
    property var dm: null
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const root = probe.Window.window.contentItem
            if (probe.step === 1) {
                navController.navigate(AppScreen.Settings)
            } else if (probe.step === 2) {
                const ss = probe.find(root, i => typeof i.showMetricDetail === "function")
                if (!ss) { probe.log("no settings screen"); Qt.exit(1); return }
                ss.showMetricDetail("pelvisSway")       // opens Settings → Swing model
            } else if (probe.step === 3) {
                probe.dm = probe.find(root, i => typeof i.selectFresh === "function" && i._type !== undefined)
                if (!probe.dm) { probe.log("no diagnostic model"); Qt.exit(1); return }
                probe.dm._type = "gates"
            } else if (probe.step === 6) {
                const rows = probe.dm.mb ? probe.dm.mb.rows("gates", {}) : []
                probe.log("selecting " + probe.arg("--probe-gate", "sequence.sightedBand"))
                probe.dm.selectFresh("gates", probe.arg("--probe-gate", "sequence.sightedBand"))
            } else if (probe.step === 8) {
                const t = []; probe.texts(root, t)
                for (const x of t) if (/of \d+|Fired|swings|Threshold|lift|Measured on|Timed on/.test(x) && x.length < 200) probe.log("text: " + x)
                const png = probe.arg("--probe-png", "")
                if (png === "") { Qt.exit(0); return }
                probe.dm.grabToImage(function (res) { res.saveToFile(png); Qt.exit(0) })
            }
        }
    }
}
