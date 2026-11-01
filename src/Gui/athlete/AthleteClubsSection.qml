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
PpGroupCard {
    id: root

    title:   qsTr("Clubs")
    hint:    qsTr("Lengths and retro-band positions — saved as you edit")
    spacing: Theme.gap(14)

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

    // Re-evaluates whenever the controller reloads (every commit calls reload()).
    readonly property var clubs: {
        void athleteController.athletes
        return athleteController.clubsFor(root.athleteUuid)
    }
    readonly property var configuredIds: {
        void athleteController.athletes
        return athleteController.bagClubIds(root.athleteUuid)
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

    // ── Body (PpGroupCard's default content) ───────────────────────────────
    Text {
        width: parent.width
        text: qsTr("Club length sets the shaft-tracker search radius. Shaft length (bottom of grip to top of hosel) and hands end (bottom of your hands from the butt) let the tracker measure an unmarked club. Band centres describe retroreflective tape (optional), measured from the butt end of the grip in millimetres.")
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorText3
        wrapMode:       Text.WordWrap
    }

    // Add club
    Row {
        spacing: Theme.gap(10)

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
    // accent) and brighter text — the chip group's "tab" look, which wraps,
    // so a full bag — up to 17 clubs — fits. Selecting a tab shows ONE detail card.
    PpChipGroup {
        width:   parent.width
        spacing: Theme.gap(5)
        wrap:    true
        style:   "tab"
        options:  root.configuredIds.map(id => ({ label: ClubFormat.display(id), value: id }))
        selected: root.selectedClubId
        onSelectionChanged: (id) => {
            // Steal focus first so an in-edit field commits
            // against the OUTGOING club before the switch.
            root.forceActiveFocus()
            root.selectedClubId = id
        }
    }

    // Detail card for the selected club
    Rectangle {
        visible: root.selectedClubId !== ""
        width:  parent.width
        height: visible ? clubCol.implicitHeight + Theme.gap(28) : 0
        radius: Theme.radius
        color:  Theme.colorBg2
        border.width: 1
        border.color: Theme.colorBorder

        ColumnLayout {
            id: clubCol
            anchors { fill: parent; margins: Theme.gap(14) }
            spacing: Theme.gap(10)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(10)

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
                    columnSpacing: Theme.gap(20)
                    rowSpacing: Theme.gap(8)

                    PpFieldLabel { text: Theme.caps(qsTr("Shaft")) }
                    PpComboBox {
                        implicitWidth: Theme.sp(150)
                        model: [qsTr("steel"), qsTr("graphite")]
                        currentIndex: root.recField(root.selectedClubId, "shaftType", "steel") === "graphite" ? 1 : 0
                        onActivated: (index) => root.commitField(root.selectedClubId, "shaftType",
                                                                 index === 1 ? "graphite" : "steel")
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Loft"), "°") }
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

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Club length"), "mm") }
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

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Shaft length"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "shaftLengthMm", 0) > 0
                              ? String(root.recField(root.selectedClubId, "shaftLengthMm", 0)) : ""
                        placeholderText: qsTr("e.g. 617")
                        validator: IntValidator { bottom: 0; top: 1400 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "shaftLengthMm", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Hands end from butt"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: root.recField(root.selectedClubId, "handsEndMm", 0) > 0
                              ? String(root.recField(root.selectedClubId, "handsEndMm", 0)) : ""
                        placeholderText: qsTr("e.g. 180")
                        validator: IntValidator { bottom: 0; top: 400 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "handsEndMm", isNaN(v) ? 0 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Band width"), "mm") }
                    PpTextField {
                        implicitWidth: Theme.sp(120)
                        text: String(root.recField(root.selectedClubId, "bandWidthMm", 25))
                        validator: IntValidator { bottom: 0; top: 100 }
                        onEditingFinished: {
                            var v = parseInt(text, 10)
                            root.commitField(root.selectedClubId, "bandWidthMm", isNaN(v) ? 25 : v)
                        }
                    }

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Band centres from butt"), "mm") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap(4)

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
                                                     Qt.formatDate(new Date(), "yyyy-MM-dd"))
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

                    PpFieldLabel { text: Theme.capsUnit(qsTr("Hosel from butt"), "mm") }
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

                    PpFieldLabel { text: Theme.caps(qsTr("Retro patch on head")) }
                    PpSwitch {
                        id: patchPill
                        checked:   root.recField(root.selectedClubId, "headPatch", false)
                        onToggled: (v) => root.commitField(root.selectedClubId, "headPatch", v)
                    }

                    PpFieldLabel { text: Theme.caps(qsTr("Notes")) }
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
