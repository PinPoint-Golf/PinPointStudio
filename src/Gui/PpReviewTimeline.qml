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

// Review timeline rail: a full-width scrub slider + the swing's phase segments as
// bold, clickable pills. Bound to shotReplay; the scrub seeks the playhead and a
// pill click jumps to that phase. The pill bracketing the current playhead is
// highlighted. Shown only in Review mode (the screen swaps it for RmTimelineChart
// in Capture/Analyse).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    readonly property var _phases: (shotReplay.analysisDetail && shotReplay.analysisDetail.phases)
                                   ? shotReplay.analysisDetail.phases : []
    readonly property real _span: Math.max(1, shotReplay.endUs - shotReplay.startUs)

    // Phase enum → short label (matches PpMetricGraph's vocabulary).
    readonly property var kPhaseNames: ["ADR", "TKW", "TOP", "TRN", "DWN", "IMP",
                                        "REL", "FIN", "MBK", "DLV", "SPD", "FLW"]

    // Index of the phase bracketing the playhead (greatest t_us <= playhead).
    readonly property int _activePhase: {
        var t = shotReplay.positionUs, best = -1
        for (var i = 0; i < _phases.length; ++i)
            if (_phases[i].t_us <= t) best = i
        return best
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.sp(10); anchors.rightMargin: Theme.sp(10)
        spacing: Theme.sp(10)
        visible: shotReplay.active

        // ── Phase pills ─────────────────────────────────────────────────────
        Row {
            spacing: Theme.sp(5)
            visible: root._phases.length > 0
            Repeater {
                model: root._phases
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool active: index === root._activePhase
                    height: Theme.sp(24)
                    width: pillTxt.implicitWidth + Theme.sp(16)
                    radius: Theme.radius
                    color: active ? Theme.colorAccentLight : Theme.colorBg2
                    border.width: 1
                    border.color: active ? Theme.colorAccentMid : Theme.colorBorderMid
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    Text {
                        id: pillTxt
                        anchors.centerIn: parent
                        text: (modelData.phase >= 0 && modelData.phase < root.kPhaseNames.length)
                              ? root.kPhaseNames[modelData.phase] : ("P" + modelData.phase)
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        font.weight: Font.DemiBold; font.letterSpacing: Theme.trackingMicro
                        color: active ? Theme.colorAccent : Theme.colorText2
                    }
                    MouseArea {
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shotReplay.seekToUs(modelData.t_us)
                    }
                }
            }
        }

        // ── Scrub slider ────────────────────────────────────────────────────
        Slider {
            id: scrub
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(20)
            from: 0; to: 1
            Binding {
                target: scrub; property: "value"; when: !scrub.pressed
                value: (shotReplay.positionUs - shotReplay.startUs) / root._span
            }
            onPressedChanged: pressed ? shotReplay.beginScrub() : shotReplay.endScrub()
            onMoved: shotReplay.seekToFraction(value)

            background: Rectangle {
                x: scrub.leftPadding
                y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                width: scrub.availableWidth; height: Theme.sp(6); radius: height / 2
                color: Theme.colorBorderMid
                Rectangle {
                    width: scrub.visualPosition * parent.width; height: parent.height
                    radius: height / 2; color: Theme.colorAccent
                }
            }
            handle: Rectangle {
                x: scrub.leftPadding + scrub.visualPosition * (scrub.availableWidth - width)
                y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                width: Theme.sp(18); height: Theme.sp(18); radius: width / 2
                color: scrub.pressed ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent
                border.width: 2; border.color: Theme.colorBg
            }
        }
    }

    // Empty state — no swing focused yet.
    Text {
        anchors.centerIn: parent
        visible: !shotReplay.active
        text: qsTr("Select a swing to review")
        color: Theme.colorText3; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
    }
}
