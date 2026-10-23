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

// END-TO-END: THE SESSION REPORT'S POSTURE PAGE ON REAL DATA (docs/design/posture_chart_design.md §5.7, stage 3b).
//
//   SessionReportModel.prepare(session, stills off) → the report map, with its `posture` block → PpSessionReport laid
//   out on an A4 sheet (as PpReportPrintHost lays it) → every page logged (section, fill) → the Posture section's
//   pages grabbed to PNG ("after") → the same map WITHOUT `posture` laid out again and the same page numbers grabbed
//   ("before": what sits at that place in the report today, the first pages of "Between now and next time") →
//   PASS/FAIL checks. Optionally the whole report (after) to PDF, and the posture block dumped to the log.
//
// Run through --probe-qml (dev builds only), offscreen, under tools/typeset-harness/guard.sh, with a settings copy
// whose athleteLibraryPath points at a SCRATCH library (the app's work-ons catch-up rewrites the session records of the
// library it is pointed at; prepare() writes nothing of its own), on a scratch copy of the session:
//
//   CFG=$(mktemp -d); mkdir -p $CFG/PinPointStudio
//   sed "s#^athleteLibraryPath=.*#athleteLibraryPath=/mnt/swingdata/scratch/posture-chart-20261022/report-lib/#" \
//       ~/.config/PinPointStudio/PinPointStudio.ini > $CFG/PinPointStudio/PinPointStudio.ini
//   XDG_CONFIG_HOME=$CFG QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen \
//     tools/typeset-harness/guard.sh timeout 600 build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
//       --probe-qml $PWD/src/Gui/tests/probes/report_posture.qml \
//       --probe-session /mnt/swingdata/scratch/posture-chart-20261022/report-lib/Nick-Henn/2026-10-20_Nick-Henn_Wrist_03 \
//       --probe-out /mnt/swingdata/scratch/posture-chart-20261022/review/report_after --probe-tag nick \
//       [--probe-theme 12] [--probe-scale 2] [--probe-before 1] [--probe-pdf <abs path>.pdf] [--probe-dump 1] [--probe-stills 1]
//   (build/run-me/report_posture.sh wraps exactly this for the three golfers.)
//
//   --probe-out    the PNG folder: <tag>_posture_p<N>.png (N = the page in the report), <tag>_before_p<N>.png
//   --probe-theme  the APP's theme while it lays out (12 Folio light, 7 Vector dark): the pages must not change
//   --probe-before 0 skips the before grabs; --probe-pdf prints every page of the (after) report to that PDF
//   --probe-dump 1 logs the map's `posture` block as POSTUREMAP[i/n]:… chunks (newlines escaped), for a fixture
//   --probe-stills 1 makes the stills (the session's videos must be there): the picture strip's frames (stage 5) get
//                  their pictures; without it the strip is left out and the page says why
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
    readonly property bool stills: arg("--probe-stills", "0") === "1"
    function log(s) { console.warn("PROBE: " + s) }
    property int fails: 0
    function check(ok, what) { log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails += 1 }
    function after(ms, fn) { const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe); t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start() }
    function finish() { log("done fails=" + fails); after(200, () => Qt.exit(fails ? 1 : 0)) }

    property var map: null
    property var sheet: null
    property var reg: null
    property var posturePages: []          // 0-based pages of the Posture section, from the after layout

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
            const po = report.posture || null
            probe.log("map: kind " + report.kind + ", " + (report.workOns || []).length + " work-ons, posture " + (po ? "present" : "absent"))
            if (po) {
                probe.log("posture head: " + po.head)
                probe.log("posture pattern: " + po.patternLine)
                probe.log("posture rows: " + ((po.profile || {}).rows || []).map(r => r.key).join(", "))
                probe.log("posture strip: " + (po.strip ? po.strip.marks.length + " marks, " + po.strip.line : "none"))
                probe.log("posture departures: " + (po.departures || []).map(d => d.line).join(" | "))
                probe.log("posture moved: " + (po.moved || []).map(x => x.when + ": " + x.line).join(" | "))
                probe.log("posture trust lines: " + (po.trust || []).length)
                const pic = po.picture || null
                probe.log("posture picture: " + (pic ? pic.typical + " (" + pic.typicalWhy + "); " + (pic.groups || []).map(g => g.camera + " ×" + (g.frames || []).length).join(", ")
                                                       : "none — " + (po.pictureWhy || []).join(" ")))
                if (pic) {
                    probe.log("posture picture legend: " + pic.legend + (pic.why && pic.why.length ? "  why: " + pic.why.join(" ") : ""))
                    for (const g of (pic.groups || []))
                        for (const f of (g.frames || []))
                            probe.log("  GEO " + f.key + " " + f.width + "×" + f.height + " src " + (f.src ? "yes" : "NO " + (f.imageWhy || ""))
                                      + " | " + f.caption + (f.note ? " | " + f.note : "") + " | "
                                      + (f.marks || []).map(m => m.role + ":" + m.label + (m.kind === "rect" ? "[x " + m.x.toFixed(1) + " w " + m.w.toFixed(1) + " y " + m.y.toFixed(1) + " h " + m.h.toFixed(1) + "]"
                                                                                       : m.kind === "vline" ? "[x " + m.x.toFixed(1) + "]" : "")).join(", "))
                    if (probe.stills) probe.check((pic.groups || []).every(g => (g.frames || []).every(f => !!f.src || !!f.imageWhy)), "every picture frame has its still, or says why not")
                }
                if (probe.dump) {
                    const text = JSON.stringify(po)
                    const n = Math.ceil(text.length / 3000)
                    for (let i = 0; i < n; ++i) console.warn("POSTUREMAP[" + (i + 1) + "/" + n + "]:" + text.substr(i * 3000, 3000))
                }
            }
            probe.check(!!po, "the map carries a posture block")
            probe.lay(report, () => {
                const info = probe.reg.pageInfo()
                const pages = []
                info.forEach((p, i) => {
                    probe.log("  p" + (i + 1) + " " + (p.section || "-") + (p.cont ? " (cont " + p.cont + ")" : "") + " fill " + (p.used / p.room).toFixed(2)
                              + (p.used > p.room + 0.5 ? "  OVER" : ""))
                    if (p.section === "Posture") pages.push(i)
                })
                probe.posturePages = pages
                const first = probe.reg.sectionPage("reportPosture")
                probe.check(!po || first > 0, "the Posture section starts a page (p. " + first + ")")
                probe.check(!po || pages.length > 0, "the Posture section has " + pages.length + " page(s): " + pages.map(p => p + 1).join(", "))
                probe.check(pages.every(p => info[p].used <= info[p].room + 0.5), "no Posture page runs into the folio band")
                const wo = probe.reg.sectionPage("reportWorkOn1"), pr = Math.max(probe.reg.sectionPage("reportPractice"), 0)
                if (po && wo > 0) probe.check(first > wo, "after the work-on pages (work-on 1 on p. " + wo + ")")
                if (po && pr > 0) probe.check(first < pr, "before Between now and next time (p. " + pr + ")")
                probe.log("pages " + probe.reg.printPageCount + "; posture pages " + pages.map(p => p + 1).join(", "))
                probe.grab(pages, "posture", () => {
                    if (probe.pdf !== "") { printer.title = ((report.meta || {}).pdfTitle) || "Session report"; printer.printToPdf(probe.sheet, probe.pdf, false) }
                    else probe.doBefore()
                })
            })
        }
    }

    function doBefore() {
        if (!probe.before || probe.posturePages.length === 0) { probe.finish(); return }
        const m = Object.assign({}, probe.map)
        delete m.posture
        probe.lay(m, () => {
            const info = probe.reg.pageInfo()
            probe.log("before: pages " + probe.reg.printPageCount + "; at the posture pages: "
                      + probe.posturePages.map(p => "p" + (p + 1) + " " + (info[p] ? (info[p].section || "-") + (info[p].cont ? " (cont)" : "") : "none")).join(", "))
            probe.check(probe.reg.sectionPage("reportPosture") === 0, "without the block: no Posture page")
            probe.grab(probe.posturePages.filter(p => p < probe.reg.printPageCount), "before", probe.finish)
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
            sessionReport.prepare(probe.sessionDir, { note: "", stills: probe.stills })
        }
    }
    Timer { interval: 600000; running: true; onTriggered: { probe.log("WATCHDOG"); Qt.exit(4) } }
}
