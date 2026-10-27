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

// PpTabs, the real component, in its two looks.
//
// THE DEFAULT IS A PROMISE TO FIVE SCREENS (charts, both diagnostics registers, the athlete form, the
// launch monitor body): the title variant was added for Home, and none of them asked for a change.
// So the default is pinned by its own stated sizes: a u(28) row, labels in the typeset's fTab, the
// current tab ruled 2 px in ink, the note on the right.
//
// The title variant is pinned by the design (§2.5 "Tab bar"): labels at Theme.fontSzTab in the display
// face, a Theme.tabUnderline accent rule, the note straight after the tabs ON the labels' baseline, the
// chip still the right-hand link on that same baseline.
//
// Keyboard: the bar is one Tab stop, the arrows move between tabs WITHOUT switching, Space/Enter switch.
// Moving is not choosing — a bar that switched on every arrow would reload a whole tab per key press.
Item {
    id: probe
    width: 1200; height: 300

    PpTypeset { id: ts }

    PpTabs {
        id: plain
        y: 0; width: parent.width
        roles: ts
        tabs: [{ key: "shot", label: "This shot", count: 3 }, { key: "session", label: "Session" }]
        current: "shot"
        note: "Reviewed against your last session"
    }

    Item { id: before; y: 80; width: 10; height: 10; activeFocusOnTab: true }

    PpTabs {
        id: title
        y: 120; width: parent.width
        roles: ts
        size: "title"
        noteAfterTabs: true
        tabs: [{ key: "coaching", label: "Coaching" }, { key: "swing", label: "Your swing" }, { key: "third", label: "Third" }]
        current: "coaching"
        note: "from 180 swings over 6 sessions"
        chip: "All work-ons ▸"
        chipObjectName: "homeAllWorkOns"
        property var activated: []
        onTabActivated: (k) => { activated = activated.concat([k]); current = k }
    }

    TestCase {
        name: "PpTabs"
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
        function tabOf(bar, key) { return all(bar, "sdTab").filter((t) => t.key === key)[0] }
        function labelOf(bar, key) { return one(tabOf(bar, key), "sdTabLabel") }
        function ruleOf(bar, key) { return one(tabOf(bar, key), "sdTabRule") }
        function baselineIn(bar, it) { return it.mapToItem(bar, 0, it.baselineOffset).y }

        function init() {
            title.current = "coaching"
            title.activated = []
        }

        // ── the default look: what the five existing screens see ──
        function test_default_look_is_unchanged() {
            compare(plain.size, "")
            verify(!plain.accentUnderline)
            verify(!plain.noteAfterTabs)
            compare(plain.implicitHeight, ts.u(28))
            compare(plain.height, ts.u(28))
            compare(all(plain, "sdTab").length, 2)
            const lab = labelOf(plain, "shot")
            compare(lab.font.pixelSize, ts.fTab.pixelSize)
            compare(lab.font.family, ts.fTab.family)
            verify(Qt.colorEqual(lab.color, ts.ink), "current label in ink")
            verify(Qt.colorEqual(labelOf(plain, "session").color, ts.ink2), "other label in ink2")
            const rule = ruleOf(plain, "shot")
            verify(rule.visible)
            compare(rule.height, 2)
            verify(Qt.colorEqual(rule.color, ts.ink), "the current tab is ruled in ink")
            verify(!ruleOf(plain, "session").visible)
        }

        function test_default_note_stays_on_the_right() {
            const n = one(plain, "sdReviewNote")
            verify(n.visible, "1200 px is wide enough for the review note")
            fuzzyCompare(n.x + n.width, plain.width, 1)
            const row = one(plain, "sdTabRow")
            verify(n.x > row.x + row.width)
        }

        // ── the title variant ──
        function test_title_sizes_and_underline_tone() {
            verify(title.isTitle)
            verify(title.accentUnderline, "the title size implies the accent underline")
            const lab = labelOf(title, "coaching")
            compare(lab.font.pixelSize, Math.round(ts.u(Theme.fontSzTab)))
            compare(lab.font.family, ts.r.displayFamily)
            verify(lab.font.pixelSize > labelOf(plain, "shot").font.pixelSize, "title labels are bigger than the toggles")
            verify(title.height >= lab.height + ts.u(Theme.tabUnderline), "the row holds the label and its rule")
            verify(title.height > plain.height)
            const rule = ruleOf(title, "coaching")
            verify(rule.visible)
            compare(rule.height, ts.u(Theme.tabUnderline))
            verify(Qt.colorEqual(rule.color, ts.accent), "the title variant rules the current tab in accent")
            verify(!ruleOf(title, "swing").visible)
            verify(Qt.colorEqual(labelOf(title, "swing").color, ts.ink2))
            // the rule sits on the bar's foot, under the label
            const rb = rule.mapToItem(title, 0, rule.height).y
            fuzzyCompare(rb, title.height, 0.5)
            verify(lab.mapToItem(title, 0, lab.height).y <= rule.mapToItem(title, 0, 0).y + 0.5, "the rule is under the label")
        }

        function test_accent_underline_alone_keeps_the_default_size() {
            plain.accentUnderline = true
            const rule = ruleOf(plain, "shot")
            compare(rule.height, ts.u(Theme.tabUnderline))
            verify(Qt.colorEqual(rule.color, ts.accent))
            compare(plain.height, ts.u(28), "the underline option does not change the row")
            compare(labelOf(plain, "shot").font.pixelSize, ts.fTab.pixelSize)
            plain.accentUnderline = false
            compare(ruleOf(plain, "shot").height, 2)
        }

        // ── the note after the tabs, on the label baseline; the chip still on the right ──
        function test_note_follows_the_tabs_on_their_baseline() {
            const n = one(title, "sdReviewNote")
            const row = one(title, "sdTabRow")
            verify(n.visible)
            fuzzyCompare(n.x, row.x + row.width + row.spacing, 1)
            const base = baselineIn(title, labelOf(title, "coaching"))
            fuzzyCompare(baselineIn(title, n), base, 1)
            const chip = one(title, "homeAllWorkOns")
            verify(chip.visible)
            fuzzyCompare(chip.x + chip.width, title.width, 1)
            fuzzyCompare(baselineIn(title, chip), base, 1)
            verify(n.x + n.width < chip.x, "the note does not run into the chip")
        }

        function test_note_folds_rather_than_meeting_the_chip() {
            const n = one(title, "sdReviewNote")
            title.note = "x".repeat(400)
            verify(!n.visible, "an over-long note folds")
            title.note = "from 180 swings over 6 sessions"
            verify(n.visible)
        }

        // ── keyboard ──
        function test_accessible_roles() {
            const t = tabOf(title, "coaching")
            compare(t.Accessible.role, Accessible.PageTab)
            verify(t.Accessible.selected)
            verify(!tabOf(title, "swing").Accessible.selected)
            compare(title.Accessible.role, Accessible.PageTabList)
        }

        function test_one_tab_stop_the_current_tab() {
            verify(tabOf(title, "coaching").activeFocusOnTab)
            verify(!tabOf(title, "swing").activeFocusOnTab)
            verify(!tabOf(title, "third").activeFocusOnTab)
            before.forceActiveFocus()
            keyClick(Qt.Key_Tab)
            verify(tabOf(title, "coaching").activeFocus, "Tab lands on the current tab")
            verify(one(tabOf(title, "coaching"), "sdTabFocus").visible, "a focused tab draws its ring")
            verify(!one(tabOf(title, "swing"), "sdTabFocus").visible)
        }

        function test_arrows_move_space_and_enter_switch() {
            tabOf(title, "coaching").forceActiveFocus()
            keyClick(Qt.Key_Right)
            verify(tabOf(title, "swing").activeFocus, "Right moves to the next tab")
            compare(title.current, "coaching", "moving is not choosing")
            compare(title.activated.length, 0)
            keyClick(Qt.Key_Space)
            compare(title.current, "swing", "Space switches")
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(title.current, "third", "Return switches")
            keyClick(Qt.Key_Right)
            verify(tabOf(title, "coaching").activeFocus, "Right wraps from the last tab to the first")
            keyClick(Qt.Key_Left)
            verify(tabOf(title, "third").activeFocus, "Left wraps from the first tab to the last")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Enter)
            compare(title.current, "swing", "Enter switches")
            compare(title.activated, ["swing", "third", "swing"])
            verify(tabOf(title, "swing").activeFocusOnTab, "the Tab stop follows the current tab")
        }

        function test_no_keyboard_when_not_interactive() {
            title.interactive = false
            verify(!tabOf(title, "coaching").activeFocusOnTab)
            tabOf(title, "coaching").forceActiveFocus()
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Space)
            compare(title.activated.length, 0, "an auto-closing cast is drawn, never pressed")
            verify(!one(tabOf(title, "coaching"), "sdTabFocus").visible)
            title.interactive = true
        }
    }
}
