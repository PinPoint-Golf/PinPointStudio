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

// WORK ONS — the faults the current athlete keeps producing, listed on the home screen in the
// DEVICES list's own language: a micro heading and one quiet row each. Deliberately not front
// and centre (docs/design/work_ons_design.md): this is the concept on trial before the
// diagnosing / improving modes are built around it.
//
// A row is a dot, a name and a count of swings. Clicking it opens the row in place — what
// the fault costs, the latest session's reading, one tick per session, a drill where the pack
// authors one, and a way into the session that last showed it. One row open at a time.
//
// Every string is WorkOnsController's. This file positions and paints.

import QtQuick
import PinPointStudio

Column {
    id: root

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: (typeof workOns !== "undefined") ? workOns : null
    // Rows drawn before "show all".
    property int collapsedCount: 5

    signal reviewSessionRequested(string sessionDir)

    readonly property var  items:     controller ? controller.items : []
    readonly property bool updating:  controller ? controller.updating : false
    property string openId: ""
    property bool   showAll: false

    width:   parent ? parent.width : 0
    spacing: 0

    Item {
        width:  root.width
        height: Theme.sp(34)

        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text:               qsTr("WORK ONS")
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              Theme.colorText3
        }

        // The catch-up, said once and quietly: sessions recorded before this existed are being
        // read, and the list below may still grow.
        Text {
            objectName: "workOnsUpdating"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: root.updating && root.controller
                     && root.controller.sessionCount < root.controller.sessionsFound
            text: root.controller
                  ? qsTr("reading %1 of %2 sessions").arg(root.controller.sessionCount)
                                                      .arg(root.controller.sessionsFound)
                  : ""
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              Theme.colorText3
        }
    }
    Item { width: 1; height: 12 }

    Item {
        visible: root.items.length === 0
        height:  visible ? 40 : 0
        width:   root.width

        Text {
            objectName: "workOnsEmpty"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: root.updating ? qsTr("Reading your sessions…")
                                : qsTr("Nothing yet. A fault is listed once it recurs across a session's shots.")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText3
        }
    }

    Repeater {
        model: root.showAll ? root.items : root.items.slice(0, root.collapsedCount)

        Column {
            id: rowCol
            required property var modelData
            readonly property bool open: root.openId === modelData.id
            objectName: "workOnRow"
            width: root.width

            Item {
                width:  parent.width
                height: Theme.sp(40)

                Row {
                    anchors.fill: parent
                    spacing: 0

                    Item {
                        width: Theme.sp(20); height: parent.height
                        Rectangle {
                            width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                            anchors.centerIn: parent
                            // Still a fault, or on its way out. Nothing here is an alarm.
                            // HOLLOW when no recent session could judge it: placed on old
                            // evidence, which may still hold and may not.
                            readonly property color tone: rowCol.modelData.status === "easing" ? Theme.colorGood
                                                                                               : Theme.colorWarn
                            color:        rowCol.modelData.unconfirmed ? "transparent" : tone
                            border.width: rowCol.modelData.unconfirmed ? 1 : 0
                            border.color: tone
                        }
                    }

                    Text {
                        width:             parent.width - Theme.sp(20) - Theme.sp(240)
                        height:            parent.height
                        text:              rowCol.modelData.name
                        font.family:       Theme.fontBody
                        font.pixelSize:    Theme.fontSzBody
                        color:             rowPress.containsMouse ? Theme.colorAccent : Theme.colorText
                        elide:             Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    Text {
                        width:  Theme.sp(240)
                        height: parent.height
                        text:   rowCol.modelData.status === "easing" ? qsTr("easing")
                              : rowCol.modelData.unconfirmed
                                    ? rowCol.modelData.lastSeenText + " · " + rowCol.modelData.countText
                                    : rowCol.modelData.countText
                        font.family:         Theme.fontData
                        font.pixelSize:      Theme.fontSzMicro
                        font.letterSpacing:  Theme.trackingData
                        color:               rowCol.modelData.status === "easing" ? Theme.colorGood
                                                                                  : Theme.colorText3
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment:   Text.AlignVCenter
                    }
                }

                MouseArea {
                    id: rowPress
                    objectName: "workOnPress"
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    root.openId = rowCol.open ? "" : rowCol.modelData.id
                }
            }

            // ── The row, opened ─────────────────────────────────────────────
            Column {
                objectName: "workOnDetail"
                visible: rowCol.open
                x:       Theme.sp(20)
                width:   parent.width - Theme.sp(20)
                spacing: Theme.sp(8)

                Text {
                    width:          parent.width
                    visible:        text !== ""
                    text:           rowCol.modelData.consequence
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText2
                    wrapMode:       Text.WordWrap
                    lineHeight:     1.4
                }

                // One tick per session, oldest first, then the count the row quoted in words.
                Row {
                    spacing: Theme.sp(10)
                    PpTickRun {
                        anchors.verticalCenter: parent.verticalCenter
                        ticks: rowCol.modelData.ticks
                        width: implicitWidth
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rowCol.modelData.coverageText !== ""
                              ? qsTr("on %1 · %2").arg(rowCol.modelData.countText)
                                                  .arg(rowCol.modelData.coverageText)
                              : qsTr("on %1").arg(rowCol.modelData.countText)
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color:              Theme.colorText3
                    }
                }

                Repeater {
                    model: [ rowCol.modelData.statusText, rowCol.modelData.latestText,
                             rowCol.modelData.causedByText ]
                    Text {
                        required property string modelData
                        width:              parent.width
                        visible:            modelData !== ""
                        text:               modelData
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color:              Theme.colorText3
                        wrapMode:           Text.WordWrap
                    }
                }

                Text {
                    width:          parent.width
                    visible:        rowCol.modelData.drillLabel !== ""
                    text:           qsTr("Try: %1 — %2").arg(rowCol.modelData.drillLabel)
                                                         .arg(rowCol.modelData.drillInstruction)
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText2
                    wrapMode:       Text.WordWrap
                    lineHeight:     1.4
                }

                Text {
                    objectName:     "workOnReview"
                    visible:        rowCol.modelData.sessionDir !== ""
                    text:           qsTr("Review the %1 session →").arg(rowCol.modelData.sessionLabel)
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorAccent

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.reviewSessionRequested(rowCol.modelData.sessionDir)
                    }
                }

                Item { width: 1; height: Theme.sp(4) }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }
        }
    }

    // What the collapsed list is not showing, and what has left it.
    Item {
        readonly property int hidden: Math.max(0, root.items.length - root.collapsedCount)
        readonly property int cleared: root.controller ? root.controller.clearedCount : 0
        visible: hidden > 0 || cleared > 0
        width:   root.width
        height:  visible ? Theme.sp(32) : 0

        Text {
            objectName: "workOnsMore"
            anchors { left: parent.left; leftMargin: Theme.sp(20); verticalCenter: parent.verticalCenter }
            visible: parent.hidden > 0
            text:    root.showAll ? qsTr("Show fewer") : qsTr("%1 more").arg(parent.hidden)
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorAccent

            MouseArea {
                anchors.fill: parent
                cursorShape:  Qt.PointingHandCursor
                onClicked:    root.showAll = !root.showAll
            }
        }

        Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: parent.cleared > 0
            text:    qsTr("%1 cleared").arg(parent.cleared)
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              Theme.colorGood
        }
    }
}
