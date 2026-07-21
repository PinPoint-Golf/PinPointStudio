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

// Verdict zone — the headline read. Score ± uncertainty (coloured by quality),
// the wrist pattern, one priority fix (the top fault + its drill), and one
// protected strength. Fault/strength come from the live WristDiagnosticsModel
// (findings are already severity-ordered) — wrist-only; on other session types
// those rows simply have no data and are omitted. Tempo has no producer yet, so
// its slot is reserved (left empty) rather than fabricated.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: zone

    property var detail: ({})
    property var wristModel: null

    readonly property int  _score:   detail && detail.overall !== undefined ? detail.overall : -1
    readonly property int  _half:    detail && detail.interval ? detail.interval.halfWidth : -1
    readonly property string _pattern: detail && detail.pattern ? detail.pattern : ""
    readonly property bool _blended: detail && detail.blended === true

    readonly property var _fault:    (wristModel && wristModel.findings && wristModel.findings.length > 0)
                                     ? wristModel.findings[0] : null
    readonly property var _strength: (wristModel && wristModel.strengths && wristModel.strengths.length > 0)
                                     ? wristModel.strengths[0] : null

    readonly property bool _hasContent: _score >= 0
    implicitHeight: _hasContent ? card.implicitHeight : 0
    visible: _hasContent

    Rectangle {
        id: card
        width: zone.width
        implicitHeight: col.implicitHeight + Theme.sp(24)
        color: Theme.colorSurface
        radius: Theme.radiusLg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder

        // Coloured left edge — quality of this shot's score.
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.sp(3)
            radius: Theme.radiusLg
            color: Theme.qualityColor(zone._score)
        }

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(16); rightMargin: Theme.sp(14); topMargin: Theme.sp(12) }
            spacing: Theme.sp(8)

            // Score line.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(8)
                Text {
                    text: qsTr("VERDICT")
                    Layout.alignment: Qt.AlignVCenter
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                    color: Theme.colorText3
                }
                Item { Layout.fillWidth: true }
                // Pattern pill (wrist resemblance).
                Rectangle {
                    visible: zone._pattern.length > 0
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: patTxt.implicitWidth + Theme.sp(14)
                    implicitHeight: Theme.sp(22)
                    radius: Theme.radius
                    color: Theme.colorBg2
                    Text {
                        id: patTxt
                        anchors.centerIn: parent
                        text: (zone._blended ? qsTr("blended · ") : "")
                              + zone._pattern.toUpperCase()
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText2
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(6)
                Text {
                    text: zone._score
                    font.family: Theme.fontData; font.pixelSize: Theme.sp(46)
                    font.weight: Font.DemiBold
                    color: Theme.qualityColor(zone._score)
                }
                Text {
                    visible: zone._half >= 0
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: Theme.sp(9)
                    text: "± " + zone._half
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText3
                }
                Item { Layout.fillWidth: true }
            }

            // Priority fix — the top fault + its drill.
            MiniCard {
                visible: zone._fault !== null
                Layout.fillWidth: true
                edgeColor: Theme.colorRagFault
                tag: qsTr("Priority fix")
                title: zone._fault ? zone._fault.name : ""
                body: zone._fault ? zone._fault.coaching : ""
                seekUs: zone._fault && zone._fault.seekUs !== undefined ? zone._fault.seekUs : -1
            }

            // One protected strength.
            MiniCard {
                visible: zone._strength !== null
                Layout.fillWidth: true
                edgeColor: Theme.colorRagGood
                tag: qsTr("Keep")
                title: zone._strength ? zone._strength.name : ""
                body: zone._strength ? zone._strength.protect : ""
                seekUs: zone._strength && zone._strength.seekUs !== undefined ? zone._strength.seekUs : -1
            }
        }
    }

    // A tagged mini-card with a coloured left edge; click seeks the replay.
    component MiniCard: Rectangle {
        property color edgeColor: Theme.colorRagNone
        property string tag: ""
        property string title: ""
        property string body: ""
        property real seekUs: -1

        implicitHeight: mcCol.implicitHeight + Theme.sp(14)
        radius: Theme.radius
        color: mcMa.containsMouse ? Theme.colorBg2 : Theme.colorBg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.sp(3); radius: Theme.radius; color: edgeColor
        }

        ColumnLayout {
            id: mcCol
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(12); rightMargin: Theme.sp(10); topMargin: Theme.sp(7) }
            spacing: Theme.sp(2)
            Text {
                text: tag
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: edgeColor
            }
            Text {
                Layout.fillWidth: true
                text: title
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Font.Medium
                color: Theme.colorText
            }
            Text {
                Layout.fillWidth: true
                visible: body.length > 0
                text: body
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }
        }
        PpPressable {
            id: mcMa
            hoverScale: 1.0
            enabled: seekUs >= 0
            onClicked: if (seekUs >= 0) shotReplay.seekToUs(seekUs)
        }
    }
}
