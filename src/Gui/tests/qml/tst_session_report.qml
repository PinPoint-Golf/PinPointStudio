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
// Every fixture is a report map as SessionReportModel writes it (session_report_model.cpp and the report_*.h blocks), made by
// tools/session-report-mock/fixtures.sh (build_fixtures.py; its header says where each case comes from): every case is
// the CURRENT SessionReportModel's map of a library session (nick, jess, no-launch-monitor, and each later page's block)
// or a stress transform of Nick's map that changes only what the case stresses, so the words and numbers are real. The .json files beside fixtures.js are the canonical copies; this suite reads the JS
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
// Every still is one of two neutral placeholder JPEGs in data/session_report/stills/ (loadMap resolves them), so the
// photo-page assertion always has a still that loaded; it still skips if none did. Silent, no window.
Item {
    id: probe
    width: 794; height: 1123

    PpSessionReport { id: reg; width: 794; height: 1123 }

    TestCase {
        id: tc
        name: "SessionReport"
        when: windowShown

        // pages per fixture (no appendix), recorded on 11 Nov 2026 when the fixtures moved onto the model's own maps
        // (tools/session-report-mock/build_fixtures.py: nick = Nick Henn 20 Oct session 3, jess = Jess Ratcliffe 20 Oct
        // session 3, no-launch-monitor = Mark Liversedge 9 Sep; the transforms are of nick; a page case is one session's own map)
        readonly property var expectedPages: ({
            "nick": 13, "jess": 13, "clean": 4, "one-work-on": 6, "five-work-ons-longest-text": 16,
            "causes-at-caps": 15, "sixty-swings-three-clubs": 16, "junior-no-photos": 12,
            "longest-names-and-note": 14, "note": 1, "note-one-swing": 1, "no-launch-monitor": 11,
            "launch-monitor-appendix": 17,      // nick + the launch monitor appendix (Nick's readings)
            "posture": 17,                      // nick + the Posture page (Nick's posture readings, with its picture strip)
            "wrist": 15,                        // Mark Liversedge's 18 Aug Wrist_02: its own map + its Posture and Wrist pages (fixtures.sh rich)
            "delivery": 18,                     // Jess Ratcliffe's 20 Oct Wrist_01: its own map + its Posture, Wrist and Club delivery pages
                                                // (the fit over her day: fixtures.sh jess — Club delivery ONE page, the trust note in two columns)
            "consistency": 15,                  // nick + the Consistency page (Nick's: two flagged swings)
            "consistency-mixed-clubs": 10,      // Mark Liversedge's 20 Oct Wrist_01: its own map + its mixed-club Consistency page
            "long-session": 13,                 // Mark Liversedge's 7 Oct: 50 swings, five work-ons, no launch monitor
            "tendencies": 14                    // nick + the Your tendencies page (Nick's)
        })

        // The app theme the ignores-the-app-theme tests switch, put back after each of them —
        // in cleanup(), so a failed check part-way still restores it (issue #171). Every other test
        // here draws the report's own fixed aesthetic and needs no app theme.
        property int themeWas: 0
        function initTestCase() { themeWas = Theme.themeIndex }
        function cleanup() { Theme.themeIndex = themeWas }

        function signature() { return JSON.stringify(reg.pageInfo().map(p => [Math.round(p.from), Math.round(p.to)])) }
        // lay a fixture out and wait for the pager (and any one-shot tightening) to settle
        function load(name) { loadMap(Fx.maps[name], name) }
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
        // a block is drawn exactly when the map has something for it — the cover, every work-on's blocks, the order page,
        // the back page — read off the fixture, so a model dump that gains or loses a block keeps the check honest
        function checkBlocksFollowTheMap(name) {
            const m = Fx.maps[name]
            const has = a => (a || []).length > 0
            compare(shown("reportCoverBall"), !!m.ball, name + ": the ball on the cover only with the ball")
            compare(shown("reportCover_workOns"), has(m.workOnsBrief), name + ": the work-ons on the cover")
            compare(shown("reportCover_found"), has(m.found), name + ": what we found")
            compare(shown("reportCover_strengths"), has(m.strengths), name + ": what went well")
            compare(shown("reportCover_watching"), has(m.watching), name + ": the watch list")
            compare(shown("reportBallMoved"), !!(m.ball && m.ball.more), name + ": the ball leaves the cover only past its caps")
            for (const w of m.workOns) {
                const k = w.rank, rows = (w.causes && w.causes.rows) || []
                for (const kind of ["seen", "possible", "ruledOut"])
                    compare(shown("reportCause_" + k + "_" + kind), rows.some(r => r.kind === kind && has(r.items)), name + " work-on " + k + ": " + kind + " only with its items")
                compare(shown("reportCameWith" + k), !!w.cameWith, name + " work-on " + k + ": What came with it only with its words")
                compare(shown("reportFeel" + k), !!w.cue, name + " work-on " + k + ": The feel only with a cue")
                compare(shown("reportCheck" + k), !!w.check, name + " work-on " + k + ": Check it yourself only with a check")
                compare(shown("reportToDo" + k), !!w.cue || has(w.drills) || !!w.check, name + " work-on " + k + ": What to do only with something to do")
            }
            if (m.order) {
                compare(shown("reportOrder_links"), has(m.order.links), name + ": Linked, not proven only with a link")
                compare(shown("reportOrder_notListed"), has(m.order.notListed), name + ": Not on the list")
            }
            compare(shown("reportOrder_checks"), has(m.checks), name + ": Worth checking next time")
            compare(shown("reportHeld"), has(m.held), name + ": a held habit named on the back page")
        }
        function test_02_emptyMeansAbsent() {
            // the two model bases between them draw every block one way or the other (the fixture facts first, so a
            // re-generated base that loses one fails here rather than quietly testing less)
            const nk = Fx.maps["nick"], js = Fx.maps["jess"]
            verify(nk.order.links.length === 0 && js.order.links.length > 0, "jess's map has order links, nick's none")
            verify(nk.held.length === 0 && js.held.length > 0, "jess's map holds a habit, nick's none")
            verify(nk.workOns.some(w => !w.cameWith) && nk.workOns.some(w => !!w.cameWith), "nick: a work-on with and one without What came with it")
            verify(nk.workOns.some(w => !w.causes.rows.some(r => r.kind === "seen")) && nk.workOns.some(w => w.causes.rows.some(r => r.kind === "seen")),
                   "nick: a work-on with and one without What we spotted")
            load("nick")
            checkBlocksFollowTheMap("nick")
            load("jess")
            checkBlocksFollowTheMap("jess")
            const links = findAll(reg, "reportOrder_links", []).filter(it => it.visible)
            const said = texts(links[0], [], false).map(t => t.text).join(" ")
            for (const l of js.order.links)
                verify(said.indexOf(l.from) >= 0 && said.indexOf(l.to) >= 0, "Linked, not proven names " + l.from + " and " + l.to + " (" + said.slice(0, 200) + ")")
            // a work-on with nothing authored to do: no feel, no self-check, no What to do heading
            loadMap(editedMap("jess", "workOns", x => { x[0].cue = ""; x[0].check = ""; x[0].drills = [] }))
            verify(!shown("reportFeel1") && !shown("reportCheck1") && !shown("reportToDo1"), "jess work-on 1 with nothing to do: no What to do")

            load("clean")
            checkBlocksFollowTheMap("clean")
            verify(!shown("reportWorkOn1") && !shown("reportOrder"), "clean: no work-on pages, no order page")
            verify(Fx.maps["clean"].watching.length > 0, "the clean case carries a watch list")
            verify(shown("reportGrid") && shown("reportNumbers"))
            verify(!shown("reportPractice"), "clean: no practice steps for a session with nothing to practise")

            load("no-launch-monitor")
            checkBlocksFollowTheMap("no-launch-monitor")
            verify(!Fx.maps["no-launch-monitor"].ball && !Fx.maps["no-launch-monitor"].numbers && !Fx.maps["no-launch-monitor"].today,
                   "the no-launch-monitor case is a session with no monitor and no earlier session that day")
            verify(!shown("reportCoverBall") && !shown("reportNumbers"), "no launch monitor: no ball, no numbers")
            verify(shown("reportPractice"))
            verify(!shown("reportEarlierToday"), "no earlier sessions: no Earlier today")
            load("nick")
            verify(shown("reportEarlierToday") && shown("reportReferenceSwing"), "nick: Earlier today and the reference swing")

            load("junior-no-photos")
            compare(stills(galley(), []).length, 0, "no approval: no still anywhere")
            verify(!shown("reportReferenceSwing"), "no approval: no reference swing")
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
            verify(shown("reportPosturePicture"), "the picture strip, between Consistency and the trust note")
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
            Theme.themeIndex = 12                       // Folio light
            load("nick")
            reg.printPage = 5; wait(50)
            const a = appearance()
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            const b = appearance()
            verify(a.length > 100, "the walk found the page")
            compare(b.length, a.length)
            for (let i = 0; i < a.length; ++i) compare(b[i], a[i], "item " + i + " changed with the app theme")
        }
        // the Posture page: its own type and inks (the report's), the metric colours in Folio light, the box plot through
        // the Charts print roles in the same fixed aesthetic — on every page of the section
        function test_06_thePosturePageIgnoresTheAppTheme() {
            Theme.themeIndex = 12                       // Folio light
            load("posture")
            const pages = reg.pageInfo().map((p, i) => p.section === "Posture" ? i : -1).filter(i => i >= 0)
            verify(pages.length > 0, "the Posture section has a page")
            const a = [], b = []
            for (const p of pages) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (const p of pages) { reg.printPage = p; wait(30); b.push(appearance()) }
            for (let k = 0; k < a.length; ++k) {
                compare(b[k].length, a[k].length, "posture page " + (pages[k] + 1) + ": items")
                for (let i = 0; i < a[k].length; ++i) compare(b[k][i], a[k][i], "posture page " + (pages[k] + 1) + " item " + i + " changed with the app theme")
            }
        }
        // ── consistency on the Posture and Wrist pages (Mark's rulings R7, R9, R11, R20; report_posture.h, report_wrist.h) ──
        // a copy of a fixture's map with one block changed by `edit`
        function editedMap(name, key, edit) {
            const m = JSON.parse(JSON.stringify(Fx.maps[name]))
            if (edit) edit(m[key], m)
            return m
        }
        // the variability badges drawn inside an item (a badge is a PpVariabilityBadge: its grade and its word)
        function badgesIn(item, out) {
            const kids = item.children || []
            for (let i = 0; i < kids.length; ++i) {
                const c = kids[i]
                if (!c || !c.visible) continue
                if (c.grade !== undefined && c.word !== undefined && c.contentLayer !== undefined) out.push(c)
                else badgesIn(c, out)
            }
            return out
        }
        // ROW.rowGrades beside each reading's name: one golfer-layer badge per club the row is graded for, in the map's
        // order and with the map's grade, each saying its word (colour is never the only channel); none for an ungraded club
        function checkRowGrades(prefix, rows, what) {
            const drawn = visibleAll(prefix + "Row")
            compare(drawn.length, rows.length, what + ": a row per reading")
            for (const r of rows) {
                const row = drawn.find(d => d.key === r.key)
                verify(!!row, what + ": the row " + r.key + " is drawn")
                const want = (r.rowGrades || []).filter(g => g.result && g.result.graded === true)
                const got = findAll(row, prefix + "Grade", []).filter(b => b.visible)
                compare(got.length, want.length, what + " " + r.key + ": a badge per graded club")
                compare(got.map(b => b.grade).join(","), want.map(g => g.result.grade).join(","), what + " " + r.key + ": the map's grades")
                verify(got.every(b => b.word !== ""), what + " " + r.key + ": every badge says its word")
                compare(findAll(row, prefix + "Grades", []).filter(f => f.visible).map(f => f.club).join(","), want.map(g => g.club).join(","),
                        what + " " + r.key + ": the clubs graded")
            }
        }
        function test_13_thePostureRowsCarryTheirConsistency() {
            const p = Fx.maps["posture"].posture
            verify(p.profile.rows.length > 0 && p.profile.rows.every(r => (r.rowGrades || []).some(g => g.result && g.result.graded)),
                   "the posture fixture grades every row (report_posture.h ROW.rowGrades)")
            load("posture")
            checkRowGrades("reportPosture", p.profile.rows, "posture")
            compare(findAll(reg, "reportPostureGrade", []).filter(b => b.visible).length,
                    p.profile.rows.reduce((a, r) => a + r.rowGrades.filter(g => g.result && g.result.graded).length, 0), "no badge the map has not")
            // an ungraded club draws no badge; a mixed session draws one per club, each named
            loadMap(editedMap("posture", "posture", x => {
                x.profile.rows[0].rowGrades[0].result.graded = false
                const g = JSON.parse(JSON.stringify(x.profile.rows[1].rowGrades[0]))
                x.profile.rows[1].rowGrades = [Object.assign({}, g, { club: "7 IRON", named: true }),
                                               Object.assign({}, g, { club: "DRIVER", named: true, result: Object.assign({}, g.result, { grade: 2 }) })]
            }))
            const r0 = visibleAll("reportPostureRow").find(d => d.key === p.profile.rows[0].key)
            compare(findAll(r0, "reportPostureGrade", []).filter(b => b.visible).length, 0, "an ungraded row: no badge")
            const r1 = visibleAll("reportPostureRow").find(d => d.key === p.profile.rows[1].key)
            compare(findAll(r1, "reportPostureGrades", []).filter(f => f.visible).map(f => f.club).join(","), "7 IRON,DRIVER", "a badge per club, in order")
            compare(findAll(r1, "reportPostureGrade", []).filter(b => b.visible).map(b => b.grade).join(","),
                    p.profile.rows[1].rowGrades[0].result.grade + ",2", "each club its own grade")
        }
        // "Consistency" on the Posture page: per club, the most and the least consistent position, each with its badge;
        // a club too few to grade says so once; no line, no block
        function test_14_thePosturePageConsistency() {
            const c = Fx.maps["posture"].posture.consistency || []
            verify(c.length > 0 && c.some(l => l.tooFew !== true && l.most && l.least), "the posture fixture carries a graded Consistency line")
            load("posture")
            verify(shown("reportPostureRepeat"), "the Consistency block on the Posture page")
            const lines = visibleAll("reportPostureRepeatLine")
            compare(lines.length, c.length, "a line per club")
            compare(lines.map(l => l.club).join(","), c.map(l => l.club).join(","), "the map's clubs, in order")
            for (let i = 0; i < c.length; ++i) {
                const said = texts(lines[i], [], false).map(t => t.text).join(" ")
                const b = badgesIn(lines[i], [])
                if (c[i].tooFew === true) {
                    compare(b.map(x => x.grade).join(","), String(c[i].result.grade), c[i].club + ": too few, one badge")
                } else {
                    verify(said.indexOf(c[i].mostText) >= 0 && said.indexOf(c[i].leastText) >= 0, c[i].club + ": says " + c[i].mostText + " and " + c[i].leastText + " (" + said + ")")
                    compare(b.map(x => x.grade).join(","), c[i].most.grade + "," + c[i].least.grade, c[i].club + ": Most, then Least, with the map's grades")
                }
                verify(b.every(x => x.word !== ""), c[i].club + ": every badge says its word")
                compare(said.indexOf(ClubFormat.display(c[i].club)) >= 0, c[i].named === true, c[i].club + ": named only in a mixed session")
            }
            // a club too few to grade: the one badge and no Most / Least
            loadMap(editedMap("posture", "posture", x => {
                x.consistency = [{ club: "7 IRON", named: false, tooFew: true, mostText: "", leastText: "", most: null, least: null,
                                   result: { graded: true, grade: 0, measurementLimited: false, basis: "scalar", basisText: "", domain: "consistency", ungradedReason: "tooFewSwings" } }]
            }))
            const tl = visibleAll("reportPostureRepeatLine")
            compare(tl.length, 1)
            compare(badgesIn(tl[0], []).map(x => x.grade).join(","), "0", "too few: one badge")
            verify(texts(tl[0], [], false).every(t => t.text.indexOf("Most") < 0 && t.text.indexOf("Least") < 0), "too few: no Most or Least")
            // nothing to say: no block, no heading
            loadMap(editedMap("posture", "posture", x => { x.consistency = [] }))
            verify(!shown("reportPostureRepeat"), "no consistency line: no Consistency block")
        }

        // a work-on's consistency over the session (R10): beside What we saw, the word "Consistency" and one golfer-layer badge
        // with the map's grade and its word; nothing for an ungraded work-on
        function test_15_theWorkOnConsistencyBadge() {
            const m = Fx.maps["nick"]
            const graded = w => !!w.consistency && w.consistency.graded === true
            verify(m.workOns.some(graded) && m.workOns.some(w => !graded(w)), "nick has a graded work-on and an ungraded one")
            load("nick")
            for (const w of m.workOns) {
                const rows = findAll(reg, "reportWorkOnConsistency" + w.rank, []).filter(it => it.visible)
                compare(rows.length, graded(w) ? 1 : 0, "work-on " + w.rank + ": a badge only when graded")
                if (!graded(w)) continue
                const b = badgesIn(rows[0], [])
                compare(b.length, 1, "work-on " + w.rank + ": one badge")
                compare(b[0].grade, w.consistency.grade, "work-on " + w.rank + ": the map's grade")
                verify(b[0].word !== "", "work-on " + w.rank + ": the badge says its word")
                verify(texts(rows[0], [], false).some(t => t.text === "Consistency"), "work-on " + w.rank + ": labelled Consistency")
            }
        }
        // the Consistency page's flagged swings: one block per swing the map lists (its title, launch monitor line and
        // sentence), its readings row by row with the map's values; the heading only with a flagged swing
        function test_16_theConsistencyPageFlaggedSwings() {
            const c = Fx.maps["consistency"].consistency
            verify(c.flagged.length > 0, "the consistency fixture carries flagged swings (Nick's)")
            load("consistency")
            verify(shown("reportConsistency") && shown("reportConsistencyTable"), "the Consistency page and its table")
            verify(shown("reportConsistencyFlagged"), "the flagged swings' heading")
            const sw = visibleAll("reportConsistencySwing")
            compare(sw.map(s => s.swing).join(","), c.flagged.map(f => f.swing).join(","), "a block per flagged swing, in the map's order")
            for (let i = 0; i < sw.length; ++i) {
                const f = c.flagged[i]
                const said = texts(sw[i], [], false).map(t => t.text)
                verify(said.indexOf(f.title) >= 0 && said.indexOf(f.sentence) >= 0, "swing " + f.swing + ": its title and sentence")
                if (f.lm) verify(said.indexOf(f.lm) >= 0, "swing " + f.swing + ": its launch monitor line")
                const want = f.groups.slice(0, 3).reduce((a, g) => a.concat(g.rows.slice(0, 8)), [])
                const rows = findAll(sw[i], "reportConsistencyRow", []).filter(r => r.visible)
                compare(rows.map(r => r.rowKey + "=" + r.value).join(","), want.map(r => r.key + "=" + (r.value || "-")).join(","),
                        "swing " + f.swing + ": its readings and values, verbatim")
            }
            // the mixed-club page: the table and its notes, no flagged swing, no heading over nothing
            const mc = Fx.maps["consistency-mixed-clubs"].consistency
            load("consistency-mixed-clubs")
            verify(shown("reportConsistencyTable"), "mixed clubs: the table")
            compare(shown("reportConsistencyFlagged"), mc.flagged.length > 0, "the flagged heading only with a flagged swing")
            compare(visibleAll("reportConsistencySwing").length, mc.flagged.length)
            compare(visibleAll("reportConsistencyNote").length, mc.notes.length, "a line per note")
        }
        // Your tendencies (after the order of work): a row per club and swing length with its usual distance and each factor's
        // word and mix line, the flow under the table when the map has one; no `tendencies`, no page
        function test_17_theTendenciesPage() {
            const t = Fx.maps["tendencies"].tendencies
            verify(t.rows.length > 0, "the tendencies fixture carries rows (Nick's)")
            load("nick")
            verify(!shown("reportTendencies"), "no tendencies in the map: no page")
            load("tendencies")
            verify(shown("reportTendencies"), "the Your tendencies page")
            const rows = visibleAll("reportTendencyRow")
            compare(rows.map(r => r.rowKey).join(","), t.rows.map(r => r.key).join(","), "a row per club and length, in the map's order")
            for (let i = 0; i < rows.length; ++i) {
                const r = t.rows[i], said = texts(rows[i], [], false).map(x => x.text)
                verify(said.indexOf(r.club) >= 0 && said.indexOf(r.distance || "-") >= 0, r.key + ": club and usual distance")
                for (const k of ["strike", "direction", "flight"]) {
                    const f = r[k] || {}
                    verify(said.indexOf(f.label && f.label !== "-" ? f.label : "-") >= 0, r.key + " " + k + ": its word " + f.label)
                    if (f.mixLine) verify(said.indexOf(f.mixLine) >= 0, r.key + " " + k + ": its mix line")
                }
            }
            const fl = t.flow
            compare(shown("reportTendencyFlow"), !!fl && (fl.total || 0) + (fl.excluded || 0) > 0, "the flow only with shots")
            compare(shown("reportTendencyFlowSankey"), !!fl && !!fl.enough, "the Sankey only with enough shots")
            compare(shown("reportTendencyFoot"), !!t.foot, "the foot only with words")
            const pg = reg.sectionPage("reportTendencies")
            verify(pg > reg.sectionPage("reportOrder") && pg < reg.sectionPage("reportWorkOn1"), "after the order of work, before the work-on pages")
        }

        // the cover's ball with several clubs: a row per club under the readings any club has; a club with no launch monitor
        // reading still has its row, "-" in every cell (shown, never left out), and every cell stays inside the text width
        function test_18_theCoverBallShowsAClubWithNoReading() {
            const b = Fx.maps["consistency-mixed-clubs"].ball
            const none = b.groups.filter(g => (g.tiles || []).length === 0)
            verify(b.groups.length > 1 && none.length > 0 && b.groups.some(g => g.tiles.length > 0),
                   "the mixed-club case has a club with readings and one without (Mark's 20 Oct 7 iron)")
            load("consistency-mixed-clubs")
            verify(shown("reportCoverBall"), "the ball on the cover")
            // the cells of the ball block the cover draws (the cover's other ball blocks — the note's, the one moved to page 2
            // — are not shown here)
            const blocks = findAll(galley(), "reportCoverBall", []).filter(it => it.visible && it.height > 0)
            compare(blocks.length, 1, "one ball block on the cover")
            const cells = findAll(blocks[0], "reportBallCell", []).filter(it => it.visible && it.height > 0)
            const heads = []
            for (const g of b.groups) for (const t of g.tiles) if (heads.indexOf(t.label) < 0) heads.push(t.label)
            const n = Math.min(4, heads.length)
            compare(cells.length, n * b.groups.length, "a cell per reading for every club, the club with none included")
            for (const g of b.groups) {
                const mine = cells.filter(c => c.club === g.club)
                compare(mine.length, n, g.club + ": its row")
                const want = heads.slice(0, n).map(h => { const t = g.tiles.find(x => x.label === h); return t ? t.value : "-" })
                compare(mine.map(c => c.text).join(","), want.join(","), g.club + ": its figures, or \"-\" where it has none")
            }
            for (const g of none) verify(cells.filter(c => c.club === g.club).every(c => c.text === "-"), g.club + ": no reading, \"-\" in every cell")
            const gal = galley(), textW = reg.galleyPager.textW
            for (const c of cells) {   // every cell of the block, the club with no reading included
                const x = c.mapToItem(gal, 0, 0).x
                verify(x >= -1.5 && x + c.width <= textW + 1.5, c.club + " " + c.text + ": inside the text width (" + Math.round(x + c.width) + " > " + Math.round(textW) + ")")
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
        // Settled: the pages unchanged for three looks AND no repagination due. The pager's cut is debounced (120 ms after
        // the galley last changed) and a squeeze's tightening re-lays a section after its own one-shot, so a slow build
        // (a Debug build on Windows) could hold the OLD pages still for 300 ms while a cut was pending: the test then read
        // a transient — 21 pages and stale section pages for the 18-page delivery fixture.
        // a fixture's stills are "stills/still.jpg" / "stills/frame.jpg" (neutral placeholders beside the fixtures, so no
        // photograph is in the repo): resolved against the fixture folder here, on a copy
        function withStills(v) {
            if (typeof v === "string") return v.indexOf("stills/") === 0 ? String(Qt.resolvedUrl("data/session_report/" + v)) : v
            if (Array.isArray(v)) return v.map(withStills)
            if (v && typeof v === "object") { const o = {}; for (const k in v) o[k] = withStills(v[k]); return o }
            return v
        }
        function loadMap(m, name) {
            reg.report = null
            reg.printPage = 0
            wait(30)
            reg.report = withStills(m)
            let last = "", same = 0
            for (let i = 0; i < 80 && same < 3; ++i) {
                wait(100)
                const s = signature()
                same = (s === last && !reg.galleyPager.paginating) ? same + 1 : 0
                last = s
            }
            verify(same >= 3, (name ? name + ": " : "") + "the pagination never settled")
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
            compare(visibleAll("reportWristRepeatLine").length, w.consistency.length, "Consistency: a line per club")
            // each DOF's consistency beside its name (ROW.rowGrades, R20): a badge per graded club, the map's grade, its word
            verify(w.profile.rows.every(r => (r.rowGrades || []).length > 0), "the wrist fixture carries every row's grades")
            checkRowGrades("reportWrist", w.profile.rows, "wrist")
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
            Theme.themeIndex = 12                       // Folio light
            load("wrist")
            const pages = reg.pageInfo().map((p, i) => p.section === "Wrist" ? i : -1).filter(i => i >= 0)
            verify(pages.length > 0, "the Wrist section has a page")
            const a = [], b = []
            for (const p of pages) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (const p of pages) { reg.printPage = p; wait(30); b.push(appearance()) }
            for (let k = 0; k < a.length; ++k) {
                compare(b[k].length, a[k].length, "wrist page " + (pages[k] + 1) + ": items")
                for (let i = 0; i < a[k].length; ++i) compare(b[k][i], a[k][i], "wrist page " + (pages[k] + 1) + " item " + i + " changed with the app theme")
            }
        }
        // ── the Club delivery page (club_delivery_chart_design.md §4.8; report_delivery.h) ──
        // a copy of the delivery fixture's map with its `delivery` block changed by `edit`
        function deliveryMap(edit) {
            const m = JSON.parse(JSON.stringify(Fx.maps["delivery"]))
            if (edit) edit(m.delivery, m)
            return m
        }
        function test_11_theDeliveryPage() {
            const d = Fx.maps["delivery"].delivery
            // absent: no `delivery` in the map (a session with no swing plane), no page, no heading
            load("wrist")
            verify(!shown("reportDelivery") && !shown("reportDeliveryBand") && !shown("reportDeliveryTrust"), "no delivery in the map: no Club delivery page")
            compare(reg.sectionPage("reportDelivery"), 0)
            loadMap(deliveryMap((x, m) => { delete m.delivery }))
            verify(!shown("reportDelivery"), "the delivery fixture without its block: no Club delivery page")
            // present: the head, the band, the usual set-up, the ball flight and its scatter, the shot shape, the trust note
            load("delivery")
            for (const n of ["reportDelivery", "reportDeliveryBand", "reportDeliveryUsual", "reportDeliveryFlight", "reportDeliveryScatter",
                             "reportDeliveryShape", "chShotShapeGrid", "reportDeliveryTrust"])
                verify(shown(n), "the Club delivery page draws " + n)
            const cols = visibleAll("reportDeliveryBandCol")
            compare(cols.length, 7, "a box per position, P1…P7")
            compare(cols.map(c => c.tag).join(","), "P1,P2,P3,P4,P5,P6,P7")
            compare(cols.map(c => c.text).join(","), d.band.positions.map(p => p.medianText).join(","), "the medians, verbatim")
            verify(cols.every(c => c.text !== "" && c.text !== "0"), "every median a figure with its unit, or \"-\"")
            const usual = visibleAll("reportDeliveryUsualItem")
            compare(usual.map(u => u.label + " " + u.text).join(" · "), d.usual.map(u => u.label + " " + u.text).join(" · "), "the usual set-up, verbatim")
            verify(usual.every(u => u.text !== "" && u.text !== "0"), "a missing set-up figure says \"-\"")
            compare(visibleAll("reportDeliveryTrustLine").map(t => t.text).join("\n"), d.trust.join("\n"), "the trust lines, open and verbatim")
            compare(findAll(reg, "chShapeMark", []).filter(k => k.visible).length, d.shape.marks.filter(k => k.available).length, "a mark per swing with a shape")
            verify(reg.sectionPage("reportDelivery") > reg.sectionPage("reportWrist"), "after the Wrist page")
            verify(reg.sectionPage("reportDelivery") > reg.sectionPage("reportPosture"), "after the Posture page")
            verify(reg.sectionPage("reportDelivery") < reg.sectionPage("reportPractice"), "before Between now and next time")
            compare(reg.insideLine.indexOf("delivery"), -1, "not on the cover's contents line")
            const pages = reg.pageInfo().map((p, i) => p.section === "Club delivery" ? i : -1).filter(i => i >= 0)
            verify(pages.length >= 1, "the Club delivery section has a page")
            compare(reg.pageInfo()[pages[0]].cont, "", "it opens on a fresh page")
            // under five swings with the curve: the sentence, no boxes
            loadMap(deliveryMap(x => { x.band.shown = false; x.band.positions = []; x.band.varies = ""
                                       x.band.sub = "The plane at each position is drawn from 5 swings with the swing plane; this session has 3." }))
            verify(!shown("reportDeliveryBand"), "no boxes")
            compare(visibleAll("reportDeliveryBandWhy").map(t => t.text).join(), "The plane at each position is drawn from 5 swings with the swing plane; this session has 3.")
            // no swing with both readings: the sentence, no scatter
            loadMap(deliveryMap(x => { x.flight.scatter = null; x.flight.legend = ""
                                       x.flight.text = "No swing in this session has both the launch monitor's path and the plane, so the plane is not set against the ball flight." }))
            verify(shown("reportDeliveryFlight") && !shown("reportDeliveryScatter"), "the ball flight says why, with no scatter")
            compare(visibleAll("reportDeliveryFlightText").map(t => t.text).join(), "No swing in this session has both the launch monitor's path and the plane, so the plane is not set against the ball flight.")
            // no monitor shape: the sentence, no grid
            loadMap(deliveryMap(x => { x.shape.shown = false; x.shape.line = ""; x.shape.sub = "The launch monitor gave no start line or curve on this session's swings." }))
            verify(shown("reportDeliveryShape") && !shown("chShotShapeGrid"), "the shot shape says why, with no grid")
            checkLayout("delivery-no-shape")
        }
        // the Club delivery page in its own inks whatever the app's theme, as the Posture and Wrist pages
        function test_12_theDeliveryPageIgnoresTheAppTheme() {
            Theme.themeIndex = 12                       // Folio light
            load("delivery")
            const pages = reg.pageInfo().map((p, i) => p.section === "Club delivery" ? i : -1).filter(i => i >= 0)
            verify(pages.length > 0, "the Club delivery section has a page")
            const a = [], b = []
            for (const p of pages) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (const p of pages) { reg.printPage = p; wait(30); b.push(appearance()) }
            for (let k = 0; k < a.length; ++k) {
                compare(b[k].length, a[k].length, "delivery page " + (pages[k] + 1) + ": items")
                for (let i = 0; i < a[k].length; ++i) compare(b[k][i], a[k][i], "delivery page " + (pages[k] + 1) + " item " + i + " changed with the app theme")
            }
        }
        // the appendix too: its tables (Folio light from the register) and the launch monitor panel's own figures,
        // embedded with their fixed aesthetic, on every appendix page
        function test_05_theLaunchMonitorAppendixIgnoresTheAppTheme() {
            Theme.themeIndex = 12                       // Folio light
            load("launch-monitor-appendix")
            const first = reg.sectionPage("reportLaunchMonitor") - 1
            verify(first > 0, "the appendix has a page")
            const a = [], b = []
            for (let p = first; p < reg.clientPageCount; ++p) { reg.printPage = p; wait(30); a.push(appearance()) }
            Theme.themeIndex = 7                        // Vector dark
            wait(300)
            for (let p = first; p < reg.clientPageCount; ++p) { reg.printPage = p; wait(30); b.push(appearance()) }
            compare(b.length, a.length)
            for (let p = 0; p < a.length; ++p) {
                compare(b[p].length, a[p].length, "appendix page " + (first + p + 1) + ": items")
                for (let i = 0; i < a[p].length; ++i) compare(b[p][i], a[p][i], "appendix page " + (first + p + 1) + " item " + i + " changed with the app theme")
            }
        }
    }
}
