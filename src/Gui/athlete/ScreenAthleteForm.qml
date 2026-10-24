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

Item {
    id: root

    signal cancelled()
    signal saved(string uuid)
    signal savedAndStarted(string uuid)
    // The Sessions tab (session report §8.0): a row's "Open session", and a missing studio detail's link.
    signal reviewSessionRequested(string sessionDir)
    signal studioSettingsRequested()
    // Which tab the page opens on: "details" (the form) or "sessions". Set before navigating here.
    property string initialTab: "details"
    // The tab on screen. Sessions exists for a saved golfer only: a new golfer has no sessions.
    property string tab: "details"
    readonly property bool sessionsTab: root.tab === "sessions" && root.editUuid !== ""
    // Main.qml sets editUuid, then initialTab, then calls loadForEdit(); a caller that sets only
    // initialTab on an already-open golfer is answered here. Either way it is spent on use.
    // A golfer newly opened lands on Details unless initialTab says otherwise; re-loading the SAME golfer
    // (Main.qml's explicit loadForEdit after setting initialTab) keeps the tab already chosen.
    onInitialTabChanged: if (root.initialTab !== "details" && root.editUuid !== "") root._applyInitialTab()
    property string _loadedUuid: ""
    function _applyInitialTab() {
        if (root.editUuid !== "" && root.initialTab === "sessions") root.tab = "sessions"
        else if (root.editUuid !== root._loadedUuid || root.editUuid === "") root.tab = "details"
        root.initialTab = "details"
        root._loadedUuid = root.editUuid
    }

    // ── Form state ────────────────────────────────────────────────────────────
    property bool   nameError:      false
    property bool   dobError:       false
    property string handedness:     "Right"
    property string heightUnit:     "ft"
    property string weightUnit:     "lb"
    property string primaryClub:    "DRIVER"
    // "" (never answered) | "female" | "male" | "declined". The tokens come from C++ and are never
    // spelled here — see sexOptions below.
    property string sex:            ""

    // ── Reports (session report §7.3) ─────────────────────────────────────────
    // Buffered like the rest of the form and written after saveAthlete, through updateAthlete; the
    // approval through setPhotosApproved, which owns its date.
    property bool   emailError:       false
    property bool   photosApproved:   false
    property string photosApprovedOn: ""          // as stored; "" when never approved
    property bool   _photosStored:    false       // what the record held when the form loaded
    // Under 18 TODAY, from the date as typed — the form's preview. A report asks again with its
    // session's own date (athleteController.isJuniorOn), so an old session keeps its answer.
    readonly property bool isJunior: root.dobOk(dobField.text)
                                     && athleteController.isJuniorOn(dobField.text.trim(), 0)
    function emailShapeOk(text) {
        var t = String(text).trim()
        return t.length === 0 || athleteController.validReportEmail(t)
    }
    // "a@b.com, , c@d.org," → "a@b.com, c@d.org": one spelling, whatever was typed between them.
    function normaliseEmails(text) {
        return String(text).split(",").map(function (x) { return x.trim() })
                           .filter(function (x) { return x.length > 0 }).join(", ")
    }
    function firstWord(text) {
        var w = String(text).trim().split(/\s+/)
        return w.length > 0 ? w[0] : ""
    }
    function approvedWords(iso) {
        var d = new Date((iso && iso.length > 0 ? iso : Qt.formatDate(new Date(), "yyyy-MM-dd")) + "T00:00:00")
        return isNaN(d.getTime()) ? "" : qsTr("approved %1").arg(Qt.formatDate(d, "d MMM yyyy"))
    }

    // The sex picker's vocabulary, from the controller: {value, label} per option. QML maps between
    // the two over THIS list, so the stored token exists in exactly one place.
    readonly property var    sexOptions: athleteController.sexOptions()
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

    // Blank is VALID — both fields are optional and unset is a real answer. Only a non-empty value
    // that is not a real date is an error, so a half-typed date does not accuse anybody of anything
    // until they leave the field.
    function dobOk(text) {
        var t = String(text).trim()
        if (t.length === 0) return true
        if (!/^\d{4}-\d{2}-\d{2}$/.test(t)) return false
        // Built and read back in LOCAL time. Round-tripping through toISOString() compared a local
        // midnight with its UTC date, so every date in British Summer Time came back as the day
        // before and was rejected. The read-back still catches "2026-02-31", which rolls into March.
        var p = t.split("-").map(Number)
        var d = new Date(p[0], p[1] - 1, p[2])
        return d.getFullYear() === p[0] && d.getMonth() === p[1] - 1 && d.getDate() === p[2]
    }

    // "" = create mode; a uuid = edit that athlete.
    property string editUuid: ""

    // Preferred-club options come from the athlete's actual bag (canonical ids,
    // vocabulary order). Re-evaluates on every club commit via athletesChanged.
    // Create mode (no uuid yet) and an empty bag fall back to the full vocabulary.
    readonly property var clubModel: {
        void athleteController.athletes
        var vocab = athleteController.clubOptions()
        if (root.editUuid === "") return vocab
        var bag = athleteController.clubsFor(root.editUuid)
        var ids = Object.keys(bag)
        if (ids.length === 0) return vocab
        ids.sort(function (a, b) { return vocab.indexOf(a) - vocab.indexOf(b) })
        return ids
    }

    onEditUuidChanged: loadForEdit()
    Component.onCompleted: loadForEdit()

    function resetForm() {
        nameField.text     = ""
        handedness         = "Right"
        heightField.text   = ""
        heightUnit         = "ft"
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
        root._applyInitialTab()
        if (editUuid === "") { resetForm(); return }

        const a = athleteController.athletes.find(function(x) { return x.uuid === editUuid })
        if (!a) { resetForm(); return }

        nameField.text     = a.name || ""
        handedness         = a.handedness  || "Right"
        // Resolve to a real bag club (handles legacy/absent values).
        primaryClub        = athleteController.effectivePrimaryClub(editUuid) || "DRIVER"
        handicapField.text = (a.handicap !== undefined && a.handicap > -900) ? String(a.handicap) : ""
        speedField.text    = (a.speedTarget && a.speedTarget > 0) ? String(a.speedTarget) : ""
        notesField.text    = a.notes || ""

        // Height/weight are STORED in base units (ft / lb) but the athlete carries a
        // saved display-unit preference. Convert the stored value back into that unit
        // so the number and the toggle agree.
        heightUnit = (a.heightUnit === "cm") ? "cm" : "ft"
        heightField.text = (a.heightValue && a.heightValue > 0)
            ? (heightUnit === "cm" ? (a.heightValue * 30.48).toFixed(1)
                                   : a.heightValue.toFixed(2))
            : ""

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
        dobError  = !root.dobOk(dobField.text)
        emailError = !root.emailShapeOk(reportEmailField.text)
        return !nameError && !dobError && !emailError
    }

    function doSave(): string {
        const hv = parseFloat(heightField.text) || 0.0
        const wv = parseFloat(weightField.text) || 0.0
        const hcp = parseFloat(handicapField.text)
        const handicap = isNaN(hcp) ? -999.0 : hcp
        const speed = parseFloat(speedField.text) || 0.0

        const uuid = athleteController.saveAthlete(
            root.editUuid,            // "" creates, uuid updates
            nameField.text.trim(),
            handedness,
            hv, heightUnit,
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
            athleteController.updateAthlete(uuid, "dob", dobField.text.trim())
            athleteController.updateAthlete(uuid, "sex", root.sex)
            // The report's details (§7.3), the same way. The approval goes through setPhotosApproved
            // only when it CHANGED, so re-saving the form never moves the date consent was given.
            athleteController.updateAthlete(uuid, "reportEmail", root.normaliseEmails(reportEmailField.text))
            athleteController.updateAthlete(uuid, "reportGreeting", reportGreetingField.text.trim())
            if (root.photosApproved !== root._photosStored) {
                athleteController.setPhotosApproved(uuid, root.photosApproved)
                root._photosStored    = root.photosApproved
                root.photosApprovedOn = athleteController.reportDetails(uuid).photosApprovedOn || ""
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
            Text {
                text:               Theme.caps(qsTr("Athlete profile"))
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
                bottomPadding:      10
            }
            PpDisplayText {
                text: root.editUuid === "" ? qsTr("New athlete") : qsTr("Edit athlete")
            }
            Item { width: 1; height: 6 }

            // ── Tabs: Details (the form) and Sessions (a saved golfer's) ──────
            Item { visible: root.editUuid !== ""; width: 1; height: Theme.sp(8) }
            PpTabs {
                id: formTabs
                objectName: "athleteFormTabs"
                visible: root.editUuid !== ""
                width:   parent.width
                height:  visible ? implicitHeight : 0
                roles:   formTs
                tabs:    [{ key: "details", label: qsTr("Details") }, { key: "sessions", label: qsTr("Sessions") }]
                current: root.sessionsTab ? "sessions" : "details"
                onTabActivated: function (key) { root.tab = key }
            }
            Item { visible: root.editUuid !== ""; width: 1; height: Theme.sp(16) }

            // ── Sessions ─────────────────────────────────────────────────────
            // The list builds its own rows and links (agent K's PpSessionReportList); its requests
            // leave through this screen's signals, which Main.qml handles.
            //
            // The list (sessionReportList) is the CURRENT golfer's — main.cpp points it at their library
            // folder. A golfer opened from the picker may not be current, and their tab must not show
            // somebody else's sessions, so it says so and offers the one press that fixes it.
            readonly property bool _listIsThisGolfer: root.editUuid !== "" && root.editUuid === athleteController.currentUuid
            PpSessionReportList {
                id: sessionList
                objectName: "athleteSessionList"
                visible: root.sessionsTab && parent._listIsThisGolfer
                width:   parent.width
                limit:   0
            }
            Column {
                objectName: "athleteSessionsNotCurrent"
                visible: root.sessionsTab && !parent._listIsThisGolfer
                width:   parent.width
                spacing: Theme.gap(8)
                Text {
                    width: parent.width
                    text: qsTr("%1's sessions are listed here when %2 is the golfer on Home.")
                            .arg(nameField.text.trim()).arg(root.firstWord(nameField.text))
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText3; wrapMode: Text.WordWrap
                }
                PpLink {
                    text: qsTr("Make %1 the golfer on Home").arg(root.firstWord(nameField.text))
                    onClicked: athleteController.selectAthlete(root.editUuid)
                }
            }
            Connections {
                target: sessionList
                ignoreUnknownSignals: true
                function onReviewSessionRequested(dir) { root.reviewSessionRequested(dir) }
                function onStudioSettingsRequested() { root.studioSettingsRequested() }
                // "add Nick's email": the address lives on this page, one tab over.
                function onGolferDetailsRequested() { root.tab = "details"; reportEmailField.forceActiveFocus() }
            }

            Column {
                id: detailsCol
                width:   parent.width
                spacing: 0
                visible: !root.sessionsTab

                Text {
                    width:          parent.width
                    text:           qsTr("Only the first two fields are required. The rest sharpen analysis and personalise coaching output.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
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
                            height: Theme.sp(36)
                            color:  Theme.colorBg2

                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.gap(14); rightMargin: Theme.gap(14) }
                                Text {
                                    text:           qsTr("Required")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:          Theme.colorText2
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text:           qsTr("Name and handedness")
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
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(18) }
                                spacing: Theme.gap(12)

                                // Name
                                Column {
                                    width:   parent.width
                                    spacing: Theme.gap(4)
                                    Text {
                                        text:               Theme.caps(qsTr("Name"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
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
                                    Text {
                                        text:               Theme.caps(qsTr("Handedness"))
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

                Item { width: 1; height: Theme.sp(16) }

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
                            height: Theme.sp(36)
                            color:  Theme.colorBg2

                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.gap(14); rightMargin: Theme.gap(14) }
                                Text {
                                    text:           qsTr("Recommended")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:          Theme.colorText2
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text:           qsTr("Improves analysis accuracy")
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
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(18) }
                                spacing: Theme.gap(12)

                                // Height
                                Column {
                                    width:   parent.width
                                    spacing: Theme.gap(4)
                                    Text {
                                        text:               Theme.caps(qsTr("Height"))
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
                                    spacing: Theme.gap(4)
                                    Text {
                                        text:               Theme.caps(qsTr("Weight"))
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
                                    spacing: Theme.gap(4)
                                    Text {
                                        text:               Theme.caps(qsTr("Handicap"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
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
                                    Text {
                                        text:               Theme.caps(qsTr("Primary club"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
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
                                    Text {
                                        text:               Theme.caps(qsTr("Date of birth"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
                                    PpTextField {
                                        id: dobField
                                        width:           Theme.sp(140)
                                        placeholderText: "YYYY-MM-DD"
                                        hasError:        root.dobError
                                        onTextChanged:   if (root.dobError && root.dobOk(text)) root.dobError = false
                                        onEditingFinished: root.dobError = !root.dobOk(text)
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
                                    Text {
                                        text:               Theme.caps(qsTr("Sex"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
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
                                            var who = athleteController.cohortLabelFor(
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
                        }
                    }
                }

                Item { width: 1; height: Theme.sp(16) }

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
                            height: Theme.sp(36)
                            color:  Theme.colorBg2

                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.gap(14); rightMargin: Theme.gap(14) }
                                Text {
                                    text:           qsTr("Optional")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:          Theme.colorText2
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text:           qsTr("Personalises coaching output")
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
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(18) }
                                spacing: Theme.gap(12)

                                // Driver speed target
                                Column {
                                    width:   parent.width
                                    spacing: Theme.gap(4)
                                    Text {
                                        text:               Theme.caps(qsTr("Driver speed target"))
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
                                    Text {
                                        text:               Theme.caps(qsTr("Notes / tags"))
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
                            height: noteText.implicitHeight + Theme.sp(20)
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
                }

                Item { width: 1; height: Theme.sp(16) }

                // ── Reports group (session report §7.3) ─────────────────────────
                //
                // Where the golfer's session reports go, what they are called, and — for a junior only —
                // whether a parent or guardian approved swing photos. These stay in the app's settings
                // beside the rest of the record; they never go into a swing, a session folder or the share.
                Rectangle {
                    objectName: "athleteReportsGroup"
                    width:        parent.width
                    height:       repCol.implicitHeight
                    radius:       Theme.radiusLg
                    border.width: 1
                    border.color: Theme.colorBorderMid
                    clip:         true
                    color:        "transparent"

                    Column {
                        id: repCol
                        width: parent.width
                        spacing: 0

                        // Header strip
                        Rectangle {
                            width:  parent.width
                            height: Theme.sp(36)
                            color:  Theme.colorBg2

                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.gap(14); rightMargin: Theme.gap(14) }
                                Text {
                                    text:           qsTr("Reports")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:          Theme.colorText2
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text:           qsTr("Used only to send this golfer their session reports.")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorText3
                                    elide:          Text.ElideRight
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
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
                            height: repBody.implicitHeight + 36
                            color:  Theme.colorSurface

                            Column {
                                id: repBody
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.gap(18) }
                                spacing: Theme.gap(12)

                                // Email — shape-checked as it is typed, the message in place. Quiet while
                                // the field has focus (a half-typed address is not a mistake yet), warn
                                // once it is left or Save is pressed.
                                Column {
                                    width:   parent.width
                                    spacing: Theme.gap(4)
                                    Text {
                                        text:               Theme.caps(qsTr("Email"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
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
                                        height:         bad ? implicitHeight : 0
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
                                    Text {
                                        text:               Theme.caps(qsTr("Greeting"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
                                    PpTextField {
                                        id: reportGreetingField
                                        objectName: "athleteReportGreeting"
                                        width:           Theme.sp(220)
                                        placeholderText: root.firstWord(nameField.text).length > 0
                                                         ? root.firstWord(nameField.text) : qsTr("e.g. Nick")
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
                                    Text {
                                        text:               Theme.caps(qsTr("Swing photos"))
                                        font.family:        Theme.fontData
                                        font.pixelSize:     Theme.fontSzMicro
                                        font.letterSpacing: Theme.trackingLabel
                                        color:              Theme.colorText3
                                    }
                                    Item {
                                        id: photosCheck
                                        objectName: "athleteReportPhotosCheck"
                                        width:  parent.width
                                        height: Math.max(Theme.sp(26), photosRow.implicitHeight)
                                        Row {
                                            id: photosRow
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: Theme.gap(10)
                                            Rectangle {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: Theme.sp(16); height: Theme.sp(16); radius: Theme.sp(4)
                                                color:        root.photosApproved ? Theme.colorAccent : "transparent"
                                                border.width: 1
                                                border.color: root.photosApproved ? Theme.colorAccent : Theme.colorBorderStrong
                                                Text {
                                                    anchors.centerIn: parent
                                                    visible:        root.photosApproved
                                                    text:           "✓"
                                                    font.family:    Theme.fontSymbol
                                                    font.pixelSize: Theme.fontSzLabel
                                                    color:          Theme.dark ? Theme.colorBg : Theme.colorSurface
                                                }
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text:           qsTr("A parent or guardian has approved sending swing photos")
                                                font.family:    Theme.fontBody
                                                font.pixelSize: Theme.fontSzBody
                                                color:          Theme.colorText
                                            }
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
                                        PpPressable {
                                            hoverScale: 1.0
                                            onClicked:  root.photosApproved = !root.photosApproved
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
                        }
                    }
                }

                // ── Clubs (edit mode only — records commit immediately, unlike the
                //    buffered scalar fields; a new athlete must be saved first) ─────
                Item { visible: root.editUuid !== ""; width: 1; height: Theme.sp(16) }
                AthleteClubsSection {
                    visible:     root.editUuid !== ""
                    athleteUuid: root.editUuid
                    width:       parent.width
                }

                // ── Action row ───────────────────────────────────────────────────
                Item { width: 1; height: Theme.sp(24) }
                Row {
                    anchors.right: parent.right
                    spacing: Theme.gap(8)
                    bottomPadding: 32

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
                Item { width: 1; height: Theme.sp(32) }
            }   // detailsCol
        }
    }

    // Declared last: children are destroyed in creation order, and the tabs read it to the end.
    PpTypeset { id: formTs }
}
