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
import PinPointStudio

// One ANALYSIS RUNS row: a summary line (time · session · ok · frames/total/score)
// that expands on tap to the per-stage breakdown. Values come pre-formatted from
// ProfilerController — pure bindings here.
Rectangle {
    id: root

    property var  runData
    property bool isAlternate: false
    property bool expanded: false

    width: parent ? parent.width : 0
    implicitHeight: col.implicitHeight
    height: implicitHeight
    color: isAlternate ? Theme.colorBg : Theme.colorSurface

    readonly property color okColor: (root.runData && root.runData.ok) ? Theme.colorGood : Theme.colorWarn

    Column {
        id: col
        width: parent.width

        // ── Summary row ──────────────────────────────────────────────────────
        Item {
            id: summary
            width: parent.width
            height: Theme.sp(30)
            visible: root.runData !== null && root.runData !== undefined

            Row {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: Theme.sp(10) }
                spacing: Theme.sp(8)

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.sp(10)
                    text: root.expanded ? "▾" : "▸"
                    font.pixelSize: Theme.sp(10)
                    color: Theme.colorText3
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.sp(52)
                    text: root.runData.timestamp
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzDataSm
                    color: Theme.colorText3
                }
                // Session badge
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: sessLbl.implicitWidth + Theme.sp(10)
                    height: Theme.sp(16)
                    radius: Theme.sp(3)
                    color: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.12)
                    border.width: 1
                    border.color: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.35)
                    Text {
                        id: sessLbl
                        anchors.centerIn: parent
                        text: root.runData.sessionLabel
                        font.family: Theme.fontData
                        font.pixelSize: Theme.sp(9)
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorAccent
                    }
                }
                // Halted badge (only when not ok)
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.runData.ok
                    width: okLbl.implicitWidth + Theme.sp(10)
                    height: Theme.sp(16)
                    radius: Theme.sp(3)
                    color: Qt.rgba(root.okColor.r, root.okColor.g, root.okColor.b, 0.12)
                    border.width: 1
                    border.color: Qt.rgba(root.okColor.r, root.okColor.g, root.okColor.b, 0.35)
                    Text {
                        id: okLbl
                        anchors.centerIn: parent
                        text: root.runData.okStr
                        font.family: Theme.fontData
                        font.pixelSize: Theme.sp(9)
                        color: root.okColor
                    }
                }
            }

            // Right-aligned metrics: frames · total · score
            Text {
                anchors { right: parent.right; rightMargin: Theme.sp(10); verticalCenter: parent.verticalCenter }
                text: root.runData.frames + qsTr(" fr · ") + root.runData.totalMsStr + qsTr(" · score ") + root.runData.scoreStr
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzDataSm
                color: Theme.colorText2
            }

            TapHandler   { onTapped: root.expanded = !root.expanded }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }

        // ── Per-stage breakdown (only built while expanded) ──────────────────
        Column {
            id: stagesCol
            width: parent.width
            visible: root.expanded

            Repeater {
                model: root.expanded && root.runData ? root.runData.stages : []

                Rectangle {
                    width: stagesCol.width
                    height: Theme.sp(22)
                    // Zebra-stripe the per-stage rows (like the scopes table) so the
                    // stage name on the left reads across to its value on the right.
                    // Offset by the run's own band so the first stage always contrasts
                    // with the summary row above — one continuous stripe per run.
                    color: (index % 2 === (root.isAlternate ? 1 : 0)) ? Theme.colorBg : Theme.colorSurface

                    Text {
                        anchors { left: parent.left; leftMargin: Theme.sp(34); verticalCenter: parent.verticalCenter }
                        text: modelData.name
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzDataSm
                        color: modelData.ran ? Theme.colorText2 : Theme.colorText3
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: Theme.sp(10); verticalCenter: parent.verticalCenter }
                        text: modelData.ran
                              ? modelData.msStr
                              : (modelData.skipReason.length > 0
                                 ? qsTr("skipped · ") + modelData.skipReason
                                 : qsTr("skipped"))
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzDataSm
                        color: modelData.ran ? Theme.colorText2 : Theme.colorText3
                    }
                }
            }
        }
    }
}
