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

// The Sessions table's selection (docs/design/sessions_screen_redesign.md §2.3, S-D1), pressed for real:
// PpSessionReportList over FIXTURE rows (a plain array, each with its shownRank, as the model's filters
// would leave it) with stand-ins for the golfer, the studio, the reports and the mailer. No session
// folder is read; nothing is sent.
//
//   click toggles · shift-click ADDS from the anchor · Ctrl/⌘-click toggles · Esc clears ·
//   ⌘-A takes the shown rows only · a row the filters hide is pruned · double-click opens and does not
//   select · the live row's chip reads "Live" · an archived row's Report cell reads "Archived"
Item {
    id: probe
    width: 1300; height: 800

    function fixture() {
        const out = []
        for (let i = 0; i < 6; ++i)
            out.push({ dir: "/fx/s" + i, dateLabel: "Tue " + (20 - i) + " Oct 2026", sessionOfDay: 1,
                       clubs: "7 iron", swings: 10 + i, devices: ["Face-on"], deviceTokens: ["faceon"],
                       workOn1: "Keep the flex in your trail knee", workOnKnown: true, isLive: false,
                       archived: false, canReport: true, state: "none", stateShort: "No report",
                       stateWords: "No report", pdfPath: "", sizeBytes: 0, sessionBytes: 1e9,
                       sessionMs: 0, shownRank: i })
        return out
    }
    property var rows: fixture()
    // Hides the given dirs as a filter would: shownRank -1, and the rest re-ranked in order.
    function hide(dirs) {
        let next = 0
        probe.rows = probe.rows.map(r => Object.assign({}, r, { shownRank: dirs.indexOf(r.dir) >= 0 ? -1 : next++ }))
    }
    function setRow(dir, patch) { probe.rows = probe.rows.map(r => r.dir === dir ? Object.assign({}, r, patch) : r) }

    QtObject { id: athletes
        property string currentUuid: "fx"; property string currentName: "Nick Henn"
        function reportDetails(u) { return { greeting: "Nick", email: "nick@example.com" } } }
    QtObject { id: studio
        property string coachName: "Alex"; property string studioName: "Studio"
        property string coachEmail: ""; property string emailSubject: ""; property string emailBody: "" }
    QtObject { id: reports
        property string busySession: ""; property int progressPage: 0; property int progressPages: 0
        signal made(string dir, bool ok, string path, string error)
        function recordSend(dir, method) {} }
    QtObject { id: mailer
        property bool sendConfigured: false; property bool sending: false; property real sendProgress: 0
        signal sendFinished(string sessionDir, bool ok, string method, string error)
        function compose() { return { ok: true, method: "dry-run" } }
        function copyText(t) {} function reveal(p) {} }

    PpSessionReportList {
        id: list
        x: 20; y: 20; width: 1200
        model: probe.rows
        reports: reports; studio: studio; athletes: athletes; mailer: mailer
    }

    SignalSpy { id: reviewSpy; target: list; signalName: "reviewSessionRequested" }

    TestCase {
        name: "SessionsSelection"
        when: windowShown

        function find(item, name) {
            if (!item) return null
            if (item.objectName === name) return item
            const k = item.children || []
            for (let i = 0; i < k.length; ++i) { const f = find(k[i], name); if (f) return f }
            return null
        }
        // Through the Repeater (list.rowAt), not by objectName: a fixture swap leaves the previous rows'
        // delegates, with the same names, in the tree until their deferred delete runs.
        function row(i) { return list.rowAt(i) }
        // A press on the Club cell: no link, chip or ⋯ there, so it is the row's own.
        function click(i, mods) {
            const r = row(i)
            verify(r, "row " + i)
            // The pointer travels there first, as a real one does: a press somewhere new straight after
            // another, with no move between, is taken for the second half of a double-click.
            mouseMove(r, r.padH + r.columnX(1) + 10, r.height / 2)
            mouseClick(r, r.padH + r.columnX(1) + 10, r.height / 2, Qt.LeftButton, mods || Qt.NoModifier)
        }
        function dirs(list_) { return list_.slice().sort() }

        function init() {
            probe.rows = probe.fixture()
            list.clearSelection()
            reviewSpy.clear()
            tryVerify(() => row(5) !== null && row(5).height > 0)
            wait(50)            // the swapped-out delegates' deferred deletes
            verify(find(list, "sessionsRow_5") === row(5))
        }

        function test_click_toggles() {
            click(0)
            compare(list.selection, ["/fx/s0"])
            verify(row(0).selected)
            click(2)
            compare(dirs(list.selection), ["/fx/s0", "/fx/s2"])
            click(0)
            compare(list.selection, ["/fx/s2"])
            compare(list.selectionInfo.count, 1)
        }

        function test_shift_click_adds_the_range_from_the_anchor() {
            click(5)                              // already selected, outside the range
            click(1)                              // the anchor
            click(3, Qt.ShiftModifier)
            compare(dirs(list.selection), ["/fx/s1", "/fx/s2", "/fx/s3", "/fx/s5"])
            // Upwards from the same anchor adds too; nothing is taken away.
            click(0, Qt.ShiftModifier)
            compare(dirs(list.selection), ["/fx/s0", "/fx/s1", "/fx/s2", "/fx/s3", "/fx/s5"])
        }

        function test_ctrl_click_toggles() {
            click(1)
            click(2, Qt.ControlModifier)
            compare(dirs(list.selection), ["/fx/s1", "/fx/s2"])
            click(2, Qt.ControlModifier)
            compare(list.selection, ["/fx/s1"])
        }

        function test_escape_clears() {
            click(1); click(2)
            compare(list.selection.length, 2)
            verify(list.activeFocus)
            keyClick(Qt.Key_Escape)
            compare(list.selection.length, 0)
            compare(list.selectionInfo.count, 0)
        }

        function test_select_all_takes_the_shown_rows_only() {
            probe.hide(["/fx/s2", "/fx/s4"])
            tryCompare(list, "shown", 4)
            list.forceActiveFocus()
            keyClick(Qt.Key_A, Qt.ControlModifier)
            compare(dirs(list.selection), ["/fx/s0", "/fx/s1", "/fx/s3", "/fx/s5"])
            list.clearSelection()
            list.selectAllShown()
            compare(list.selection.length, 4)
        }

        function test_a_row_the_filters_hide_is_pruned() {
            click(0); click(1); click(2)
            compare(list.selection.length, 3)
            probe.hide(["/fx/s1"])
            tryCompare(list, "shown", 5)
            tryVerify(() => list.selection.length === 2)
            compare(dirs(list.selection), ["/fx/s0", "/fx/s2"])
            compare(list.selectionInfo.count, 2)
        }

        function test_double_click_opens_and_does_not_select() {
            click(3)
            const before = list.selection.slice()
            const r = row(1)
            mouseMove(r, r.padH + r.columnX(1) + 10, r.height / 2)
            mouseDoubleClickSequence(r, r.padH + r.columnX(1) + 10, r.height / 2)
            compare(reviewSpy.count, 1)
            compare(reviewSpy.signalArguments[0][0], "/fx/s1")
            compare(list.selection, before)
        }

        function test_enter_opens_the_focused_row() {
            click(2)
            list.focusIndex = 4
            keyClick(Qt.Key_Return)
            compare(reviewSpy.count, 1)
            compare(reviewSpy.signalArguments[0][0], "/fx/s4")
            compare(list.selection, ["/fx/s2"])
            keyClick(Qt.Key_Space)
            compare(dirs(list.selection), ["/fx/s2", "/fx/s4"])
        }

        function test_the_live_row_says_live() {
            probe.setRow("/fx/s0", { isLive: true, canReport: false })
            tryVerify(() => row(0) !== null && row(0).isLive)
            const chip = find(row(0), "sessionsReview")
            verify(chip)
            compare(chip.text, "Live")
            compare(find(row(1), "sessionsReview").text, "Review")
            // Review on the live row resumes it: "".
            chip.clicked()
            compare(reviewSpy.count, 1)
            compare(reviewSpy.signalArguments[0][0], "")
        }

        function test_an_archived_row_reads_archived() {
            probe.setRow("/fx/s2", { archived: true })
            tryVerify(() => row(2) !== null && row(2).archived)
            compare(find(row(2), "sessionsStateShort").text, "Archived")
            verify(!find(row(2), "sessionsReportAction").visible)
            compare(find(row(2), "sessionsReview").text, "Restore and review")
            compare(find(row(1), "sessionsStateShort").text, "No report")
            verify(find(row(1), "sessionsReportAction").visible)
        }

        function test_the_row_menu_opens_on_its_row() {
            list.openRowMenu(3)
            tryVerify(() => row(3).menuOpen)
            list.closeRowMenu()
            tryVerify(() => !row(3).menuOpen)
            compare(list.selection.length, 0)
        }
    }
}
