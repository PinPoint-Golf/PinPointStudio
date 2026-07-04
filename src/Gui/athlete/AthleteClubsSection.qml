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
import PinPointStudio

// Per-athlete club records — a card group for ScreenAthleteForm (edit mode).
// Records live under athletes/<uuid>/clubs keyed by the canonical club name
// (the same id markup writes to truth.json meta.club). Club length feeds the
// shaft-tracker search radius; the band fields describe retroreflective tape
// (optional, high value in dark rooms) for the instrumented-club pipeline —
// docs/validation/instrumented_club_protocol.md. Band positions are the
// CENTRES of each tape band, measured from the butt end of the grip, in mm.
//
// Unlike the scalar athlete fields (buffered until Save), club edits commit
// IMMEDIATELY via athleteController — the section is only shown in edit mode.
Rectangle {
    id: root

    property string athleteUuid: ""

    // Selected tab. Kept valid against the configured list: first club by
    // default; follows removals to a surviving neighbour.
    property string selectedClubId: ""
    onConfiguredIdsChanged: _fixSelection()
    Component.onCompleted:  _fixSelection()
    function _fixSelection() {
        if (configuredIds.indexOf(selectedClubId) < 0)
            selectedClubId = configuredIds.length > 0 ? configuredIds[0] : ""
    }

    width:        parent ? parent.width : implicitWidth
    height:       cardCol.implicitHeight
    radius:       Theme.radiusLg
    border.width: 1
    border.color: Theme.colorBorderMid
    clip:         true
    color:        "transparent"

    // Re-evaluates whenever the controller reloads (every commit calls reload()).
    readonly property var clubs: {
        void athleteController.athletes
        return athleteController.clubsFor(root.athleteUuid)
    }
    readonly property var configuredIds: {
        var ids = Object.keys(root.clubs)
        var vocab = athleteController.clubOptions()
        ids.sort(function(a, b) { return vocab.indexOf(a) - vocab.indexOf(b) })
        return ids
    }
    readonly property var unconfiguredIds: {
        var out = []
        var vocab = athleteController.clubOptions()
        for (var i = 0; i < vocab.length; i++)
            if (!(vocab[i] in root.clubs))
                out.push(vocab[i])
        return out
    }

    function recField(id, field, dflt) {
        var rec = root.clubs[id]
        if (!rec || rec[field] === undefined) return dflt
        return rec[field]
    }
    function commitField(id, field, value) {
        var rec = root.clubs[id] || {}
        var copy = {}
        for (var k in rec) copy[k] = rec[k]
        copy[field] = value
        athleteController.setClubRecord(root.athleteUuid, id, copy)
    }
    // Parse "270, 320, 555" -> sorted [270,320,555]; null on any invalid token.
    function parseCenters(text) {
        var t = String(text).trim()
        if (t === "") return []
        var parts = t.split(",")
        var out = []
        for (var i = 0; i < parts.length; i++) {
            var v = parseInt(parts[i].trim(), 10)
            if (isNaN(v) || v <= 0) return null
            out.push(v)
        }
        out.sort(function(a, b) { return a - b })
        return out
    }
    // Describe a band layout for the helper caption, e.g. -> "2–1–3".
    function bandPattern(centers) {
        if (!centers || centers.length === 0) return ""
        var groups = [1]
        for (var i = 1; i < centers.length; i++) {
            if (centers[i] - centers[i - 1] > 120) groups.push(1)   // >120 mm gap = new group
            else groups[groups.length - 1]++
        }
        return groups.join("–")
    }

    Column {
        id: cardCol
        width: parent.width
        spacing: 0

        // Header strip
        Rectangle {
            width:  parent.width
            height: Theme.sp(36)
            color:  Theme.colorBg2

            RowLayout {
                anchors { fill: parent; leftMargin: Theme.sp(14); rightMargin: Theme.sp(14) }
                Text {
                    text:           qsTr("Clubs")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:          Theme.colorText2
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }
                Text {
                    text:           qsTr("Lengths and retro-band positions — saved as you edit")
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
            height: body.implicitHeight + 36
            color:  Theme.colorSurface

            Column {
                id: body
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(18) }
                spacing: Theme.sp(14)

                Text {
                    width: parent.width
                    text: qsTr("Club length sets the shaft-tracker search radius. Band centres describe retroreflective tape (optional — high value in dark rooms), measured from the butt end of the grip in millimetres.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                    wrapMode:       Text.WordWrap
                }

                // Add club
                Row {
                    spacing: Theme.sp(10)

                    PpComboBox {
                        id: addCombo
                        implicitWidth: Theme.sp(220)
                        model: root.unconfiguredIds
                        displayFn: ClubFormat.display
                        enabled: root.unconfiguredIds.length > 0
                    }

                    PpButton {
                        label:   qsTr("Add club")
                        primary: true
                        enabled: root.unconfiguredIds.length > 0
                        onClicked: {
                            var id = root.unconfiguredIds[addCombo.currentIndex]
                            if (!id) return
                            // factory spec defaults (loft/length/shaft) per club
                            athleteController.setClubRecord(root.athleteUuid, id,
                                athleteController.defaultClubRecord(id))
                            root.selectedClubId = id
                        }
                    }
                }

                Text {
                    visible: root.configuredIds.length === 0
                    width: parent.width
                    text: qsTr("No clubs recorded for this athlete yet.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText3
                    wrapMode:       Text.WordWrap
                }

                // Club tabs — the athlete's configured clubs in the app's stage-tab
                // language (see PpModeStage on the Wrist screen): no border, no
                // pill; the active club carries a 2px accent underline (theme
                // accent) and brighter text. A Flow, not the stage's Row, so a full
                // bag — up to 17 clubs — wraps. Selecting a tab shows ONE detail card.
                Flow {
                    width:   parent.width
                    spacing: Theme.sp(5)

                    Repeater {
                        model: root.configuredIds

                        Rectangle {
                            required property string modelData
                            readonly property bool sel: modelData === root.selectedClubId

                            height:  Theme.sp(30)
                            width:   tabTxt.implicitWidth + Theme.sp(26)
                            radius:  Theme.radius
                            // Faint fill on select/hover (alpha-ramped rest → no
                            // colour flash); selection is carried by the underline.
                            color:   sel || tabMa.containsMouse
                                         ? Theme.colorBg2
                                         : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                            Rectangle {  // active underline (theme accent)
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 2
                                color:  sel ? Theme.colorAccent : "transparent"
                            }

                            Text {
                                id: tabTxt
                                anchors.centerIn: parent
                                text:           ClubFormat.display(modelData)
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          sel ? Theme.colorText : Theme.colorText3
                            }

                            PpPressable {
                                id: tabMa
                                onClicked: {
                                    // Steal focus first so an in-edit field commits
                                    // against the OUTGOING club before the switch.
                                    root.forceActiveFocus()
                                    root.selectedClubId = modelData
                                }
                            }
                        }
                    }
                }

                // Detail card for the selected club
                Rectangle {
                    visible: root.selectedClubId !== ""
                    width:  body.width
                    height: visible ? clubCol.implicitHeight + Theme.sp(28) : 0
                    radius: Theme.radius
                    color:  Theme.colorBg2
                    border.width: 1
                    border.color: Theme.colorBorder

                    ColumnLayout {
                        id: clubCol
                        anchors { fill: parent; margins: Theme.sp(14) }
                        spacing: Theme.sp(10)

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.sp(10)

                                Text {
                                    text: root.selectedClubId
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody
                                    font.weight:    Font.DemiBold
                                    color:          Theme.colorText
                                }
                                Text {
                                    visible: root.recField(root.selectedClubId, "tapedOn", "") !== ""
                                    text: qsTr("taped %1").arg(root.recField(root.selectedClubId, "tapedOn", ""))
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorText3
                                }
                                Item { Layout.fillWidth: true }
                                PpButton {
                                    label: qsTr("Remove")
                                    destructive: true
                                    onClicked: athleteController.removeClubRecord(root.athleteUuid, root.selectedClubId)
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: Theme.sp(20)
                                rowSpacing: Theme.sp(8)

                                Text {
                                    text:               qsTr("SHAFT")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpComboBox {
                                    implicitWidth: Theme.sp(150)
                                    model: [qsTr("steel"), qsTr("graphite")]
                                    currentIndex: root.recField(root.selectedClubId, "shaftType", "steel") === "graphite" ? 1 : 0
                                    onActivated: (index) => root.commitField(root.selectedClubId, "shaftType",
                                                                             index === 1 ? "graphite" : "steel")
                                }

                                Text {
                                    text:               qsTr("LOFT (°)")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    implicitWidth: Theme.sp(120)
                                    text: root.recField(root.selectedClubId, "loftDeg", 0) > 0
                                          ? String(root.recField(root.selectedClubId, "loftDeg", 0)) : ""
                                    placeholderText: qsTr("e.g. 34")
                                    validator: DoubleValidator { bottom: 0; top: 80; decimals: 1 }
                                    onEditingFinished: {
                                        var v = parseFloat(text)
                                        root.commitField(root.selectedClubId, "loftDeg", isNaN(v) ? 0 : v)
                                    }
                                }

                                Text {
                                    text:               qsTr("CLUB LENGTH (MM)")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    implicitWidth: Theme.sp(120)
                                    text: root.recField(root.selectedClubId, "lengthMm", 0) > 0
                                          ? String(root.recField(root.selectedClubId, "lengthMm", 0)) : ""
                                    placeholderText: qsTr("e.g. 1120")
                                    validator: IntValidator { bottom: 0; top: 1400 }
                                    onEditingFinished: {
                                        var v = parseInt(text, 10)
                                        root.commitField(root.selectedClubId, "lengthMm", isNaN(v) ? 0 : v)
                                    }
                                }

                                Text {
                                    text:               qsTr("BAND WIDTH (MM)")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    implicitWidth: Theme.sp(120)
                                    text: String(root.recField(root.selectedClubId, "bandWidthMm", 25))
                                    validator: IntValidator { bottom: 0; top: 100 }
                                    onEditingFinished: {
                                        var v = parseInt(text, 10)
                                        root.commitField(root.selectedClubId, "bandWidthMm", isNaN(v) ? 25 : v)
                                    }
                                }

                                Text {
                                    text:               qsTr("BAND CENTRES (MM FROM BUTT)")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.sp(4)

                                    PpTextField {
                                        id: centersField
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("e.g. 270, 320, 555, 760, 810, 860")
                                        text: {
                                            var c = root.recField(root.selectedClubId, "bandCentersMm", [])
                                            return c.length ? c.join(", ") : ""
                                        }
                                        hasError: root.parseCenters(text) === null
                                        onEditingFinished: {
                                            var parsed = root.parseCenters(text)
                                            if (parsed === null) return   // keep hasError showing
                                            root.commitField(root.selectedClubId, "bandCentersMm", parsed)
                                            if (parsed.length > 0)
                                                root.commitField(root.selectedClubId, "tapedOn",
                                                                 new Date().toISOString().slice(0, 10))
                                        }
                                    }

                                    Text {
                                        readonly property var centers: root.recField(root.selectedClubId, "bandCentersMm", [])
                                        text: centersField.hasError
                                              ? qsTr("Invalid — comma-separated whole millimetres only")
                                              : centers.length === 0
                                                ? qsTr("Empty = untaped club (passive tracking only)")
                                                : qsTr("%1 bands, pattern %2").arg(centers.length)
                                                                              .arg(root.bandPattern(centers))
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color:          centersField.hasError ? Theme.colorWarn : Theme.colorText3
                                    }
                                }

                                Text {
                                    text:               qsTr("HOSEL FROM BUTT (MM)")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    implicitWidth: Theme.sp(120)
                                    text: root.recField(root.selectedClubId, "hoselFromButtMm", 0) > 0
                                          ? String(root.recField(root.selectedClubId, "hoselFromButtMm", 0)) : ""
                                    placeholderText: qsTr("e.g. 1090")
                                    validator: IntValidator { bottom: 0; top: 1400 }
                                    onEditingFinished: {
                                        var v = parseInt(text, 10)
                                        root.commitField(root.selectedClubId, "hoselFromButtMm", isNaN(v) ? 0 : v)
                                    }
                                }

                                Text {
                                    text:               qsTr("RETRO PATCH ON HEAD")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                Rectangle {
                                    id: patchPill
                                    readonly property bool checked: root.recField(root.selectedClubId, "headPatch", false)
                                    width:  Theme.sp(34)
                                    height: Theme.sp(18)
                                    radius: Theme.sp(9)
                                    color:  patchPill.checked ? Theme.colorAccent : Theme.colorBg3
                                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                                    Rectangle {
                                        width:  Theme.sp(12)
                                        height: Theme.sp(12)
                                        radius: Theme.sp(6)
                                        color:  "white"
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: patchPill.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                                        Behavior on x { NumberAnimation { duration: 120 } }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape:  Qt.PointingHandCursor
                                        onClicked:    root.commitField(root.selectedClubId, "headPatch", !patchPill.checked)
                                    }
                                }

                                Text {
                                    text:               qsTr("NOTES")
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingLabel
                                    color:              Theme.colorText3
                                }
                                PpTextField {
                                    Layout.fillWidth: true
                                    text: root.recField(root.selectedClubId, "notes", "")
                                    placeholderText: qsTr("e.g. tape replaced, measured twice")
                                    onEditingFinished: root.commitField(root.selectedClubId, "notes", text)
                                }
                            }
                    }
                }
            }
        }
    }
}
