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

// Per-(source,part) coverage lanes over the swing time axis. Each lane's `bins`
// (0 present / 1 low-conf / 2 gap) renders as a fixed row of cells; a selection band
// marks the detail window; a phase ladder runs underneath. Read-only.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Column {
    id: strip
    property var    model: null      // SwingCoverageModel
    property var    phases: []       // [{label,t_us}]
    property real   spanUs: 1
    property real   windowStartUs: 0
    property real   windowEndUs: 0
    property var    colorFn: function(k) { return Theme.colorText3 }
    property bool   showTitle: true   // hidden when a collapsible section header owns the title
    readonly property int labelW: Theme.sp(150)

    padding: Theme.sp(10); spacing: Theme.sp(6)

    // Lane labels arrive as "KIND · Name"; split so a compact kind glyph replaces the
    // word, freeing room for the full (untruncated) name.
    function kindOf(lbl) { var i = lbl.indexOf(" · "); return i < 0 ? "" : lbl.substring(0, i) }
    function nameOf(lbl) { var i = lbl.indexOf(" · "); return i < 0 ? lbl : lbl.substring(i + 3) }
    function kindGlyph(k) {
        switch (k) {
            case "IMU":    return "◉"
            case "Metric": return "∿"
            case "Pose":   return "◆"
            case "Club":   return "▮"
        }
        return "•"
    }

    Row {  // legend
        spacing: Theme.sp(14)
        Text { visible: strip.showTitle
               text: qsTr("COVERAGE"); font.family: Theme.fontData
               font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
               color: Theme.colorText3; anchors.verticalCenter: parent.verticalCenter }
        Row {
            spacing: Theme.sp(5); anchors.verticalCenter: parent.verticalCenter
            Rectangle { width: Theme.sp(10); height: Theme.sp(8); radius: 2; color: Theme.colorText2
                        anchors.verticalCenter: parent.verticalCenter; opacity: 0.7 }
            Text { text: qsTr("present"); font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                   color: Theme.colorText3; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: Theme.sp(5); anchors.verticalCenter: parent.verticalCenter
            Rectangle { width: Theme.sp(10); height: Theme.sp(8); radius: 2
                        color: Qt.rgba(0.91, 0.71, 0.29, 0.55)
                        anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("low conf"); font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                   color: Theme.colorText3; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: Theme.sp(5); anchors.verticalCenter: parent.verticalCenter
            Rectangle { width: Theme.sp(10); height: Theme.sp(8); radius: 2; color: "#140F0C"
                        border.width: 1; border.color: Theme.colorBorderMid
                        anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("gap"); font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                   color: Theme.colorText3; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    Repeater {
        model: strip.model
        delegate: Row {
            required property string label
            required property string colorKey
            required property var    bins      // list<int>
            width: strip.width - 2 * Theme.sp(10)
            spacing: Theme.sp(8)
            Row {
                width: strip.labelW
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.sp(5)
                Text {
                    text: strip.kindGlyph(strip.kindOf(label))
                    color: strip.colorFn(colorKey)
                    width: Theme.sp(14); horizontalAlignment: Text.AlignHCenter
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Theme.fontSzBody2
                }
                Text {
                    width: strip.labelW - Theme.sp(14) - Theme.sp(5)
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                    text: strip.nameOf(label); color: Theme.colorText
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                }
                HoverHandler { id: laneHover }
                ToolTip.visible: laneHover.hovered
                ToolTip.text: label
                ToolTip.delay: 400
            }
            Item {
                width: parent.width - strip.labelW - Theme.sp(8)
                height: Theme.sp(13)
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.fill: parent; color: "#140F0C"; radius: Theme.sp(3) }
                Row {
                    anchors.fill: parent; spacing: 2
                    Repeater {
                        model: bins
                        delegate: Rectangle {
                            required property int modelData
                            width: (parent.width - (bins.length - 1) * 2) / bins.length
                            height: parent.height; radius: 2
                            color: modelData === 2 ? "transparent"
                                 : modelData === 1 ? Qt.rgba(0.91, 0.71, 0.29, 0.55)
                                 : strip.colorFn(colorKey)
                        }
                    }
                }
                // selection band
                Rectangle {
                    visible: strip.spanUs > 0
                    x: parent.width * (strip.windowStartUs / strip.spanUs)
                    width: parent.width * ((strip.windowEndUs - strip.windowStartUs) / strip.spanUs)
                    y: -3; height: parent.height + 6
                    color: Qt.rgba(0.49, 0.75, 0.67, 0.10)
                    border.width: 0
                    Rectangle { width: 1; height: parent.height; color: Theme.colorAccent; opacity: 0.45 }
                    Rectangle { width: 1; height: parent.height; anchors.right: parent.right
                                color: Theme.colorAccent; opacity: 0.45 }
                }
            }
        }
    }

    // phase ladder
    Row {
        width: strip.width - 2 * Theme.sp(10)
        Item { width: strip.labelW; height: Theme.sp(12) }
        Item {
            width: parent.width - strip.labelW - Theme.sp(8); height: Theme.sp(12)
            Repeater {
                model: strip.phases
                delegate: Text {
                    required property var modelData
                    x: Math.min(parent.width - implicitWidth,
                                parent.width * (strip.spanUs > 0 ? modelData.t_us / strip.spanUs : 0))
                    text: "▏" + modelData.label
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }
            }
        }
    }
}
