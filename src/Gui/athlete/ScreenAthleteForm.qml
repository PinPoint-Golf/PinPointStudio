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
import PinPointStudio

Item {
    id: root

    signal cancelled()
    signal saved(string uuid)
    signal savedAndStarted(string uuid)

    // ── Form state ────────────────────────────────────────────────────────────
    property bool   nameError:      false
    property bool   dobError:       false
    property bool   heightError:    false
    property string handedness:     "Right"
    property string heightUnit:     "ft"
    property string weightUnit:     "lb"
    property string primaryClub:    "DRIVER"
    // "" (never answered) | "female" | "male" | "declined". The tokens come from C++ and are never
    // spelled here — see sexOptions below.
    property string sex:            ""
    // The height as loaded (ft) and the text and unit the field showed for it. Saving with the field
    // untouched keeps the stored height exactly: the field shows decimal feet to 2 places, so reading
    // "5.92" back would move a stored 5'11" (5.9167 ft) on every save of an unrelated field.
    property real   _heightStoredFt:  0
    property string _heightShownText: ""
    property string _heightShownUnit: ""
    // Blank is valid (unknown); anything else must read as a height. A typo is never saved as 0 —
    // that would quietly wipe a stored height — so the form refuses it, as it refuses a bad date.
    // Untouched: the field still shows what the form put there for the stored height (on load, or
    // after a unit toggle of that same value). Save and the toggle then work from the stored height,
    // never from the rounded text, so neither moves a height nobody retyped.
    function heightUntouched(): bool {
        return root._heightStoredFt > 0
            && heightField.text === root._heightShownText
            && root.heightUnit === root._heightShownUnit
    }
    // The stored height as the field shows it — the one formatting, for load and toggle alike.
    function heightShownFor(ft: real, unit: string): string {
        return unit === "cm" ? (ft * 30.48).toFixed(1) : ft.toFixed(2)
    }
    function heightOk(text: string): bool {
        return text.trim() === "" || app.athleteController.parseHeight(text, root.heightUnit) > 0
    }

    // ── Reports (session report §7.3) ─────────────────────────────────────────
    // Buffered like the rest of the form and written after saveAthlete, through updateAthlete; the
    // approval through setPhotosApproved, which owns its date.
    property bool   emailError:       false
    property bool   photosApproved:   false
    property string photosApprovedOn: ""          // as stored; "" when never approved
    property bool   _photosStored:    false       // what the record held when the form loaded
    // Under 18 TODAY, from the date as typed — the form's preview. A report asks again with its
    // session's own date (athleteController.isJuniorOn), so an old session keeps its answer.
    readonly property bool isJunior: app.athleteController.validDob(dobField.text)
                                     && app.athleteController.isJuniorOn(dobField.text.trim(), 0)
    // The text rules are the controller's (athlete_controller.cpp), beside the C++ that stores and
    // reads the same fields: validReportEmail, normaliseReportEmails (the one stored spelling),
    // greetingDefault (the first word a blank greeting falls back to) and validDob.
    function emailShapeOk(text) {
        var t = String(text).trim()
        return t.length === 0 || app.athleteController.validReportEmail(t)
    }
    function approvedWords(iso) {
        var d = new Date((iso && iso.length > 0 ? iso : Qt.formatDate(new Date(), "yyyy-MM-dd")) + "T00:00:00")
        return isNaN(d.getTime()) ? "" : qsTr("approved %1").arg(Qt.formatDate(d, "d MMM yyyy"))
    }

    // The sex picker's vocabulary, from the controller: {value, label} per option. QML maps between
    // the two over THIS list, so the stored token exists in exactly one place.
    readonly property var    sexOptions: app.athleteController.sexOptions()
    readonly property var    sexLabels:  root.sexOptions.map(function (o) { return o.label })

    function sexLabelFor(value) {
        for (var i = 0; i < root.sexOptions.length; ++i)
            if (root.sexOptions[i].value === value) return root.sexOptions[i].label
        return ""
    }
    function sexValueFor(label) {
        for (var i = 0; i < root.sexOptions.length; ++i)
            if (root.sexOptions[i].label === label) return root.sexOptions[i].value
        return ""
    }

    // "" = create mode; a uuid = edit that athlete.
    property string editUuid: ""

    // Preferred-club options come from the athlete's actual bag (canonical ids,
    // vocabulary order). Re-evaluates on every club commit via athletesChanged.
    // Create mode (no uuid yet) and an empty bag fall back to the full vocabulary.
    readonly property var clubModel: {
        void app.athleteController.athletes
        var vocab = app.athleteController.clubOptions()
        if (root.editUuid === "") return vocab
        var ids = app.athleteController.bagClubIds(root.editUuid)
        return ids.length > 0 ? ids : vocab
    }

    onEditUuidChanged: loadForEdit()
    Component.onCompleted: loadForEdit()

    function resetForm() {
        nameField.text     = ""
        handedness         = "Right"
        heightField.text   = ""
        heightUnit         = "ft"
        _heightStoredFt    = 0
        _heightShownText   = ""
        _heightShownUnit   = ""
        heightError        = false
        weightField.text   = ""
        weightUnit         = "lb"
        handicapField.text = ""
        primaryClub        = "DRIVER"
        speedField.text    = ""
        notesField.text    = ""
        dobField.text      = ""
        sex                = ""
        nameError          = false
        dobError           = false
        reportEmailField.text    = ""
        reportGreetingField.text = ""
        photosApproved     = false
        photosApprovedOn   = ""
        _photosStored      = false
        emailError         = false
    }

    function loadForEdit() {
        if (editUuid === "") { resetForm(); return }

        const a = app.athleteController.athletes.find(function(x) { return x.uuid === editUuid })
        if (!a) { resetForm(); return }

        nameField.text     = a.name || ""
        handedness         = a.handedness  || "Right"
        // Resolve to a real bag club (handles legacy/absent values).
        primaryClub        = app.athleteController.effectivePrimaryClub(editUuid) || "DRIVER"
        handicapField.text = (a.handicap !== undefined && a.handicap > -900) ? String(a.handicap) : ""
        speedField.text    = (a.speedTarget && a.speedTarget > 0) ? String(a.speedTarget) : ""
        notesField.text    = a.notes || ""

        // Height/weight are STORED in base units (ft / lb) but the athlete carries a
        // saved display-unit preference. Convert the stored value back into that unit
        // so the number and the toggle agree.
        heightUnit = (a.heightUnit === "cm") ? "cm" : "ft"
        heightField.text = (a.heightValue && a.heightValue > 0) ? heightShownFor(a.heightValue, heightUnit) : ""
        _heightStoredFt  = (a.heightValue && a.heightValue > 0) ? a.heightValue : 0
        _heightShownText = heightField.text
        _heightShownUnit = heightUnit
        heightError      = false

        weightUnit = (a.weightUnit === "kg") ? "kg" : "lb"
        weightField.text = (a.weightValue && a.weightValue > 0)
            ? (weightUnit === "kg" ? (a.weightValue / 2.20462).toFixed(1)
                                   : a.weightValue.toFixed(1))
            : ""

        dobField.text = a.dob || ""
        sex           = a.sex || ""

        reportEmailField.text    = a.reportEmail || ""
        reportGreetingField.text = a.reportGreeting || ""
        photosApproved   = a.reportPhotosApproved === true
        photosApprovedOn = a.reportPhotosApprovedOn || ""
        _photosStored    = photosApproved

        nameError  = false
        dobError   = false
        emailError = false
    }

    function validate(): bool {
        nameError = nameField.text.trim() === ""
        dobError  = !app.athleteController.validDob(dobField.text)
        heightError = !root.heightOk(heightField.text)
        emailError = !root.emailShapeOk(reportEmailField.text)
        return !nameError && !dobError && !heightError && !emailError
    }

    function doSave(): string {
        // 5'11" is five feet eleven: the C++ reader takes every spelling the placeholder invites
        // (parseFloat stopped at the apostrophe and stored whole feet). Untouched, the stored value.
        // An untouched height, or one validate() should have refused, keeps the stored height (unset
        // stays unset). It is handed over in ft, the stored unit, so saveAthlete's cm→ft division never
        // re-rounds it: the record keeps its exact bits, and the display unit is written on its own
        // below. Blank is the user clearing the height.
        const parsedHeight = app.athleteController.parseHeight(heightField.text, root.heightUnit)
        const keepStored = root.heightUntouched()
                        || (heightField.text.trim() !== "" && !(parsedHeight > 0))
        const hv     = keepStored ? root._heightStoredFt
                     : heightField.text.trim() === "" ? 0.0 : parsedHeight
        const hvUnit = keepStored ? "ft" : heightUnit
        const wv = parseFloat(weightField.text) || 0.0
        const hcp = parseFloat(handicapField.text)
        const handicap = isNaN(hcp) ? -999.0 : hcp
        const speed = parseFloat(speedField.text) || 0.0

        const uuid = app.athleteController.saveAthlete(
            root.editUuid,            // "" creates, uuid updates
            nameField.text.trim(),
            handedness,
            hv, hvUnit,
            wv, weightUnit,
            handicap,
            primaryClub,
            speed,
            notesField.text.trim()
        )

        // Written through the generic field setter rather than as two more positional arguments on
        // a call that already takes eleven. They are demographics for norm cohorts and nothing else
        // reads them, so they do not belong in the signature every caller of saveAthlete has to
        // thread through.
        if (uuid !== "") {
            if (hvUnit !== heightUnit) app.athleteController.updateAthlete(uuid, "heightUnit", heightUnit)
            app.athleteController.updateAthlete(uuid, "dob", dobField.text.trim())
            app.athleteController.updateAthlete(uuid, "sex", root.sex)
            // The report's details (§7.3), the same way. The approval goes through setPhotosApproved
            // only when it CHANGED, so re-saving the form never moves the date consent was given.
            app.athleteController.updateAthlete(uuid, "reportEmail", app.athleteController.normaliseReportEmails(reportEmailField.text))
            app.athleteController.updateAthlete(uuid, "reportGreeting", reportGreetingField.text.trim())
            if (root.photosApproved !== root._photosStored) {
                app.athleteController.setPhotosApproved(uuid, root.photosApproved)
                root._photosStored    = root.photosApproved
                root.photosApprovedOn = app.athleteController.reportDetails(uuid).photosApprovedOn || ""
            }
        }
        return uuid
    }

    ScrollView {
        anchors.fill:    parent
        contentWidth:    width
        clip:            true

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Theme.contentWidth(parent.width)
            spacing: 0
            y:       32

            // ── Header ───────────────────────────────────────────────────────
            PpPageHeader {
                style:      "screen"
                eyebrowGap: 10
                eyebrow:    Theme.caps(qsTr("Athlete profile"))
                title:      root.editUuid === "" ? qsTr("New athlete") : qsTr("Edit athlete")
            }
            Item { width: 1; height: Theme.gap(6) }

            Column {
                id: detailsCol
                width:   parent.width
                spacing: 0

                PpCardNote {
                    kind:          "form"
                    width:         parent.width
                    text:          qsTr("Only the first two fields are required. The rest sharpen analysis and personalise coaching output.")
                    bottomPadding: Theme.gap(24)
                }

                // ── Required group ───────────────────────────────────────────────
                PpGroupCard {
                    title: qsTr("Required")
                    hint:  qsTr("Name and handedness")

                    // Name
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Name")) }
                        PpTextField {
                            id: nameField
                            width:           parent.width
                            placeholderText: qsTr("e.g. Mark Carter")
                            hasError:        root.nameError
                            onTextChanged:   if (root.nameError && text.trim() !== "") root.nameError = false
                        }
                        Text {
                            visible:        root.nameError
                            height:         root.nameError ? implicitHeight : 0
                            text:           qsTr("Name is required")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorWarn
                        }
                    }

                    // Handedness
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Handedness")) }
                        PpChipGroup {
                            options:  ["Right", "Left"]
                            selected: root.handedness
                            onSelectionChanged: function(v) { root.handedness = v }
                        }
                    }
                }

                Item { width: 1; height: Theme.gap(16) }

                // ── Recommended group ────────────────────────────────────────────
                PpGroupCard {
                    title: qsTr("Recommended")
                    hint:  qsTr("Improves analysis accuracy")

                    // Height
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Height")) }
                        Row {
                            width:   parent.width
                            spacing: 0
                            PpTextField {
                                id: heightField
                                width:           parent.width - heightUnitToggle.width
                                placeholderText: root.heightUnit === "ft" ? "e.g. 5'11\"" : "e.g. 180"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                hasError:        root.heightError
                                // As the date: a half-typed height accuses nobody until they leave the field.
                                onTextChanged:   if (root.heightError && root.heightOk(text)) root.heightError = false
                                onEditingFinished: root.heightError = !root.heightOk(text)
                            }
                            PpSegmentedControl {
                                id: heightUnitToggle
                                style:    "unit"
                                options:  ["ft", "cm"]
                                selected: root.heightUnit
                                onActivated: function(unit) {
                                    if (unit !== root.heightUnit && root.heightUntouched()) {
                                        // The stored height in the other unit; the field stays untouched.
                                        heightField.text      = root.heightShownFor(root._heightStoredFt, unit)
                                        root._heightShownText = heightField.text
                                        root._heightShownUnit = unit
                                        root.heightUnit       = unit
                                        return
                                    }
                                    const val = app.athleteController.parseHeight(heightField.text, root.heightUnit)
                                    if (val > 0) {
                                        if (unit === "cm" && root.heightUnit === "ft")
                                            heightField.text = (val * 30.48).toFixed(1)
                                        else if (unit === "ft" && root.heightUnit === "cm")
                                            heightField.text = (val / 30.48).toFixed(2)
                                    }
                                    root.heightUnit = unit
                                }
                            }
                        }
                        Text {
                            visible:        root.heightError
                            height:         root.heightError ? implicitHeight : 0
                            text:           qsTr("Use 5'11\", 180 cm or 1.80 m, or leave it blank")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorWarn
                        }
                    }

                    // Weight
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Weight")) }
                        Row {
                            width:   parent.width
                            spacing: 0
                            PpTextField {
                                id: weightField
                                width:           parent.width - weightUnitToggle.width
                                placeholderText: root.weightUnit === "lb" ? "e.g. 178" : "e.g. 80"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                            PpSegmentedControl {
                                id: weightUnitToggle
                                style:    "unit"
                                options:  ["lb", "kg"]
                                selected: root.weightUnit
                                onActivated: function(unit) {
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
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Handicap")) }
                        PpTextField {
                            id: handicapField
                            width:            Theme.sp(100)
                            placeholderText:  "e.g. 5"
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }
                    }

                    // Primary club
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Primary club")) }
                        // Sourced from the athlete's bag (canonical ids). Sync
                        // currentIndex imperatively — not via a binding — so
                        // picking an item doesn't sever the link on reload.
                        PpComboBox {
                            id: primaryClubCombo
                            implicitWidth: Theme.sp(220)
                            model: root.clubModel
                            displayFn: ClubFormat.display
                            onActivated: function (i) { root.primaryClub = root.clubModel[i] }
                            function syncFromValue() {
                                var idx = root.clubModel.indexOf(root.primaryClub)
                                if (idx < 0 && root.clubModel.length > 0) {
                                    root.primaryClub = root.clubModel[0]
                                    idx = 0
                                }
                                currentIndex = idx >= 0 ? idx : 0
                            }
                            Component.onCompleted: syncFromValue()
                            onModelChanged: syncFromValue()
                            Connections {
                                target: root
                                function onPrimaryClubChanged() { primaryClubCombo.syncFromValue() }
                            }
                        }
                    }

                    // ── Date of birth and sex ────────────────────────
                    //
                    // Both OPTIONAL, and the caption says so and says why. Asking somebody
                    // for their sex without stating what it is used for is worse than not
                    // asking; and unset has to read as a legitimate answer, because it is —
                    // a golfer who leaves both blank is graded by the corridor that
                    // describes everyone, never "not measured".
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Date of birth")) }
                        PpTextField {
                            id: dobField
                            width:           Theme.sp(140)
                            placeholderText: "YYYY-MM-DD"
                            hasError:        root.dobError
                            // Blank is VALID — both fields are optional and unset is a real answer. Only a
                            // non-empty value that is not a real date is an error, so a half-typed date
                            // does not accuse anybody of anything until they leave the field.
                            onTextChanged:   if (root.dobError && app.athleteController.validDob(text)) root.dobError = false
                            onEditingFinished: root.dobError = !app.athleteController.validDob(text)
                        }
                        Text {
                            visible:        root.dobError
                            height:         root.dobError ? implicitHeight : 0
                            text:           qsTr("Use YYYY-MM-DD, or leave it blank")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorWarn
                        }
                    }

                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Sex")) }
                        // The chips show labels and carry TOKENS. Both come from C++
                        // (athleteController.sexOptions), so the stored spelling exists in
                        // one place — QML maps between the two over that list and never
                        // writes a token itself.
                        PpChipGroup {
                            options:  root.sexLabels
                            selected: root.sexLabelFor(root.sex)
                            onSelectionChanged: function(v) { root.sex = root.sexValueFor(v) }
                        }
                        Text {
                            width:          parent.width
                            text:           qsTr("Optional. Used only to pick the normative corridor a reading is graded against — leave either blank and you are graded against the general one.")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                        }

                        // What the two fields above actually RESOLVE to, live. Without it a
                        // date and a word have no visible consequence, and the band
                        // boundaries are discoverable only by reading a header comment.
                        //
                        // "Today" is said out loud because the band is derived at the SWING
                        // date, not stored — so this is a preview of now, and a swing from
                        // four years ago will resolve the band this golfer was in then.
                        Text {
                            width:   parent.width
                            visible: text.length > 0
                            text: {
                                if (root.dobError) return ""
                                var who = app.athleteController.cohortLabelFor(
                                    dobField.text.trim(), root.sex,
                                    Qt.formatDate(new Date(), "yyyy-MM-dd"))   // local today
                                return who.length > 0
                                    ? qsTr("Today this places you in: %1").arg(who)
                                    : ""
                            }
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorAccent
                            wrapMode:       Text.WordWrap
                        }
                    }
                }

                Item { width: 1; height: Theme.gap(16) }

                // ── Optional group ───────────────────────────────────────────────
                PpGroupCard {
                    title: qsTr("Optional")
                    hint:  qsTr("Personalises coaching output")

                    // Driver speed target
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Driver speed target")) }
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
                                width:  Theme.sp(36)
                                height: Theme.sp(34)
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
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Notes / tags")) }
                        PpTextField {
                            id: notesField
                            width:           parent.width
                            placeholderText: "e.g. junior, left-low, early ext"
                        }
                    }

                    // Note strip
                    footer: Rectangle {
                        width:  parent.width
                        height: noteText.implicitHeight + Theme.gap(20)
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
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(18); topMargin: Theme.gap(10) }
                            text:           qsTr("Pinpoint builds baselines automatically from early sessions. You don't need to know your driver speed to get started.")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzDataSm
                            font.weight:    Font.Light
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.4
                        }
                    }
                }

                Item { width: 1; height: Theme.gap(16) }

                // ── Reports group (session report §7.3) ─────────────────────────
                //
                // Where the golfer's session reports go, what they are called, and — for a junior only —
                // whether a parent or guardian approved swing photos. These stay in the app's settings
                // beside the rest of the record; they never go into a swing, a session folder or the share.
                PpGroupCard {
                    objectName: "athleteReportsGroup"
                    title: qsTr("Reports")
                    hint:  qsTr("Used only to send this golfer their session reports.")
                    elideHint: true

                    // Email — shape-checked as it is typed, the message in place. Quiet while
                    // the field has focus (a half-typed address is not a mistake yet), warn
                    // once it is left or Save is pressed.
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Email")) }
                        PpTextField {
                            id: reportEmailField
                            objectName: "athleteReportEmail"
                            width:           parent.width
                            placeholderText: qsTr("e.g. nick@example.com — several, separated by commas")
                            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                            hasError:        root.emailError || (!activeFocus && !root.emailShapeOk(text))
                            onTextChanged:   if (root.emailError && root.emailShapeOk(text)) root.emailError = false
                        }
                        Text {
                            objectName: "athleteReportEmailMessage"
                            readonly property bool bad: !root.emailShapeOk(reportEmailField.text)
                            visible:        bad
                            width:          parent.width
                            text:           qsTr("Not an email address yet. Use the form name@example.com, with a comma between two addresses.")
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          reportEmailField.activeFocus && !root.emailError ? Theme.colorText3 : Theme.colorWarn
                            wrapMode:       Text.WordWrap
                        }
                        Text {
                            visible:        root.isJunior
                            width:          parent.width
                            text:           qsTr("Under 18: use a parent's or guardian's address. Both can be listed.")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                        }
                    }

                    // Greeting — what the email and page 1 call them; blank = the first word.
                    Column {
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Greeting")) }
                        PpTextField {
                            id: reportGreetingField
                            objectName: "athleteReportGreeting"
                            width:           Theme.sp(220)
                            readonly property string fallback: app.athleteController.greetingDefault(nameField.text)
                            placeholderText: fallback.length > 0 ? fallback : qsTr("e.g. Nick")
                        }
                        Text {
                            width:          parent.width
                            text:           qsTr("What the email and the report call them. Left blank, the first word of the name.")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                        }
                    }

                    // Photo approval — a junior only (under 18 on the date of birth above).
                    Column {
                        objectName: "athleteReportPhotos"
                        visible: root.isJunior
                        width:   parent.width
                        spacing: Theme.gap(4)
                        PpFieldLabel { text: Theme.caps(qsTr("Swing photos")) }
                        PpCheckRow {
                            id: photosCheck
                            objectName: "athleteReportPhotosCheck"
                            width:     parent.width
                            label:     qsTr("A parent or guardian has approved sending swing photos")
                            checked:   root.photosApproved
                            onToggled: root.photosApproved = !root.photosApproved
                            Text {
                                objectName: "athleteReportPhotosDate"
                                anchors.verticalCenter: parent.verticalCenter
                                visible:        root.photosApproved
                                // The stored date while it stands; today's for a tick not yet saved.
                                text:           root.approvedWords(root._photosStored ? root.photosApprovedOn : "")
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                            }
                        }
                        Text {
                            width:          parent.width
                            text:           root.photosApproved
                                            ? qsTr("Reports include swing photos and the reference swing.")
                                            : qsTr("Until it is ticked, reports leave out swing photos and show the charts instead.")
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                            wrapMode:       Text.WordWrap
                        }
                    }
                }

                // ── Clubs (edit mode only — records commit immediately, unlike the
                //    buffered scalar fields; a new athlete must be saved first) ─────
                Item { visible: root.editUuid !== ""; width: 1; height: Theme.gap(16) }
                AthleteClubsSection {
                    visible:     root.editUuid !== ""
                    athleteUuid: root.editUuid
                    width:       parent.width
                }

                // ── Action row ───────────────────────────────────────────────────
                Item { width: 1; height: Theme.gap(24) }
                Row {
                    anchors.right: parent.right
                    spacing: Theme.gap(8)
                    bottomPadding: Theme.gap(32)

                    PpButton {
                        label: qsTr("Cancel")
                        onClicked: root.cancelled()
                    }
                    PpButton {
                        label:   root.editUuid === "" ? qsTr("Save") : qsTr("Save changes")
                        primary: root.editUuid !== ""        // primary in edit mode (no Save-and-start there)
                        onClicked: {
                            if (!validate()) return
                            const uuid = doSave()
                            if (uuid !== "") root.saved(uuid)
                        }
                    }
                    PpButton {
                        visible: root.editUuid === ""        // create flow only
                        label:   qsTr("Save and start ↗")
                        primary: true
                        onClicked: {
                            if (!validate()) return
                            const uuid = doSave()
                            if (uuid !== "") root.savedAndStarted(uuid)
                        }
                    }
                }
                Item { width: 1; height: Theme.gap(32) }
            }   // detailsCol
        }
    }
}
