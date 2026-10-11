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
    signal tabActivated(string key)
    signal chipActivated()
    readonly property var keyKinds: []

    readonly property bool counts: roles.shows("tabCounts", width)
    implicitHeight: roles.u(28)
    height: implicitHeight
    Row {
        id: tabRow
        anchors.bottom: parent.bottom
        spacing: tb.roles.atLeast(tb.width, "compact") ? tb.roles.u(26) : tb.roles.u(18)
        Repeater {
            model: tb.roles ? (tb.tabs) : []
            delegate: Item {
                id: tab
                objectName: "sdTab"
                required property var modelData
                readonly property string key: modelData.key
                readonly property bool on: tb.current === modelData.key
                width: tl.implicitWidth; height: tb.height
                Row {
                    id: tl
                    spacing: tb.roles.u(5)
                    anchors.verticalCenter: parent.verticalCenter
                    Text { id: tlt; objectName: "sdTabLabel"; text: tab.modelData.label; font: tb.roles.fTab; color: tab.on ? tb.roles.ink : tb.roles.ink2 }
                    Text { objectName: "sdTabCount"; visible: tb.counts && tab.modelData.count !== undefined; text: tab.modelData.count === undefined ? "" : tab.modelData.count
                           font: tb.roles.fFigS; color: tb.roles.ink3; anchors.baseline: tlt.baseline }
                }
                Rectangle { visible: tab.on; anchors.bottom: parent.bottom; width: parent.width; height: 2; color: tb.roles.ink }
                MouseArea { enabled: tb.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: tb.tabActivated(tab.modelData.key) }
            }
        }
    }
    Text {
        id: chipT
        objectName: tb.chipObjectName
        visible: tb.chip.length > 0
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        text: tb.chip
        font: tb.interactive && !tb.chipOn ? tb.roles.fSmallL : tb.roles.fSmall
        color: tb.chipOn ? tb.roles.ink : tb.interactive ? tb.roles.link : tb.roles.ink3
        MouseArea { enabled: tb.interactive; anchors.fill: parent; anchors.margins: -tb.roles.u(4); cursorShape: Qt.PointingHandCursor; onClicked: tb.chipActivated() }
    }
    Text {
        objectName: "sdReviewNote"
        visible: tb.note.length > 0 && tb.roles.shows("reviewNote", tb.width)
                 && implicitWidth <= tb.width - tabRow.width - (chipT.visible ? chipT.width + tb.roles.u(24) : 0) - tb.roles.u(24)
        anchors.right: chipT.visible ? chipT.left : parent.right; anchors.rightMargin: chipT.visible ? tb.roles.u(24) : 0
        anchors.verticalCenter: parent.verticalCenter
        text: tb.note; font: tb.roles.fSmall; color: tb.roles.ink3
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: tb.roles.hair }
}
