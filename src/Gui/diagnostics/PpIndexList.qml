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

// INDEXLIST — A's index of the clean conditions: name, leader dots, value and unit on a right edge,
// in as many columns as fit (roles.flows.index em each, column-major so it reads down). Ink marks a
// session pattern clean on this shot, ° marks a watched condition; its fault line follows
// in brackets, the fault line. `grouped` adds B's group heads
// (patterns clean here / watched / clean all session), alphabetical within each group.
Item {
    id: ix
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var conditions: []                // readout.conditions filtered to the clean ones (any order)
    property bool grouped: false
    property string title: "Inside the corridor"
    property bool showHead: true
    property string legend: "In brackets: the fault line  ·  Ink: a session pattern, clean on this shot  ·  ° watching  ·  the rest clean all session"
    signal conditionActivated(string id)
    signal conditionHovered(string id)

    readonly property var keyKinds: []
    readonly property var groupsDef: [["pattern", "Session patterns, clean here"], ["watching", "Watched, clean here"], ["clean all session", "Clean all session"]]
    readonly property var items: {
        const cs = (conditions || []).slice().sort((a, b) => String(a.name).localeCompare(String(b.name)))
        if (!grouped) return cs.map(c => ({ c: c, head: "" }))
        const out = []
        groupsDef.forEach(gd => {
            const xs = cs.filter(c => c.tierTag === gd[0])
            xs.forEach((c, i) => out.push({ c: c, head: i === 0 ? gd[1] + " — " + xs.length : "" }))
        })
        const other = cs.filter(c => groupsDef.every(gd => gd[0] !== c.tierTag))
        other.forEach((c, i) => out.push({ c: c, head: i === 0 ? "Other" : "" }))
        return out
    }
    readonly property real cg: roles.u(22)
    readonly property int cols: roles.columns(width, roles.flows.index, cg)
    readonly property real cw: (width - (cols - 1) * cg) / cols
    readonly property real rowH: Math.max(roles.u(12.6), roles.fSmall.pixelSize * 1.3 + roles.u(2))

    implicitHeight: col.height
    height: implicitHeight

    Column {
        id: col
        width: ix.width
        Item {
            property bool atom: true
            property bool keepNext: true
            visible: ix.showHead
            width: ix.width; height: visible ? ihead.height + ix.roles.u(6) : 0
            Flow {
                id: ihead
                width: parent.width
                spacing: ix.roles.u(8)
                Text { id: ttl; text: ix.title; font: ix.roles.fSection; color: ix.roles.ink }
                Text { text: (ix.conditions || []).length; font: ix.roles.fFigS; color: ix.roles.ink3; height: ttl.height; verticalAlignment: Text.AlignVCenter }
                Item { width: ix.roles.u(8); height: 1 }
                Text {
                    visible: !ix.grouped && ix.legend.length > 0
                    width: Math.min(implicitWidth, ihead.width); wrapMode: Text.WordWrap
                    height: Math.max(implicitHeight, ttl.height); verticalAlignment: Text.AlignVCenter
                    text: ix.legend; font: ix.roles.fMicro; color: ix.roles.ink3
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: ix.roles.hw; color: ix.roles.hair }
        }
        Item { width: 1; height: ix.roles.u(4) }
        Grid {
            id: grid
            property bool atom: true
            columns: ix.cols
            rows: Math.ceil(ix.items.length / ix.cols)
            flow: Grid.TopToBottom
            columnSpacing: ix.cg
            Repeater {
                model: ix.roles ? (ix.items) : []
                delegate: Item {
                    id: it
                    required property var modelData
                    readonly property var c: modelData.c
                    readonly property bool hot: ix.brush && ix.brush.hoveredCondition === c.id
                    width: ix.cw; height: (gh.visible ? gh.height : 0) + Math.max(ix.rowH, inm.height + ix.roles.u(2))
                    opacity: ix.brush ? ix.brush.conditionOpacity(c.id, ix.roles.dim) : 1
                    Behavior on opacity { NumberAnimation { duration: ix.roles.normal } }
                    Text {
                        id: gh
                        visible: !!it.modelData.head
                        topPadding: ix.roles.u(6); bottomPadding: ix.roles.u(2)
                        text: it.modelData.head || ""
                        font: ix.roles.fSmallM; color: ix.roles.ink
                    }
                    Item {
                        y: gh.visible ? gh.height : 0
                        width: parent.width; height: parent.height - y
                        Rectangle { anchors.fill: parent; visible: it.hot; color: ix.roles.rowHover }
                        Text {
                            id: imed; anchors.right: parent.right; anchors.baseline: inm.baseline
                            text: it.c.faultText ? " (" + it.c.faultText + ")" : ""
                            font: ix.roles.fSmall; color: ix.roles.ink3
                        }
                        Text {
                            id: iunit; anchors.right: imed.left; anchors.baseline: inm.baseline
                            text: ix.roles.shortUnit(ix.roles.unitOf(it.c.valueText)); font: ix.roles.fSmall; color: ix.roles.ink3
                        }
                        Text {
                            id: ival; anchors.right: iunit.left; anchors.rightMargin: iunit.text.length ? 3 : 0
                            anchors.baseline: inm.baseline
                            text: ix.roles.fig(it.c.valueText); font: ix.roles.fFigS; color: ix.roles.ink2
                        }
                        Text {
                            id: inm
                            y: ix.roles.u(1)
                            width: Math.min(implicitWidth, ix.cw - ival.width - iunit.width - imed.width - ix.roles.u(16))
                            wrapMode: Text.WordWrap
                            text: it.c.name + (!ix.grouped && it.c.tierTag === "watching" ? "°" : "")
                            font: ix.roles.fSmall
                            color: it.c.tierTag === "pattern" || it.hot ? ix.roles.ink : ix.roles.ink2
                        }
                        Item {
                            visible: inm.lineCount === 1
                            anchors { left: inm.right; leftMargin: 4; right: ival.left; rightMargin: 5; top: inm.top; bottom: inm.bottom }
                            clip: true
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                text: ix.roles.r.leader.repeat(60); font: ix.roles.fSmall; color: Qt.alpha(ix.roles.ink3, 0.8)
                            }
                        }
                        MouseArea {
                            enabled: ix.interactive; visible: enabled
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onContainsMouseChanged: { if (ix.brush) ix.brush.hoverCondition(it.c.id, containsMouse); ix.conditionHovered(containsMouse ? it.c.id : "") }
                            onClicked: ix.conditionActivated(it.c.id)
                        }
                    }
                }
            }
        }
        Item { width: 1; height: ix.roles.u(4) }
        Rectangle { width: ix.width; height: ix.roles.hw; color: ix.roles.hair }
    }
}
