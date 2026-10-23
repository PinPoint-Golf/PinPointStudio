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

// END-TO-END: THE SESSION REPORT'S WRIST PAGE ON REAL DATA (docs/design/session_report_design.md "The Wrist page").
//
//   SessionReportModel.prepare(session, stills off) → the report map, with its `wrist` block → the block's findings
//   logged (head, lines, rows, wash, trust) and, with --probe-dump 1, the block dumped to the log → PASS/FAIL checks.
//   With --probe-lay 1 the map is laid out on an A4 sheet as PpReportPrintHost lays it, every page logged, the Wrist
//   section's pages grabbed to PNG ("after") with the Posture section's, and the same map WITHOUT `wrist` laid out again
//   and the same page numbers grabbed ("before", and the Posture pages again: they must not move or change);
//   --probe-pdf prints the (after) report. The page checks: the page is there (or, absent, not); every block the block
//   has words for is drawn, and the trust note with every line; no cell or figure is a bare "0" or empty where "-"
//   belongs (P1's "0°" is address, legitimate); no vendor name anywhere on the page (core_pack_test's forbidden list);
//   the Posture section's pages are the same pages, cut at the same places, with and without the Wrist page.
//
// Run through --probe-qml (dev builds only), offscreen, under tools/typeset-harness/guard.sh, with a settings copy
// whose athleteLibraryPath points at a SCRATCH library (the app's work-ons catch-up rewrites the session records of the
// library it is pointed at; prepare() writes nothing of its own), on a scratch copy of the session:
//
//   CFG=$(mktemp -d); mkdir -p $CFG/PinPointStudio
//   sed "s#^athleteLibraryPath=.*#athleteLibraryPath=/mnt/swingdata/scratch/wrist-report-20261023/report-lib/#" \
//       ~/.config/PinPointStudio/PinPointStudio.ini > $CFG/PinPointStudio/PinPointStudio.ini
//   XDG_CONFIG_HOME=$CFG QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen \
//     tools/typeset-harness/guard.sh timeout 600 build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio \
//       --probe-qml $PWD/src/Gui/tests/probes/report_wrist.qml \
//       --probe-session /mnt/swingdata/scratch/wrist-report-20261023/report-lib/Mark-Liversedge/2026-08-18_Mark-Liversedge_Wrist_02 \
//       --probe-out /mnt/swingdata/scratch/wrist-report-20261023/review/report_after --probe-tag rich \
//       [--probe-expect present|absent] [--probe-theme 12] [--probe-scale 2] [--probe-lay 1] [--probe-before 1]
//       [--probe-pdf <abs path>.pdf] [--probe-dump 1] [--probe-stills 1]
//   (build/run-me/report_wrist.sh wraps exactly this for the four cases.)
//
//   --probe-expect present | absent: the check on the block (default present)
//   --probe-out    the PNG folder (--probe-lay 1): <tag>_wrist_p<N>.png (N = the page in the report), <tag>_before_p<N>.png,
//                  <tag>_postureA_p<N>.png / <tag>_postureB_p<N>.png (the Posture pages with / without the wrist block:
//                  build/run-me/report_wrist.sh diffs them)
//   --probe-theme  the APP's theme while it lays out (12 Folio light, 7 Vector dark): the pages must not change
//   --probe-dump 1 logs the map's `wrist` block as WRISTMAP[i/n]:… chunks, for a fixture (run-me reassembles them)
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
    readonly property string expect: arg("--probe-expect", "present")
    readonly property real scale: parseFloat(arg("--probe-scale", "2"))
    readonly property bool layout: arg("--probe-lay", "0") === "1"
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
    property var wristPages: []            // 0-based pages of the Wrist section, from the after layout
    property var posturePages: []          // the Posture section's, from the after layout: [index, from, to]
    // core_pack_test's forbidden brand tokens (split literals as there), and the launch monitor's own name
    readonly property var forbidden: ["titleist", "tpi", "trackman", "flightscope", "foresight", "k-vest", "kvest", "gears", "swingcatalyst",
                                      "boditrak", "performance institute", "certified", "hackmotion", "sports" + "box", "golf" + "tec",
                                      "golf" + " digest", "golf" + "digest", "gc" + "quad", "gc " + "quad", "heuristic"]
    function findAll(item, name, out) {
        if (!item) return out
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) findAll(kids[i], name, out)
        return out
    }
    function shownAll(name) { return findAll(probe.reg, name, []).filter(it => it.visible && it.height > 0) }
    // every visible Text under an item (a run's axis is skipped where asked: its "0" is the axis, not a reading)
    function textsUnder(item, out, skipRun) {
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const c = kids[i]
            if (!c || !c.visible) continue
            if (skipRun && c.objectName === "chRun") continue
            if (c.text !== undefined && c.truncated !== undefined && c.contentWidth !== undefined) out.push(c)
            textsUnder(c, out, skipRun)
        }
        return out
    }
    function pageChecks(w) {
        const head = probe.findAll(probe.reg, "reportWrist", [])[0] || null
        const page = head ? head.parent : null
        if (!w) { probe.check(!head || !head.visible || head.height === 0, "no wrist block: no Wrist page drawn"); return }
        probe.check(!!page, "the Wrist page is in the galley")
        if (!page) return
        const pf = w.profile || {}
        probe.check(probe.shownAll("reportWristProfile").length === 1, "the checkpoint profile")
        probe.check(probe.shownAll("reportWristRow").length === (pf.rows || []).length, "a row per wrist DOF (" + (pf.rows || []).length + ")")
        const cells = probe.shownAll("reportWristCell")
        probe.check(cells.length === 8 * (pf.rows || []).length, "eight cells a row (" + cells.length + ")")
        const bad = cells.filter(c => c.text === "" || c.text === "0")
        probe.check(bad.length === 0, "every cell is a figure with its unit, or \"-\"" + (bad.length ? ": " + bad.map(c => c.tag + "=" + JSON.stringify(c.text)).join(" ") : ""))
        const pts = (w.scoreRun || {}).points || []
        probe.check(probe.shownAll("reportWristScore").length === (pts.length > 0 ? 1 : 0), "the score run" + (pts.length ? "" : " (none: no points)"))
        probe.check(pts.every(p => p.measured ? /^[0-9]+$/.test(p.text) : p.text === "-"), "a score is its figure, an unscored swing \"-\"")
        const st = w.styles || {}
        const nLines = st.hasIndex && (st.columns || []).length ? Math.ceil(st.columns.length / 15) : 0
        probe.check(probe.shownAll("reportWristStyles").length === 1, "the style block (" + (nLines ? nLines + " line(s) of 15" : "the note alone") + ")")
        probe.check(probe.shownAll("reportWristStylesLine").length === nLines, "the style table's lines: " + nLines)
        const sc = probe.shownAll("reportWristStyleCell")
        probe.check(sc.every(c => c.text !== ""), "every style cell a figure or \"-\"")
        probe.check(sc.filter(c => c.graded).length === (st.columns || []).length * (nLines ? 1 : 0), "one style in bold per swing")
        probe.check(probe.shownAll("reportWristFinding").length === (w.recurring || []).length, "a finding per recurring condition (" + (w.recurring || []).length + ")")
        const marks = probe.shownAll("reportWristMark")
        probe.check(marks.length === (w.recurring || []).reduce((a, r) => a + (r.marks || []).length, 0), "a mark per swing per finding")
        probe.check(marks.filter(m => m.fired).length === (w.recurring || []).reduce((a, r) => a + (r.marks || []).filter(k => k.fired).length, 0), "filled where it fired")
        probe.check(probe.shownAll("reportWristRepeat").length === (w.repeat ? 1 : 0), "Repeatable? " + (w.repeat ? "drawn" : "absent"))
        const mw = w.movesWith || null
        if (mw && mw.available) probe.check(probe.shownAll("reportWristMovesRow").length === (mw.rows || []).length, "what moves with: a row per reading (" + (mw.rows || []).length + ")")
        else probe.check(probe.shownAll("reportWristMovesRow").length === 0 && (!mw || probe.shownAll("reportWristMovesRefusal").length === 1), "what moves with: refused, the refusal alone")
        probe.check(probe.shownAll("reportWristTrust").length === 1, "the trust note")
        const tl = probe.shownAll("reportWristTrustLine").map(t => t.text)
        probe.check(JSON.stringify(tl) === JSON.stringify(w.trust || []), "every trust line, verbatim (" + tl.length + ")")
        // the honesty greps on what is DRAWN
        const drawn = probe.textsUnder(page, [], false).map(t => String(t.text))
        const lower = drawn.join("\n").toLowerCase() + "\n" + probe.strings(w, "wrist", []).map(x => x[1]).join("\n").toLowerCase()
        const hits = probe.forbidden.filter(f => lower.indexOf(f) >= 0)
        probe.check(hits.length === 0, "no vendor name on the page" + (hits.length ? ": " + hits.join(", ") : ""))
        // (a resemblance of 0 % is a reading: the style table's cells are checked above, not here)
        const bare = probe.textsUnder(page, [], true).filter(t => t.objectName !== "reportWristStyleCell" && /^\s*0\s*$/.test(String(t.text)))
        probe.check(bare.length === 0, "no bare \"0\" drawn where \"-\" belongs (the score axis's 0 aside)" + (bare.length ? ": " + bare.length : ""))
        probe.check(drawn.indexOf(w.sizeLine) < 0 || (w.trust || []).indexOf(w.sizeLine) >= 0, "the small-session words said once")
        probe.log("drawn texts on the Wrist page: " + drawn.length)
    }

    PpPdfPrinter {
        id: printer
        dpi: 200; settleMs: 400; title: "Session report"; author: "PinPoint Studio"
        onFinished: (ok, path, pages, error) => { probe.check(ok, "the PDF " + path + " (" + pages + " pages)" + (ok ? "" : ": " + error)); probe.after(200, probe.doBefore) }
    }

    // every string in the block, for the honesty checks
    function strings(v, path, out) {
        if (typeof v === "string") out.push([path, v])
        else if (Array.isArray(v)) v.forEach((x, i) => strings(x, path + "/" + i, out))
        else if (v && typeof v === "object") Object.keys(v).forEach(k => strings(v[k], path + "/" + k, out))
        return out
    }

    Connections {
        target: sessionReport
        function onPrepared(dir, ok, report, error) {
            if (!ok) { probe.log("FAILED " + dir + ": " + error); Qt.exit(3); return }
            probe.map = report
            const w = report.wrist || null
            probe.log("map: kind " + report.kind + ", " + (report.workOns || []).length + " work-ons, posture " + (report.posture ? "present" : "absent")
                      + ", wrist " + (w ? "present" : "absent"))
            if (w) {
                probe.log("wrist kicker: " + w.kicker)
                probe.log("wrist head: " + w.head)
                probe.log("wrist style: " + w.styleLine)
                probe.log("wrist score: " + w.scoreLine)
                const pf = w.profile || {}
                probe.log("wrist caption: " + pf.caption)
                for (const r of (pf.rows || []))
                    probe.log("  ROW " + r.key + " (" + r.label + ", " + r.camera + ", " + r.unit + ") " + r.poleText + " | "
                              + (r.cells || []).map(c => c.tag + " " + c.text + (c.wash === "band" ? " [" + Math.round(c.bandLo) + "," + Math.round(c.bandHi) + "]" : "")
                                                        + (c.outside ? " OUT " + c.ringWord : "")).join("  "))
                probe.log("wrist score run: " + ((w.scoreRun || {}).points || []).map(p => p.swing + ":" + p.text).join(" "))
                probe.log("wrist styles: " + ((w.styles || {}).columns || []).length + " columns" + ((w.styles || {}).note ? " — " + w.styles.note : ""))
                probe.log("wrist recurring: " + (w.recurring || []).map(r => r.name + " " + r.countText).join(" | ") + (w.recurring && w.recurring.length ? "" : " " + w.recurringEmpty))
                probe.log("wrist repeat: " + w.repeat)
                const mw = w.movesWith || null
                probe.log("wrist movesWith: " + (!mw ? "none" : mw.available ? mw.head + "; " + (mw.rows || []).length + " rows; " + mw.chanceLine : "refused — " + mw.refusal))
                for (const t of (w.trust || [])) probe.log("  TRUST " + t)
                if (w.trustMore) probe.log("  TRUST " + w.trustMore)
                const all = probe.strings(w, "wrist", [])
                const shot = all.filter(s => !s[0].includes("/movesWith/rows/") && /\b[Ss]hots?\b/.test(s[1]))
                probe.check(shot.length === 0, "the block counts swings, never shots" + (shot.length ? ": " + shot.slice(0, 3).map(s => s[0] + " = " + s[1]).join(" | ") : ""))
                const brand = all.filter(s => /hackmotion|heuristic/i.test(s[1]))
                probe.check(brand.length === 0, "no product name, no 'heuristic'" + (brand.length ? ": " + brand[0][1] : ""))
                const zero = all.filter(s => /(^|\s)0(\s|$)/.test(s[1]) && s[0].endsWith("/text"))
                probe.log("cells whose text is a bare 0: " + zero.length + (zero.length ? " — " + zero.slice(0, 4).map(s => s[0]).join(", ") : ""))
                if (probe.dump) {
                    const text = JSON.stringify(w)
                    const n = Math.ceil(text.length / 3000)
                    for (let i = 0; i < n; ++i) console.warn("WRISTMAP[" + (i + 1) + "/" + n + "]:" + text.substr(i * 3000, 3000))
                }
            }
            probe.check(probe.expect === "absent" ? !w : !!w, "the map " + (probe.expect === "absent" ? "carries no" : "carries a") + " wrist block")
            if (!probe.layout) { probe.finish(); return }
            probe.lay(report, () => {
                const info = probe.reg.pageInfo()
                const pages = []
                info.forEach((p, i) => {
                    probe.log("  p" + (i + 1) + " " + (p.section || "-") + (p.cont ? " (cont " + p.cont + ")" : "") + " fill " + (p.used / p.room).toFixed(2)
                              + (p.used > p.room + 0.5 ? "  OVER" : ""))
                    if (p.section === "Wrist") pages.push(i)
                })
                probe.wristPages = pages
                probe.posturePages = info.map((p, i) => p.section === "Posture" ? [i, Math.round(p.from), Math.round(p.to)] : null).filter(x => !!x)
                const first = probe.reg.sectionPage("reportWrist")
                probe.check(w ? first > 0 : first === 0, w ? "the Wrist page is in the report" : "no wrist block: no Wrist page")
                probe.pageChecks(w)
                probe.check(!w || first > 0, "the Wrist section starts a page (p. " + first + ")")
                probe.check(!w || pages.length > 0, "the Wrist section has " + pages.length + " page(s): " + pages.map(p => p + 1).join(", "))
                probe.check(pages.every(p => info[p].used <= info[p].room + 0.5), "no Wrist page runs into the folio band")
                const po = probe.reg.sectionPage("reportPosture"), pr = Math.max(probe.reg.sectionPage("reportPractice"), 0)
                if (w && po > 0) probe.check(first > po, "after the Posture page (p. " + po + ")")
                if (w && pr > 0) probe.check(first < pr, "before Between now and next time (p. " + pr + ")")
                probe.log("pages " + probe.reg.printPageCount + "; wrist pages " + pages.map(p => p + 1).join(", "))
                probe.grab(pages, "wrist", () => probe.grab(probe.posturePages.map(x => x[0]), "postureA", () => {
                    if (probe.pdf !== "") { printer.title = ((report.meta || {}).pdfTitle) || "Session report"; printer.printToPdf(probe.sheet, probe.pdf, false) }
                    else probe.doBefore()
                }))
            })
        }
    }

    function doBefore() {
        if (!probe.before || probe.wristPages.length === 0) { probe.finish(); return }
        const m = Object.assign({}, probe.map)
        delete m.wrist
        probe.lay(m, () => {
            const info = probe.reg.pageInfo()
            probe.log("before: pages " + probe.reg.printPageCount + "; at the wrist pages: "
                      + probe.wristPages.map(p => "p" + (p + 1) + " " + (info[p] ? (info[p].section || "-") + (info[p].cont ? " (cont)" : "") : "none")).join(", "))
            probe.check(probe.reg.sectionPage("reportWrist") === 0, "without the block: no Wrist page")
            const pb = info.map((p, i) => p.section === "Posture" ? [i, Math.round(p.from), Math.round(p.to)] : null).filter(x => !!x)
            probe.check(JSON.stringify(pb) === JSON.stringify(probe.posturePages), "the Posture section unchanged by the Wrist page: the same pages, cut at the same places ("
                        + probe.posturePages.map(x => "p" + (x[0] + 1)).join(", ") + ")")
            probe.grab(probe.wristPages.filter(p => p < probe.reg.printPageCount), "before",
                       () => probe.grab(pb.map(x => x[0]), "postureB", probe.finish))
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
