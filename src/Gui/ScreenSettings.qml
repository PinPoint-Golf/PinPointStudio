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
import QtQuick.Layouts
import PinPoint

Item {
    id: root

    property int    cameraCount:    cameraManager.cameraList.length
    property int    imuCount:       imuManager.imuEnumeratedCount
    property int    activeNavIndex: 0
    property string searchQuery:    ""

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Sidenav ──────────────────────────────────────────────────────────
        Item {
            id: sidenav
            Layout.preferredWidth: Theme.sidenavWidth
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: Theme.colorSurface
            }

            Rectangle {
                anchors.right:  parent.right
                anchors.top:    parent.top
                anchors.bottom: parent.bottom
                width:   1
                color:   Theme.colorBorderMid
                opacity: Theme.borderOpacityNormal
            }

            Column {
                anchors.fill: parent
                spacing: 0

                // ── Search field ─────────────────────────────────────────────
                Item {
                    width:  parent.width
                    height: Theme.sp(46)

                    Row {
                        id: searchRow
                        anchors.left:       parent.left
                        anchors.right:      parent.right
                        anchors.top:        parent.top
                        anchors.leftMargin: Theme.sp(10)
                        anchors.rightMargin: Theme.sp(10)
                        anchors.topMargin:  Theme.sp(10)
                        height:  Theme.sp(20)
                        spacing: Theme.sp(6)

                        Text {
                            id: searchIcon
                            text: "⌕"
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color: searchInput.activeFocus ? Theme.colorText2 : Theme.colorText3
                            anchors.verticalCenter: parent.verticalCenter
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        }

                        TextField {
                            id: searchInput
                            width: searchRow.width - searchIcon.width - searchRow.spacing
                            height: Theme.sp(20)
                            placeholderText:      qsTr("Search settings…")
                            placeholderTextColor: Theme.colorText3
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight:    Font.Light
                            color:          Theme.colorText
                            background:     null
                            verticalAlignment: TextInput.AlignVCenter
                            onTextChanged: root.searchQuery = text.toLowerCase()
                        }
                    }

                    Rectangle {
                        anchors.bottom:       parent.bottom
                        anchors.bottomMargin: Theme.sp(6)
                        anchors.left:         parent.left
                        anchors.right:        parent.right
                        anchors.leftMargin:   Theme.sp(10)
                        anchors.rightMargin:  Theme.sp(10)
                        height: 1
                        color: searchInput.activeFocus ? Theme.colorAccent : Theme.colorBorderMid
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                }

                // ── Nav items ────────────────────────────────────────────────
                Repeater {
                    model: [
                        { navIdx: 0, icon: "⊞", label: qsTr("General"),       sectionHead: qsTr("General"),  hasBadge: false },
                        { navIdx: 1, icon: "◐", label: qsTr("Appearance"),     sectionHead: "",               hasBadge: false },
                        { navIdx: 2, icon: "▭", label: qsTr("Displays"),       sectionHead: "",               hasBadge: false },
                        { navIdx: 3, icon: "⊙", label: qsTr("Cameras"),        sectionHead: qsTr("Hardware"), hasBadge: true  },
                        { navIdx: 4, icon: "⌖", label: qsTr("IMUs"),           sectionHead: "",               hasBadge: true  },
                        { navIdx: 5, icon: "◎", label: qsTr("Launch Monitor"), sectionHead: "",               hasBadge: false },
                        { navIdx: 6, icon: "▤", label: qsTr("Archiving"),      sectionHead: qsTr("Data"),     hasBadge: false }
                    ]

                    delegate: Column {
                        required property var modelData

                        width:   sidenav.width
                        spacing: 0

                        // Section eyebrow
                        Item {
                            width:   parent.width
                            height:  modelData.sectionHead !== "" ? Theme.sp(14) + eyebrow.implicitHeight + Theme.sp(4) : 0
                            visible: modelData.sectionHead !== ""

                            Text {
                                id: eyebrow
                                anchors.left:         parent.left
                                anchors.leftMargin:   Theme.sp(16)
                                anchors.bottom:       parent.bottom
                                anchors.bottomMargin: Theme.sp(4)
                                text:                 modelData.sectionHead
                                font.family:          Theme.fontBody
                                font.pixelSize:       Theme.fontSzMicro
                                font.weight:          Font.Light
                                font.letterSpacing:   Theme.trackingMicro
                                font.capitalization:  Font.AllUppercase
                                color:                Theme.colorText3
                            }
                        }

                        // Nav item row
                        Item {
                            id: navItem
                            width:  parent.width
                            height: Theme.sp(38)

                            readonly property bool isActive: root.activeNavIndex === modelData.navIdx
                            readonly property bool hovered:  navArea.containsMouse
                            readonly property int  actualBadge: !modelData.hasBadge ? -1
                                                                : (modelData.navIdx === 3 ? root.cameraCount
                                                                : (modelData.navIdx === 4 ? root.imuCount : -1))

                            opacity: root.searchQuery.length === 0
                                     || modelData.label.toLowerCase().indexOf(root.searchQuery) >= 0
                                     ? 1.0 : 0.3
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                            Rectangle {
                                anchors.fill: parent
                                color: navItem.isActive ? Theme.colorAccentLight
                                                        : (navItem.hovered ? Theme.colorBg2 : "transparent")
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            Rectangle {
                                anchors.left:   parent.left
                                anchors.top:    parent.top
                                anchors.bottom: parent.bottom
                                width: 2
                                color: navItem.isActive ? Theme.colorAccent : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            Text {
                                id: navItemIcon
                                anchors.left:           parent.left
                                anchors.leftMargin:     Theme.sp(16)
                                anchors.verticalCenter: parent.verticalCenter
                                text:           modelData.icon
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzBody
                                color: navItem.isActive ? Theme.colorAccent
                                                        : (navItem.hovered ? Theme.colorText2 : Theme.colorText3)
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            Text {
                                anchors.left:           navItemIcon.right
                                anchors.leftMargin:     Theme.sp(10)
                                anchors.right:          navItemBadge.visible ? navItemBadge.left : parent.right
                                anchors.rightMargin:    Theme.sp(10)
                                anchors.verticalCenter: parent.verticalCenter
                                text:           modelData.label
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                font.weight:    navItem.isActive ? Font.Normal : Font.Light
                                color: navItem.isActive ? Theme.colorAccent
                                                        : (navItem.hovered ? Theme.colorText : Theme.colorText2)
                                elide: Text.ElideRight
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            Text {
                                id: navItemBadge
                                anchors.right:          parent.right
                                anchors.rightMargin:    Theme.sp(12)
                                anchors.verticalCenter: parent.verticalCenter
                                text:           navItem.actualBadge >= 0 ? navItem.actualBadge.toString() : ""
                                visible:        navItem.actualBadge >= 0
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color: navItem.isActive ? Theme.colorAccent : Theme.colorText3
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            }

                            MouseArea {
                                id: navArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape:  Qt.PointingHandCursor
                                onClicked:    root.activeNavIndex = modelData.navIdx
                            }
                        }
                    }
                }
            }
        }

        // ── Content area ─────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            color: Theme.colorBg

            StackLayout {
                anchors.fill:  parent
                currentIndex:  root.activeNavIndex

                GeneralPanel {                                      // 0
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                }
                AppearancePanel {                                   // 1
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                }
                DisplaysPanel {                                     // 2
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                }
                CamerasPanel {                                      // 3
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                }
                ImusPanel {                                         // 4
                    Layout.fillWidth:  true
                    Layout.fillHeight: true
                }
                ScreenPlaceholder { titleText: "Launch Monitor" }  // 5
                ScreenPlaceholder { titleText: "Archiving"      }  // 6
            }
        }
    }
}
