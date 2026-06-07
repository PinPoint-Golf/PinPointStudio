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
import PinPointStudio

Item {
    id: root

    signal addAthleteRequested()
    signal athletePickerRequested()
    signal startSessionRequested(int sessionTypeIndex)
    signal systemRequested()

    property int    selectedType: 0
    property string selectedClub: "Driver"

    property var athMap: {
        if (!athleteController.hasCurrentAthlete) return {}
        var list = athleteController.athletes
        for (var i = 0; i < list.length; i++) {
            if (list[i].uuid === athleteController.currentUuid) return list[i]
        }
        return {}
    }

    onAthMapChanged: {
        var club = athMap.primaryClub
        if (club !== undefined && club !== "") selectedClub = club
    }

    Component.onCompleted: resourceMonitor.refresh()

    Timer {
        interval: 2000
        running:  root.visible
        repeat:   true
        onTriggered: resourceMonitor.refresh()
    }

    Rectangle { anchors.fill: parent; color: Theme.colorBg }

    Flickable {
        anchors.fill:  parent
        contentWidth:  width
        contentHeight: mainCol.implicitHeight + 80
        clip:          true

        Column {
            id: mainCol
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Theme.contentWidth(parent.width)
            spacing: 0

            Item { width: 1; height: Theme.sp(48) }

            // ── Section 1: Athlete identity ──────────────────────────────────

            // No athlete state
            Column {
                visible: !athleteController.hasCurrentAthlete
                height:  visible ? implicitHeight : 0
                width:   parent.width
                spacing: 0

                Text {
                    text:               qsTr("GOLF SWING ANALYSIS")
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:              Theme.colorText3
                }
                Item { width: 1; height: Theme.sp(12) }

                Text {
                    width:          parent.width
                    text:           qsTr("Welcome to Pinpoint Studio")
                    font.family:    Theme.fontDisplay
                    font.italic:    Theme.fontDisplayItalic
                    font.weight: Theme.fontDisplayWeight
                    font.pixelSize: Theme.fontSzDisplay
                    color:          Theme.colorText
                    wrapMode:       Text.WordWrap
                    lineHeight:     1.1
                }
                Item { width: 1; height: Theme.sp(10) }

                Text {
                    width:          parent.width
                    text:           qsTr("An open-source workshop for understanding the golf swing — cameras, IMUs, and ground forces working together to show you what's actually happening.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText2
                    wrapMode:       Text.WordWrap
                    lineHeight:     1.7
                }
                Item { width: 1; height: Theme.sp(32) }

                Text {
                    width:          parent.width
                    text:           qsTr("Start by adding an athlete")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.colorText
                }
                Item { width: 1; height: Theme.sp(4) }

                Text {
                    width:          parent.width
                    text:           qsTr("Every session belongs to someone. That's usually you.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText3
                }
                Item { width: 1; height: Theme.sp(16) }

                Rectangle {
                    width:  parent.width
                    height: Theme.sp(42)
                    radius: Theme.radius
                    color:  Theme.colorAccent

                    Text {
                        anchors.centerIn: parent
                        text:           qsTr("Add your first athlete")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.dark ? Theme.colorBg : "#FFFFFF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.addAthleteRequested()
                    }
                }
                Item { width: 1; height: Theme.sp(48) }

                Row {
                    width:   parent.width
                    spacing: Theme.sp(10)

                    Repeater {
                        model: [
                            { icon: "⊞", title: qsTr("Connect a camera"), desc: qsTr("Basler or GenTL over USB3") },
                            { icon: "⌖", title: qsTr("Pair wrist IMUs"),   desc: qsTr("Lead and trail hand sensors") },
                            { icon: "↗", title: qsTr("Read the docs"),     desc: qsTr("Setup guides and hardware") }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width:  (parent.width - 20) / 3
                            height: secCardCol.implicitHeight + 28
                            radius: Theme.radius
                            color:  "transparent"
                            border.width: 1
                            border.color: Theme.colorBorder

                            Column {
                                id: secCardCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(14) }
                                spacing: 0

                                Item { width: 1; height: Theme.sp(14) }
                                Text {
                                    text:           modelData.icon
                                    font.pixelSize: Theme.sp(16)
                                    color:          Theme.colorText2
                                }
                                Item { width: 1; height: 7 }
                                Text {
                                    width:          parent.width
                                    text:           modelData.title
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText
                                }
                                Item { width: 1; height: 3 }
                                Text {
                                    width:          parent.width
                                    text:           modelData.desc
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText3
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.4
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape:  Qt.PointingHandCursor
                                onEntered:    parent.color = Theme.colorBg2
                                onExited:     parent.color = "transparent"
                            }
                        }
                    }
                }
                Item { width: 1; height: Theme.sp(24) }

                Text {
                    width:               parent.width
                    text:                qsTr("Nothing connects to the cloud unless you configure it.")
                    font.family:         Theme.fontBody
                    font.pixelSize:      Theme.fontSzBody2
                    font.weight:         Theme.fontBodyWeight
                    color:               Theme.colorText3
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Has athlete: compact identity row
            Item {
                visible: athleteController.hasCurrentAthlete
                height:  visible ? 52 : 0
                width:   parent.width

                Rectangle {
                    id: avatarCircle
                    width: Theme.sp(52); height: Theme.sp(52); radius: Theme.sp(26)
                    anchors.left:           parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    color:        Theme.colorAccentLight
                    border.width: 1
                    border.color: Theme.colorAccentMid

                    Text {
                        anchors.centerIn: parent
                        text:           athleteController.currentInitials
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.sp(16)
                        color:          Theme.colorAccent
                    }
                }

                Column {
                    anchors.left:           avatarCircle.right
                    anchors.leftMargin:     16
                    anchors.right:          switchLink.left
                    anchors.rightMargin:    12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.sp(3)

                    Text {
                        width:          parent.width
                        text:           athleteController.currentName
                        font.family:    Theme.fontDisplay
                        font.italic:    Theme.fontDisplayItalic
                        font.weight: Theme.fontDisplayWeight
                        font.pixelSize: Math.min(Theme.fontSzDisplay, 24)
                        color:          Theme.colorText
                        elide:          Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: {
                            var m   = root.athMap
                            var hcp = Theme.formatHandicap(m.handicap)
                            var sc  = (m.sessionCount !== undefined)
                                          ? m.sessionCount + " sessions" : "0 sessions"
                            return athleteController.currentHandedness + " · " + hcp + " · " + sc
                        }
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color:              Theme.colorText3
                        elide:              Text.ElideRight
                    }
                }

                Text {
                    id: switchLink
                    anchors.right:          parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text:           qsTr("Switch →")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorAccent

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.athletePickerRequested()
                    }
                }
            }

            // ── Section 2: Session launcher ──────────────────────────────────
            Column {
                visible: athleteController.hasCurrentAthlete
                height:  visible ? implicitHeight : 0
                width:   parent.width
                spacing: 0

                Item { width: 1; height: Theme.sp(40) }

                // Banner — title font at double the largest theme title size.
                Text {
                    text:           qsTr("PinPoint Studio")
                    font.family:    Theme.fontDisplay
                    font.italic:    Theme.fontDisplayItalic
                    font.weight:    Theme.fontDisplayWeight
                    font.pixelSize: Theme.fontSzDisplay * 2
                    color:          Theme.colorText
                }
                Item { width: 1; height: Theme.sp(20) }

                Text {
                    text:               qsTr("NEW SESSION")
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:              Theme.colorText3
                }
                Item { width: 1; height: Theme.sp(16) }

                Row {
                    spacing: Theme.sp(20)
                    width:   parent.width

                    Repeater {
                        model: [
                            { icon: "◑", name: qsTr("Swing analysis"), desc: qsTr("Capture golf shots with IMUs on your spine and review your sequencing and key swing metrics to assess your swing"), img: "qrc:/assets/tiles/golfswing.png", reqCameras: 2, reqImus: 3, idx: 0 },
                            { icon: "⌖", name: qsTr("Wrist motion"),   desc: qsTr("Hit shots with IMUs on your lead wrist and hand to assess how your wrist angles impact club delivery"),              img: "qrc:/assets/tiles/grip.png",      reqCameras: 1, reqImus: 2, idx: 1 },
                            { icon: "⇅", name: qsTr("Ground forces"),  desc: qsTr("Hit shots with IMUs on your hips to assess how you use the ground to generate power"),                              img: "qrc:/assets/tiles/feet.png",      reqCameras: 2, reqImus: 3, idx: 2 },
                            { icon: "✦", name: qsTr("AI coach"),       desc: qsTr("Work with an AI coach to hit shots and get feedback on your swing and how to improve"),                             img: "qrc:/assets/tiles/coach.png",     reqCameras: 2, reqImus: 3, idx: 3 }
                        ]

                        delegate: HmTypeCard {
                            required property var modelData
                            width:           (parent.width - 3 * Theme.sp(20)) / 4
                            height:          width * 1.6
                            imageSource:     modelData.img
                            iconText:        modelData.icon
                            typeName:        modelData.name
                            description:     modelData.desc
                            camerasRequired: modelData.reqCameras
                            camerasOptional: modelData.optCameras || false
                            imusRequired:    modelData.reqImus
                            camerasCount:    cameraManager.cameraList.length
                            imusCount:       imuManager.imuEnumeratedCount
                            isSelected:      root.selectedType === modelData.idx
                            onClicked:       root.selectedType = modelData.idx
                            onDoubleClicked: {
                                root.selectedType = modelData.idx
                                root.startSessionRequested(modelData.idx)
                            }
                        }
                    }
                }
                Item { width: 1; height: Theme.sp(20) }

                Row {
                    width:   parent.width
                    spacing: Theme.sp(16)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width:              Theme.sp(36)
                        text:               qsTr("CLUB")
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color:              Theme.colorText3
                    }

                    PpChipGroup {
                        anchors.verticalCenter: parent.verticalCenter
                        options:  ["Driver", "3-wood", "5-iron", "7-iron", "Wedge"]
                        selected: root.selectedClub
                        onSelectionChanged: function(value) { root.selectedClub = value }
                    }
                }
                Item { width: 1; height: Theme.sp(20) }

                Rectangle {
                    width:  parent.width
                    height: Theme.sp(44)
                    radius: Theme.radius
                    color:  Theme.colorAccent

                    Text {
                        anchors.centerIn: parent
                        text:           qsTr("▶  Start session")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.sp(14)
                        font.weight:    Font.Normal
                        color:          Theme.dark ? Theme.colorBg : "#FFFFFF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.startSessionRequested(root.selectedType)
                    }
                }
            }

            // ── Section 3: Device readiness ──────────────────────────────────
            Item { width: 1; height: Theme.sp(44) }

            Item {
                width:  parent.width
                height: Theme.sp(20)

                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text:               qsTr("DEVICES")
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:              Theme.colorText3
                }

                Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text:           qsTr("System resources →")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorAccent

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.systemRequested()
                    }
                }
            }
            Item { width: 1; height: 12 }

            // Empty devices state
            Item {
                visible: resourceMonitor.devices.length === 0
                height:  visible ? 40 : 0
                width:   parent.width

                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text:           qsTr("No devices detected. Connect a camera or IMU to get started.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText3
                }
            }

            // Device rows — inline, no card chrome
            Repeater {
                model: resourceMonitor.devices

                Item {
                    required property var modelData
                    property var d: modelData
                    width:  mainCol.width
                    height: Theme.sp(40)

                    Rectangle {
                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                        height: 1
                        color:  Theme.colorBorder
                    }

                    Row {
                        anchors.fill: parent
                        spacing:      0

                        Item {
                            id: dotItem
                            property var d: parent.parent.d
                            width:  Theme.sp(20)
                            height: parent.height

                            Rectangle {
                                width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                                anchors.centerIn: parent
                                color: {
                                    var d = dotItem.d
                                    if (d.status === "streaming" || d.status === "connected") return Theme.colorGood
                                    if (d.hasWarning || d.status === "stalled") return Theme.colorWarn
                                    return Theme.colorBorderStrong
                                }
                            }
                        }

                        Text {
                            width:             parent.width - 20 - 80
                            height:            parent.height
                            text:              parent.parent.d.name
                            font.family:       Theme.fontBody
                            font.pixelSize:    Theme.fontSzBody
                            color:             Theme.colorText
                            elide:             Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            width:  Theme.sp(80)
                            height: parent.height
                            text: {
                                var d = parent.parent.d
                                if (d.kind === "Camera")
                                    return d.status === "streaming" ? d.dataRateHz.toFixed(0) + " fps" : qsTr("idle")
                                return d.status === "connected" ? d.dataRateHz.toFixed(0) + " Hz" : qsTr("disconnected")
                            }
                            font.family:         Theme.fontData
                            font.pixelSize:      Theme.fontSzMicro
                            font.letterSpacing:  Theme.trackingData
                            color: {
                                var d = parent.parent.d
                                return (d.status === "streaming" || d.status === "connected")
                                           ? Theme.colorGood : Theme.colorText3
                            }
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment:   Text.AlignVCenter
                        }
                    }
                }
            }

            // Warning notice (first warning only)
            Item {
                visible: resourceMonitor.warnings.length > 0
                height:  visible ? (warnNotice.implicitHeight + 12) : 0
                width:   parent.width

                RmWarningNotice {
                    id: warnNotice
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    message: resourceMonitor.warnings.length > 0 ? resourceMonitor.warnings[0] : ""
                }
            }

            Item { width: 1; height: 60 }
        }
    }
}
