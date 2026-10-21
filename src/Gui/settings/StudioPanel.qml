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

// Settings → Studio (panelIndex 11). docs/design/session_report_design.md §7.1 and §7.2.
//
// The studio and its one coach, entered once and printed on every session report: the masthead, the
// running head, the sign-off box, page 8's "How to practise a change", and the email that carries the
// PDF. Everything lives in StudioProfile (context property `studioProfile`, INI group Studio/); this
// panel only edits it. Text commits on editingFinished (Return, or the field losing focus), the way
// every other settings panel commits.
//
// The logo and the coach's photo are chosen with the in-app file picker (PpFilePicker — this
// application opens no native dialogs) and COPIED into the app's data folder by StudioProfile, so
// moving or deleting the original never breaks a report.
//
// Any change here moves studioProfile.stamp, which puts already-made reports out of date (§8.2) —
// except the email subject and message, which are not in the PDF.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Item {
    id: root

    readonly property bool hasProfile: typeof studioProfile !== "undefined" && studioProfile !== null

    // ── Search-scroll contract (ScreenSettings → SettingsIndex) ───────────────

    property string lastHighlightId: ""

    function findChild(parent, name) {
        for (var i = 0; i < parent.children.length; i++) {
            var child = parent.children[i]
            if (child.objectName === name) return child
            var found = findChild(child, name)
            if (found) return found
        }
        return null
    }

    function scrollToItem(itemId) {
        if (!itemId) return true
        var target = findChild(contentCol, itemId)
        if (!target) return false
        var mapped = target.mapToItem(contentCol, 0, 0)
        scrollView.contentItem.contentY = Math.max(0, Math.min(
            mapped.y - Theme.sp(24),
            scrollView.contentItem.contentHeight - scrollView.height
        ))
        target.searchHighlight = true
        lastHighlightId = itemId
        highlightTimer.restart()
        return true
    }

    Timer {
        id: highlightTimer
        interval: 1800
        onTriggered: {
            var target = root.findChild(contentCol, root.lastHighlightId)
            if (target) target.searchHighlight = false
        }
    }

    // A file URL for an image the profile holds, with the stamp on it: the copy keeps its name
    // (logo.png) when it is replaced, and the Image cache would otherwise go on drawing the old one.
    function imageUrl(path) {
        if (!path || path.length === 0) return ""
        return "file://" + (/^[A-Za-z]:\//.test(path) ? "/" : "") + encodeURI(path) + "?v=" + studioProfile.stamp
    }

    // ── the picker, shared by the logo and the photo ──────────────────────────
    property string pickFor: ""           // "logo" | "coach"
    property string pickError: ""
    property string pickErrorFor: ""
    readonly property alias filePicker: picker      // for probes: the picker lives in the overlay
    function pick(what) {
        root.pickFor = what
        root.pickError = ""
        picker.title = what === "logo" ? qsTr("Choose the studio's logo") : qsTr("Choose the coach's photo")
        picker.hint  = what === "logo"
                ? qsTr("PNG, JPEG or SVG. A copy is kept, so the original can be moved or deleted.")
                : qsTr("PNG or JPEG, ideally square. A copy is kept, so the original can be moved or deleted.")
        picker.nameFilters = what === "logo" ? ["*.png", "*.jpg", "*.jpeg", "*.svg"] : ["*.png", "*.jpg", "*.jpeg"]
        picker.open()
    }
    PpFilePicker {
        id: picker
        objectName: "studioFilePicker"
        onChosen: function (path) {
            var ok = root.pickFor === "logo" ? studioProfile.importLogo(path) : studioProfile.importCoachPhoto(path)
            root.pickErrorFor = root.pickFor
            root.pickError = ok ? "" : qsTr("That file could not be used. Choose a PNG, JPEG or SVG image.")
        }
    }

    // ── row furniture, as ArchivingPanel draws it ─────────────────────────────

    component SectionHead: Text {
        font.family:         Theme.fontBody
        font.pixelSize:      Theme.fontSzMicro
        font.letterSpacing:  Theme.trackingMicro
        font.capitalization: Theme.capsFont
        color:               Theme.colorText3
    }

    component RowTitle: ColumnLayout {
        property string title
        property string subtitle
        Layout.fillWidth: true
        spacing: Theme.gap(3)
        Text { text: parent.title; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; color: Theme.colorText }
        Text {
            text: parent.subtitle; visible: text.length > 0
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }

    // One searchable setting: its content in a column, and the search highlight drawn BEHIND it rather
    // than inside the column — a layout would give the highlight a slot of its own (and its spacing).
    component SettingRow: Item {
        id: sr
        property bool searchHighlight: false
        property real spacing: Theme.gap(6)
        default property alias content: srCol.data
        Layout.fillWidth: true
        implicitHeight: srCol.implicitHeight
        Rectangle {
            x: -Theme.sp(6); y: -Theme.sp(6)
            width: sr.width + Theme.sp(12); height: sr.height + Theme.sp(12)
            color: Theme.colorAccentLight; radius: Theme.radius
            opacity: sr.searchHighlight ? 1.0 : 0.0
        }
        ColumnLayout { id: srCol; width: sr.width; spacing: sr.spacing }
    }

    // One labelled text field that commits to the profile. `value` is what the profile holds; `commit`
    // writes it back. The field re-reads `value` after a commit, so a value the profile normalised
    // (trimmed, a colour respelled) shows as it is stored.
    component FieldRow: SettingRow {
        id: fr
        property string title
        property string subtitle
        property string value
        property string placeholder
        property alias  field: tf
        signal commit(string text)
        RowTitle { title: fr.title; subtitle: fr.subtitle }
        PpTextField {
            id: tf
            Layout.fillWidth: true
            text: fr.value
            placeholderText: fr.placeholder
            onEditingFinished: { fr.commit(text); text = Qt.binding(function () { return fr.value }) }
        }
    }

    // A multi-line field in PpTextField's look: the booking sentence, a step's body, the message.
    component AreaField: TextArea {
        id: ta
        property string value
        signal commit(string text)
        Layout.fillWidth: true
        text: ta.value
        wrapMode: TextArea.Wrap
        leftPadding: Theme.gap(10); rightPadding: Theme.gap(10)
        topPadding: Theme.gap(8); bottomPadding: Theme.gap(8)
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; font.weight: Theme.fontBodyWeight
        color: Theme.colorText
        placeholderTextColor: Theme.colorText3
        selectByMouse: true
        onActiveFocusChanged: if (!activeFocus) { ta.commit(text); text = Qt.binding(function () { return ta.value }) }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.colorSurface
            border.width: 1
            border.color: ta.activeFocus ? Theme.colorAccent : Theme.colorBorderStrong
        }
    }

    // An image the profile holds (the logo, the coach's photo): a preview, Choose…, Remove, and the
    // reason in place when a file was refused.
    component ImageRow: SettingRow {
        id: ir
        property string what                 // "logo" | "coach"
        property string title
        property string subtitle
        property string path
        property Component empty: null      // drawn when there is no image (the monogram, for the logo)
        spacing: Theme.gap(8)
        RowTitle { title: ir.title; subtitle: ir.subtitle }
        RowLayout {
            spacing: Theme.gap(16)
            Rectangle {
                objectName: "studioPreview_" + ir.what
                Layout.preferredWidth:  Theme.sp(ir.what === "logo" ? 180 : 96)
                Layout.preferredHeight: Theme.sp(96)
                radius: Theme.radius
                color: Theme.colorSurface
                border.width: 1
                border.color: Theme.colorBorderMid
                Image {
                    id: irImg
                    anchors.fill: parent
                    anchors.margins: Theme.gap(10)
                    visible: ir.path.length > 0
                    source: root.imageUrl(ir.path)
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    asynchronous: true
                    smooth: true; mipmap: true
                    sourceSize.width: Theme.sp(360); sourceSize.height: Theme.sp(192)
                }
                Loader {
                    anchors.centerIn: parent
                    active: ir.path.length === 0 && ir.empty !== null
                    sourceComponent: ir.empty
                }
                Text {
                    anchors.centerIn: parent
                    visible: ir.path.length === 0 && ir.empty === null
                    text: qsTr("No photo")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
                }
            }
            ColumnLayout {
                spacing: Theme.gap(8)
                PpButton {
                    objectName: "studioChoose_" + ir.what
                    label: ir.path.length > 0 ? qsTr("Replace…") : qsTr("Choose…")
                    onClicked: root.pick(ir.what)
                }
                PpLink {
                    objectName: "studioRemove_" + ir.what
                    visible: ir.path.length > 0
                    text: qsTr("Remove")
                    onClicked: ir.what === "logo" ? studioProfile.clearLogo() : studioProfile.clearCoachPhoto()
                }
            }
        }
        Text {
            visible: root.pickError.length > 0 && root.pickErrorFor === ir.what
            text: root.pickError
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorWarn
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }

    // ── the page ──────────────────────────────────────────────────────────────

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentCol.y + contentCol.implicitHeight + Theme.sp(28)

        ColumnLayout {
            id: contentCol
            x:       Theme.sp(32)
            y:       Theme.sp(28)
            width:   Math.min(parent.width - Theme.sp(64), Theme.sp(760))
            spacing: Theme.gap(20)
            enabled: root.hasProfile

            SectionHead { text: Theme.caps(qsTr("Reports")) }
            PpDisplayText { text: qsTr("Studio") }
            Text {
                text: qsTr("Your studio and its coach, as every session report you send shows them: the masthead, the sign-off, page 8's practice steps and the email that carries the PDF. Changing anything here puts reports already made out of date, so they can be made again.")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
                color: Theme.colorText3; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }

            // What a report would still be missing, said once at the top, in the report sheet's words.
            Repeater {
                model: root.hasProfile ? studioProfile.missingDetails : []
                delegate: Text {
                    required property var modelData
                    objectName: "studioMissing"
                    text: "⚠ " + String(modelData.text).replace(/ — add it in Settings → Studio$/, "")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText2
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                }
            }

            // ── The studio ───────────────────────────────────────────────────
            SectionHead { text: Theme.caps(qsTr("The studio")) }
            FieldRow {
                objectName: "setting_studioName"
                title: qsTr("Studio name")
                subtitle: qsTr("The masthead, the running head and the sign-off. Without one, reports carry “PinPoint Studio”.")
                placeholder: qsTr("e.g. Northfield Golf Studio")
                value: root.hasProfile ? studioProfile.studioName : ""
                onCommit: (t) => studioProfile.studioName = t
            }
            ImageRow {
                objectName: "setting_studioLogo"
                what: "logo"
                title: qsTr("Logo")
                subtitle: qsTr("Without a logo, a monogram is made from the studio name, in the accent colour.")
                path: root.hasProfile ? studioProfile.logoPath : ""
                empty: Component {
                    Rectangle {
                        objectName: "studioMonogram"
                        width: Theme.sp(64); height: width; radius: width / 2
                        color: studioProfile.accent
                        Text {
                            anchors.centerIn: parent
                            text: studioProfile.monogram
                            color: "white"
                            font.family: Theme.fontDisplay; font.pixelSize: Theme.sp(26)
                        }
                    }
                }
            }

            // The accent: one colour, a swatch row and a hex field; the warning in place.
            SettingRow {
                id: accentRow
                objectName: "setting_studioAccent"
                property string typed: root.hasProfile ? studioProfile.accent : ""
                readonly property bool typedValid: root.hasProfile && studioProfile.isColour(accentField.text)
                spacing: Theme.gap(8)
                RowTitle {
                    title: qsTr("Accent colour")
                    subtitle: qsTr("The masthead rule, the headings' small capitals, the work-on numbers and the target-range shading. Faults stay in the warning colour whatever this is.")
                }
                RowLayout {
                    spacing: Theme.gap(8)
                    Repeater {
                        model: root.hasProfile ? studioProfile.accentPresets() : []
                        delegate: Rectangle {
                            required property string modelData
                            objectName: "studioSwatch"
                            readonly property bool on: root.hasProfile && studioProfile.accent === modelData
                            width: Theme.sp(26); height: width; radius: width / 2
                            color: modelData
                            border.width: on ? 2 : 0
                            border.color: Theme.colorText
                            Rectangle {   // a gap ring between the swatch and its selection ring
                                visible: parent.on
                                anchors.centerIn: parent
                                width: parent.width - 6; height: width; radius: width / 2
                                color: "transparent"; border.width: 2; border.color: Theme.colorSurface
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: studioProfile.accent = parent.modelData }
                        }
                    }
                    Item { width: Theme.gap(8); height: 1 }
                    PpTextField {
                        id: accentField
                        objectName: "studioAccentField"
                        Layout.preferredWidth: Theme.sp(120)
                        text: accentRow.typed
                        placeholderText: "#1f5a4c"
                        hasError: text.trim().length > 0 && !accentRow.typedValid
                        onEditingFinished: {
                            if (accentRow.typedValid) studioProfile.accent = text
                            text = Qt.binding(function () { return accentRow.typed })
                        }
                    }
                }
                Text {
                    objectName: "studioAccentWarning"
                    readonly property string candidate: accentRow.typedValid ? accentField.text : (root.hasProfile ? studioProfile.accent : "")
                    visible: text.length > 0
                    text: accentField.text.trim().length > 0 && !accentRow.typedValid
                          ? qsTr("Not a colour. Use a hex value such as #1f5a4c, or press a swatch.")
                          : root.hasProfile && studioProfile.accentTooLight(candidate)
                            ? qsTr("Too light to read as text on white paper. Headings and work-on numbers in this colour will be hard to see; a darker shade will read.")
                            : ""
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorWarn
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Contact ──────────────────────────────────────────────────────
            SectionHead { text: Theme.caps(qsTr("Contact — each line prints only when it is filled in")) }
            FieldRow {
                objectName: "setting_studioAddress"
                title: qsTr("Address")
                subtitle: qsTr("Optional. The report's footer and sign-off.")
                placeholder: qsTr("e.g. 1 High Street, Northfield NF1 2AB")
                value: root.hasProfile ? studioProfile.address : ""
                onCommit: (t) => studioProfile.address = t
            }
            FieldRow {
                objectName: "setting_studioPhone"
                title: qsTr("Phone")
                placeholder: qsTr("e.g. 01234 567890")
                value: root.hasProfile ? studioProfile.phone : ""
                onCommit: (t) => studioProfile.phone = t
            }
            FieldRow {
                objectName: "setting_studioEmail"
                title: qsTr("Email")
                subtitle: qsTr("The studio's own address. Replies to a sent report go to the coach's address below.")
                placeholder: qsTr("e.g. hello@northfieldgolf.co.uk")
                value: root.hasProfile ? studioProfile.email : ""
                onCommit: (t) => studioProfile.email = t
            }
            FieldRow {
                objectName: "setting_studioWebsite"
                title: qsTr("Website")
                placeholder: qsTr("e.g. northfieldgolf.co.uk")
                value: root.hasProfile ? studioProfile.website : ""
                onCommit: (t) => studioProfile.website = t
            }
            SettingRow {
                objectName: "setting_studioBooking"
                RowTitle {
                    title: qsTr("Booking")
                    subtitle: qsTr("One sentence in the sign-off box, telling the golfer how to book the next session.")
                }
                AreaField {
                    objectName: "studioBookingField"
                    placeholderText: qsTr("e.g. Book your next session at northfieldgolf.co.uk or reply to this email.")
                    value: root.hasProfile ? studioProfile.booking : ""
                    onCommit: (t) => studioProfile.booking = t
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── The coach ────────────────────────────────────────────────────
            SectionHead { text: Theme.caps(qsTr("The coach — every report is from this coach")) }
            FieldRow {
                objectName: "setting_studioCoachName"
                title: qsTr("Name")
                subtitle: qsTr("The sign-off and the page header.")
                placeholder: qsTr("e.g. Alex Morgan")
                value: root.hasProfile ? studioProfile.coachName : ""
                onCommit: (t) => studioProfile.coachName = t
            }
            FieldRow {
                objectName: "setting_studioCoachTitle"
                title: qsTr("Title")
                placeholder: qsTr("e.g. Head coach")
                value: root.hasProfile ? studioProfile.coachTitle : ""
                onCommit: (t) => studioProfile.coachTitle = t
            }
            FieldRow {
                objectName: "setting_studioCoachEmail"
                title: qsTr("Email")
                subtitle: qsTr("Where the golfer's replies go.")
                placeholder: qsTr("e.g. alex@northfieldgolf.co.uk")
                value: root.hasProfile ? studioProfile.coachEmail : ""
                onCommit: (t) => studioProfile.coachEmail = t
            }
            FieldRow {
                objectName: "setting_studioCoachPhone"
                title: qsTr("Phone")
                subtitle: qsTr("Optional.")
                placeholder: qsTr("e.g. 07700 900123")
                value: root.hasProfile ? studioProfile.coachPhone : ""
                onCommit: (t) => studioProfile.coachPhone = t
            }
            ImageRow {
                objectName: "setting_studioCoachPhoto"
                what: "coach"
                title: qsTr("Photo")
                subtitle: qsTr("Optional. Shown in the sign-off box.")
                path: root.hasProfile ? studioProfile.coachPhotoPath : ""
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── How to practise ──────────────────────────────────────────────
            SectionHead { text: Theme.caps(qsTr("How to practise a change — page 8 of every report")) }
            SettingRow {
                id: stepsCol
                objectName: "setting_studioPractice"
                spacing: Theme.gap(14)
                readonly property var steps: root.hasProfile ? studioProfile.practiceSteps : []
                function commit() {
                    var out = []
                    for (var i = 0; i < stepRepeater.count; ++i) {
                        var it = stepRepeater.itemAt(i)
                        if (it) out.push({ head: it.headText, body: it.bodyText })
                    }
                    studioProfile.practiceSteps = out
                }
                RowLayout {
                    Layout.fillWidth: true
                    RowTitle {
                        title: qsTr("Four steps")
                        subtitle: qsTr("A short head and one or two sentences each. A step left wholly blank is left out.")
                    }
                    PpLink {
                        objectName: "studioPracticeReset"
                        text: qsTr("Use the original text")
                        visible: root.hasProfile && JSON.stringify(studioProfile.practiceSteps) !== JSON.stringify(studioProfile.defaultPracticeSteps())
                        onClicked: studioProfile.practiceSteps = studioProfile.defaultPracticeSteps()
                    }
                }
                Repeater {
                    id: stepRepeater
                    model: 4
                    delegate: RowLayout {
                        id: stepRow
                        required property int index
                        readonly property var step: index < stepsCol.steps.length ? stepsCol.steps[index] : ({ head: "", body: "" })
                        readonly property string headText: headField.text.trim()
                        readonly property string bodyText: bodyField.text.trim()
                        objectName: "studioStep"
                        Layout.fillWidth: true
                        spacing: Theme.gap(12)
                        Text {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: Theme.gap(6)
                            Layout.preferredWidth: Theme.sp(18)
                            text: String(index + 1)
                            font.family: Theme.fontDisplay; font.pixelSize: Theme.fontSzHeading
                            color: root.hasProfile ? studioProfile.accent : Theme.colorAccent
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.gap(6)
                            PpTextField {
                                id: headField
                                objectName: "studioStepHead"
                                Layout.fillWidth: true
                                text: stepRow.step.head || ""
                                placeholderText: qsTr("Head, e.g. Rehearse without a ball")
                                onEditingFinished: stepsCol.commit()
                            }
                            AreaField {
                                id: bodyField
                                objectName: "studioStepBody"
                                value: stepRow.step.body || ""
                                placeholderText: qsTr("What to do, in a sentence or two")
                                onCommit: stepsCol.commit()
                            }
                        }
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── The email ────────────────────────────────────────────────────
            SectionHead { text: Theme.caps(qsTr("The email that carries the report")) }
            Text {
                text: qsTr("{first} is the golfer's greeting, {date} the session's date, {coach} the coach's name, {studio} the studio's name.")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            FieldRow {
                objectName: "setting_studioEmailSubject"
                title: qsTr("Subject")
                value: root.hasProfile ? studioProfile.emailSubject : ""
                placeholder: root.hasProfile ? studioProfile.defaultEmailSubject() : ""
                onCommit: (t) => studioProfile.emailSubject = t
            }
            SettingRow {
                objectName: "setting_studioEmailBody"
                RowLayout {
                    Layout.fillWidth: true
                    RowTitle {
                        title: qsTr("Message")
                        subtitle: qsTr("The coach's note is in the PDF, so it is not repeated here. Your mail program opens the message for you to read before it is sent.")
                    }
                    PpLink {
                        objectName: "studioEmailReset"
                        text: qsTr("Use the original text")
                        visible: root.hasProfile && (studioProfile.emailBody !== studioProfile.defaultEmailBody()
                                                     || studioProfile.emailSubject !== studioProfile.defaultEmailSubject())
                        onClicked: { studioProfile.emailSubject = ""; studioProfile.emailBody = "" }
                    }
                }
                AreaField {
                    objectName: "studioEmailBodyField"
                    Layout.preferredHeight: Math.max(implicitHeight, Theme.sp(150))
                    value: root.hasProfile ? studioProfile.emailBody : ""
                    onCommit: (t) => studioProfile.emailBody = t
                }
            }
            Item { Layout.preferredHeight: Theme.sp(8) }
        }
    }
}
