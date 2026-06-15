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

// The dense position × DOF grid — a RAG heatmap table behind a collapsible "Data" drawer (hidden by
// default to keep the instrument-light feel). Rows = DOFs, columns = P1–P8; each cell shows the Δ
// value on a RAG-tinted background. Pure binding; colours/sizes via Theme.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

ColumnLayout {
    id: root

    property var gridRows: []       // [{ name, cells:[{ value, rag, available, ref }] }]
    property var positions: []      // [{ id, … }]
    property int selected: 0
    property real labelWidth: Theme.sp(150)

    spacing: Theme.sp(4)

    function _bg(rag) {
        return rag === "green" ? Theme.colorGoodLight
             : rag === "amber" ? Theme.colorAttentionLight
             : rag === "red"   ? Theme.colorErrorLight
             :                   "transparent"
    }
    function _fg(rag) {
        return rag === "green" ? Theme.colorRagGood
             : rag === "amber" ? Theme.colorRagWatch
             : rag === "red"   ? Theme.colorRagFault
             :                   Theme.colorText3
    }
    function _text(cell) {
        return cell.ref ? "0"
             : !cell.available ? "◆"
             : (cell.value > 0 ? "+" : "") + Math.round(cell.value)
    }

    // ── Drawer toggle ───────────────────────────────────────────────────────────────
    property bool open: false
    MouseArea {
        Layout.fillWidth: true
        implicitHeight: toggleRow.implicitHeight + Theme.sp(6)
        cursorShape: Qt.PointingHandCursor
        onClicked: root.open = !root.open
        Row {
            id: toggleRow
            spacing: Theme.sp(2)
            Text {
                text: root.open ? "▾" : "▸"
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText2
            }
            Text {
                text: qsTr("Data — position × degree-of-freedom")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText2
            }
        }
    }

    // ── Table ─────────────────────────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.open
        spacing: Theme.sp(2)

        // Header — P1…P8.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(3)
            Item { Layout.preferredWidth: root.labelWidth; Layout.fillHeight: true }
            Repeater {
                model: root.positions
                delegate: Text {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData.tag
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                    font.weight: index === root.selected ? Font.Medium : Font.Normal
                    color: index === root.selected ? Theme.colorText : Theme.colorText3
                }
            }
        }

        // One row per DOF.
        Repeater {
            model: root.gridRows
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.sp(3)
                Text {
                    Layout.preferredWidth: root.labelWidth
                    text: parent.modelData.name
                    elide: Text.ElideRight
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText2
                }
                Repeater {
                    model: parent.modelData.cells
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.sp(22)
                        radius: Theme.radius
                        color: root._bg(modelData.rag)
                        border.width: index === root.selected ? Theme.borderWidth : 0
                        border.color: Theme.colorBorderStrong
                        Text {
                            anchors.centerIn: parent
                            text: root._text(parent.modelData)
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                            color: root._fg(parent.modelData.rag)
                        }
                    }
                }
            }
        }
    }
}
