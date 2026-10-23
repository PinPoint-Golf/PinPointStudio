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
import QtTest
import PinPointStudio
import "data/session_report/fixtures.js" as Fx

// THE SESSION REPORT'S LAYOUT CONTRACT (docs/design/session_report_design.md §9), proved on the stress set (§9.2).
//
// Every fixture is a report map in the final schema (build/report-build/REPORT_MAP.md), cut by
// build/report-build/register/build_fixtures.py from the two real mock maps, so the words and numbers are real and only
// what a case stresses is changed. The .json files beside fixtures.js are the canonical copies; this suite reads the JS
// library because QuickTest cannot read a local file with XMLHttpRequest.
//
// For each fixture the register is laid out on an A4 sheet (794 × 1123 logical) and every page is stepped. It FAILS if:
//   · any page's atoms reach past its room (they would cross the folio band — the pager clips an oversized atom);
//   · any Text is truncated or elided, except the cover's fitted lines (§9.1 rule 3: they are marked `fitted`), or any
//     Text's content is wider than its box or than the text width;
//   · any page other than a section's last is less than a quarter full;
//   · the cover is more than one page (page 2 must open a new section);
//   · the page count is not the one recorded for the fixture (a change to it is a layout change: look at the pages);
//   · an empty block is drawn (rule 6: no heading over nothing), by objectName, per fixture.
// It also proves the report ignores the app's theme (§7.1): the same fixture in Folio light and Vector dark draws every
// Text in the same family, size and colour, and every Rectangle in the same colour.
//
// Stills point at the mock's JPEGs on the share; without the share they are empty frames of the same size, so nothing
// here depends on them except the photo-page assertion, which only runs on a still that loaded. Silent, no window.
Item {
    id: probe
    width: 794; height: 1123

    PpSessionReport { id: reg; width: 794; height: 1123 }

    TestCase {
        id: tc
        name: "SessionReport"
        when: windowShown

        // pages per fixture, recorded from the print_report.qml renders of 20 Oct (build/report-build/register); no appendix
        readonly property var expectedPages: ({
            "nick": 11, "jess": 10, "clean": 4, "one-work-on": 6, "five-work-ons-longest-text": 16,
            "causes-at-caps": 13, "sixty-swings-three-clubs": 14, "junior-no-photos": 11,
            "longest-names-and-note": 11, "note": 1, "no-launch-monitor": 10,
            "launch-monitor-appendix": 15,      // nick + the launch monitor appendix (Nick Henn's 20 Oct session 3 readings)
            "posture": 15,                      // nick + the Posture page (Nick Henn's 20 Oct session 3 posture readings; + its picture strip, stage 5)
            "wrist": 17                         // posture + the Wrist page (Mark Liversedge's 18 Aug Wrist_02 wrist block: report_wrist.sh rich)
        })

        function signature() { return JSON.stringify(reg.pageInfo().map(p => [Math.round(p.from), Math.round(p.to)])) }
        // lay a fixture out and wait for the pager (and any one-shot tightening) to settle
        function load(name) {
            reg.report = null
            reg.printPage = 0
            wait(30)
            reg.report = Fx.maps[name]
            let last = "", same = 0
            for (let i = 0; i < 80 && same < 3; ++i) {
                wait(100)
                const s = signature()
                same = (s === last) ? same + 1 : 0
                last = s
            }
            verify(same >= 3, name + ": the pagination never settled")
        }
        function find(item, name) {
            if (!item) return null
            if (item.objectName === name) return item
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) { const r = find(kids[i], name); if (r) return r }
            return null
        }
        function findAll(item, name, out) {
            if (!item) return out
            if (item.objectName === name) out.push(item)
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) findAll(kids[i], name, out)
            return out
        }
        // drawn: any item of that name visible with a height (a name can recur: the note's own numbers table)
        function shown(name) { return findAll(reg, name, []).some(it => it.visible && it.height > 0) }
        // every Text in the galley (the pager's own, painted-over furniture and the off-galley measuring copies excluded)
        function texts(item, out, hidden) {
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) {
                const c = kids[i]
                if (!c || !c.visible) continue
                const s = String(c)
                if (s.indexOf("PpRunningHead") === 0 || s.indexOf("PpFolio") === 0) continue
                const h = hidden || c.opacity === 0
                if (c.text !== undefined && c.truncated !== undefined && c.contentWidth !== undefined) { if (!h) out.push(c) }
                texts(c, out, h)
            }
            return out
        }
        function stills(item, out) {
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) {
                const c = kids[i]
                if (!c || !c.visible) continue
                if (c.objectName === "reportStill" && c.opacity !== 0) out.push(c)
                stills(c, out)
            }
            return out
        }

        function checkLayout(name) {
            const info = reg.pageInfo()
            compare(reg.printPageCount, reg.clientPageCount, name + ": no appendix without a body")
            for (let p = 0; p < info.length; ++p) {
                reg.printPage = p
                wait(20)
                const pg = info[p]
                verify(pg.used <= pg.room + 0.5, name + " p" + (p + 1) + ": its atoms reach " + Math.round(pg.used - pg.room) + " px into the folio band")
                // a continued page that ends its section is never stranded (PpPager.minTail): a quarter page at least
                if (pg.cont !== "")
                    verify(pg.used >= 0.25 * pg.room, name + " p" + (p + 1) + " (" + pg.section + ", continued) is stranded: " + Math.round(100 * pg.used / pg.room) + "% full")
                if (!pg.sectionEnds)
                    verify(pg.used >= 0.25 * pg.room, name + " p" + (p + 1) + " (" + pg.section + ") is less than a quarter full and is not its section's last page")
            }
            if (info.length > 1)
                compare(info[1].cont, "", name + ": the cover runs onto page 2")
            const textW = reg.galleyPager.textW, gal = galley()
            for (const t of texts(gal, [], false)) {
                if (t.text === "") continue
                const what = name + ": \"" + String(t.text).slice(0, 48) + "\""
                if (t.fitted !== true) verify(!t.truncated, what + " is truncated outside the cover")
                if (t.width > 0) verify(t.contentWidth <= t.width + 1.5, what + " is wider than its box (" + Math.round(t.contentWidth) + " > " + Math.round(t.width) + ")")
                const x = t.mapToItem(gal, 0, 0).x
                verify(x >= -1.5 && x + Math.max(t.width, t.contentWidth) <= textW + 1.5, what + " leaves the text width")
            }
            if (tc.expectedPages[name] !== undefined)
                compare(reg.clientPageCount, tc.expectedPages[name], name + ": page count")
        }

        function test_01_everyFixtureHoldsTheLayoutContract_data() { return Fx.names.map(n => ({ tag: n, name: n })) }
        function test_01_everyFixtureHoldsTheLayoutContract(row) {
            load(row.name)
            checkLayout(row.name)
        }

        // rule 6, empty means absent: a block with nothing to say is not drawn, nor its heading
        function test_02_emptyMeansAbsent() {
            load("nick")
            verify(shown("reportCoverBall"), "nick: the ball on the cover")
            verify(shown("reportCover_workOns") && shown("reportCover_found") && shown("reportCover_strengths"))
            verify(!shown("reportCover_watching"), "nick has habits: no watch list on the cover")
            verify(!shown("reportCause_1_seen"), "work-on 1: nothing spotted, so no What we spotted")
            verify(shown("reportCause_1_ruledOut"))
            verify(!shown("reportCameWith2"), "work-on 2: nothing came with it, so no What came with it")
            verify(shown("reportCameWith1"))
            verify(!shown("reportBallMoved"), "within the caps the ball never leaves the cover")
            verify(shown("reportOrder_links") && shown("reportOrder_notListed") && shown("reportOrder_checks"))
            verify(!shown("reportHeld"), "nothing held")

            load("jess")
            verify(!shown("reportOrder_links"), "jess: no links, so no Linked, not proven")
            verify(!shown("reportFeel1"), "jess work-on 1 has no feel: no The feel line")
            verify(!shown("reportCheck1"), "jess work-on 1 has no self-check")
            verify(!shown("reportToDo1"), "jess work-on 1 has nothing to do yet: no What to do heading")

            load("clean")
            verify(!shown("reportWorkOn1") && !shown("reportOrder"), "clean: no work-on pages, no order page")
            verify(!shown("reportCover_workOns") && !shown("reportCover_found"))
            verify(shown("reportCover_watching") && shown("reportCover_strengths"))
            verify(shown("reportGrid") && shown("reportNumbers"))
            verify(!shown("reportPractice"), "clean: no practice steps for a session with nothing to practise")

            load("no-launch-monitor")
            verify(!shown("reportCoverBall") && !shown("reportNumbers"), "no launch monitor: no ball, no numbers")
            verify(shown("reportPractice"))
            verify(!shown("reportEarlierToday"), "no earlier sessions: no Earlier today")

            load("junior-no-photos")
            compare(stills(galley(), []).length, 0, "no approval: no still anywhere")
            verify(!shown("reportReferenceSwing"), "no approval: no reference swing")
            verify(shown("reportHeld"), "a held habit is named on the back page")
            for (let p = 0; p < reg.printPageCount; ++p) { reg.printPage = p; verify(!reg.printPageHasPhoto, "junior p" + (p + 1) + " has no photograph") }

            load("nick")
            verify(!shown("reportLaunchMonitor") && !shown("reportLaunchMonitorShots") && !shown("reportLaunchMonitorStats"),
                   "no lmAppendix in the map: no launch monitor appendix")
            load("launch-monitor-appendix")
            verify(shown("reportLaunchMonitor") && shown("reportLaunchMonitorShots") && shown("reportLaunchMonitorStats"),
                   "the launch monitor appendix: figures, every shot, the summary")
            compare(reg.insideLine.indexOf("launch monitor"), -1, "the appendices stay off the cover's contents (the studio's contact keeps its room)")
            verify(reg.sectionPage("reportLaunchMonitorShots") > reg.sectionPage("reportLaunchMonitor")
                   && reg.sectionPage("reportLaunchMonitorStats") > reg.sectionPage("reportLaunchMonitorShots"),
                   "figures, then the shot tables, then the summary, each from its own page")

            // the Posture page (posture_chart_design.md §5.7): only with the map's `posture`; after the work-ons, before
            // the back matter; every block it has words for, nothing it has not
            load("nick")
            verify(!shown("reportPosture") && !shown("reportPostureProfile"), "no posture in the map: no Posture page")
            load("posture")
            verify(shown("reportPosture") && shown("reportPostureProfile") && shown("reportPostureStrip") && shown("reportPostureTrust"),
                   "the Posture page: the head, the profile, the pattern by shot, the trust note")
            compare(findAll(reg, "reportPostureRow", []).filter(it => it.visible).length, Fx.maps["posture"].posture.profile.rows.length, "a row per reading")
            compare(findAll(reg, "reportPostureCell", []).filter(it => it.visible).length, 8 * Fx.maps["posture"].posture.profile.rows.length, "eight cells a row")
            verify(findAll(reg, "reportPostureCell", []).every(c => c.text !== ""), "every cell says a figure or \"-\"")
            // At address (§7.4.2): the block between the profile and What moved, a row per reading, the ⚠ on each
            const adm = Fx.maps["posture"].posture.address
            verify(!!adm && adm.rows.length > 0, "the posture fixture carries the At address block")
            verify(shown("reportPostureAddress"), "the At address block on the Posture page")
            const arows = findAll(reg, "reportPostureAddressRow", []).filter(it => it.visible)
            compare(arows.length, adm.rows.length, "a row per address reading")
            verify(arows.every(r => r.warned && r.text !== ""), "each says its figure (or \"-\") and carries the ⚠")
            verify(shown("reportPostureMoved") === Fx.maps["posture"].posture.moved.length > 0, "What moved only with a line")
            verify(shown("reportPostureDepartures") === Fx.maps["posture"].posture.departures.length > 0, "Departures only with a departure")
            // the picture strip (stage 5): the typical swing's frames, each with its lines; the frames' marks are the map's
            const pic = Fx.maps["posture"].posture.picture
            verify(!!pic, "the posture fixture carries a picture")
            verify(shown("reportPosturePicture"), "the picture strip, between Repeatable? and the trust note")
            const pframes = findAll(reg, "reportPosturePictureFrame", []).filter(it => it.visible)
            compare(pframes.length, pic.groups.reduce((a, g) => a + g.frames.length, 0), "a frame per picture frame")
            verify(pframes.every(f => f.coachDrawn > 0), "every frame carries its frozen address lines")
            compare(pframes.filter(f => f.bandDrawn > 0).length, pic.groups.filter(g => g.frames.some(f => f.marks.some(m => m.role === "band"))).length,
                    "the band only at impact")
            // one pager atom per camera, each with its own heading, so the pager can break between them (a page is never
            // left a quarter empty by one strip too tall to follow the text)
            const pgroups = findAll(reg, "reportPosturePictureGroup", []).filter(it => it.visible)
            compare(pgroups.length, 2, "two picture atoms: down the line, face-on")
            compare(pgroups.map(g => g.camera).join(","), pic.groups.map(g => g.camera).join(","), "down the line, then face-on")
            verify(pgroups.every(g => g.atom === true), "each camera is its own pager atom")
            verify(findAll(reg, "reportPosturePicture", []).every(c => c.atom !== true), "the strip itself is no longer one atom")
            for (let i = 0; i < pgroups.length; ++i) {
                const heads = findAll(pgroups[i], "reportPosturePictureHeading", []).filter(it => it.visible)
                compare(heads.length, 1, "camera " + (i + 1) + ": its own heading")
                verify(heads[0].text.indexOf(pic.groups[i].label) > 0, "the heading names its camera: " + heads[0].text)
                compare(findAll(pgroups[i], "reportPosturePictureFrame", []).filter(it => it.visible).length, pic.groups[i].frames.length,
                        "camera " + (i + 1) + ": its own frames")
            }
            verify(reg.sectionPage("reportPosture") > reg.sectionPage("reportWorkOn1"), "after the work-on pages")
            verify(reg.sectionPage("reportPosture") < reg.sectionPage("reportPractice"), "before Between now and next time")
            compare(reg.insideLine.indexOf("posture"), -1, "the cover's contents line keeps its room")

            load("note")
            compare(reg.printPageCount, 1, "the note is one page")
            verify(!shown("reportGrid") && !shown("reportAbout") && !shown("reportWorkOn1"))
            verify(shown("reportNoteBall") && shown("reportNumbers"))
        }

        // the page that draws a still is the page the PDF writer stores as JPEG; the cover never is
        function test_03_photoPages() {
            load("nick")
            reg.printPage = 0
            verify(!reg.printPageHasPhoto, "the cover has no photograph")
            const loaded = stills(galley(), []).filter(s => s.isPhoto)
            if (loaded.length === 0) skip("the share's stills are not reachable here")
            for (const s of loaded) {
                reg.printPage = reg.galleyPager.pageOf(s) - 1
                wait(20)
                verify(reg.printPageHasPhoto, "page " + (reg.printPage + 1) + " draws a still")
            }
        }

        // §7.1: the report prints in Folio light whatever the app's theme, without the app re-theming to print
        // the pager's galley (inside its clipped page window): the report's own blocks, without the pager's furniture
        function galley() {
            const win = (reg.galleyPager.children || []).find(k => k.clip === true)
            return (win.children || []).find(k => k.spacing !== undefined)
        }
        function appearance() {
            const out = []
            function walk(it) {
                const kids = it === reg.galleyPager ? [galley()] : (it.children || [])
                for (let i = 0; i < kids.length; ++i) {
                    const c = kids[i]
                    if (!c || !c.visible) continue
                    const s = String(c)
                    if (s.indexOf("PpRunningHead") === 0 || s.indexOf("PpFolio") === 0) continue
                    if (c.text !== undefined && c.font !== undefined) out.push("T " + c.font.family + " " + c.font.pixelSize + " " + c.font.weight + " " + c.color)
                    else if (c.border !== undefined && c.radius !== undefined) out.push("R " + c.color + " " + c.border.color)
                    walk(c)
                }
            }
            walk(reg)
            return out
        }
        function test_04_theReportIgnoresTheAppTheme() {
            const start = Theme.themeIndex
            Theme.themeIndex = 12                       // Folio light
            load("nick")
            reg.printPage = 5; wait(50)
            const a = appearance()
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            const b = appearance()
            Theme.themeIndex = start
            verify(a.length > 100, "the walk found the page")
            compare(b.length, a.length)
            for (let i = 0; i < a.length; ++i) compare(b[i], a[i], "item " + i + " changed with the app theme")
        }
        // the Posture page: its own type and inks (the report's), the metric colours in Folio light, the box plot through
        // the Charts print roles in the same fixed aesthetic — on every page of the section
        function test_06_thePosturePageIgnoresTheAppTheme() {
            const start = Theme.themeIndex
            Theme.themeIndex = 12                       // Folio light
            load("posture")
            const pages = reg.pageInfo().map((p, i) => p.section === "Posture" ? i : -1).filter(i => i >= 0)
            verify(pages.length > 0, "the Posture section has a page")
            const a = [], b = []
            for (const p of pages) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (const p of pages) { reg.printPage = p; wait(30); b.push(appearance()) }
            Theme.themeIndex = start
            for (let k = 0; k < a.length; ++k) {
                compare(b[k].length, a[k].length, "posture page " + (pages[k] + 1) + ": items")
                for (let i = 0; i < a[k].length; ++i) compare(b[k][i], a[k][i], "posture page " + (pages[k] + 1) + " item " + i + " changed with the app theme")
            }
        }
        // ── the Wrist page (session_report_design.md "The Wrist page"; report_wrist.h) ──
        function visibleAll(name) { return findAll(reg, name, []).filter(it => it.visible && it.height > 0) }
        // a copy of the wrist fixture's map with its `wrist` block changed by `edit`
        function wristMap(edit) {
            const m = JSON.parse(JSON.stringify(Fx.maps["wrist"]))
            if (edit) edit(m.wrist, m)
            return m
        }
        function loadMap(m) {
            reg.report = null
            reg.printPage = 0
            wait(30)
            reg.report = m
            let last = "", same = 0
            for (let i = 0; i < 80 && same < 3; ++i) {
                wait(100)
                const s = signature()
                same = (s === last) ? same + 1 : 0
                last = s
            }
            verify(same >= 3, "the pagination never settled")
        }
        // n swings: the score run, the style table and every finding's marks carried out to n from the fixture's own
        function longSession(w, n) {
            const pts = w.scoreRun.points, cols = w.styles.columns
            w.scoreRun.points = Array.from({ length: n }, (_, i) => Object.assign({}, pts[i % pts.length], { swing: i + 1 }))
            w.styles.columns = Array.from({ length: n }, (_, i) => ({ swing: i + 1, label: "Swing " + (i + 1), short: String(i + 1) }))
            w.styles.rows.forEach(r => { const c = r.cells; r.cells = Array.from({ length: n }, (_, i) => c[i % c.length]) })
            w.recurring.forEach(f => { const k = f.marks; f.marks = Array.from({ length: n }, (_, i) => ({ swing: i + 1, fired: k[i % k.length].fired })) })
            return w
        }
        function test_07_theWristPage() {
            const w = Fx.maps["wrist"].wrist
            // absent: no `wrist` in the map, no page, no heading
            load("posture")
            verify(!shown("reportWrist") && !shown("reportWristProfile") && !shown("reportWristTrust"), "no wrist in the map: no Wrist page")
            compare(reg.sectionPage("reportWrist"), 0)
            loadMap(wristMap((x, m) => { delete m.wrist }))
            verify(!shown("reportWrist"), "the wrist fixture without its block: no Wrist page")
            // present: every block the block has words for, in the Posture page's order
            load("wrist")
            for (const n of ["reportWrist", "reportWristProfile", "reportWristScore", "reportWristStyles", "reportWristRecurring", "reportWristRepeat",
                             "reportWristMoves", "reportWristTrust"])
                verify(shown(n), "the Wrist page draws " + n)
            compare(visibleAll("reportWristRow").length, w.profile.rows.length, "a row per wrist DOF")
            const cells = visibleAll("reportWristCell")
            compare(cells.length, 8 * w.profile.rows.length, "eight cells a row")
            verify(cells.every(c => c.text !== "" && c.text !== "0"), "every cell a figure with its unit, or \"-\"")
            compare(cells.map(c => c.text).join(","), w.profile.rows.map(r => r.cells.map(c => c.text).join(",")).join(","), "the cells say the block's medians, verbatim")
            compare(cells.filter(c => c.outside).length, w.profile.rows.reduce((a, r) => a + r.cells.filter(c => c.outside).length, 0), "outside where the block says")
            compare(visibleAll("reportWristStylesLine").length, Math.ceil(w.styles.columns.length / 15), "fifteen swings a line")
            compare(visibleAll("reportWristStyleCell").filter(c => c.graded).length, w.styles.columns.length, "one style in bold per swing")
            compare(visibleAll("reportWristFinding").length, w.recurring.length, "a finding per recurring condition")
            compare(visibleAll("reportWristMark").filter(k => k.fired).length, w.recurring.reduce((a, f) => a + f.marks.filter(k => k.fired).length, 0), "filled where it fired")
            compare(visibleAll("reportWristMovesRow").length, w.movesWith.rows.length, "what moves with: a row per reading")
            compare(visibleAll("reportWristTrustLine").map(t => t.text).join("\n"), w.trust.join("\n"), "the trust lines, open and verbatim")
            verify(shown("reportWristScoreLine"), "the score line in the head")
            // the head's style line is the resemblance clause alone (never the counts twice); absent with no index
            verify(shown("reportWristStyleLine") === !!w.styleLine)
            verify(reg.sectionPage("reportWrist") > reg.sectionPage("reportPosture"), "after the Posture page")
            verify(reg.sectionPage("reportWrist") < reg.sectionPage("reportPractice"), "before Between now and next time")
            compare(reg.insideLine.indexOf("wrist"), -1, "not on the cover's contents line")
            const pages = reg.pageInfo().map((p, i) => p.section === "Wrist" ? i : -1).filter(i => i >= 0)
            verify(pages.length >= 1, "the Wrist section has a page")
            compare(reg.pageInfo()[pages[0]].cont, "", "it opens on a fresh page")
            // no index: the note alone, no table, no style line
            loadMap(wristMap(x => { x.styleLine = ""; x.styles = { columns: [], rows: [], counts: [], hasIndex: false, more: "", legend: "", countsLine: "",
                                                                   note: "No swing in this session carries a resemblance index, so there is no style to compare." } }))
            verify(shown("reportWristStyles"), "the style block says the note")
            compare(visibleAll("reportWristStylesLine").length, 0, "and draws no table")
            verify(!shown("reportWristStyleLine"), "no style line in the head")
            // refused: the refusal alone under the heading
            loadMap(wristMap(x => { x.movesWith = { available: false, head: "", sub: "", rows: [], chanceLine: "", note: "",
                                                    refusal: "No swing of this session reads the lead wrist's bow / cup." } }))
            compare(visibleAll("reportWristMovesRow").length, 0)
            compare(visibleAll("reportWristMovesRefusal").map(t => t.text).join(), "No swing of this session reads the lead wrist's bow / cup.")
            // nothing fired: the empty words alone
            loadMap(wristMap(x => { x.recurring = []; x.recurringLegend = "" }))
            verify(shown("reportWristRecurring"), "What keeps firing says nothing fired")
            compare(visibleAll("reportWristFinding").length, 0)
        }
        // the register's caps (a guard: the map caps first), over-long lists cut at them
        function test_08_theWristPageCaps() {
            loadMap(wristMap(x => {
                longSession(x, 130)
                const r = x.profile.rows
                while (x.profile.rows.length < 7) x.profile.rows.push(JSON.parse(JSON.stringify(r[x.profile.rows.length % 3])))
                const f = x.recurring
                while (x.recurring.length < 10) x.recurring.push(JSON.parse(JSON.stringify(f[x.recurring.length % f.length])))
                const mr = x.movesWith.rows
                while (x.movesWith.rows.length < 9) x.movesWith.rows.push(JSON.parse(JSON.stringify(mr[0])))
                while (x.trust.length < 20) x.trust.push("Trust line " + x.trust.length + ".")
            }))
            compare(visibleAll("reportWristRow").length, 5, "five rows at most")
            compare(visibleAll("reportWristStylesLine").length, 8, "120 swings at most in the style table: eight lines")
            compare(visibleAll("reportWristFinding").length, 8, "eight findings at most")
            verify(visibleAll("reportWristFinding").every(f => findAll(f, "reportWristMark", []).filter(k => k.visible).length === 120), "120 marks a finding at most")
            compare(visibleAll("reportWristMovesRow").length, 7, "seven readings at most")
            compare(visibleAll("reportWristTrustLine").length, 16, "sixteen trust lines at most")
        }
        // a long session runs on: "Wrist, continued" on every later page, the layout contract on every page
        function test_09_aLongSessionContinuesTheWristPage() {
            loadMap(wristMap(x => longSession(x, 40)))
            const info = reg.pageInfo()
            const pages = info.map((p, i) => p.section === "Wrist" ? i : -1).filter(i => i >= 0)
            verify(pages.length >= 2, "forty swings run onto a second page (" + pages.length + ")")
            verify(pages.slice(1).every(i => info[i].cont === "Wrist"), "each later page says Wrist, continued")
            compare(visibleAll("reportWristStylesLine").length, 3, "forty swings: three lines of the style table")
            verify(visibleAll("reportWristFinding").every(f => findAll(f, "reportWristMark", []).filter(k => k.visible).length === 40), "forty marks a finding")
            checkLayout("wrist-40")
            for (const i of pages) {
                reg.printPage = i; wait(30)
                const cont = findAll(reg, "reportContinued", []).filter(t => t.visible)
                if (info[i].cont !== "") verify(cont.length === 1 && cont[0].text === "Wrist, continued", "p" + (i + 1) + ": Wrist, continued")
            }
        }
        // the Wrist page in its own inks whatever the app's theme, as the Posture page
        function test_10_theWristPageIgnoresTheAppTheme() {
            const start = Theme.themeIndex
            Theme.themeIndex = 12                       // Folio light
            load("wrist")
            const pages = reg.pageInfo().map((p, i) => p.section === "Wrist" ? i : -1).filter(i => i >= 0)
            verify(pages.length > 0, "the Wrist section has a page")
            const a = [], b = []
            for (const p of pages) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (const p of pages) { reg.printPage = p; wait(30); b.push(appearance()) }
            Theme.themeIndex = start
            for (let k = 0; k < a.length; ++k) {
                compare(b[k].length, a[k].length, "wrist page " + (pages[k] + 1) + ": items")
                for (let i = 0; i < a[k].length; ++i) compare(b[k][i], a[k][i], "wrist page " + (pages[k] + 1) + " item " + i + " changed with the app theme")
            }
        }
        // the appendix too: its tables (Folio light from the register) and the launch monitor panel's own figures,
        // embedded with their fixed aesthetic, on every appendix page
        function test_05_theLaunchMonitorAppendixIgnoresTheAppTheme() {
            const start = Theme.themeIndex
            Theme.themeIndex = 12                       // Folio light
            load("launch-monitor-appendix")
            const first = reg.sectionPage("reportLaunchMonitor") - 1
            verify(first > 0, "the appendix has a page")
            const a = [], b = []
            for (let p = first; p < reg.clientPageCount; ++p) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (let p = first; p < reg.clientPageCount; ++p) { reg.printPage = p; wait(30); b.push(appearance()) }
            Theme.themeIndex = start
            compare(b.length, a.length)
            for (let p = 0; p < a.length; ++p) {
                compare(b[p].length, a[p].length, "appendix page " + (first + p + 1) + ": items")
                for (let i = 0; i < a[p].length; ++i) compare(b[p][i], a[p][i], "appendix page " + (first + p + 1) + " item " + i + " changed with the app theme")
            }
        }
    }
}
