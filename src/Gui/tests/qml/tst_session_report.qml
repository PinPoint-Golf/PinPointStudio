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
            "launch-monitor-appendix": 15       // nick + the launch monitor appendix (Nick Henn's 20 Oct session 3 readings)
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
