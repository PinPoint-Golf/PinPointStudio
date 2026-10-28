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

// HmWorkOns on PpExpandRow, loaded for real with a stand-in controller. What it presses is what
// the work_ons_home probe asserts in the running app — a row starts closed, a click opens its
// detail in place, opening another closes the first — plus what the probe cannot reach without a
// library: the keyboard, the five-row fold, the review link's signal and the area filter.
Item {
    id: probe
    width: 900; height: 900

    function item(id, name, sessionDir, group) {
        return {
            id: id, name: name, group: group, status: "active", unconfirmed: false, lastSeenText: "",
            countText: "9 of 22 swings", swingsFired: 9, swingsTotal: 22,
            ticks: [ { state: "fired" }, { state: "clean" }, { state: "notAssessable" } ],
            consequence: "What it costs.", statusText: "Active", latestText: "Latest",
            coverageText: "", causedByText: "", drillLabel: "", drillInstruction: "",
            sessionDir: sessionDir, sessionLabel: "Mon 26 Oct"
        }
    }

    QtObject {
        id: fake
        property var  items: [
            probe.item("ee", "Early extension", "/s/a", "posture"),
            probe.item("hs", "Hip stall",       "/s/b", "sequence"),
            probe.item("lp", "Loss of posture", "",     "posture"),
            probe.item("ott", "Over the top",   "/s/c", "armsAndClub"),
            probe.item("fl", "Flip",            "/s/d", "release"),
            probe.item("sw", "Sway",            "/s/e", "lateral")
        ]
        property bool updating: false
        property int  clearedCount: 0
        property int  sessionCount: 3
        property int  sessionsFound: 3
    }

    property string reviewed: ""

    HmWorkOns {
        id: list
        width: 860
        controller: fake
        onReviewSessionRequested: (dir) => probe.reviewed = dir
    }

    TestCase {
        name: "WorkOnsRows"
        when: windowShown

        function rows() {
            const out = []
            const walk = function (it) {
                if (!it) return
                if (it.objectName === "workOnRow") out.push(it)
                for (const k of it.children || []) walk(k)
            }
            walk(list)
            return out
        }
        function drawn(item) {
            for (let t = item; t; t = t.parent) if (!t.visible) return false
            return item.width > 0 && item.height > 0
        }

        function init() {
            list.openId = ""
            list.showAll = false
            list.groupFilter = ""
            probe.reviewed = ""
        }

        function test_five_rows_until_more() {
            compare(rows().length, 5)
            list.showAll = true
            compare(rows().length, 6)
        }

        function test_a_row_starts_closed() {
            for (const r of rows()) {
                const d = findChild(r, "workOnDetail")
                verify(d !== null)
                verify(!d.visible)
                verify(!r.expanded)
            }
        }

        function test_a_click_opens_the_detail_in_place_and_another_closes_it() {
            const rs = rows()
            const d0 = findChild(rs[0], "workOnDetail")
            const closedH = rs[0].height
            findChild(rs[0], "workOnPress").clicked(null)
            verify(d0.visible && d0.height > 0)
            compare(rs[0].height, closedH + d0.height)
            verify(drawn(findChild(rs[0], "workOnReview")))
            findChild(rs[1], "workOnPress").clicked(null)
            verify(!d0.visible)
            verify(findChild(rs[1], "workOnDetail").visible)
            findChild(rs[1], "workOnPress").clicked(null)
            compare(list.openId, "")
        }

        function test_the_mouse_opens_it_too() {
            const r = rows()[0]
            mouseClick(r, r.width / 2, r.rowHeight / 2)
            compare(list.openId, "ee")
        }

        function test_the_keyboard_opens_it() {
            const r = rows()[2]
            r.forceActiveFocus()
            tryVerify(function () { return r.activeFocus })
            keyClick(Qt.Key_Space)
            compare(list.openId, "lp")
            verify(findChild(r, "workOnDetail").visible)
            // No session to review: the link is not drawn.
            verify(!findChild(r, "workOnReview").visible)
            keyClick(Qt.Key_Return)
            compare(list.openId, "")
        }

        function test_review_says_which_fault() {
            const r = rows()[1]
            findChild(r, "workOnPress").clicked(null)
            // Practise lives on Home now: the row offers none.
            compare(findChild(r, "workOnPractise"), null)
            findChild(r, "workOnReview").clicked(null)
            compare(probe.reviewed, "/s/b")
        }

        function test_the_area_filter_lists_that_area_only() {
            list.groupFilter = "posture"
            compare(rows().length, 2)
            compare(list.items.map(it => it.id), ["ee", "lp"])
            verify(!drawn(findChild(list, "workOnsMore")))
            list.groupFilter = "impact"
            compare(rows().length, 0)
            verify(drawn(findChild(list, "workOnsEmpty")))
            list.groupFilter = ""
            compare(rows().length, 5)
        }

        function test_the_more_link_is_there() {
            const more = findChild(list, "workOnsMore")
            verify(more !== null && drawn(more))
        }
    }
}
