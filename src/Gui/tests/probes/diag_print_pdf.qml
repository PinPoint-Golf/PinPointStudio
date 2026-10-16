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
import QtQuick.Controls
import PinPointStudio

// DIAG PRINT PDF — renders the Session diagnostics report (PpDiagPrint) through the app's print-to-PDF
// function (PpPdfPrinter -> PdfDocumentWriter) into one PDF per theme. Runner:
// build/run-me/verify-diag-print.sh, which writes the PDFs to the scratch share, never the repo.
// Same set-up as render_variant3.qml's print path: load the session, replay the shot, enter
// Analyse, find the real body (sdBody), lay the register on an A4 sheet fed from it, open the detail.
// Then, instead of saving PNGs, the sheet is handed to PpPdfPrinter, which steps printPage itself.
//
//   PinPointStudio --probe-qml print_probe.qml
//     --probe-session <dir> --probe-ordinal 8
//     --probe-register-type <QML type name>        (default PpDiagPrint, the module's report register)
//     --probe-register <abs path to a register .qml> (a register file outside the module, for a mock)
//     --probe-print portrait|landscape  --probe-themes 7,12  --probe-detail over_the_top
//     --probe-dpi 300  --probe-out <dir>  --probe-tag <prefix>
//
// Writes <out>/<tag>_print-<orient>_<aesthetic>_<light|dark>.pdf and logs "PROBE: wrote … (N pages)".
// Exit 0 when every PDF was written, 3 when any failed, 1/2 on set-up failure.
// Run it like variant3.sh does: QT_QPA_PLATFORM="offscreen:configfile=screen_print.json", through guard.sh.
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property var themes: arg("--probe-themes", "7,12").split(",").map(Number)
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string tag: arg("--probe-tag", "print")
    readonly property string registerPath: arg("--probe-register", arg("--probe-variant", ""))
    readonly property string registerType: arg("--probe-register-type", registerPath === "" ? "PpDiagPrint" : "")
    readonly property string detailId: arg("--probe-detail", "over_the_top")
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))
    readonly property string orientation: arg("--probe-print", "portrait")
    readonly property int dpi: parseInt(arg("--probe-dpi", "300"))
    property var body: null
    property var sheet: null
    property var register: null
    property var jobs: []
    property int startTheme: 0
    property int failures: 0
    function log(s) { console.warn("PROBE: " + s) }
    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }

    PpPdfPrinter {
        id: printer
        dpi: probe.dpi
        settleMs: 900                       // the harness's page settle; the pager lays out over frames
        title: "Session diagnostics"
        onPageWritten: (index, count) => probe.log("page " + (index + 1) + "/" + count)
        onFinished: (ok, path, pages, error) => {
            if (ok) probe.log("wrote " + path + " (" + pages + " pages)")
            else { probe.failures++; probe.log("FAILED " + path + ": " + error) }
            probe.nextTheme()
        }
    }

    // The register feeds from the live body exactly as render_variant3.qml's feed() does.
    function feed() {
        const b = probe.body, v = probe.register
        if (v.body !== undefined) v.body = b
        v.readout = b.readout; v.cards = b.cards; v.source = b.source; v.header = b.header
        v.detail = b.detail
    }

    // An A4 sheet of white paper at 96 dpi logical, the register laid over all of it. The sheet is
    // what the printer grabs (so paper shows where the register draws nothing) and it relays
    // printPage / printPageCount to the register.
    function makeSheet(comp) {
        const portrait = probe.orientation !== "landscape"
        const pw = portrait ? 794 : 1123, ph = portrait ? 1123 : 794
        probe.sheet = Qt.createQmlObject(
            'import QtQuick; Rectangle { z: 1000000; color: "#FFFFFF"; property var reg: null;'
            + ' property int printPage: 0; readonly property int printPageCount: reg ? Math.max(1, reg.printPageCount) : 0;'
            + ' onPrintPageChanged: if (reg) reg.printPage = printPage }',
            probe.Window.window.contentItem)
        probe.sheet.width = pw; probe.sheet.height = ph
        probe.register = comp.createObject(probe.sheet, { width: pw, height: ph })
        if (!probe.register) { probe.log("register create failed: " + comp.errorString()); Qt.exit(2); return false }
        if (probe.register.medium !== undefined) probe.register.medium = "print"
        if (probe.register.printOrientation !== undefined) probe.register.printOrientation = probe.orientation
        probe.sheet.reg = probe.register
        return true
    }

    function nextTheme() {
        if (probe.jobs.length === 0) {
            Theme.themeIndex = probe.startTheme
            after(300, () => Qt.exit(probe.failures ? 3 : 0))
            return
        }
        Theme.themeIndex = probe.jobs.shift()
        probe.body._openDetail(probe.detailId)
        after(1500, () => {
            probe.feed()
            probe.sheet.printPage = 0
            after(1200, () => {
                const path = probe.outDir + "/" + probe.tag + "_print-" + probe.orientation + "_"
                           + Theme.aesthetic + "_" + (Theme.dark ? "dark" : "light") + ".pdf"
                probe.log("printing " + probe.sheet.printPageCount + " pages to " + path)
                printer.printToPdf(probe.sheet, path, probe.orientation === "landscape")
            })
        })
    }

    function registerComponent() {
        if (probe.registerType !== "")
            return Qt.createComponent("PinPointStudio", probe.registerType)
        if (probe.registerPath === "") return null
        return Qt.createComponent(probe.registerPath.startsWith("file:") ? probe.registerPath
                                                                        : "file://" + probe.registerPath)
    }

    Timer {
        interval: 2500; running: true
        onTriggered: {
            probe.startTheme = Theme.themeIndex
            // Silent: mute every TingPlayer in the scene before anything can play.
            let muted = 0
            const seen = new Set()
            function mute(o) {
                if (!o || seen.has(o)) return
                seen.add(o)
                if (String(o).indexOf("TingPlayer") === 0) { o.volume = 0; o.frequency = 1; muted++ }
                const ds = o.data || []
                for (let i = 0; i < ds.length; ++i) mute(ds[i])
            }
            mute(probe.Window.window.contentItem)
            probe.log("muted " + muted + " ting players")
            for (const t of probe.themes) probe.jobs.push(t)
            // the only panel, so in tabs it is the one on screen (else another tab hides sdBody)
            for (const k of ["camera", "swing3d", "launchMonitor", "charts", "table", "markup", "trust"]) ViewLayout.setPanel(SessionMode.analyse, k, false)
            ViewLayout.setPanel(SessionMode.analyse, "sessionDiagnostics", true)
            ViewLayout.setArrangement(SessionMode.analyse, "tabs")
            navController.navigate(2)
            SessionMode.enterCapture()
            sessionReviewController.loadSession(probe.sessionDir)
            probe.after(4000, () => {
                for (let i = 0; i < shots.count; ++i) {
                    const o = shots.objectAt(i)
                    if (o.ordinal === probe.ordinal) SessionMode.enterReplay(o.shotId, o.swingDir)
                }
                probe.after(3000, () => {
                    SessionMode.enterAnalyse()
                    probe.after(4000, () => {
                        const bs = []
                        probe.walk(probe.Window.window.contentItem, "sdBody", bs)
                        if (bs.length < 1) { probe.log("no sdBody"); Qt.exit(1); return }
                        probe.body = bs[0]
                        const comp = probe.registerComponent()
                        if (!comp || comp.status !== Component.Ready) {
                            probe.log("register error: " + (comp ? comp.errorString() : "give --probe-register or --probe-register-type"))
                            Qt.exit(2); return
                        }
                        if (!probe.makeSheet(comp)) return
                        probe.nextTheme()
                    })
                })
            })
        }
    }
}
