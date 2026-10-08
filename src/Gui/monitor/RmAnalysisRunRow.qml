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
import QtQuick.Controls.Basic
import PinPointStudio

// One ANALYSIS RUNS row: a summary line (time · session · ok · frames/total/score)
// that expands on tap to the run's timeline — the critical path's summary, one lane
// per pool thread, and a waterfall of the stages in the order they started. Values
// come pre-formatted from ProfilerController (the critical path is marked when the
// run is recorded, analysis_profiling.cpp) — pure bindings and geometry here.
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

    // The run as a timeline: every stage at its offset on one axis, 0 … spanMs (the
    // last stage's end), shared by the thread lanes and the waterfall rows. Bars run
    // from the stage label's left edge to just short of the value column.
    readonly property real spanMs: (root.runData && root.runData.spanMs) ? root.runData.spanMs : 0
    readonly property int  laneCount: (root.runData && root.runData.threads) ? root.runData.threads : 0
    readonly property real trackX: Theme.sp(34)
    readonly property real trackW: Math.max(0, root.width - root.trackX - Theme.sp(72))
    readonly property color pathColor:  Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.85)
    readonly property color otherColor: Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.30)

    // Axis ticks at a round step, at most five intervals over the span.
    readonly property var ticks: {
        if (root.spanMs <= 0)
            return []
        var steps = [50, 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000, 20000, 50000]
        var step = steps[steps.length - 1]
        for (var i = 0; i < steps.length; ++i) {
            if (root.spanMs / steps[i] <= 5) {
                step = steps[i]
                break
            }
        }
        var out = []
        for (var t = 0; t <= root.spanMs; t += step)
            out.push(t)
        return out
    }

    function barX(ms) { return root.spanMs > 0 ? root.trackX + root.trackW * Math.max(0, ms) / root.spanMs : root.trackX }
    function barW(s)  { return root.spanMs > 0 ? Math.max(Theme.sp(2), root.trackW * (s.endMs - s.startMs) / root.spanMs) : 0 }
    function fmtOffset(ms) { return ms >= 1000 ? (ms / 1000).toFixed(ms % 1000 === 0 ? 0 : 1) + " s" : ms.toFixed(0) + " ms" }

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

        // ── Timeline (only built while expanded) ─────────────────────────────
        Column {
            id: stagesCol
            width: parent.width
            visible: root.expanded

            // Where the wall time went: the chain of stages that set it.
            Item {
                width: stagesCol.width
                height: Theme.sp(24)
                visible: root.expanded && root.spanMs > 0
                Text {
                    anchors { left: parent.left; leftMargin: root.trackX; right: legend.left; rightMargin: Theme.sp(8); verticalCenter: parent.verticalCenter }
                    text: root.runData ? root.runData.pathStr : ""
                    elide: Text.ElideRight
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzDataSm
                    color: Theme.colorText2
                }
                Row {
                    id: legend
                    anchors { right: parent.right; rightMargin: Theme.sp(10); verticalCenter: parent.verticalCenter }
                    spacing: Theme.sp(6)
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.sp(10); height: Theme.sp(6); radius: Theme.sp(1); color: root.pathColor }
                    Text { text: qsTr("critical path"); font.family: Theme.fontData; font.pixelSize: Theme.sp(9); color: Theme.colorText3 }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: Theme.sp(10); height: Theme.sp(6); radius: Theme.sp(1); color: root.otherColor }
                    Text { text: qsTr("off the path"); font.family: Theme.fontData; font.pixelSize: Theme.sp(9); color: Theme.colorText3 }
                }
            }

            // One lane per pool thread: what ran where, what overlapped, and the gaps.
            Item {
                id: lanes
                readonly property real laneH: Theme.sp(12)
                readonly property real laneGap: Theme.sp(3)
                readonly property real lanesH: root.laneCount * (laneH + laneGap)
                width: stagesCol.width
                height: lanesH + Theme.sp(16)
                visible: root.expanded && root.spanMs > 0 && root.laneCount > 0

                Repeater {
                    model: root.expanded ? root.laneCount : 0
                    Item {
                        y: index * (lanes.laneH + lanes.laneGap)
                        width: lanes.width
                        height: lanes.laneH
                        Text {
                            anchors { left: parent.left; leftMargin: Theme.sp(10); verticalCenter: parent.verticalCenter }
                            text: "T" + index
                            font.family: Theme.fontData
                            font.pixelSize: Theme.sp(9)
                            color: Theme.colorText3
                        }
                        Rectangle {
                            x: root.trackX
                            width: root.trackW
                            height: parent.height
                            radius: Theme.sp(2)
                            color: Theme.colorBg3
                        }
                    }
                }

                Repeater {
                    model: root.expanded && root.runData ? root.runData.stages : []
                    Rectangle {
                        z: 2
                        visible: modelData.ran
                        x: root.barX(modelData.startMs)
                        y: modelData.thread * (lanes.laneH + lanes.laneGap)
                        width: root.barW(modelData)
                        height: lanes.laneH
                        radius: Theme.sp(2)
                        color: modelData.critical ? root.pathColor : root.otherColor
                        // A hairline in the row colour separates back-to-back stages.
                        border.width: 1
                        border.color: Theme.colorSurface

                        HoverHandler { id: laneHover }
                        ToolTip.visible: laneHover.hovered
                        ToolTip.delay: 200
                        ToolTip.text: modelData.name + " · " + modelData.msStr + " · " + modelData.spanStr
                                      + (modelData.critical ? qsTr(" · critical path") : "")
                    }
                }

                // Time axis under the lanes, its gridlines behind the bars (z 1 < 2).
                Repeater {
                    model: root.expanded ? root.ticks : []
                    Item {
                        z: 1
                        Rectangle {
                            x: root.barX(modelData)
                            width: 1
                            height: lanes.lanesH - lanes.laneGap
                            color: Qt.rgba(Theme.colorText3.r, Theme.colorText3.g, Theme.colorText3.b, 0.25)
                        }
                        Text {
                            y: lanes.lanesH + Theme.sp(1)
                            x: Math.min(root.trackX + root.trackW - width,
                                        Math.max(root.trackX, root.barX(modelData) - width / 2))
                            text: root.fmtOffset(modelData)
                            font.family: Theme.fontData
                            font.pixelSize: Theme.sp(9)
                            color: Theme.colorText3
                        }
                    }
                }
            }

            // Waterfall: one row per stage in the order they started, its bar where it
            // ran on the same axis as the lanes.
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

                    // Drawn first, so the text stays on top and legible.
                    Rectangle {
                        x: root.barX(modelData.startMs)
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.barW(modelData)
                        height: Theme.sp(14)
                        radius: Theme.sp(2)
                        visible: modelData.ran && root.spanMs > 0
                        color: modelData.critical
                               ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.45)
                               : Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b, 0.20)
                    }

                    // Critical-path marker.
                    Rectangle {
                        anchors { left: parent.left; leftMargin: Theme.sp(20); verticalCenter: parent.verticalCenter }
                        width: Theme.sp(6)
                        height: width
                        radius: width / 2
                        visible: modelData.ran && modelData.critical
                        color: Theme.colorAccent
                    }

                    Text {
                        anchors { left: parent.left; leftMargin: root.trackX; verticalCenter: parent.verticalCenter }
                        text: modelData.name
                        font.family: Theme.fontData
                        font.pixelSize: Theme.fontSzDataSm
                        color: !modelData.ran ? Theme.colorText3
                               : modelData.critical ? Theme.colorText : Theme.colorText2
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

                    HoverHandler { id: rowHover; enabled: modelData.ran }
                    ToolTip.visible: rowHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text: modelData.ran ? "T" + modelData.thread + " · " + modelData.spanStr : ""
                }
            }
        }
    }
}
