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

// HmHomeTabs, the real component, on a fake controller and a fake settings store.
//
// The design (ux_refactor_away_from_4_modes §2.3, D6): Home opens on Coaching; the tab last chosen
// is remembered PER ATHLETE in homeViewByAthlete[uuid].tab, without disturbing the area table's own
// keys in the same entry; the bar has three tabs (Matchups the third) and no chip. The Coaching tab is
// HmSwingSummary + HmFocus re-laid on the table components, so this also pins what those must
// still do: Practise passes the focus up, and why/how is one click down rather than on the page.
Item {
    id: probe
    width: 1200; height: 900

    QtObject {
        id: fakeWorkOns
        property string athleteDir: "/fake/athlete"
        property bool   updating: false
        property bool   summaryReady: true
        property bool   summaryUpdating: false
        property string summarySubtitle: "From 180 swings over 6 sessions"
        property var focusItem: ({
            present: true,
            title: "Trail hip load and hold",
            aimFor: ["your trail knee keeps its flex going back", "your hips turn in place going back"],
            rightNow: [
                { text: "your trail leg straightens going back", share: 0.95, frequency: "almost every swing",
                  trend: 0, sessions: [true, true, true, true] },
                { text: "your hips sway away from the target going back", share: 0.6,
                  frequency: "more than half your swings", trend: 1, sessions: [false, true, true, true, true] }],
            why: "A straight trail leg lets your hips slide instead of turn.",
            practiseLabel: "Trail hip load and hold",
            practise: "Take your address position, make a slow backswing turn, and stop at the top.",
            reason: "Picked first: it comes earliest in your swing and covers two of the things on your list.",
            reasonShort: "covers 2 things on your list · comes earliest in your swing",
            focusConditionId: "trail_knee_straighten"
        })
        property var doWellItems: [
            { text: "you keep your arms wide at the top", caption: "in the ideal range on almost every swing",
              share: 0.9, sessions: [true, true, true, true, true] }]
        property var nextItems: [
            { text: "your hands come over the top coming down", share: 1.0, frequency: "almost every swing",
              trend: 0, sessions: [true, true, true, true] },
            { text: "your body turns in the wrong order coming down", share: 0.7, frequency: "most swings",
              trend: -1, sessions: [true, true, true] }]
        property var areaItems: []
    }

    QtObject {
        id: fakeSettings
        property var homeViewByAthlete: ({})
    }

    HmHomeTabs {
        id: tabs
        width: 1100
        controller:  fakeWorkOns
        settings:    fakeSettings
        athleteUuid: "athlete-a"
    }

    SignalSpy { id: practiseSpy; target: tabs; signalName: "practiseRequested" }

    TestCase {
        name: "HmHomeTabs"
        when: windowShown

        function all(root, name) {
            const out = []
            const walk = (it) => {
                if (!it) return
                if (it.objectName === name) out.push(it)
                for (let i = 0; i < it.children.length; ++i) walk(it.children[i])
            }
            walk(root)
            return out
        }
        function one(root, name) { const a = all(root, name); verify(a.length > 0, "no " + name); return a[0] }
        function drawn(item) {
            for (let t = item; t; t = t.parent) if (!t.visible) return false
            return item.width > 0 && item.height > 0
        }
        function tabOf(key) { return all(tabs, "sdTab").filter((t) => t.key === key)[0] }

        function init() {
            fakeSettings.homeViewByAthlete = {}
            tabs.athleteUuid = ""
            tabs.athleteUuid = "athlete-a"
            practiseSpy.clear()
            const row = all(tabs, "focusWhyRow")[0]
            if (row) row.expanded = false
        }

        // ── default ──
        function test_default_is_coaching() {
            compare(tabs.current, "coaching")
            const bar = one(tabs, "homeTabBar")
            compare(bar.current, "coaching")
            compare(bar.size, "title")
            verify(bar.noteAfterTabs)
            compare(bar.note, "From 180 swings over 6 sessions", "the evidence line is the subtitle")
            verify(drawn(one(tabs, "homeTabCoaching")))
            verify(!one(tabs, "homeTabSwing").visible)
            verify(drawn(one(tabs, "swingSummary")))
            verify(drawn(one(tabs, "focusCard")))
            compare(Object.keys(fakeSettings.homeViewByAthlete).length, 0, "showing the default writes nothing")
        }

        // ── switching ──
        function test_clicking_a_tab_switches_and_remembers() {
            mouseClick(tabOf("swing"))
            compare(tabs.current, "swing")
            compare(one(tabs, "homeTabBar").current, "swing")
            // (the area page may be empty on a fake with no areas, so it is asked whether it shows)
            verify(one(tabs, "homeTabSwing").visible)
            verify(!drawn(one(tabs, "homeTabCoaching")))
            verify(!drawn(one(tabs, "swingSummary")))
            compare(fakeSettings.homeViewByAthlete["athlete-a"].tab, "swing")
            mouseClick(tabOf("coaching"))
            compare(tabs.current, "coaching")
            verify(drawn(one(tabs, "swingSummary")))
            compare(fakeSettings.homeViewByAthlete["athlete-a"].tab, "coaching")
        }

        function test_an_unknown_tab_is_refused() {
            tabs.select("nonsense")
            compare(tabs.current, "coaching")
            compare(Object.keys(fakeSettings.homeViewByAthlete).length, 0)
        }

        // ── per athlete ──
        function test_each_athlete_keeps_their_own_tab() {
            tabs.select("swing")
            tabs.athleteUuid = "athlete-b"
            compare(tabs.current, "coaching", "a second athlete starts on Coaching")
            verify(fakeSettings.homeViewByAthlete["athlete-b"] === undefined, "nothing written for B by looking")
            tabs.athleteUuid = "athlete-a"
            compare(tabs.current, "swing", "the first athlete's choice came back")
            tabs.athleteUuid = "athlete-b"
            tabs.select("swing")
            tabs.select("coaching")
            tabs.athleteUuid = "athlete-a"
            compare(tabs.current, "swing", "B's choices did not touch A's")
            compare(fakeSettings.homeViewByAthlete["athlete-b"].tab, "coaching")
        }

        function test_writing_the_tab_keeps_the_area_keys() {
            fakeSettings.homeViewByAthlete = { "athlete-a": { expanded: ["posture"], expandedSeen: true },
                                               "athlete-c": { tab: "swing" } }
            tabs.athleteUuid = ""
            tabs.athleteUuid = "athlete-a"
            tabs.select("swing")
            const e = fakeSettings.homeViewByAthlete["athlete-a"]
            compare(e.tab, "swing")
            compare(e.expanded.length, 1)
            compare(e.expanded[0], "posture")
            verify(e.expandedSeen)
            compare(fakeSettings.homeViewByAthlete["athlete-c"].tab, "swing", "other athletes are untouched")
        }

        function test_a_stored_tab_is_read_on_arrival() {
            fakeSettings.homeViewByAthlete = { "athlete-d": { tab: "swing" } }
            tabs.athleteUuid = "athlete-d"
            compare(tabs.current, "swing")
            verify(one(tabs, "homeTabSwing").visible)
        }

        // ── the tabs ──
        function test_three_tabs_and_no_chip() {
            compare(all(tabs, "sdTab").map((t) => t.key).join(","), "coaching,swing,matchups")
            compare(one(tabs, "homeTabBar").chip, "", "no 'All work-ons': a fault opens in place")
            mouseClick(tabOf("matchups"))
            compare(tabs.current, "matchups")
            verify(one(tabs, "homeTabMatchups").visible)
        }

        // ── the Coaching tab: HmFocus and HmSwingSummary on the table components ──
        function test_focus_practise_passes_the_focus_up() {
            const button = one(tabs, "focusPracticeButton")
            verify(drawn(button))
            mouseClick(button)
            compare(practiseSpy.count, 1)
            compare(practiseSpy.signalArguments[0][0], "trail_knee_straighten")
            compare(practiseSpy.signalArguments[0][1], "Trail hip load and hold")
        }

        function test_focus_rows_and_why_how_one_click_down() {
            compare(one(tabs, "focusTitle").text, "Trail hip load and hold")
            compare(one(tabs, "focusReason").text, "covers 2 things on your list · comes earliest in your swing")
            const aims = all(tabs, "focusAim"), nows = all(tabs, "focusNow")
            compare(aims.length, 2)
            compare(nows.length, 2)
            verify(aims.every(drawn) && nows.every(drawn))
            // each aim faces the fault it answers
            for (let i = 0; i < 2; ++i)
                fuzzyCompare(aims[i].mapToItem(tabs, 0, 0).y + aims[i].height / 2,
                             nows[i].mapToItem(tabs, 0, 0).y + nows[i].height / 2, 1)
            const row = one(tabs, "focusWhyRow")
            verify(drawn(row))
            verify(!row.expanded, "why/how is closed by default")
            verify(!drawn(one(tabs, "focusPractise")), "how to practise is not on the page until asked for")
            verify(!drawn(one(tabs, "focusWhy")))
            mouseClick(row.headerRow)
            verify(row.expanded)
            const practise = one(tabs, "focusPractise")
            verify(drawn(practise))
            compare(practise.text, fakeWorkOns.focusItem.practise)
            verify(drawn(one(tabs, "focusWhy")))
        }

        function test_do_well_and_next_rows() {
            const well = all(tabs, "doWellItem"), next = all(tabs, "nextItem")
            compare(well.length, 1)
            compare(next.length, 2)
            verify(well.every(drawn) && next.every(drawn))
            compare(well[0].captionText, "in the ideal range on almost every swing")
            // the do-well meter shows the share: 0.9 → nine segments lit
            const meter = all(well[0], "meterSegment")
            compare(meter.length, 10)
            compare(meter[0].parent.filled, 9)
            const chips = next.map((it) => drawn(one(it, "nextChip")))
            verify(chips[0] && !chips[1], "only the first wears Next")
            verify(!drawn(one(tabs, "doWellEmpty")))
            verify(!drawn(one(tabs, "needsWorkEmpty")))
            compare(all(tabs, "diagnosticsLink").length, 0, "the old 'Swing diagnostics →' row is gone")
        }
    }
}
