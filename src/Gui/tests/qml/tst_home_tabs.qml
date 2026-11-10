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
// keys in the same entry; the bar has four tabs (Tendencies the third, Matchups the fourth) and no chip.
// Tendencies (shot_outcomes_design.md §7.2) holds one open row at a time and is told apart in words; a row says each
// factor's usual (strike, direction, flight) and opens one block per factor with something to say. The filter line
// (HmShotFilter) picks the club, strike, direction and flight, counts in its lists, remembered per athlete. The Coaching tab is
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

        property bool tendenciesUpdating: false
        property string tendencyNote: "From 140 shots over 6 sessions"
        property var tendencyItems: probe.tendencyRows
        property string clubScope: "all"
        property bool clubScopeRefreshing: false
        property bool matchupsRefreshing: false
        property bool clubScopeWorking: false
        property var clubScopes: [
            { value: "group:driver", group: "driver", label: "Driver", clubs: [{ value: "club:DRIVER", label: "Driver" }] },
            { value: "group:mid_irons", group: "mid_irons", label: "Mid irons",
              clubs: [{ value: "club:6 IRON", label: "6 iron" }, { value: "club:7 IRON", label: "7 iron" }] },
            { value: "group:long_irons", group: "long_irons", label: "Long irons", clubs: [{ value: "club:3 IRON", label: "3 iron" }] },
            { value: "group:wedges", group: "wedges", label: "Wedges", clubs: [{ value: "club:GAP WEDGE", label: "Gap wedge" }] }]
        property string shotFilter: "all"
        property var shotFilterOptions: ({
            strike:    [{ value: "any", label: "Any", count: 42 }, { value: "solid", label: "Solid", count: 20 },
                        { value: "thin", label: "Thin", count: 11 }, { value: "fat", label: "Fat", count: 0 }],
            direction: [{ value: "any", label: "Any", count: 42 }, { value: "straight", label: "Straight", count: 15 },
                        { value: "pull", label: "Pull", count: 11 }, { value: "push", label: "Push", count: 5 }],
            flight:    [{ value: "any", label: "Any", count: 42 }, { value: "mid", label: "Mid", count: 30 },
                        { value: "high", label: "High", count: 12 }]
        })
        property string shotFilterLine: "Your pulled mid-iron shots: 11 over 5 sessions, against 31 others."
        function tendencyDetail(key) { return probe.tendencyDetails[key] || ({}) }
    }

    readonly property var tendencyRows: [
        { key: "driver|full", club: "Driver", clubLine: "Driver", length: "full",
          distance: "188 ± 20 yd", clubGroup: "driver", sessions: 5, shots: 60, workOnText: "Active",
          strike:    { factor: "strike", value: "toe", label: "Toe", fault: true, building: false, share: 0.25,
                       frequency: "sometimes", sessions: 5, shots: 60, seenWith: "you stand too far from the ball",
                       trend: 0, trendWords: "→ steady",
                       mix: { line: "Solid 60% · Toe 25% · Other 15%",
                              parts: [{ key: "solid", kind: "good", count: 36 }, { key: "toe", kind: "main", count: 15 },
                                      { key: "other", kind: "other", count: 9 }] } },
          direction: { factor: "direction", value: "slice", label: "Slice", fault: true, building: false, share: 0.37,
                       frequency: "fairly often", sessions: 5, shots: 60,
                       seenWith: "your hands come over the top coming down", seenWithId: "over_the_top", strength: "likely",
                       trend: 1, trendWords: "↑ growing",
                       mix: { line: "Straight 30% · Fade 33% · Slice 37%",
                              parts: [{ key: "straight", kind: "good", count: 18 }, { key: "fade", kind: "value", count: 20 },
                                      { key: "slice", kind: "main", count: 22 }] } },
          flight:    { factor: "flight", value: "high", label: "High", fault: false, building: false, sessions: 5, shots: 60,
                       frequency: "most shots", mix: { line: "High 70% · Mid 30%", parts: [] } } },
        { key: "irons|full", club: "Irons", clubLine: "Irons", length: "full",
          distance: "147 ± 31 yd", clubGroup: "mid_irons", sessions: 4, shots: 50, workOnText: "",
          strike:    { factor: "strike", value: "solid", label: "Solid", fault: false, building: false, sessions: 4, shots: 50,
                       frequency: "most shots" },
          direction: { factor: "direction", value: "pull", label: "Pull, often with a long tail of words that wraps",
                       fault: true, building: false, share: 0.3, frequency: "sometimes", sessions: 4, shots: 50,
                       seenWith: "you stand up through the ball", seenWithId: "early_extension", strength: "possible",
                       trend: 0, trendWords: "→ steady",
                       mix: { line: "Straight 31% · Pull 41% · Draw 16% · Push 6% · Other 6% · and a good many more words than the column can hold",
                              parts: [{ key: "straight", kind: "good", count: 10 }, { key: "draw", kind: "value", count: 5 },
                                      { key: "pull", kind: "main", count: 13 }, { key: "other", kind: "other", count: 9 }] } },
          flight:    { factor: "flight", value: "mid", label: "Mid", fault: false, building: false, sessions: 4, shots: 50 } },
        { key: "wedges|partial", clubGroup: "wedges", distance: "", club: "Wedges", clubLine: "Wedges · part swing",
          length: "partial", sessions: 1, shots: 9, workOnText: "",
          strike:    { factor: "strike", value: "thin", label: "Thin", fault: true, building: true, sessions: 1, shots: 9,
                       frequency: "sometimes" },
          direction: { factor: "direction", value: "straight", label: "Straight", fault: false, building: true, sessions: 1, shots: 9 },
          flight:    { factor: "flight", value: "", label: "-", fault: false, building: true, sessions: 0, shots: 0 } }
    ]
    readonly property var tendencyDetails: ({
        "driver|full": { strike:    { chain: [{ text: "the ball comes off the toe", words: "seen with" },
                                              { text: "you stand too far from the ball", words: "" }] },
                         direction: { chain: [{ text: "the ball slices", words: "because" }, { text: "your hands come over the top", words: "" }],
                                      alsoSeen: "Also seen: you stand up through the ball.",
                                      playTo: "Favour the left side off the tee.", practiseConditionId: "over_the_top" },
                         flight: {} },
        "irons|full":  { strike: {},
                         direction: { chain: [{ text: "the ball starts left", words: "seen with" }, { text: "you stand up through the ball", words: "" }],
                                      alsoSeen: "", playTo: "", practiseConditionId: "early_extension" },
                         flight: {} }
    })

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
            fakeWorkOns.tendencyItems = probe.tendencyRows
            fakeWorkOns.tendencyNote  = "From 140 shots over 6 sessions"
            fakeWorkOns.clubScope  = "all"
            fakeWorkOns.shotFilter = "all"
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
        function test_four_tabs_and_no_chip() {
            compare(all(tabs, "sdTab").map((t) => t.key).join(","), "coaching,swing,tendencies,matchups")
            compare(one(tabs, "homeTabBar").chip, "", "no 'All work-ons': a fault opens in place")
            mouseClick(tabOf("matchups"))
            compare(tabs.current, "matchups")
            verify(one(tabs, "homeTabMatchups").visible)
        }

        // ── the Tendencies tab ──
        function laidOut() { waitForRendering(tabs); wait(0) }
        function rowOf(key) { return one(tabs, "homeTendencyRow:" + key) }
        function showTendencies() { mouseClick(tabOf("tendencies")); laidOut() }

        function test_tendencies_is_a_remembered_tab_with_its_evidence() {
            showTendencies()
            compare(tabs.current, "tendencies")
            verify(drawn(one(tabs, "homeTabTendencies")))
            verify(!drawn(one(tabs, "homeTabCoaching")))
            compare(one(tabs, "homeTabBar").note, "From 140 shots over 6 sessions", "the evidence line is the shots'")
            compare(fakeSettings.homeViewByAthlete["athlete-a"].tab, "tendencies")
            compare(all(tabs, "homeTendencyRow:driver|full").length, 1)
            compare(all(tabs, "homeTendencyRow:irons|full").length, 1)
            compare(rowOf("wedges|partial").factorWords("strike"), "Building — 1 session", "a building factor says so, never a verdict")
            verify(!rowOf("wedges|partial").fault("strike"), "and does not wear the fault tone")
            compare(rowOf("wedges|partial").factorWords("flight"), "", "a factor never read has nothing to build")
            verify(!rowOf("wedges|partial").hasDetail, "and has nothing to open")
        }

        function test_tendencies_usual_distance_is_the_average_and_spread_in_words() {
            showTendencies()
            compare(one(rowOf("driver|full"), "tendencyDistance").text, "188 ± 20 yd")
            compare(one(rowOf("wedges|partial"), "tendencyDistance").text, "—", "a part swing has no usual distance")
        }

        function clubBox() { return one(tabs, "shotFilterClub") }
        function test_tendencies_follow_the_shared_filter_with_groups_only() {
            showTendencies()
            verify(drawn(clubBox()))
            compare(clubBox().model.join(","), "All clubs,Driver,Mid irons,Long irons,Wedges", "groups only: this page lists the clubs")
            verify(!drawn(one(tabs, "shotFilterStrike")), "Tendencies shows the Club picker alone")
            verify(!drawn(one(tabs, "shotFilterLine")))
            tabs.selectClubScope("group:mid_irons")
            laidOut()
            compare(all(tabs, "homeTendencyRow:driver|full").length, 0, "another group's club is gone")
            verify(drawn(rowOf("irons|full")))
            compare(fakeSettings.homeViewByAthlete["athlete-a"].clubScope, "group:mid_irons", "remembered for the athlete")
            compare(clubBox().displayText, "Mid irons")
            tabs.selectClubScope("club:7 IRON")
            laidOut()
            verify(drawn(rowOf("irons|full")), "a chosen club shows its group")
            compare(clubBox().displayText, "Mid irons", "and the picker names the group")
            tabs.select("coaching")
            laidOut()
            compare(clubBox().model.join(","), "All clubs,Driver,Mid irons,6 iron,7 iron,Long irons,Wedges",
                    "the other tabs list a group's clubs (a group of one club lists only the group)")
            compare(clubBox().displayText, "7 iron")
            verify(drawn(one(tabs, "shotFilterStrike")), "and the strike, direction and flight pickers")
            showTendencies()
            tabs.selectClubScope("group:long_irons")
            laidOut()
            verify(drawn(one(tabs, "tendenciesEmpty")), "a group with no rows says so")
            compare(one(tabs, "tendenciesEmpty").text, "No tendencies with these clubs yet.")
            tabs.selectClubScope("all")
            laidOut()
            verify(drawn(rowOf("driver|full")))
        }

        function test_tendencies_factor_cells_say_the_usual() {
            showTendencies()
            const d = rowOf("driver|full"), i = rowOf("irons|full"), w = rowOf("wedges|partial")
            compare(one(one(d, "tendencyDirection"), "tendencyFactorLabel").text, "Slice")
            compare(one(one(d, "tendencyDirection"), "tendencyFactorWords").text, "fairly often", "a fault has its words")
            verify(Qt.colorEqual(one(one(d, "tendencyDirection"), "tendencyFactorLabel").color, Theme.colorWarn), "in the fault tone")
            const solid = one(one(i, "tendencyStrike"), "tendencyFactorLabel")
            compare(solid.text, "Solid")
            verify(!Qt.colorEqual(solid.color, Theme.colorWarn), "a value is plain")
            verify(!drawn(one(one(i, "tendencyStrike"), "tendencyFactorWords")), "and stands alone")
            compare(one(one(d, "tendencyFlight"), "tendencyFactorLabel").text, "High")
            compare(one(one(w, "tendencyFlight"), "tendencyFactorLabel").text, "—", "nothing to name")
            compare(one(d, "tendencyWorkOn").text, "Active")
            compare(one(i, "tendencyWorkOn").text, "—")
        }

        function test_tendencies_a_long_cell_wraps_and_the_row_grows() {
            showTendencies()
            const r = rowOf("irons|full"), cell = one(r, "tendencyDirection"), label = one(cell, "tendencyFactorLabel")
            verify(label.lineCount > 1, "a long usual takes more than one line")
            verify(r.rowHeight >= cell.implicitHeight, "the row is tall enough to hold it")
            for (const n of ["tendencyClub", "tendencyStrike", "tendencyFlight"])
                verify(r.rowHeight >= one(r, n).implicitHeight, "and every other cell: " + n)
            mouseClick(r.headerRow)
            laidOut()
            const block = one(r, "tendencyBlock:direction"), words = one(block, "tendencyMixWords")
            verify(drawn(one(block, "tendencyMixBar")), "the open block draws the factor's bar over its words")
            verify(words.lineCount > 1, "a long mix takes more than one line")
            verify(!words.truncated, "and loses none of its words")
            mouseClick(r.headerRow)
            laidOut()
        }

        function test_tendencies_one_row_open_at_a_time() {
            showTendencies()
            const a = rowOf("irons|full"), b = rowOf("driver|full")
            verify(!a.expanded && !b.expanded, "closed by default")
            mouseClick(a.headerRow)
            laidOut()
            verify(a.expanded)
            verify(!b.expanded)
            mouseClick(b.headerRow)
            laidOut()
            verify(b.expanded)
            verify(!a.expanded, "opening one closes the other")
            compare(all(b, "tendencyBlockHead").filter(drawn).map(h => h.text).join(","),
                    [Theme.caps("Strike"), Theme.caps("Direction")].join(","), "one block per factor with something to say")
            verify(drawn(one(one(b, "tendencyBlock:direction"), "tendencyPlayTo")), "the open row plays to it")
            compare(one(one(b, "tendencyBlock:direction"), "tendencyTrend").text, "↑ growing", "with the trend in words")
            mouseClick(b.headerRow)
            laidOut()
            verify(!b.expanded, "and a second press closes it")
        }

        function test_tendencies_the_open_row_survives_a_publish() {
            showTendencies()
            mouseClick(rowOf("driver|full").headerRow)
            laidOut()
            verify(rowOf("driver|full").expanded)
            fakeWorkOns.tendencyItems = probe.tendencyRows.slice()
            laidOut()
            verify(rowOf("driver|full").expanded, "the rows rebuild; the open key is held above them")
            verify(!rowOf("irons|full").expanded)
        }

        function test_tendencies_practice_passes_the_condition_up() {
            showTendencies()
            const row = rowOf("driver|full")
            mouseClick(row.headerRow)
            laidOut()
            const buttons = all(row, "tendencyPractise").filter(drawn)
            compare(buttons.length, 1, "practice where the factor has one")
            mouseClick(buttons[0])
            compare(practiseSpy.count, 1)
            compare(practiseSpy.signalArguments[0][0], "over_the_top")
            compare(practiseSpy.signalArguments[0][1], "your hands come over the top coming down")
        }

        function test_tendencies_says_why_there_is_nothing_yet() {
            fakeWorkOns.tendencyItems = []
            fakeWorkOns.tendencyNote  = "Hit a few sessions with the launch monitor on"
            showTendencies()
            compare(one(tabs, "tendenciesEmpty").text, "Hit a few sessions with the launch monitor on")
            verify(drawn(one(tabs, "tendenciesEmpty")))
            compare(one(tabs, "homeTabBar").note, "", "no table to back: the bar stays quiet")
        }

        // ── the filter line ──
        function test_filter_counts_in_the_list_and_the_choice_in_the_field() {
            laidOut()
            const box = one(tabs, "shotFilterStrike")
            verify(drawn(box))
            compare(box.model.join(","), "Any,Solid  ·  20,Thin  ·  11,Fat  ·  0")
            compare(box.displayText, "Any")
            verify(!box.itemEnabledFn(3), "a choice that leaves nothing is muted")
            verify(box.itemEnabledFn(0) && box.itemEnabledFn(2))
            verify(!drawn(one(tabs, "shotFilterClear")), "nothing chosen: no Clear")
            compare(one(tabs, "shotFilterLine").text, fakeWorkOns.shotFilterLine)
            box.activated(2)
            laidOut()
            compare(fakeWorkOns.shotFilter, "strike:thin|direction:any|flight:any")
            compare(fakeSettings.homeViewByAthlete["athlete-a"].shotFilter, "strike:thin|direction:any|flight:any",
                    "remembered for the athlete")
            compare(box.displayText, "Thin", "the field says only the choice")
            compare(box.currentIndex, 2)
            const dir = one(tabs, "shotFilterDirection")
            dir.activated(2)
            laidOut()
            compare(fakeWorkOns.shotFilter, "strike:thin|direction:pull|flight:any", "the choices add up")
            compare(dir.displayText, "Pull")
            tabs.selectClubScope("group:mid_irons")
            laidOut()
            verify(drawn(one(tabs, "shotFilterClear")))
            mouseClick(one(tabs, "shotFilterClear"))
            laidOut()
            compare(fakeWorkOns.shotFilter, "all", "Clear clears every choice")
            compare(fakeWorkOns.clubScope, "all")
            compare(box.displayText, "Any")
        }

        function test_filter_is_read_back_for_each_athlete() {
            fakeSettings.homeViewByAthlete = { "athlete-e": { clubScope: "club:7 IRON", shotFilter: "strike:any|direction:pull|flight:mid" } }
            tabs.athleteUuid = "athlete-e"
            laidOut()
            compare(fakeWorkOns.clubScope, "club:7 IRON")
            compare(fakeWorkOns.shotFilter, "strike:any|direction:pull|flight:mid")
            compare(one(tabs, "shotFilterDirection").displayText, "Pull")
            compare(one(tabs, "shotFilterFlight").displayText, "Mid")
            compare(one(tabs, "shotFilterStrike").displayText, "Any")
            verify(fakeSettings.homeViewByAthlete["athlete-e"].outcomeScope === undefined, "no outcomeScope key")
        }

        // ── the Coaching tab: HmFocus and HmSwingSummary on the table components ──
        function test_focus_practise_passes_the_focus_up() {
            verify(waitForPolish(one(tabs, "focusCard").parent))
            const button = one(tabs, "focusPracticeButton")
            verify(drawn(button))
            mouseClick(button)
            compare(practiseSpy.count, 1)
            compare(practiseSpy.signalArguments[0][0], "trail_knee_straighten")
            compare(practiseSpy.signalArguments[0][1], "Trail hip load and hold")
        }

        function test_focus_rows_and_why_how_one_click_down() {
            verify(waitForPolish(one(tabs, "focusCard").parent))
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
