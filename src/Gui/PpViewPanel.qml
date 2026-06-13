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

// View panel for the session toolbar. Three sections: a segmented PRESETS pill,
// an ARRANGEMENT schematic group (tabs/split/stage), and a PANELS toggle grid.
// All state resolves through ViewLayout, persisted per SessionController::Type.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property int sessionType: -1

    implicitWidth:  Theme.sp(300)
    implicitHeight: col.implicitHeight + Theme.sp(26)

    readonly property var panelMeta: [
        { key: "camera",    label: qsTr("Camera"),    ready: true  },
        { key: "charts",    label: qsTr("Charts"),    ready: false },
        { key: "carousel",  label: qsTr("Carousel"),  ready: true  },
        { key: "timeline",  label: qsTr("Timeline"),  ready: false },
        { key: "dashboard", label: qsTr("Dashboard"), ready: false },
        { key: "table",     label: qsTr("Table"),     ready: false }
    ]

    Column {
        id: col
        anchors { fill: parent; margins: Theme.sp(13) }
        spacing: Theme.sp(12)

        // ── PRESETS ─────────────────────────────────────────────────────────
        Column {
            width: parent.width
            spacing: Theme.sp(8)
            Text {
                text: qsTr("LAYOUT PRESETS")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
            }
            PpSegmentedControl {
                width: parent.width
                options:  ViewLayout.presetOrder
                selected: ViewLayout.presetFor(root.sessionType)
                onActivated: ViewLayout.applyPreset(root.sessionType, value)
            }
        }

        PpDivider { width: parent.width }

        // ── ARRANGEMENT ─────────────────────────────────────────────────────
        Column {
            width: parent.width
            spacing: Theme.sp(9)
            Text {
                text: qsTr("ARRANGEMENT")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
            }
            Row {
                width: parent.width
                spacing: Theme.sp(7)
                readonly property real cardW: (width - 2 * Theme.sp(7)) / 3
                ArrangeCard { value: "tabs";  label: qsTr("Tabs");  width: parent.cardW }
                ArrangeCard { value: "split"; label: qsTr("Split"); width: parent.cardW }
                ArrangeCard { value: "stage"; label: qsTr("Stage"); width: parent.cardW }
            }
        }

        PpDivider { width: parent.width }

        // ── PANELS ──────────────────────────────────────────────────────────
        Column {
            width: parent.width
            spacing: Theme.sp(9)
            Text {
                text: qsTr("PANELS")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
            }
            Grid {
                width: parent.width
                columns: 2
                columnSpacing: Theme.sp(14)
                rowSpacing: Theme.sp(9)
                Repeater {
                    model: root.panelMeta
                    delegate: Item {
                        required property var modelData
                        width: (parent.width - Theme.sp(14)) / 2
                        height: Theme.sp(20)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                            color: ViewLayout.isPanelOn(root.sessionType, modelData.key)
                                   ? Theme.colorText : Theme.colorText2
                        }
                        MiniToggle {
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            checked: ViewLayout.isPanelOn(root.sessionType, modelData.key)
                            onToggled: ViewLayout.setPanel(root.sessionType, modelData.key, !checked)
                        }
                    }
                }
            }
        }
    }

    // ── arrangement schematic card ──────────────────────────────────────────
    component ArrangeCard: Rectangle {
        property string value: ""
        property string label: ""
        readonly property bool active: ViewLayout.arrangementFor(root.sessionType) === value
        height: Theme.sp(46)
        radius: Theme.radius
        color: active ? Theme.colorAccentLight : "transparent"
        border.width: 1
        border.color: active ? Theme.colorAccentMid : Theme.colorBorderMid
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Column {
            anchors.centerIn: parent
            spacing: Theme.sp(5)
            // schematic glyph — two/one/dominant rects
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 2
                Repeater {
                    model: value === "split" ? 2 : 1
                    Rectangle {
                        width: value === "stage" ? Theme.sp(13) : Theme.sp(9)
                        height: Theme.sp(13); radius: 2
                        color: "transparent"
                        border.width: 1
                        border.color: parent.parent.parent.active ? Theme.colorAccent : Theme.colorText3
                    }
                }
                Column {
                    visible: value === "stage"
                    spacing: 2
                    Rectangle { width: Theme.sp(5); height: Theme.sp(5.5); radius: 1; color: "transparent"; border.width: 1; border.color: parent.parent.parent.parent.active ? Theme.colorAccent : Theme.colorText3 }
                    Rectangle { width: Theme.sp(5); height: Theme.sp(5.5); radius: 1; color: "transparent"; border.width: 1; border.color: parent.parent.parent.parent.active ? Theme.colorAccent : Theme.colorText3 }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: label
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                color: active ? Theme.colorAccent : Theme.colorText2
            }
        }
        MouseArea {
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ViewLayout.setArrangement(root.sessionType, value)
        }
    }

    // ── compact on/off toggle ───────────────────────────────────────────────
    component MiniToggle: Rectangle {
        property bool checked: false
        signal toggled()
        width: Theme.sp(34); height: Theme.sp(18); radius: height / 2
        color: checked ? Theme.colorAccent : Theme.colorBg3
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Rectangle {
            width: Theme.sp(14); height: width; radius: width / 2
            y: Theme.sp(2)
            x: checked ? parent.width - width - Theme.sp(2) : Theme.sp(2)
            color: checked ? (Theme.dark ? Theme.colorBg : "#FFFFFF") : Theme.colorText3
            Behavior on x { NumberAnimation { duration: Theme.durationFast } }
        }
        MouseArea {
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.toggled()
        }
    }
}
