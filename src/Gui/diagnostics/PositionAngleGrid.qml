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
// value on a RAG-tinted background with the RAG's glyph beside it (● in range, ▲ watch, ■ fault,
// ◆ no data), so the grid reads without its colour. Pure binding; colours/sizes via Theme.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

ColumnLayout {
    id: root

    property var gridRows: []       // [{ name, cells:[{ value, rag, available, ref }] }]
    property var positions: []      // [{ id, … }]
    property int selected: 0
    // The DOF names' column; it gives way on a narrow panel so the eight cells keep room for a
    // glyph and a value.
    property real labelWidth: Math.min(Theme.sp(150), Math.round(width * 0.24))

    spacing: Theme.gap(4)

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
    // The RAG's shape, beside a judged value. The reference cell is the zero everything is measured
    // from, and a cell with no data is its ◆ already, so neither takes one.
    function _glyph(cell) {
        if (cell.ref || !cell.available) return ""
        return cell.rag === "green" ? "●" : cell.rag === "amber" ? "▲" : cell.rag === "red" ? "■" : ""
    }

    // ── Drawer toggle ───────────────────────────────────────────────────────────────
    property bool open: false
    Item {
        Layout.fillWidth: true
        implicitHeight: toggleRow.implicitHeight + Theme.sp(8)
        Row {
            id: toggleRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gap(6)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.open ? "▾" : "▸"
                font.pixelSize: Theme.fontSzBody2
                color: dtMa.containsMouse ? Theme.colorText : Theme.colorText3
            }
            PpMicro {
                anchors.verticalCenter: parent.verticalCenter
                text: Theme.caps(qsTr("Data · position × degree of freedom"))
                color: dtMa.containsMouse ? Theme.colorText : Theme.colorText3
            }
        }
        PpPressable { id: dtMa; hoverScale: 1.0; onClicked: root.open = !root.open }
    }

    // ── Table ─────────────────────────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.open
        spacing: Theme.gap(2)

        // Header — P1…P8.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(3)
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
                spacing: Theme.gap(3)
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
                        id: cell
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.sp(22)
                        radius: Theme.radius
                        color: root._bg(modelData.rag)
                        border.width: index === root.selected ? Theme.borderWidth : 0
                        border.color: Theme.colorBorderStrong
                        // A narrow cell closes the glyph up to its value and sets both smaller,
                        // so the pair still fits rather than the glyph being dropped.
                        readonly property bool tight: width < Theme.sp(42)
                        Row {
                            anchors.centerIn: parent
                            spacing: cell.tight ? 1 : Theme.sp(3)
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== ""
                                text: root._glyph(cell.modelData)
                                font.family: Theme.fontSymbol
                                font.pixelSize: cell.tight ? Theme.fontSzMicro - 2 : Theme.fontSzMicro
                                color: root._fg(cell.modelData.rag)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root._text(cell.modelData)
                                font.family: Theme.fontData
                                font.pixelSize: cell.tight ? Theme.fontSzMicro : Theme.fontSzLabel
                                color: root._fg(cell.modelData.rag)
                            }
                        }
                    }
                }
            }
        }
    }
}
