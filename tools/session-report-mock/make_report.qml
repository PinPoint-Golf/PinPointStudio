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

// MAKE A SESSION'S REPORT, the whole way: SessionReportModel.make() → the map → the stills → PpReportPrintHost
// (Main.qml) lays the register on its hidden sheet and prints it → report_meta.json. The PDF lands where the
// app puts it: <session>/report/<yyyy-mm-dd>_<Golfer>_session-report.pdf.
//
//   PinPointStudio --probe-qml make_report.qml --probe-session <session dir> [--probe-note "…"]
//
// ⛔ Run it ONLY against a scratch library ($SP/safe_probe.sh), never the live share.
//
// Logs progress, then "PROBE: made <pdf> …" and the state the session list would show. Exit 0 when the PDF
// was written, 3 when it was not, 4 on the watchdog.
Item {
    id: probe
    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property string note: arg("--probe-note", "")
    property double t0: 0
    function log(s) { console.warn("PROBE: " + s) }
    Component.onCompleted: log("start make_report " + sessionDir)

    Connections {
        target: sessionReport
        function onProgressChanged() { if (sessionReport.progressPages > 0) probe.log("page " + sessionReport.progressPage + " / " + sessionReport.progressPages) }
        function onMade(dir, ok, pdfPath, error) {
            const secs = ((Date.now() - probe.t0) / 1000).toFixed(1)
            if (!ok) { probe.log("FAILED " + dir + ": " + error + " (" + secs + " s)"); Qt.exit(3); return }
            const st = sessionReport.state(dir)
            probe.log("made " + pdfPath + " in " + secs + " s; state " + st.state + ", " + st.sizeBytes + " bytes, \"" + st.words + "\"")
            Qt.exit(0)
        }
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            if (probe.sessionDir === "") { probe.log("give --probe-session"); Qt.exit(2); return }
            const pf = sessionReport.preflight(probe.sessionDir)
            probe.log("preflight " + JSON.stringify(pf))
            probe.log("state before " + JSON.stringify(sessionReport.state(probe.sessionDir)))
            probe.t0 = Date.now()
            sessionReport.make(probe.sessionDir, { note: probe.note })
        }
    }
    Timer { interval: 840000; running: true; onTriggered: { probe.log("WATCHDOG"); Qt.exit(4) } }
}
