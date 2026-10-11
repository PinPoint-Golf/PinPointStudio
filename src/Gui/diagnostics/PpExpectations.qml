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

// EXPECTATIONS — the Cold session's prior (round 4, F41/F42): what this golfer's fault profile says to
// look for, before the session has enough swings to call a pattern. Each expectation is a dashed row:
// its name, then the profile's sentence. Headed "Usually yours · expectations to test, not findings" and
// closed by the caption that it biases ranking and presentation only. NOT drawn at all when the list is
// empty (the restraint rule). The cold line ("Too few swings to call a pattern.") is the register's head.
// items: source.expectations [{ name, text }].
Column {
    id: ex
    objectName: "sdExpectations"
    property QtObject roles: PpDiagRoles {}
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property var items: []
    property string title: "Usually yours"
    property string sub: "expectations to test, not findings"
    property string caption: "From your fault profile. It biases what is ranked and shown first; it never counts as a firing."
    property bool atom: false
    readonly property var keyKinds: []
    readonly property bool hasItems: (items || []).length > 0

    visible: hasItems
    height: hasItems ? implicitHeight : 0
    spacing: roles.u(6)
    Item {
        property bool atom: true
        property bool keepNext: true
        width: ex.width; height: hd.height + ex.roles.u(4)
        Flow {
            id: hd
            width: parent.width; spacing: ex.roles.u(10)
            Text { id: ht; text: ex.title; font: ex.roles.fSection; color: ex.roles.ink }
            Text { text: ex.sub; font: ex.roles.fSmallI; color: ex.roles.ink3; height: ht.height; verticalAlignment: Text.AlignVCenter }
        }
    }
    Repeater {
        model: ex.roles && ex.hasItems ? ex.items : []
        delegate: Item {
            id: card
            objectName: "sdExpectationCard"
            property bool atom: true
            required property var modelData
            width: ex.width; height: col.height + ex.roles.u(12)
            // the dashed frame: drawn, so the row reads as unconfirmed at a glance
            Canvas {
                anchors.fill: parent
                readonly property var deps: [width, height, ex.roles.ink3]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset()
                    ctx.strokeStyle = Qt.alpha(ex.roles.ink3, 0.8); ctx.lineWidth = 1; ctx.setLineDash([3, 3])
                    ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                }
            }
            Column {
                id: col
                x: ex.roles.u(10); y: ex.roles.u(6); width: card.width - 2 * ex.roles.u(10)
                spacing: ex.roles.u(2)
                Text { width: parent.width; wrapMode: Text.WordWrap; text: card.modelData.name || ""; font: ex.roles.fBody; color: ex.roles.ink2 }
                Text { visible: !!card.modelData.text; width: Math.min(parent.width, ex.roles.flows.measure * ex.roles.em); wrapMode: Text.WordWrap
                       text: card.modelData.text || ""; font: ex.roles.fSmall; color: ex.roles.ink3; lineHeight: ex.roles.r.smallLeading }
            }
        }
    }
    Text {
        property bool atom: true
        property bool keepPrev: true
        width: Math.min(ex.width, ex.roles.flows.measure * ex.roles.em * 1.3); wrapMode: Text.WordWrap
        text: ex.caption; font: ex.roles.fMicro; color: ex.roles.ink3
    }
}
