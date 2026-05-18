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

    signal cancelled()
    signal saved(string uuid)
    signal savedAndStarted(string uuid)

    // ── Form state ────────────────────────────────────────────────────────────
    property bool   nameError:      false
    property string handedness:     "Right"
    property string heightUnit:     "ft"
    property string weightUnit:     "lb"
    property string primaryClub:    "Driver"

    function validate(): bool {
        nameError = nameField.text.trim() === ""
        return !nameError
    }

    function doSave(): string {
        const hv = parseFloat(heightField.text) || 0.0
        const wv = parseFloat(weightField.text) || 0.0
        const hcp = parseFloat(handicapField.text)
        const handicap = isNaN(hcp) ? -1.0 : hcp
        const speed = parseFloat(speedField.text) || 0.0

        return athleteController.createAthlete(
            nameField.text.trim(),
            handedness,
            hv, heightUnit,
            wv, weightUnit,
            handicap,
            primaryClub,
            speed,
            notesField.text.trim()
        )
    }

    ScrollView {
        anchors.fill:    parent
        contentWidth:    width
        clip:            true

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Math.min(640, parent.width - 48)
            spacing: 0
            y:       32

            // ── Header ───────────────────────────────────────────────────────
            Text {
                text:               "ATHLETE PROFILE"
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
                bottomPadding:      10
            }
            Text {
                text:            "New athlete"
                font.family:     Theme.fontDisplay
                font.italic:     Theme.fontDisplayItalic
                font.pixelSize:  Theme.fontSzDisplay
                color:           Theme.colorText
                bottomPadding:   6
            }
            Text {
                width:          parent.width
                text:           "Only the first two fields are required. The rest sharpen analysis and personalise coaching output."
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Font.Light
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
                lineHeight:     1.5
                bottomPadding:  24
            }

            // ── Required group ───────────────────────────────────────────────
            Rectangle {
                width:        parent.width
                height:       reqCol.implicitHeight
                radius:       Theme.radiusLg
                border.width: 1
                border.color: Theme.colorBorderMid
                clip:         true
                color:        "transparent"

                Column {
                    id: reqCol
                    width: parent.width
                    spacing: 0

                    // Header strip
                    Rectangle {
                        width:  parent.width
                        height: 36
                        color:  Theme.colorBg2

                        RowLayout {
                            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                            Text {
                                text:           "Required"
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                font.letterSpacing: Theme.trackingMicro
                                color:          Theme.colorText2
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text:           "Name and handedness"
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        // Bottom border
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width:  parent.width
                            height: 1
                            color:  Theme.colorBorderMid
                        }
                    }

                    // Body
                    Rectangle {
                        width:  parent.width
                        height: reqBody.implicitHeight + 36
                        color:  Theme.colorSurface

                        Column {
                            id: reqBody
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                            spacing: 12

                            // Name
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "NAME"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    id: nameField
                                    width:           parent.width
                                    placeholderText: "e.g. Mark Carter"
                                    hasError:        root.nameError
                                    onTextChanged:   if (root.nameError && text.trim() !== "") root.nameError = false
                                }
                                Text {
                                    visible:        root.nameError
                                    height:         root.nameError ? implicitHeight : 0
                                    text:           "Name is required"
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorWarn
                                }
                            }

                            // Handedness
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "HANDEDNESS"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpChipGroup {
                                    options:  ["Right", "Left"]
                                    selected: root.handedness
                                    onSelectionChanged: function(v) { root.handedness = v }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 16 }

            // ── Recommended group ────────────────────────────────────────────
            Rectangle {
                width:        parent.width
                height:       recCol.implicitHeight
                radius:       Theme.radiusLg
                border.width: 1
                border.color: Theme.colorBorderMid
                clip:         true
                color:        "transparent"

                Column {
                    id: recCol
                    width: parent.width
                    spacing: 0

                    // Header strip
                    Rectangle {
                        width:  parent.width
                        height: 36
                        color:  Theme.colorBg2

                        RowLayout {
                            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                            Text {
                                text:           "Recommended"
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                font.letterSpacing: Theme.trackingMicro
                                color:          Theme.colorText2
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text:           "Improves analysis accuracy"
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width:  parent.width
                            height: 1
                            color:  Theme.colorBorderMid
                        }
                    }

                    // Body
                    Rectangle {
                        width:  parent.width
                        height: recBody.implicitHeight + 36
                        color:  Theme.colorSurface

                        Column {
                            id: recBody
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                            spacing: 12

                            // Height
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "HEIGHT"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                Row {
                                    width:   parent.width
                                    spacing: 0
                                    PpTextField {
                                        id: heightField
                                        width:           parent.width - heightUnitToggle.width
                                        placeholderText: root.heightUnit === "ft" ? "e.g. 5'11\"" : "e.g. 180"
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    }
                                    PpUnitToggle {
                                        id: heightUnitToggle
                                        units:    ["ft", "cm"]
                                        selected: root.heightUnit
                                        onSelectionChanged: function(unit) {
                                            const val = parseFloat(heightField.text)
                                            if (!isNaN(val) && val > 0) {
                                                if (unit === "cm" && root.heightUnit === "ft")
                                                    heightField.text = (val * 30.48).toFixed(1)
                                                else if (unit === "ft" && root.heightUnit === "cm")
                                                    heightField.text = (val / 30.48).toFixed(2)
                                            }
                                            root.heightUnit = unit
                                        }
                                    }
                                }
                            }

                            // Weight
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "WEIGHT"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                Row {
                                    width:   parent.width
                                    spacing: 0
                                    PpTextField {
                                        id: weightField
                                        width:           parent.width - weightUnitToggle.width
                                        placeholderText: root.weightUnit === "lb" ? "e.g. 178" : "e.g. 80"
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    }
                                    PpUnitToggle {
                                        id: weightUnitToggle
                                        units:    ["lb", "kg"]
                                        selected: root.weightUnit
                                        onSelectionChanged: function(unit) {
                                            const val = parseFloat(weightField.text)
                                            if (!isNaN(val) && val > 0) {
                                                if (unit === "kg" && root.weightUnit === "lb")
                                                    weightField.text = (val / 2.20462).toFixed(1)
                                                else if (unit === "lb" && root.weightUnit === "kg")
                                                    weightField.text = (val * 2.20462).toFixed(1)
                                            }
                                            root.weightUnit = unit
                                        }
                                    }
                                }
                            }

                            // Handicap
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "HANDICAP"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    id: handicapField
                                    width:            100
                                    placeholderText:  "e.g. 5"
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                }
                            }

                            // Primary club
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "PRIMARY CLUB"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpChipGroup {
                                    options:  ["Driver", "3-wood", "5-iron", "7-iron", "Wedge"]
                                    selected: root.primaryClub
                                    onSelectionChanged: function(v) { root.primaryClub = v }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 16 }

            // ── Optional group ───────────────────────────────────────────────
            Rectangle {
                width:        parent.width
                height:       optCol.implicitHeight
                radius:       Theme.radiusLg
                border.width: 1
                border.color: Theme.colorBorderMid
                clip:         true
                color:        "transparent"

                Column {
                    id: optCol
                    width: parent.width
                    spacing: 0

                    // Header strip
                    Rectangle {
                        width:  parent.width
                        height: 36
                        color:  Theme.colorBg2

                        RowLayout {
                            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                            Text {
                                text:           "Optional"
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                font.letterSpacing: Theme.trackingMicro
                                color:          Theme.colorText2
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text:           "Personalises coaching output"
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width:  parent.width
                            height: 1
                            color:  Theme.colorBorderMid
                        }
                    }

                    // Body fields
                    Rectangle {
                        width:  parent.width
                        height: optBody.implicitHeight + 36
                        color:  Theme.colorSurface

                        Column {
                            id: optBody
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                            spacing: 12

                            // Driver speed target
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "DRIVER SPEED TARGET"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                Row {
                                    width:   parent.width
                                    spacing: 0
                                    PpTextField {
                                        id: speedField
                                        width:            parent.width - speedUnit.width
                                        placeholderText:  "e.g. 105"
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    }
                                    Rectangle {
                                        id: speedUnit
                                        width:  36
                                        height: 34
                                        color:  Theme.colorBg2
                                        border.width: 1
                                        border.color: Theme.colorBorderStrong
                                        radius: Theme.radius
                                        Text {
                                            anchors.centerIn: parent
                                            text:           "mph"
                                            font.family:    Theme.fontData
                                            font.pixelSize: Theme.fontSzMicro
                                            color:          Theme.colorText3
                                        }
                                    }
                                }
                            }

                            // Notes
                            Column {
                                width:   parent.width
                                spacing: 4
                                Text {
                                    text:               "NOTES / TAGS"
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    id: notesField
                                    width:           parent.width
                                    placeholderText: "e.g. junior, left-low, early ext"
                                }
                            }
                        }
                    }

                    // Note strip
                    Rectangle {
                        width:  parent.width
                        height: noteText.implicitHeight + 20
                        color:  Theme.colorSurface
                        border.width: 0

                        Rectangle {
                            anchors.top:  parent.top
                            width:        parent.width
                            height:       1
                            color:        Theme.colorBorderMid
                        }

                        Text {
                            id: noteText
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18; topMargin: 10 }
                            text:           "Pinpoint builds baselines automatically from early sessions. You don't need to know your driver speed to get started."
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzDataSm
                            font.weight:    Font.Light
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.4
                        }
                    }
                }
            }

            // ── Action row ───────────────────────────────────────────────────
            Item { width: 1; height: 24 }
            Row {
                anchors.right: parent.right
                spacing: 8
                bottomPadding: 32

                PpButton {
                    label: "Cancel"
                    onClicked: root.cancelled()
                }
                PpButton {
                    label: "Save"
                    onClicked: {
                        if (!validate()) return
                        const uuid = doSave()
                        if (uuid !== "") root.saved(uuid)
                    }
                }
                PpButton {
                    label: "Save and start ↗"
                    primary: true
                    onClicked: {
                        if (!validate()) return
                        const uuid = doSave()
                        if (uuid !== "") root.savedAndStarted(uuid)
                    }
                }
            }
            Item { width: 1; height: 32 }
        }
    }
}
