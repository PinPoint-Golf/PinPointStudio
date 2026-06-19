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

// One finding card — a fault (amber/red) or a strength (green "keep"). Tapping it expands the
// biomechanical "why" + coaching/protect text + corroboration, and seeks the timeline to the
// finding's primary checkpoint. Pure binding; all values via Theme.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: root

    // finding = { id,name,severity,category,confidence,lowConfidence,magnitude,positions:[tags],
    //             ballFlight:[…],explanation,coaching,protect,corroboratedBy:[…],linkedTo,seekUs }
    property var finding: ({})
    property bool _open: false
    signal seek(real us)

    readonly property string _sev: finding.severity || "watch"
    readonly property bool   _strength: _sev === "good"

    function _sevColor(s) {
        return s === "fault" ? Theme.colorRagFault
             : s === "good"  ? Theme.colorRagGood
             :                 Theme.colorRagWatch
    }
    function _sevGlyph(s) { return s === "fault" ? "■" : s === "good" ? "●" : "▲" }
    function _sevLabel(s) { return s === "fault" ? qsTr("fault") : s === "good" ? qsTr("keep") : qsTr("watch") }

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + Theme.sp(20)
    radius: Theme.radius
    color: Theme.colorSurface
    border.width: Theme.borderWidth
    border.color: Theme.colorBorder
    opacity: finding.lowConfidence ? 0.82 : 1.0

    scale: cardTap.pressed ? 0.97 : cardHover.hovered ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(10) }
        spacing: Theme.sp(6)

        // ── header: glyph · name · severity pill ─────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(6)
            Text {
                text: root._sevGlyph(root._sev)
                font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzDataSm
                color: root._sevColor(root._sev)
            }
            Text {
                Layout.fillWidth: true
                text: root.finding.name || ""
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; font.weight: Font.Medium
                color: Theme.colorText
                elide: Text.ElideRight
            }
            Rectangle {
                implicitWidth: sevLabel.implicitWidth + Theme.sp(10)
                implicitHeight: sevLabel.implicitHeight + Theme.sp(3)
                radius: height / 2
                color: Qt.alpha(root._sevColor(root._sev), 0.16)
                Text {
                    id: sevLabel
                    anchors.centerIn: parent
                    text: root._sevLabel(root._sev)
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                    font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                    color: root._sevColor(root._sev)
                }
            }
        }

        // ── linked-pair indicator ────────────────────────────────────────────────
        Text {
            visible: (root.finding.linkedTo || "") !== ""
            text: qsTr("linked · ") + (root.finding.linkedTo || "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }

        // ── chips: position tags + ball-flight ───────────────────────────────────
        Flow {
            Layout.fillWidth: true
            spacing: Theme.sp(3)
            Repeater {
                model: root.finding.positions || []
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: pTxt.implicitWidth + Theme.sp(8); implicitHeight: pTxt.implicitHeight + Theme.sp(3)
                    radius: Theme.radius; color: Theme.colorBg2
                    border.width: Theme.borderWidth; border.color: Theme.colorBorder
                    Text { id: pTxt; anchors.centerIn: parent; text: parent.modelData
                           font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText2 }
                }
            }
            Repeater {
                model: root.finding.ballFlight || []
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: bTxt.implicitWidth + Theme.sp(8); implicitHeight: bTxt.implicitHeight + Theme.sp(3)
                    radius: Theme.radius; color: Theme.colorBg2
                    border.width: Theme.borderWidth; border.color: Theme.colorBorder
                    Text { id: bTxt; anchors.centerIn: parent; text: parent.modelData
                           font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText2 }
                }
            }
        }

        // ── expanded detail ──────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            visible: root._open
            spacing: Theme.sp(4)
            Text {
                Layout.fillWidth: true
                text: root.finding.explanation || ""
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorText2
            }
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: root._strength ? (root.finding.protect || "") : (root.finding.coaching || "")
                wrapMode: Text.WordWrap
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: root._strength ? Theme.colorRagGood : Theme.colorText
            }
            Text {
                Layout.fillWidth: true
                visible: (root.finding.corroboratedBy || []).length > 0
                text: qsTr("corroborated · ") + (root.finding.corroboratedBy || []).join(", ")
                wrapMode: Text.WordWrap
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
            }
        }

        // ── confidence pips ──────────────────────────────────────────────────────
        RowLayout {
            visible: !root._strength || true
            spacing: Theme.sp(2)
            Text {
                text: qsTr("confidence")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }
            Row {
                spacing: Theme.sp(0.5)
                Repeater {
                    model: 4
                    delegate: Rectangle {
                        required property int index
                        width: Theme.sp(7); height: Theme.sp(2); radius: Theme.sp(0.5)
                        color: index < Math.round((root.finding.confidence || 0) * 4)
                               ? Theme.colorText2 : Theme.colorBorderStrong
                    }
                }
            }
        }
    }

    TapHandler { id: cardTap; onTapped: { root._open = !root._open; if (root.finding.seekUs > 0) root.seek(root.finding.seekUs) } }
    HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
}
