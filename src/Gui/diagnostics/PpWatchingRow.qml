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

// Watching (n) — seen, not yet evidence, and it never headlines.
//
// COLLAPSED BY DEFAULT AND TRUNCATING, which is the design's judgement rather than a space
// saving: a condition below the pattern gate has fired at least once, and giving it the
// same weight as a pattern would be the panel claiming a recurrence it cannot support
// (brief §3.2). One tracked line says the model is watching without asserting anything.
//
// Expanding opens THE SAME CONTENT larger, not a different view (brief §8) — the same rows,
// one per line, so nothing is revealed by expanding that the collapsed row was hiding.
//
// Unfolded, each is a row of the card in the FAULTS idiom: a hairline above it and the dashed
// ring leading the name — the unconfirmed badge, because a watched condition is exactly that:
// seen, and not yet evidence.

import QtQuick
import PinPointStudio

Item {
    id: root

    // SessionDiagnosticsModel::watching() — [{ id, name, recurrence }]
    property var items: []
    property bool expanded: false
    // Off on the session panel's WATCHING tab, where the rows are the whole tab: no fold, no
    // caret, and the label says what the list is rather than counting it (the tab counts).
    property bool foldable: true
    // The panel's fit scale. See PpSessionDiagnosticsBody._fitFor().
    property real fit: 1.0

    signal toggled()
    // A watched condition opens too. It has a ledger like any other — a recurrence, a run, an
    // authored ancestry — and the only thing below the pattern gate is the CLAIM, not the
    // evidence. Cheap and consistent: the row a golfer is most likely to ask "why?" about is the
    // one the panel is deliberately not asserting anything about yet.
    signal itemActivated(string conditionId)

    objectName: "sdWatchingRow"

    function px(n) { return Math.round(n * Theme.fontScale * root.fit) }

    readonly property int tzCaption: Math.max(1, Math.round(Theme.sp(8) * fit))
    readonly property int tzMicro:   Math.max(1, Math.round(Theme.fontSzMicro * fit))
    readonly property int tzBody:    Math.max(1, Math.round(Theme.fontSzBody2 * fit))
    readonly property int count: items ? items.length : 0

    // Held off screen while the panel is showing a condition detail. A `visible:` at the use site
    // would REPLACE the count binding below rather than combining with it, and a watching row
    // that reappeared empty once the detail closed would be a row asserting nothing about two
    // conditions it no longer has.
    property bool suppressed: false

    visible: !suppressed && count > 0
    implicitHeight: visible ? (expanded ? body.implicitHeight : px(22)) : 0

    // The joined one-liner. The separator is the only thing this file contributes — every
    // name and every recurrence count is the model's wording.
    readonly property string _line: {
        if (!items) return ""
        const parts = []
        for (let i = 0; i < items.length; ++i)
            parts.push(items[i].name + " " + items[i].recurrence)
        return parts.join(" · ")
    }

    // COLLAPSE/EXPAND, declared FIRST so the expanded rows' own taps sit on top of it: in
    // QtQuick declaration order is hit order, and a row that opened a condition must not also
    // fold the region it is in. Collapsed there are no rows, so this is the whole surface.
    MouseArea {
        anchors.fill: parent
        enabled: root.foldable
        onClicked: root.toggled()
    }

    Column {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: root.foldable ? root.px(4) : 0

        Item {
            width: parent.width
            height: root.px(22)

            PpMicro {
                id: label
                objectName: "sdWatchingLabel"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.foldable
                      ? (root.expanded ? "▴ " : "▾ ") + Theme.caps(qsTr("Watching (%1)")).arg(root.count)
                      : Theme.caps(qsTr("Seen, not yet a pattern · tap one to trace it"))
                font.pixelSize: root.tzMicro
            }
            Text {
                objectName: "sdWatchingLine"
                anchors.left: label.right
                anchors.leftMargin: root.px(9)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.expanded
                text: root._line
                elide: Text.ElideRight
                font.family: Theme.fontData
                font.pixelSize: root.tzMicro
                color: Theme.colorText2
            }
        }

        Repeater {
            model: root.expanded ? root.items : []

            Item {
                id: watchItem
                required property var modelData
                objectName: "sdWatchingItem"
                width: body.width
                height: root.foldable ? root.px(16) : root.px(36)

                Rectangle {
                    visible: !root.foldable
                    width: parent.width; height: 1
                    color: Theme.colorBorder
                }
                PpBadge {
                    id: watchBadge
                    visible: !root.foldable
                    anchors.verticalCenter: parent.verticalCenter
                    size: root.px(16)
                    kind: "unconfirmed"
                    tone: Theme.colorText3
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: root.foldable ? root.px(14) : watchBadge.width + root.px(10)
                    anchors.right: recurrence.left
                    anchors.rightMargin: root.px(9)
                    anchors.verticalCenter: parent.verticalCenter
                    text: watchItem.modelData.name || ""
                    elide: Text.ElideRight
                    font.family: Theme.fontBody
                    font.pixelSize: root.foldable ? root.tzMicro : root.tzBody
                    font.weight: Theme.fontBodyWeight
                    color: itemTap.containsMouse ? Theme.colorAccent : Theme.colorText
                }
                Text {
                    id: recurrence
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: watchItem.modelData.recurrence || ""
                    font.family: Theme.fontData
                    font.pixelSize: root.tzMicro
                    color: Theme.colorText3
                }

                MouseArea {
                    id: itemTap
                    objectName: "sdWatchingItemTap"
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.itemActivated(watchItem.modelData.id || "")
                }
            }
        }
    }
}
