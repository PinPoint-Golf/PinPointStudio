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

// TABS — the view toggles: label + quiet count, the current one ruled in ink. Chrome thins last and
// least: counts go below compact (each view's head states its own), the review note on the right goes
// below medium (to a note at the foot of the view); the toggles never go. Screen only.
// Round 4: `interactive` (false on an auto-closing cast: the tabs are drawn, never pressed); an optional
// CHIP sits quietly at the far right of the row (the declared miss, F66: "Declare miss" / "Miss: Slice"),
// a link while interactive, plain ink3 text otherwise; `chipOn` draws it in ink (its picker is open).
// Phase 4a (Home's Coaching | Your swing bar), all opt-in, the default look unchanged:
//   • `size: "title"` sets the labels at Theme.fontSzTab in the display face, and the row grows to hold them;
//   • `accentUnderline` (implied by the title size) rules the current tab in accent, Theme.tabUnderline thick;
//   • `noteAfterTabs` puts the note LEFT, straight after the tabs, on the labels' baseline (Home's evidence
//     line) instead of on the right; the chip stays the right-aligned link and, with either option, sits on
//     the same baseline.
// KEYBOARD (every instance, while interactive): the bar is ONE Tab stop (the current tab; the first when none
// is current), Left/Right move between tabs (wrapping), Home/End go to the ends, Space/Enter/Return activate
// the focused tab; a focused tab and a focused chip draw an accent focus ring. Tabs are Accessible.PageTab
// (selected = current) in an Accessible.PageTabList; the chip is a Link.
// objectNames: sdTabBar (callers may override), sdTabRow, sdTab, sdTabLabel, sdTabCount, sdTabRule (the
// current tab's underline), sdTabFocus (a tab's focus ring), sdReviewNote, the chip (chipObjectName), sdChipFocus.
Item {
    id: tb
    objectName: "sdTabBar"
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var tabs: []                        // [{ key, label, count }]
    property string current: ""
    property string note: ""                     // e.g. the review note; tier 3
    property string chip: ""                     // e.g. "Declare miss" / "Miss: Slice"; "" = none
    property bool chipOn: false
    property string chipObjectName: "sdMissChip"
    property string size: ""                     // "" (the view toggles) | "title" (Home's tab bar)
    property bool accentUnderline: size === "title"
    property bool noteAfterTabs: false
    signal tabActivated(string key)
    signal chipActivated()
    readonly property var keyKinds: []

    readonly property bool isTitle: size === "title"
    readonly property bool counts: roles.shows("tabCounts", width)
    // the label role: the view toggles' fTab, or the title size in the display face
    readonly property font tabFont: isTitle
        ? Qt.font({ family: roles.r.displayFamily, pixelSize: Math.round(roles.u(Theme.fontSzTab)), weight: roles.r.displayWeight })
        : roles.fTab
    // the chip and an after-tabs note share the labels' baseline; the default keeps both centred
    readonly property bool onBaseline: isTitle || noteAfterTabs
    readonly property int currentIndex: {
        for (let i = 0; i < tabs.length; ++i) if (tabs[i] && tabs[i].key === current) return i
        return -1
    }
    function focusTab(i) {
        const n = tabRep.count
        if (n <= 0) return
        const it = tabRep.itemAt(((i % n) + n) % n)
        if (it) it.forceActiveFocus(Qt.TabFocusReason)
    }

    FontMetrics { id: tabFm; font: tb.tabFont }
    implicitHeight: isTitle ? Math.ceil(tabFm.height) + roles.u(14) : roles.u(28)
    height: implicitHeight
    Accessible.role: Accessible.PageTabList
    Row {
        id: tabRow
        objectName: "sdTabRow"
        anchors.bottom: parent.bottom
        spacing: tb.roles.atLeast(tb.width, "compact") ? tb.roles.u(26) : tb.roles.u(18)
        // the first label's baseline, so the note and chip can sit on it
        baselineOffset: tabRep.count > 0 && tabRep.itemAt(0) ? tabRep.itemAt(0).baselineOffset : 0
        Repeater {
            id: tabRep
            model: tb.roles ? (tb.tabs) : []
            delegate: Item {
                id: tab
                objectName: "sdTab"
                required property var modelData
                required property int index
                readonly property string key: modelData.key
                readonly property bool on: tb.current === modelData.key
                width: tl.implicitWidth; height: tb.height
                baselineOffset: tl.y + tlt.baselineOffset
                // one Tab stop for the bar: the current tab (the first when none is current)
                activeFocusOnTab: tb.interactive && (tab.on || (tb.currentIndex < 0 && tab.index === 0))
                Keys.enabled: tb.interactive
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Left) tb.focusTab(tab.index - 1)
                    else if (e.key === Qt.Key_Right) tb.focusTab(tab.index + 1)
                    else if (e.key === Qt.Key_Home) tb.focusTab(0)
                    else if (e.key === Qt.Key_End) tb.focusTab(tabRep.count - 1)
                    else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) tb.tabActivated(tab.key)
                    else return;
                    e.accepted = true
                }
                Accessible.role: Accessible.PageTab
                Accessible.name: String(modelData.label || "")
                Accessible.focusable: tb.interactive
                Accessible.selectable: true
                Accessible.selected: tab.on
                Accessible.onPressAction: if (tb.interactive) tb.tabActivated(tab.key)
                Row {
                    id: tl
                    spacing: tb.roles.u(5)
                    anchors.verticalCenter: parent.verticalCenter
                    Text { id: tlt; objectName: "sdTabLabel"; text: tab.modelData.label; font: tb.tabFont; color: tab.on ? tb.roles.ink : tb.roles.ink2 }
                    Text { objectName: "sdTabCount"; visible: tb.counts && tab.modelData.count !== undefined; text: tab.modelData.count === undefined ? "" : tab.modelData.count
                           font: tb.roles.fFigS; color: tb.roles.ink3; anchors.baseline: tlt.baseline }
                }
                Rectangle { objectName: "sdTabRule"; visible: tab.on; anchors.bottom: parent.bottom; width: parent.width
                            height: tb.accentUnderline ? tb.roles.u(Theme.tabUnderline) : 2
                            color: tb.accentUnderline ? tb.roles.accent : tb.roles.ink }
                Rectangle {
                    objectName: "sdTabFocus"
                    visible: tb.interactive && tab.activeFocus
                    anchors.fill: tl; anchors.margins: -tb.roles.u(4)
                    radius: tb.roles.u(3); color: "transparent"
                    border.width: Math.max(1, Math.round(tb.roles.u(1.5))); border.color: tb.roles.accent
                }
                MouseArea { enabled: tb.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tb.tabActivated(tab.modelData.key) }
            }
        }
    }
    Text {
        id: chipT
        objectName: tb.chipObjectName
        visible: tb.chip.length > 0
        anchors.right: parent.right
        anchors.verticalCenter: tb.onBaseline ? undefined : parent.verticalCenter
        anchors.baseline: tb.onBaseline ? tabRow.baseline : undefined
        text: tb.chip
        font: tb.interactive && !tb.chipOn ? tb.roles.fSmallL : tb.roles.fSmall
        color: tb.chipOn ? tb.roles.ink : tb.interactive ? tb.roles.link : tb.roles.ink3
        activeFocusOnTab: tb.interactive && visible
        Keys.enabled: tb.interactive
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { tb.chipActivated(); e.accepted = true }
        }
        Accessible.role: Accessible.Link
        Accessible.name: tb.chip
        Accessible.focusable: tb.interactive
        Accessible.onPressAction: if (tb.interactive) tb.chipActivated()
        Rectangle {
            objectName: "sdChipFocus"
            visible: tb.interactive && chipT.activeFocus
            anchors.fill: parent; anchors.margins: -tb.roles.u(4)
            radius: tb.roles.u(3); color: "transparent"
            border.width: Math.max(1, Math.round(tb.roles.u(1.5))); border.color: tb.roles.accent
        }
        MouseArea { enabled: tb.interactive; anchors.fill: parent; anchors.margins: -tb.roles.u(4); cursorShape: Qt.PointingHandCursor; onClicked: tb.chipActivated() }
    }
    Text {
        objectName: "sdReviewNote"
        // on the right (default): it folds when it would meet the tabs; after the tabs: when it would meet the chip
        readonly property real room: tb.noteAfterTabs
            ? tb.width - tabRow.width - tabRow.spacing - (chipT.visible ? chipT.width + tb.roles.u(24) : 0)
            : tb.width - tabRow.width - (chipT.visible ? chipT.width + tb.roles.u(24) : 0) - tb.roles.u(24)
        visible: tb.note.length > 0 && tb.roles.shows("reviewNote", tb.width) && implicitWidth <= room
        anchors.left: tb.noteAfterTabs ? tabRow.right : undefined
        anchors.leftMargin: tb.noteAfterTabs ? tabRow.spacing : 0
        anchors.right: tb.noteAfterTabs ? undefined : (chipT.visible ? chipT.left : parent.right)
        anchors.rightMargin: !tb.noteAfterTabs && chipT.visible ? tb.roles.u(24) : 0
        anchors.verticalCenter: tb.noteAfterTabs ? undefined : parent.verticalCenter
        anchors.baseline: tb.noteAfterTabs ? tabRow.baseline : undefined
        text: tb.note; font: tb.roles.fSmall; color: tb.roles.ink3
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: tb.roles.hair }
}
