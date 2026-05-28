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
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPoint

Item {
    id: root

    signal athleteSelected(string uuid)
    signal newAthleteRequested()

    property string selectedUuid: athleteController.currentUuid

    property var filteredAthletes: {
        const q = searchField.text.trim().toLowerCase()
        if (q === "") return athleteController.athletes
        return athleteController.athletes.filter(function(a) {
            return a.name.toLowerCase().indexOf(q) >= 0
        })
    }

    // Pre-select the current athlete whenever this screen becomes visible
    onVisibleChanged: {
        if (visible) selectedUuid = athleteController.currentUuid
    }

    Flickable {
        anchors.fill:    parent
        contentWidth:    width
        contentHeight:   contentCol.implicitHeight + 64
        clip:            true

        Column {
            id: contentCol
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Theme.contentWidth(parent.width)
            spacing: 0
            y:       32

            // ── Header ───────────────────────────────────────────────────────
            Text {
                text:               qsTr("ATHLETES")
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
                bottomPadding:      Theme.sp(10)
            }
            Text {
                text:           qsTr("Choose athlete")
                font.family:    Theme.fontDisplay
                font.italic:    Theme.fontDisplayItalic
                font.weight: Theme.fontDisplayWeight
                font.pixelSize: Theme.fontSzDisplay
                color:          Theme.colorText
                bottomPadding:  Theme.sp(24)
            }

            // ── Recent section ───────────────────────────────────────────────
            Column {
                width:   parent.width
                spacing: Theme.sp(8)
                visible: athleteController.athletes.length > 0

                Text {
                    text:               qsTr("RECENT")
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:              Theme.colorText3
                    bottomPadding:      Theme.sp(2)
                }

                Row {
                    id: recentRow
                    width:   parent.width
                    spacing: Theme.sp(10)

                    readonly property int cardCount: Math.min(3, athleteController.athletes.length)

                    Repeater {
                        model: recentRow.cardCount

                        PpAthleteCard {
                            required property int index
                            width:        (recentRow.width - (recentRow.cardCount - 1) * recentRow.spacing)
                                              / recentRow.cardCount
                            athleteData:  athleteController.athletes[index]
                            isSelected:   athleteData.uuid === root.selectedUuid
                            onClicked:    root.selectedUuid = athleteData.uuid
                        }
                    }
                }
            }

            Item {
                width:  1
                height: athleteController.athletes.length > 0 ? Theme.sp(24) : 0
            }

            // ── All athletes header row ───────────────────────────────────────
            RowLayout {
                width: parent.width

                Text {
                    text:           qsTr("All athletes · %1 total").arg(athleteController.athletes.length)
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:          Theme.colorText3
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }

                Rectangle {
                    width:  Theme.sp(180)
                    height: Theme.sp(28)
                    radius: Theme.radius
                    color:  Theme.colorSurface
                    border.width: 1
                    border.color: searchField.activeFocus ? Theme.colorAccent : Theme.colorBorderStrong
                    Layout.alignment: Qt.AlignVCenter

                    TextField {
                        id: searchField
                        anchors { fill: parent; leftMargin: Theme.sp(8); rightMargin: Theme.sp(8) }
                        placeholderText:      qsTr("⌕  Search…")
                        placeholderTextColor: Theme.colorText3
                        background:           null
                        font.family:          Theme.fontBody
                        font.pixelSize:       Theme.fontSzBody2
                        color:                Theme.colorText
                        leftPadding:          0
                        rightPadding:         0
                        topPadding:           0
                        bottomPadding:        0
                        verticalAlignment:    TextInput.AlignVCenter
                    }
                }
            }

            Item { width: 1; height: Theme.sp(8) }

            // ── Athletes list ─────────────────────────────────────────────────
            Rectangle {
                width:        parent.width
                height:       root.filteredAthletes.length > 0
                                  ? root.filteredAthletes.length * Theme.sp(44)
                                  : Theme.sp(44)
                radius:       Theme.radius
                border.width: 1
                border.color: Theme.colorBorderMid
                clip:         true
                color:        "transparent"

                Text {
                    anchors.centerIn: parent
                    visible:        root.filteredAthletes.length === 0
                    text:           athleteController.athletes.length === 0
                                        ? qsTr("No athletes yet — add one below")
                                        : qsTr("No athletes match \"%1\"").arg(searchField.text)
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText3
                }

                ListView {
                    id: listView
                    anchors.fill:   parent
                    model:          root.filteredAthletes
                    clip:           true
                    interactive:    false
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: PpAthleteRow {
                        required property var modelData
                        required property int index
                        width:       listView.width
                        athleteData: modelData
                        rowIndex:    index
                        isSelected:  modelData.uuid === root.selectedUuid
                        onClicked:   root.selectedUuid = modelData.uuid
                    }
                }
            }

            // ── Action row ───────────────────────────────────────────────────
            Item { width: 1; height: Theme.sp(20) }

            RowLayout {
                width:   parent.width
                spacing: Theme.sp(8)

                PpButton {
                    label:    qsTr("＋ New athlete")
                    onClicked: root.newAthleteRequested()
                }
                PpButton {
                    label:    qsTr("Import roster")
                    onClicked: console.log("Import roster pressed")
                }
                Item { Layout.fillWidth: true }
                PpButton {
                    label:       qsTr("Delete athlete")
                    destructive: true
                    enabled:     root.selectedUuid !== ""
                    onClicked: {
                        athleteController.deleteAthlete(root.selectedUuid)
                        root.selectedUuid = ""
                    }
                }
                PpButton {
                    label: {
                        if (root.selectedUuid === "") return qsTr("Select athlete ↗")
                        const found = athleteController.athletes.find(
                            function(a) { return a.uuid === root.selectedUuid })
                        return found ? qsTr("Select %1 ↗").arg(found.name) : qsTr("Select athlete ↗")
                    }
                    primary: true
                    enabled: root.selectedUuid !== ""
                    onClicked: {
                        athleteController.selectAthlete(root.selectedUuid)
                        root.athleteSelected(root.selectedUuid)
                    }
                }
            }

            Item { width: 1; height: Theme.sp(32) }
        }
    }
}
