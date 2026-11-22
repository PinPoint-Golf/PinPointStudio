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

// The View menu's three coach-line sets (ViewLayout coachSetsFor / setCoachSet;
// auto_annotations_design.md §1c), against the real ViewLayout over the suite's scratch
// AppSettings: each set toggles on its own and persists per mode; Capture never has any; and the
// migration of the old single `coachLines` bool — true reads as Head & hips + Plane (what that
// toggle drew), Posture off; false / absent reads as all off.
TestCase {
    name: "CoachLineSets"

    property var saved: null
    function initTestCase() { saved = appSettings.viewLayoutByMode }
    function cleanupTestCase() { appSettings.viewLayoutByMode = saved }
    function init() { appSettings.viewLayoutByMode = ({}) }

    function store(mode, entry) {
        var m = {}
        m[String(mode)] = entry
        appSettings.viewLayoutByMode = m
    }
    function sets(mode) { return ViewLayout.coachSetsFor(mode) }

    function test_default_is_all_off() {
        compare(sets(1 /* Replay */), { posture: false, plane: false, headHips: false })
        verify(!ViewLayout.coachLinesOn(1 /* Replay */))
    }

    function test_each_set_toggles_on_its_own_and_persists() {
        var keys = ["posture", "plane", "headHips"]
        for (var i = 0; i < keys.length; ++i) {
            appSettings.viewLayoutByMode = ({})
            ViewLayout.setCoachSet(1 /* Replay */, keys[i], true)
            for (var j = 0; j < keys.length; ++j)
                compare(ViewLayout.coachSetOn(1 /* Replay */, keys[j]), i === j, keys[i] + " on leaves " + keys[j] + (i === j ? " on" : " off"))
            verify(ViewLayout.coachLinesOn(1 /* Replay */), "any set on ⇒ coachLinesOn")
            var stored = appSettings.viewLayoutByMode[String(1 /* Replay */)]
            compare(stored.coachLineSets[keys[i]], true, "persisted in coachLineSets")
            compare(stored.coachLines, true, "the legacy bool is written as 'any set on'")
            ViewLayout.setCoachSet(1 /* Replay */, keys[i], false)
            verify(!ViewLayout.coachLinesOn(1 /* Replay */), keys[i] + " off again ⇒ none")
            compare(appSettings.viewLayoutByMode[String(1 /* Replay */)].coachLines, false)
        }
        // per mode: Analyse untouched by Replay's toggles
        ViewLayout.setCoachSet(1 /* Replay */, "plane", true)
        verify(!ViewLayout.coachLinesOn(2 /* Analyse */), "another mode keeps its own sets")
        // an unrelated write keeps the sets
        ViewLayout.setArrangement(1 /* Replay */, "stage")
        verify(ViewLayout.coachSetOn(1 /* Replay */, "plane"), "a layout write keeps the sets")
        ViewLayout.setCoachSet(1 /* Replay */, "nonsense", true)
        compare(sets(1 /* Replay */), { posture: false, plane: true, headHips: false }, "an unknown set is ignored")
    }

    function test_capture_never_has_coach_lines() {
        ViewLayout.setCoachSet(0 /* Capture */, "posture", true)
        compare(sets(0 /* Capture */), { posture: false, plane: false, headHips: false })
        verify(!ViewLayout.coachLinesOn(0 /* Capture */))
    }

    function test_migration_from_the_single_toggle() {
        store(1 /* Replay */, { panels: ["camera"], arrangement: "split", coachLines: true })
        compare(sets(1 /* Replay */), { posture: false, plane: true, headHips: true },
                "coachLines true ⇒ Head & hips + Plane, Posture off (what the old toggle drew)")
        store(1 /* Replay */, { panels: ["camera"], arrangement: "split", coachLines: false })
        compare(sets(1 /* Replay */), { posture: false, plane: false, headHips: false }, "false ⇒ all off")
        store(1 /* Replay */, { panels: ["camera"], arrangement: "split" })
        compare(sets(1 /* Replay */), { posture: false, plane: false, headHips: false }, "absent ⇒ all off")
        // the first set toggled after migration keeps the migrated others
        store(1 /* Replay */, { panels: ["camera"], arrangement: "split", coachLines: true })
        ViewLayout.setCoachSet(1 /* Replay */, "posture", true)
        compare(sets(1 /* Replay */), { posture: true, plane: true, headHips: true }, "migrated sets carried into the first write")
        // the sets win over a stale legacy bool
        store(1 /* Replay */, { panels: ["camera"], arrangement: "split", coachLines: true,
                                    coachLineSets: { posture: true, plane: false, headHips: false } })
        compare(sets(1 /* Replay */), { posture: true, plane: false, headHips: false }, "coachLineSets wins when present")
    }

    function test_legacy_shim_sets_all() {
        ViewLayout.setCoachLines(1 /* Replay */, true)
        compare(sets(1 /* Replay */), { posture: true, plane: true, headHips: true })
        ViewLayout.setCoachLines(1 /* Replay */, false)
        compare(sets(1 /* Replay */), { posture: false, plane: false, headHips: false })
    }
}
