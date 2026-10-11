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

// INLINEPICKER — a choice that opens IN PLACE, never a pop-up (BRIEF3 §1; round 4, F67: the declared-miss
// picker opens under the tab row). A quiet question, then one row per choice — the current one marked —
// then the clearing row. It pushes what is below it down; it closes by the register setting `open` false
// (a pick, the chip again, or Esc). Nothing here is coloured: it is a control, not a finding.
// items: [{ id, label, note }]; current: the chosen id ("" = none). picked(id) — "" for the clearing row.
Column {
    id: ip
    objectName: "sdMissPicker"
    property QtObject roles: PpTypeset {}
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property bool open: true
    property string title: "What is the bad shot?"
    property var items: []
    property string current: ""
    property string clearLabel: "No declared miss"
    property string itemObjectName: "sdMissCandidate"
    property string clearObjectName: "sdMissClear"
    readonly property var keyKinds: []
    signal picked(string id)

    visible: open
    spacing: 0
    topPadding: roles.u(6); bottomPadding: roles.u(8)
    Text { objectName: "sdMissPickerLabel"; text: ip.title; font: ip.roles.fLabel; color: ip.roles.ink3; bottomPadding: ip.roles.u(4) }
    Repeater {
        model: ip.roles && ip.open ? (ip.items || []) : []
        delegate: Item {
            id: it
            objectName: ip.itemObjectName
            required property var modelData
            readonly property string missId: modelData.id
            readonly property bool on: ip.current === modelData.id
            width: ip.width; height: Math.max(ip.roles.u(24), lab.height + ip.roles.u(8))
            Rectangle { anchors.fill: parent; visible: ma.containsMouse; color: ip.roles.rowHover }
            Text { x: ip.roles.u(2); anchors.verticalCenter: parent.verticalCenter; text: it.on ? "●" : "○"; font: ip.roles.fSmall; color: it.on ? ip.roles.ink : ip.roles.ink3 }
            Text { id: lab; x: ip.roles.u(20); width: parent.width - x; anchors.verticalCenter: parent.verticalCenter; wrapMode: Text.WordWrap; textFormat: Text.StyledText
                   text: ip.roles.esc(it.modelData.label) + (it.modelData.note ? "  <font color=\"" + ip.roles.hex(ip.roles.ink3) + "\">" + ip.roles.esc(it.modelData.note) + "</font>" : "")
                   font: it.on ? ip.roles.fBodyM : ip.roles.fBody; color: ip.roles.ink }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ip.roles.hw; color: ip.roles.hair }
            MouseArea { id: ma; enabled: ip.interactive; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ip.picked(it.modelData.id) }
        }
    }
    Item {
        objectName: ip.clearObjectName
        visible: ip.open
        width: ip.width; height: ip.roles.u(26)
        Text { x: ip.roles.u(20); anchors.verticalCenter: parent.verticalCenter; text: ip.clearLabel
               font: ip.current === "" ? ip.roles.fSmall : ip.roles.fSmallL; color: ip.current === "" ? ip.roles.ink : ip.roles.link }
        MouseArea { enabled: ip.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ip.picked("") }
    }
}
