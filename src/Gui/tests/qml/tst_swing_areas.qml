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

// HmSwingAreas and HmAreaRow, loaded for real with a stand-in controller and a stand-in settings
// object: the rows in the controller's order, the weakest measurable area open on first show, the
// open areas remembered per athlete (a table the golfer closed stays closed), Practise only on an
// active or easing work-on, the "New" tag, an area the rig cannot measure, the grade's word, and
// the keyboard opening a row.
Item {
    id: probe
    width: 1100; height: 1000

    function item(id, label, workOn, grade, extra) {
        const it = {
            conditionId: id, label: label, golfer: label + ", in plain words", band: "habit", share: 0.7,
            fired: 17, assessable: 22, measureId: id + ".m", hasTypical: true, typical: 6.9,
            corridorLo: 0.0, corridorHi: 3.0, corridorShape: "twoSided", unit: "cm",
            grade: grade, workOn: workOn, sessionsListed: workOn === "" ? 0 : 2
        }
        for (const k in (extra || {})) it[k] = extra[k]
        return it
    }
    function area(group, label, habits, extra) {
        const a = {
            group: group, label: label, measurable: true, measured: 6, habits: habits, occasional: 1,
            clean: 3, unclear: 0, habitTrend: 0, hasTrend: true, topFault: "", topFaultId: "",
            workOnsActive: 0, workOnsNew: 0, items: []
        }
        for (const k in (extra || {})) a[k] = extra[k]
        return a
    }
    readonly property var fixture: [
        probe.area("posture", "Posture", 4, {
            habitTrend: 1, topFault: "Early extension", topFaultId: "ee", workOnsActive: 2, workOnsNew: 1,
            items: [probe.item("ee", "Early extension", "active", "action"),
                    probe.item("hs", "Hip stall",       "new",    "watch", { hasTypical: false, corridorShape: "" }),
                    probe.item("lp", "Loss of posture", "",       "watch", { typical: 7, corridorShape: "ceiling",
                                                                             corridorHi: 5, unit: "°" })]
        }),
        probe.area("sequence", "Sequence", 1, {
            habitTrend: -1, topFault: "Arms first", workOnsActive: 1,
            items: [probe.item("af", "Arms first", "easing", "watch")]
        }),
        probe.area("setup", "Setup", 0, { hasTrend: false, items: [probe.item("bp", "Ball position", "", "good")] }),
        probe.area("ballFlight", "Ball flight", 0, { measurable: false, measured: 0, occasional: 0, clean: 0,
                                                     hasTrend: false })
    ]

    QtObject {
        id: fake
        property var    areaItems: []
        property bool   updating: false
        property string athleteDir: "/lib/a"
    }
    QtObject {
        id: store
        property var homeViewByAthlete: ({})
        property int writes: 0
        onHomeViewByAthleteChanged: writes += 1
    }

    property var practised: []
    property string allIn: ""

    HmSwingAreas {
        id: areas
        width: 1000
        controller:  fake
        settings:    store
        athleteUuid: "a"
        onPractiseRequested:  (conditionId, label) => probe.practised = [conditionId, label]
        onAllInAreaRequested: (group) => probe.allIn = group
    }

    TestCase {
        name: "SwingAreas"
        when: windowShown

        function all(root, name) {
            const out = []
            const walk = function (it) {
                if (!it) return
                if (it.objectName === name) out.push(it)
                for (const k of it.children || []) walk(k)
            }
            walk(root)
            return out
        }
        function rows() { return all(areas, "areaRow").sort((a, b) => a.y - b.y) }
        function row(group) { return rows().find(r => r.group === group) }
        function shown(root, name) { return all(root, name).filter(i => i.visible) }

        // A fresh athlete, nothing stored: the controller lands its areas.
        function init() {
            fake.areaItems = []
            store.homeViewByAthlete = ({})
            areas.athleteUuid = "a"
            areas.settings = null
            areas.settings = store                  // re-reads the (empty) store
            probe.practised = []
            probe.allIn = ""
            fake.areaItems = probe.fixture
            waitForRendering(areas)
        }

        function test_the_rows_render_in_the_controllers_order() {
            const r = rows()
            compare(r.length, 4)
            compare(r.map(x => x.areaLabel).join(","), "Posture,Sequence,Setup,Ball flight")
            compare(r[0].habitsText, "4 habits")
            compare(r[1].habitsText, "1 habit")
            compare(r[0].trendText, "+1")
            compare(r[1].trendText, "−1")
            compare(r[2].trendText, "–")
            compare(r[0].faultText, "Early extension")
            compare(r[2].faultText, "—")
            compare(r[0].workOnsText, "1 active · 1 new")
            compare(r[1].workOnsText, "1 active")
            compare(r[2].workOnsText, "")
        }

        function test_the_first_measurable_area_opens_on_first_show() {
            verify(row("posture").expanded)
            verify(!row("sequence").expanded)
            verify(!row("setup").expanded)
            const e = store.homeViewByAthlete["a"]
            verify(e !== undefined)
            compare(Array.from(e.expanded).join(","), "posture")
            verify(e.expandedSeen)
        }

        function test_the_first_measurable_one_even_when_an_unmeasurable_area_leads() {
            fake.areaItems = []
            store.homeViewByAthlete = ({})
            areas.athleteUuid = "b"
            fake.areaItems = [probe.fixture[3], probe.fixture[1]]
            verify(row("sequence").expanded)
            verify(!row("ballFlight").expanded)
        }

        function test_the_expansion_is_remembered_per_athlete() {
            // Athlete a: open Setup as well.
            row("setup").activate()
            compare(Array.from(store.homeViewByAthlete["a"].expanded).join(","), "posture,setup")

            // Athlete b, first show: its own weakest area opens; a's state is untouched.
            areas.athleteUuid = "b"
            verify(row("posture").expanded)
            verify(!row("setup").expanded)
            row("posture").activate()               // b closes everything
            verify(!row("posture").expanded)
            compare(Array.from(store.homeViewByAthlete["b"].expanded).length, 0)
            compare(Array.from(store.homeViewByAthlete["a"].expanded).join(","), "posture,setup")

            // Back to a, then b again: each as left. b's all-closed table stays closed.
            areas.athleteUuid = "a"
            verify(row("posture").expanded && row("setup").expanded && !row("sequence").expanded)
            areas.athleteUuid = "b"
            verify(rows().every(r => !r.expanded))
        }

        function test_an_all_closed_table_survives_a_reload() {
            row("posture").activate()
            verify(rows().every(r => !r.expanded))
            // The areas land again (a recompute) and the component re-reads the store.
            fake.areaItems = []
            areas.settings = null
            areas.settings = store
            fake.areaItems = probe.fixture
            verify(rows().every(r => !r.expanded))
            verify(store.homeViewByAthlete["a"].expandedSeen)
        }

        function test_other_keys_of_the_entry_are_kept() {
            const m = Object.assign({}, store.homeViewByAthlete)
            m["a"] = Object.assign({}, m["a"], { tab: "swing" })
            store.homeViewByAthlete = m
            row("setup").activate()
            compare(store.homeViewByAthlete["a"].tab, "swing")
        }

        function test_practise_only_on_active_and_easing_items() {
            const posture = row("posture")
            const items = all(posture, "areaItemRow")
            compare(items.length, 3)
            const buttons = shown(posture, "areaPractise")
            compare(buttons.length, 1)              // ee (active); hs is new, lp is not listed
            buttons[0].clicked()
            compare(probe.practised, ["ee", "Early extension"])

            row("sequence").activate()
            const easing = shown(row("sequence"), "areaPractise")
            compare(easing.length, 1)
            easing[0].clicked()
            compare(probe.practised, ["af", "Arms first"])

            row("setup").activate()
            compare(shown(row("setup"), "areaPractise").length, 0)
        }

        function test_a_newly_listed_item_wears_the_new_tag() {
            const tags = shown(row("posture"), "areaNewTag")
            compare(tags.length, 1)
            const item = all(row("posture"), "areaItemRow").find(r => r.conditionId === "hs")
            verify(all(item, "areaNewTag")[0].visible)
            compare(all(item, "areaPractise").filter(b => b.visible).length, 0)
        }

        function test_an_unmeasurable_area_says_so_and_does_not_open() {
            const bf = row("ballFlight")
            verify(!bf.measurable)
            compare(all(bf, "areaHabitsText")[0].text, "not measured with this rig")
            verify(!all(bf, "areaBandBar")[0].visible)
            verify(!bf.enabled)
            verify(!bf.activeFocusOnTab)
            mouseClick(bf, bf.width / 2, bf.rowHeight / 2)
            verify(!bf.expanded)
        }

        function test_the_grade_word_is_printed_with_its_figures() {
            const items = all(row("posture"), "areaItemRow")
            const ee = items.find(r => r.conditionId === "ee")
            const hs = items.find(r => r.conditionId === "hs")
            const lp = items.find(r => r.conditionId === "lp")
            compare(ee.gradeWord, Theme.corridorWord("action"))
            verify(ee.gradeWord !== "")
            compare(all(ee, "areaItemGrade")[0].text, ee.gradeWord)
            verify(Qt.colorEqual(all(ee, "areaItemGrade")[0].color, Theme.corridorTone("action")))
            compare(ee.countText, "17/22")
            compare(ee.typicalText, "6.9 cm · 0.0–3.0")
            compare(hs.typicalText, "")             // no typical: nothing printed
            compare(lp.typicalText, "7.0° · ≤ 5.0") // a ceiling grades its high edge only
        }

        function test_the_all_link_names_the_area() {
            const link = all(row("posture"), "areaAllLink")[0]
            verify(link.visible)
            compare(link.text, "All of posture →")
            link.clicked(null)
            compare(probe.allIn, "posture")
        }

        function test_the_keyboard_opens_a_row() {
            const seq = row("sequence")
            verify(!seq.expanded)
            seq.forceActiveFocus()
            tryVerify(function () { return seq.activeFocus })
            keyClick(Qt.Key_Space)
            verify(seq.expanded)
            compare(Array.from(store.homeViewByAthlete["a"].expanded).join(","), "posture,sequence")
            keyClick(Qt.Key_Return)
            verify(!seq.expanded)
        }

        function test_reading_and_empty_states() {
            fake.areaItems = []
            fake.updating = true
            verify(all(areas, "swingAreasUpdating")[0].parent.visible)
            verify(!all(areas, "swingAreasTable")[0].visible)
            fake.updating = false
            verify(all(areas, "swingAreasEmpty")[0].parent.visible)
            fake.areaItems = probe.fixture
            verify(all(areas, "swingAreasTable")[0].visible)
            verify(!all(areas, "swingAreasEmpty")[0].parent.visible)
        }
    }
}
