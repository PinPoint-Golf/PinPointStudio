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

import QtQuick

// DUMP THE REPORT MAP — the one map SessionReportModel builds for a session (build/report-build/REPORT_MAP.md),
// without printing it. The C++ model's own output, so it can be diffed against the mock's report.json.
//
//   PinPointStudio --probe-qml dump_report.qml --probe-session <session dir>
//                  [--probe-note "coach's note"] [--probe-stills 0]
//
// ⛔ Run it ONLY against a scratch library (build/report-build: $SP/safe_probe.sh), never the live share:
// the app's work-ons catch-up rewrites every session record of the library it is pointed at.
//
// The map is printed to the log in chunks ("REPORTMAP[i/n]:…", newlines escaped); reassemble it with
// build/report-build/model-I/extract.py <log> <map.json>. (A file:// PUT from QML writes nothing here.)
// Exit 0 when the map was made, 3 when the model refused, 4 on the watchdog.
Item {
    id: probe
    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property string note: arg("--probe-note", "")
    readonly property bool stills: arg("--probe-stills", "1") !== "0"
    function log(s) { console.warn("PROBE: " + s) }
    Component.onCompleted: log("start dump_report " + sessionDir)

    Connections {
        target: sessionReport
        function onPrepared(dir, ok, report, error) {
            if (!ok) { probe.log("FAILED " + dir + ": " + error); Qt.exit(3); return }
            const text = JSON.stringify(report, null, 1)
            const n = Math.ceil(text.length / 3000)
            for (let i = 0; i < n; ++i) console.warn("REPORTMAP[" + (i + 1) + "/" + n + "]:" + text.substr(i * 3000, 3000).replace(/\n/g, "\\n"))
            probe.log("done: kind " + report.kind + ", " + (report.workOns || []).length + " work-ons")
            Qt.exit(0)
        }
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            if (probe.sessionDir === "") { probe.log("give --probe-session"); Qt.exit(2); return }
            probe.log("preparing " + probe.sessionDir)
            sessionReport.prepare(probe.sessionDir, { note: probe.note, stills: probe.stills })
        }
    }
    Timer { interval: 600000; running: true; onTriggered: { probe.log("WATCHDOG"); Qt.exit(4) } }
}
