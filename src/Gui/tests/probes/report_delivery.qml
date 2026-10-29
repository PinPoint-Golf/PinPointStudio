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

// END-TO-END: THE SESSION REPORT'S CLUB DELIVERY PAGE ON REAL DATA (docs/design/club_delivery_chart_design.md §4.8, stage 4).
//
//   SessionReportModel.prepare(session, stills off) → the report map, with its `delivery` block → PpSessionReport laid
//   out on an A4 sheet (as PpReportPrintHost lays it) → every page logged (section, fill) → the Club delivery section's
//   pages grabbed to PNG ("after") → the same map WITHOUT `delivery` laid out again and the same page numbers grabbed
//   ("before": what sits at that place in the report today, the first pages of "Between now and next time") →
//   PASS/FAIL checks. Optionally the whole report (after) to PDF, and the delivery block dumped to the log.
//
// Run through --probe-qml (dev builds only), offscreen, under tools/typeset-harness/guard.sh, with a settings copy
// whose athleteLibraryPath points at a SCRATCH library (the app's work-ons catch-up rewrites the session records of the
// library it is pointed at; prepare() writes nothing of its own), on scratch copies of the sessions — keeping the
// golfer/session folder layout, so the presenter's fit finds the golfer's other sessions of the same day:
//
//   CFG=$(mktemp -d); mkdir -p $CFG/PinPointStudio
//   sed "s#^athleteLibraryPath=.*#athleteLibraryPath=/mnt/swingdata/scratch/delivery-report-20261029/lib/#" \
//       ~/.config/PinPointStudio/PinPointStudio.ini > $CFG/PinPointStudio/PinPointStudio.ini
//   XDG_CONFIG_HOME=$CFG QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen \
//     tools/typeset-harness/guard.sh timeout 600 build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
//       --probe-qml $PWD/src/Gui/tests/probes/report_delivery.qml \
//       --probe-session /mnt/swingdata/scratch/delivery-report-20261029/lib/Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_01 \
//       --probe-out /mnt/swingdata/scratch/delivery-report-20261029/review --probe-tag jess \
//       [--probe-theme 12] [--probe-scale 2] [--probe-before 1] [--probe-pdf <abs path>.pdf] [--probe-dump 1]
//   (build/run-me/report_delivery.sh wraps exactly this for Mark 5 Oct and Jess 20 Oct.)
//
//   --probe-out    the PNG folder: <tag>_delivery_p<N>.png (N = the page in the report), <tag>_before_p<N>.png
//   --probe-theme  the APP's theme while it lays out (12 Folio light, 7 Vector dark): the pages must not change
//   --probe-before 0 skips the before grabs; --probe-pdf prints every page of the (after) report to that PDF
//   --probe-dump 1 logs the map as REPORTMAP[i/n]:… chunks (the whole map: a tst_session_report fixture is a map)
// "PROBE:" lines carry the findings, "PROBE: PASS|FAIL …" the checks. Exit 0 = every check passed; 3 = the model
// refused; 4 = the watchdog.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property string out: arg("--probe-out", "")
    readonly property string tag: arg("--probe-tag", "report")
    readonly property real scale: parseFloat(arg("--probe-scale", "2"))
    readonly property bool before: arg("--probe-before", "1") !== "0"
    readonly property string pdf: arg("--probe-pdf", "")
    readonly property bool dump: arg("--probe-dump", "0") === "1"
    function log(s) { console.warn("PROBE: " + s) }
    property int fails: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails += 1 }
    function after(ms, fn) { const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe); t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start() }
    function finish() { log("done fails=" + fails); after(200, () => Qt.exit(fails ? 1 : 0)) }

    property var map: null
    property var sheet: null
    property var reg: null
    property var deliveryPages: []         // 0-based pages of the Club delivery section, from the after layout

    PpPdfPrinter {
        id: printer
        dpi: 200; settleMs: 400; title: "Session report"; author: "PinPoint Studio"
        onFinished: (ok, path, pages, error) => { probe.check(ok, "the PDF " + path + " (" + pages + " pages)" + (ok ? "" : ": " + error)); probe.after(200, probe.doBefore) }
    }

    Connections {
        target: sessionReport
        function onPrepared(dir, ok, report, error) {
            if (!ok) { probe.log("FAILED " + dir + ": " + error); Qt.exit(3); return }
            probe.map = report
            const dl = report.delivery || null
            probe.log("map: kind " + report.kind + ", " + (report.workOns || []).length + " work-ons, posture " + (report.posture ? "present" : "absent")
                      + ", wrist " + (report.wrist ? "present" : "absent") + ", delivery " + (dl ? "present" : "absent"))
            if (dl) {
                probe.log("delivery kicker: " + dl.kicker)
                probe.log("delivery head: " + dl.head)
                const band = dl.band || {}
                probe.log("delivery band: shown " + band.shown + ", n " + band.n + ", scale " + (band.lo || 0).toFixed(1) + "…" + (band.hi || 0).toFixed(1)
                          + " | " + (band.positions || []).map(p => p.tag + " " + p.medianText + " " + p.halfText).join(", "))
                probe.log("delivery band sub: " + band.sub)
                probe.log("delivery varies: " + band.varies)
                probe.log("delivery usual: " + (dl.usual || []).map(u => u.label + " " + u.text).join(" · "))
                const fl = dl.flight || {}
                probe.log("delivery flight: " + fl.text + (fl.attackText ? " | " + fl.attackText : "") + " | pool " + fl.pool + " | holds " + fl.holds)
                const sc = fl.scatter || null
                probe.log("delivery scatter: " + (sc ? sc.points.length + " dots (" + sc.points.filter(p => p.faint).length + " faint), line " + (sc.line ? "yes" : "no")
                                                       + ", x " + sc.xRange.lo.toFixed(1) + "…" + sc.xRange.hi.toFixed(1) + " ticks " + (sc.xTicks || []).map(t => t.text).join(" ")
                                                       + ", y " + sc.yRange.lo.toFixed(1) + "…" + sc.yRange.hi.toFixed(1) + " ticks " + (sc.yTicks || []).map(t => t.text).join(" ")
                                                     : "none") + (fl.legend ? " | " + fl.legend : ""))
                const sh = dl.shape || {}
                probe.log("delivery shape: shown " + sh.shown + ", " + (sh.marks || []).filter(m => m.available).length + " of " + (sh.marks || []).length
                          + " marked | " + sh.line + " | " + sh.sub)
                probe.log("delivery trust lines: " + (dl.trust || []).length + (dl.trustMore ? " + " + dl.trustMore : ""))
                for (const t of (dl.trust || [])) probe.log("  TRUST " + t)
                probe.check((band.positions || []).every(p => p.medianText !== "" && p.halfText !== ""), "every band figure is a figure or \"-\"")
            }
            if (probe.dump) {
                const text = JSON.stringify(report)
                const n = Math.ceil(text.length / 3000)
                for (let i = 0; i < n; ++i) console.warn("REPORTMAP[" + (i + 1) + "/" + n + "]:" + text.substr(i * 3000, 3000))
            }
            probe.check(!!dl, "the map carries a delivery block")
            probe.lay(report, () => {
                const info = probe.reg.pageInfo()
                const pages = []
                info.forEach((p, i) => {
                    probe.log("  p" + (i + 1) + " " + (p.section || "-") + (p.cont ? " (cont " + p.cont + ")" : "") + " fill " + (p.used / p.room).toFixed(2)
                              + (p.used > p.room + 0.5 ? "  OVER" : ""))
                    if (p.section === "Club delivery") pages.push(i)
                })
                probe.deliveryPages = pages
                const first = probe.reg.sectionPage("reportDelivery")
                probe.check(!dl || first > 0, "the Club delivery section starts a page (p. " + first + ")")
                probe.check(!dl || pages.length > 0, "the Club delivery section has " + pages.length + " page(s): " + pages.map(p => p + 1).join(", "))
                probe.check(pages.every(p => info[p].used <= info[p].room + 0.5), "no Club delivery page runs into the folio band")
                const po = probe.reg.sectionPage("reportPosture"), wr = probe.reg.sectionPage("reportWrist")
                const pr = Math.max(probe.reg.sectionPage("reportPractice"), 0)
                if (dl && po > 0) probe.check(first > po, "after the Posture page (p. " + po + ")")
                if (dl && wr > 0) probe.check(first > wr, "after the Wrist page (p. " + wr + ")")
                if (dl && pr > 0) probe.check(first < pr, "before Between now and next time (p. " + pr + ")")
                probe.log("pages " + probe.reg.printPageCount + "; delivery pages " + pages.map(p => p + 1).join(", "))
                probe.grab(pages, "delivery", () => {
                    if (probe.pdf !== "") { printer.title = ((report.meta || {}).pdfTitle) || "Session report"; printer.printToPdf(probe.sheet, probe.pdf, false) }
                    else probe.doBefore()
                })
            })
        }
    }

    function doBefore() {
        if (!probe.before || probe.deliveryPages.length === 0) { probe.finish(); return }
        const m = Object.assign({}, probe.map)
        delete m.delivery
        probe.lay(m, () => {
            const info = probe.reg.pageInfo()
            probe.log("before: pages " + probe.reg.printPageCount + "; at the delivery pages: "
                      + probe.deliveryPages.map(p => "p" + (p + 1) + " " + (info[p] ? (info[p].section || "-") + (info[p].cont ? " (cont)" : "") : "none")).join(", "))
            probe.check(probe.reg.sectionPage("reportDelivery") === 0, "without the block: no Club delivery page")
            probe.grab(probe.deliveryPages.filter(p => p < probe.reg.printPageCount), "before", probe.finish)
        })
    }

    // lay a map out on a fresh A4 sheet, as PpReportPrintHost does, and wait for the pager to settle
    function lay(report, then) {
        if (probe.sheet) probe.sheet.destroy()
        probe.sheet = Qt.createQmlObject('import QtQuick; Rectangle { z: 1000000; color: "#FFFFFF"; width: 794; height: 1123; property var reg: null; property int printPage: 0; '
                                         + 'readonly property int printPageCount: reg ? reg.printPageCount : 0; readonly property bool printPageHasPhoto: reg ? reg.printPageHasPhoto : false; '
                                         + 'onPrintPageChanged: if (reg) reg.printPage = printPage }', probe.Window.window.contentItem)
        const comp = Qt.createComponent("PinPointStudio", "PpSessionReport")
        if (comp.status !== Component.Ready) { probe.log("register error: " + comp.errorString()); Qt.exit(2); return }
        probe.reg = comp.createObject(probe.sheet, { report: report })
        probe.sheet.reg = probe.reg
        let last = "", same = 0, n = 0
        const poll = () => {
            const s = JSON.stringify(probe.reg.pageInfo().map(p => [Math.round(p.from), Math.round(p.to)]))
            same = s === last ? same + 1 : 0
            last = s
            if (same >= 3 || ++n > 80) then()
            else probe.after(120, poll)
        }
        probe.after(600, poll)
    }
    function grab(pages, kind, then) {
        if (pages.length === 0 || probe.out === "") { then(); return }
        const p = pages[0]
        probe.sheet.printPage = p
        probe.after(350, () => {
            probe.sheet.grabToImage(r => {
                const f = probe.out + "/" + probe.tag + "_" + kind + "_p" + (p < 9 ? "0" : "") + (p + 1) + ".png"
                probe.check(r.saveToFile(f), "wrote " + f)
                probe.grab(pages.slice(1), kind, then)
            }, Qt.size(Math.round(794 * probe.scale), Math.round(1123 * probe.scale)))
        })
    }

    Timer {
        interval: 2500; running: true
        onTriggered: {
            if (probe.sessionDir === "") { probe.log("give --probe-session"); Qt.exit(2); return }
            Theme.themeIndex = parseInt(probe.arg("--probe-theme", "12"))
            probe.log("theme " + Theme.aesthetic + (Theme.dark ? " dark" : " light") + "; preparing " + probe.sessionDir)
            sessionReport.prepare(probe.sessionDir, { note: "", stills: false })
        }
    }
    Timer { interval: 600000; running: true; onTriggered: { probe.log("WATCHDOG"); Qt.exit(4) } }
}
