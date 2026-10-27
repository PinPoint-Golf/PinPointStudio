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

// PpExpandRow, loaded for real. Its contract is the design's acceptance line for it: a row that
// opens its detail in place, by mouse AND by keyboard, says whether it is open to an assistive
// technology, and — for a list that keeps one row open — leaves `expanded` to its owner.
Item {
    id: probe
    width: 600; height: 500

    property int toggles: 0
    property int controlledToggles: 0

    Column {
        width: 540

        PpExpandRow {
            id: row
            accessibleName: "Posture"
            pressObjectName: "rowPress"
            detailIndent: 40
            detailRightInset: 10
            onToggled: probe.toggles += 1
            Text { text: "Posture" }
            detail: Component {
                Rectangle { objectName: "rowDetail"; height: 80; color: "transparent" }
            }
        }

        // Owned: the caller binds expanded and answers toggled(), as HmWorkOns does.
        PpExpandRow {
            id: owned
            property bool open: false
            expanded: owned.open
            autoToggle: false
            chevronSide: "right"
            onToggled: { probe.controlledToggles += 1; owned.open = !owned.open }
            Text { text: "Owned" }
        }
    }

    TestCase {
        name: "ExpandRow"
        when: windowShown

        function init() {
            row.expanded = false
            owned.open = false
            probe.toggles = 0
            probe.controlledToggles = 0
        }

        function test_starts_closed_with_its_detail_built_and_hidden() {
            verify(!row.expanded)
            const d = findChild(row, "rowDetail")
            verify(d !== null)
            verify(!d.visible)
            compare(row.height, Theme.sp(Theme.rowH))
        }

        function test_a_click_opens_the_detail_in_place() {
            mouseClick(row, row.width / 2, row.rowHeight / 2)
            verify(row.expanded)
            compare(probe.toggles, 1)
            const d = findChild(row, "rowDetail")
            verify(d.visible)
            compare(row.height, Theme.sp(Theme.rowH) + 80)
            compare(d.mapToItem(row, 0, 0).y, row.rowHeight)
            compare(d.mapToItem(row, 0, 0).x, 40)
            compare(d.width, row.width - 40 - 10)
            mouseClick(row, row.width / 2, row.rowHeight / 2)
            verify(!row.expanded)
            compare(row.height, Theme.sp(Theme.rowH))
        }

        function test_the_named_press_area_toggles() {
            const press = findChild(row, "rowPress")
            verify(press !== null)
            press.clicked(null)
            verify(row.expanded)
        }

        function test_the_keyboard_opens_and_closes_it() {
            verify(row.activeFocusOnTab)
            row.forceActiveFocus()
            tryVerify(function () { return row.activeFocus })
            keyClick(Qt.Key_Space)
            verify(row.expanded)
            keyClick(Qt.Key_Return)
            verify(!row.expanded)
            keyClick(Qt.Key_Enter)
            verify(row.expanded)
            compare(probe.toggles, 3)
        }

        function test_assistive_technology_sees_a_button_and_its_state() {
            compare(row.Accessible.role, Accessible.Button)
            compare(row.Accessible.name, "Posture")
            verify(row.Accessible.checkable)
            verify(!row.Accessible.checked)
            row.expanded = true
            verify(row.Accessible.checked)
        }

        function test_an_owned_row_leaves_expanded_to_its_owner() {
            mouseClick(owned, owned.width / 2, owned.rowHeight / 2)
            compare(probe.controlledToggles, 1)
            verify(owned.expanded)                  // the owner opened it, through its binding
            owned.open = false                      // and the binding still holds
            verify(!owned.expanded)
        }
    }
}
