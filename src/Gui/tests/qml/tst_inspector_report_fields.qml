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

// The session report's words, in the inspector (#60).
//
// A habit's work-on page — its title, its feel, its picture — is edited in the Diagnostic Model now,
// and the pack loader's verdict on the words is shown under the field while it is typed. The façade
// half (which fields, which findings, which drills can be attached) is model_browser_test's. This is
// the half a façade test cannot reach: that each field gets the right control, that a finding the row
// carries is DRAWN under it, that the line follows the text in the box as it changes, and that an
// empty Drills section still offers the way to add one — the control whose absence meant a drill
// written in the panel could never reach a report.
//
// Built by hand, as tst_inspector_citation.qml builds its pane, so nothing here needs the shipped
// pack: the rows are the shape ModelBrowser.inspect() returns.
Item {
    id: probe
    width: 520; height: 900

    property var added: []

    readonly property string badGoal: "Don't let your lower back arch at address, ever, whatever you do"
    readonly property string goodGoal: "Set up with a natural curve in your lower back"

    function field(name, label, kind, value, options, issues) {
        var r = { type: "", id: name, label: label, detail: "", tone: "", navigable: false,
                  field: name, kind: kind, value: value, options: options || [], group: "report" }
        if (issues !== undefined) r.issues = issues
        return r
    }

    function goalIssue() {
        return { code: "golferPhraseWording", severity: "warning",
                 message: "'s_posture' goal \"" + probe.badGoal + "\" has more than 60 characters." }
    }

    // A habit with a badly worded title (and the loader's finding about it on the row), a clean
    // feel, a picture kind, and NO drills yet.
    function habitDetail(goalValue, goalIssues) {
        return {
            found: true, type: "characteristics", id: "s_posture",
            label: "Poor posture", eyebrow: "Characteristic", subtitle: "s_posture",
            badges: [], source: "shipped", dirty: false,
            sections: [
                { title: "Report", kind: "fields", note: "What the session report prints.",
                  action: "", count: 3,
                  rows: [
                      probe.field("goal", "Work-on title", "text", goalValue, [], goalIssues),
                      probe.field("cue", "Feel to practice", "prose",
                                  "Feel your belt buckle tip down towards the ball.", [], []),
                      probe.field("picture.kind", "Picture", "enum", "change",
                                  [ { value: "", label: "Not set" },
                                    { value: "change", label: "Change from address" },
                                    { value: "none", label: "No picture" } ], [])
                  ] },
                { title: "Drills", kind: "list", note: "None yet — its work-on page would have nothing to practise.",
                  action: "drill", count: 0, rows: [] }
            ]
        }
    }

    ModelInspector {
        id: ins
        anchors.fill: parent
        editable: true
        onAddRowRequested: (action) => probe.added.push(action)
    }

    function findText(item, wanted) {
        if (item.text !== undefined && String(item.text) === wanted) return item
        for (var i = 0; i < item.children.length; i++) {
            var hit = findText(item.children[i], wanted)
            if (hit) return hit
        }
        return null
    }

    TestCase {
        name: "InspectorReportFields"
        when: windowShown

        function init() {
            probe.added = []
            ins.issuesFor = null
            ins.objectActions = true
            ins.detail = probe.habitDetail(probe.badGoal, [ probe.goalIssue() ])
        }

        // Each report field gets the control its kind names, holding the row's value — the same
        // three editors every other field uses, found by the names the probe and the façade agree on.
        function test_each_report_field_has_its_editor() {
            var goal = findChild(ins, "fieldEditor:goal:text")
            verify(goal !== null)
            verify(goal.visible)
            compare(goal.text, probe.badGoal)

            var cue = findChild(ins, "fieldEditor:cue:prose")
            verify(cue !== null)
            verify(cue.visible)

            var kind = findChild(ins, "fieldEditor:picture.kind:enum")
            verify(kind !== null)
            verify(kind.visible)
            compare(kind.value, "change")
            // A closed vocabulary is PICKED, never typed: the picture's kind has no text box.
            verify(!findChild(ins, "fieldEditor:picture.kind:text").visible)
        }

        // The loader's finding the row arrived with is drawn under the field it is about — and only
        // there: the clean feel beside it shows nothing.
        function test_a_finding_on_the_row_is_shown_under_its_field() {
            var line = findChild(ins, "fieldIssues:goal")
            verify(line !== null)
            verify(line.visible)
            verify(findText(ins, probe.goalIssue().message) !== null)

            var cueLine = findChild(ins, "fieldIssues:cue")
            verify(cueLine !== null)
            verify(!cueLine.visible)
        }

        // LIVE: the line is the verdict on the text in the box, asked through issuesFor as it changes,
        // and it falls back to the row's own findings once the text is what the row holds again.
        //
        // Driven on the pane init() built rather than on a second one: re-seating `detail` rebuilds
        // every delegate, and a lookup by name could then find one on its way out.
        function test_the_line_follows_the_text_as_it_is_typed() {
            var asked = []
            ins.issuesFor = function (f, v) {
                asked.push(f)
                return v.indexOf("Don't") === 0 ? [ probe.goalIssue() ] : []
            }
            var goal = findChild(ins, "fieldEditor:goal:text")
            var line = findChild(ins, "fieldIssues:goal")
            verify(goal !== null && line !== null)
            verify(line.visible)

            // A good title typed over the bad one: the loader would say nothing, so neither does the line.
            goal.text = probe.goodGoal
            verify(asked.indexOf("goal") >= 0)
            verify(!line.visible)

            // Back to what the row holds: the row's own finding is shown again, without asking.
            asked = []
            goal.text = probe.badGoal
            compare(asked.length, 0)
            verify(line.visible)

            // And a picked field is never asked about as typed text: its findings arrive with the row,
            // after the pick commits. (Its text box exists, hidden, as every row's editors do.)
            var kind = findChild(ins, "fieldEditor:picture.kind:text")
            kind.text = "anything"
            compare(asked.length, 0)
        }

        // The control that was missing. A habit with no drills still shows its Drills section, and
        // the section offers the add — keyed by its action, so the panel opens the drill picker.
        function test_an_empty_drills_list_offers_add_drill() {
            var add = findChild(ins, "addAffordance:drill")
            verify(add !== null)
            verify(add.visible)
            compare(add.text, "+ add drill")
            mouseClick(add)
            compare(probe.added, [ "drill" ])
        }

        // A gate's pane is editable in its golfer words and nothing else: Copy and Delete act on the
        // object, which is part of the analysis, so a pane told so does not offer them.
        function test_no_copy_or_delete_without_object_actions() {
            var del = findText(ins, "Delete")
            verify(del !== null)
            verify(del.visible)
            ins.objectActions = false
            verify(!del.visible)
            // …while the fields stay live.
            verify(findChild(ins, "fieldEditor:goal:text").enabled)
        }
    }
}
