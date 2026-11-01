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
//
// SENDING REPORTS (§8.3, "Changed (Mark, 21 October)") is the one section that is not the profile: it edits
// the coach's own mail account on THIS computer through ReportMailer (context property `reportMailer`) —
// provider (presets fill the server), login, and the password, which goes to the system's secure store
// (Keychain, Windows Credential Manager) and is never shown again, only "Saved in …". Until it is set up,
// a session row's Email hands the PDF to the mail program as before; once it is, Email sends it itself.
// Changing the login or the server forgets the saved password (the C++ does it), and the password row
// follows `hasPassword`, so it asks again.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

SettingsPanelBase {
    id: root

    maxContentWidth: Theme.sp(760)
    contentEnabled:  root.hasProfile
    // The login, password and test rows show only once a provider is chosen: a search for one of them
    // lands on the provider.
    landingFor: function (target) {
        return target.visible ? target : (root.findChild(root.contentColumn, "setting_sendProvider") || target)
    }

    readonly property bool hasProfile: typeof studioProfile !== "undefined" && studioProfile !== null
    readonly property bool hasMailer:  typeof reportMailer !== "undefined" && reportMailer !== null
    // Sending reports is set up: a session row's Email sends the report itself.
    readonly property bool sendReady:  root.hasMailer && reportMailer.sendConfigured

    // A file URL for an image the profile holds, with the stamp on it: the copy keeps its name
    // (logo.png) when it is replaced, and the Image cache would otherwise go on drawing the old one.
    function imageUrl(path) {
        if (!path || path.length === 0) return ""
        return Theme.fileUrl(path) + "?v=" + studioProfile.stamp
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

    // ── Sending reports: the words and the test ──────────────────────────────
    readonly property var providerKeys:   ["gmail", "icloud", "microsoft", "other"]
    readonly property var providerLabels: [qsTr("Gmail"), qsTr("iCloud"), qsTr("Microsoft 365"), qsTr("Other")]
    readonly property string provider: root.hasMailer ? reportMailer.provider : ""
    readonly property var securityKeys:   ["starttls", "tls", "none"]
    readonly property var securityLabels: [qsTr("STARTTLS"), qsTr("SSL/TLS"), qsTr("None")]
    // Where the password is kept, in the words the coach knows it by.
    readonly property string passwordStore: Qt.platform.os === "osx"     ? qsTr("Keychain")
                                          : Qt.platform.os === "windows" ? qsTr("Windows Credential Manager")
                                          :                                qsTr("the app's settings file")

    // The test email: "" | "running" | "ok" | "failed", its time, its words, and the address it went to
    // (the one reports come from — ReportMailer's rule: the login when it is an address, else the coach's
    // email). Any change to the set-up makes an old result meaningless, so it goes.
    property string testState: ""
    property real   testAtMs:  0
    property string testError: ""
    property string testTo:    ""
    function sendTestNow() {
        if (!root.hasMailer) return
        const coachEmail = root.hasProfile ? studioProfile.coachEmail : ""
        const fromName = root.hasProfile ? reportMailer.senderName(studioProfile.coachName, studioProfile.studioName) : ""
        root.testTo = reportMailer.fromAddress(coachEmail)
        const res = reportMailer.sendTest(fromName, coachEmail)
        if (!res || !res.ok) { root.testState = "failed"; root.testError = (res && res.error) || qsTr("The test could not be sent."); return }
        root.testState = "running"
        root.testError = ""
    }
    Connections {
        target: root.hasMailer ? reportMailer : null
        function onTestFinished(ok, error, atMs) {
            if (root.testState !== "running") return
            root.testState = ok ? "ok" : "failed"
            root.testError = error
            root.testAtMs  = atMs
        }
        // A changed set-up makes the last test's result, and a password refusal about the old login, stale.
        function onSendingSettingsChanged() {
            if (root.testState !== "running") root.testState = ""
            passwordRow.error = ""
        }
    }

    // One labelled text field that commits to the profile. `value` is what the profile holds; `commit`
    // writes it back. The field re-reads `value` after a commit, so a value the profile normalised
    // (trimmed, a colour respelled) shows as it is stored.
    component FieldRow: SettingsRow {
        id: fr
        property string title
        property string subtitle
        property string value
        property string placeholder
        property alias  field: tf
        signal commit(string text)
        SettingsRowTitle { title: fr.title; subtitle: fr.subtitle }
        PpTextField {
            id: tf
            Layout.fillWidth: true
            text: fr.value
            placeholderText: fr.placeholder
            onEditingFinished: { fr.commit(text); text = Qt.binding(function () { return fr.value }) }
        }
    }

    // An image the profile holds (the logo, the coach's photo): a preview, Choose…, Remove, and the
    // reason in place when a file was refused.
    component ImageRow: SettingsRow {
        id: ir
        property string what                 // "logo" | "coach"
        property string title
        property string subtitle
        property string path
        property Component empty: null      // drawn when there is no image (the monogram, for the logo)
        spacing: Theme.gap(8)
        SettingsRowTitle { title: ir.title; subtitle: ir.subtitle }
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

    PpPageHeader {
        Layout.fillWidth: true
        spacing:     root.contentSpacing
        eyebrow:     Theme.caps(qsTr("Reports"))
        title:       qsTr("Studio")
        description: qsTr("Your studio and its coach, as every session report you send shows them: the masthead, the sign-off, page 8's practice steps and the email that carries the PDF. Changing anything here puts reports already made out of date, so they can be made again.")
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
    PpMicro { kind: "body"; text: Theme.caps(qsTr("The studio")) }
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
    SettingsRow {
        id: accentRow
        objectName: "setting_studioAccent"
        property string typed: root.hasProfile ? studioProfile.accent : ""
        readonly property bool typedValid: root.hasProfile && studioProfile.isColour(accentField.text)
        spacing: Theme.gap(8)
        SettingsRowTitle {
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
    PpMicro { kind: "body"; text: Theme.caps(qsTr("Contact — each line prints only when it is filled in")) }
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
    SettingsRow {
        objectName: "setting_studioBooking"
        SettingsRowTitle {
            title: qsTr("Booking")
            subtitle: qsTr("One sentence in the sign-off box, telling the golfer how to book the next session.")
        }
        PpTextArea {
            objectName: "studioBookingField"
            Layout.fillWidth: true
            commitOnBlur: true
            placeholderText: qsTr("e.g. Book your next session at northfieldgolf.co.uk or reply to this email.")
            value: root.hasProfile ? studioProfile.booking : ""
            onCommit: (t) => studioProfile.booking = t
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── The coach ────────────────────────────────────────────────────
    PpMicro { kind: "body"; text: Theme.caps(qsTr("The coach — every report is from this coach")) }
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
    PpMicro { kind: "body"; text: Theme.caps(qsTr("How to practice a change — page 8 of every report")) }
    SettingsRow {
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
            SettingsRowTitle {
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
                    PpTextArea {
                        id: bodyField
                        objectName: "studioStepBody"
                        Layout.fillWidth: true
                        commitOnBlur: true
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
    PpMicro { kind: "body"; text: Theme.caps(qsTr("The email that carries the report")) }
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
    SettingsRow {
        objectName: "setting_studioEmailBody"
        RowLayout {
            Layout.fillWidth: true
            SettingsRowTitle {
                title: qsTr("Message")
                subtitle: root.sendReady
                          ? qsTr("The coach's note is in the PDF, so it is not repeated here. Reports are sent from the app, so the message goes exactly as written here, with no chance to read it first.")
                          : qsTr("The coach's note is in the PDF, so it is not repeated here. Your mail program opens the message for you to read before it is sent.")
            }
            PpLink {
                objectName: "studioEmailReset"
                text: qsTr("Use the original text")
                visible: root.hasProfile && (studioProfile.emailBody !== studioProfile.defaultEmailBody()
                                             || studioProfile.emailSubject !== studioProfile.defaultEmailSubject())
                onClicked: { studioProfile.emailSubject = ""; studioProfile.emailBody = "" }
            }
        }
        PpTextArea {
            objectName: "studioEmailBodyField"
            Layout.fillWidth: true
            commitOnBlur: true
            Layout.preferredHeight: Math.max(implicitHeight, Theme.sp(150))
            value: root.hasProfile ? studioProfile.emailBody : ""
            onCommit: (t) => studioProfile.emailBody = t
        }
    }

    PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

    // ── Sending reports ──────────────────────────────────────────────
    PpMicro { kind: "body"; objectName: "studioSendingHead"; text: Theme.caps(qsTr("Sending reports")) }
    Text {
        objectName: "studioSendingStatus"
        text: root.provider === ""
              ? qsTr("Not set up. Email opens your mail program with the report attached, for you to send.")
              : root.sendReady
                ? (reportMailer.username !== ""
                   ? qsTr("Reports are sent from %1 by %2.").arg(reportMailer.username).arg(reportMailer.host)
                   : qsTr("Reports are sent by %1.").arg(reportMailer.host))
                : (reportMailer.security === "none" && reportMailer.username !== "")
                  // The C++ never sends a login in the clear, so say why None with a login is not enough.
                  ? qsTr("Not finished. A login is only sent over a secure connection: choose STARTTLS or SSL/TLS, or leave the login empty if your mail server needs none.")
                  : qsTr("Not finished. Until the login and password are saved, Email opens your mail program with the report attached, for you to send.")
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
        color: Theme.colorText2; wrapMode: Text.WordWrap; Layout.fillWidth: true
    }

    // The provider: a preset fills the server; Other asks for it.
    SettingsRow {
        objectName: "setting_sendProvider"
        spacing: Theme.gap(10)
        SettingsRowTitle {
            title: qsTr("Send from")
            subtitle: qsTr("Your own mail account, set up once on this computer. Email on a session then sends the report itself.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            PpSegmentedControl {
                objectName: "studioSendProviderControl"
                Layout.preferredWidth: Theme.sp(440)
                options:  root.providerLabels
                selected: root.providerKeys.indexOf(root.provider) >= 0
                          ? root.providerLabels[root.providerKeys.indexOf(root.provider)] : ""
                onActivated: (v) => {
                    const k = root.providerKeys[root.providerLabels.indexOf(v)]
                    if (k && k !== root.provider) reportMailer.setProvider(k)
                }
            }
            Item { Layout.fillWidth: true }
            PpLink {
                objectName: "studioSendStop"
                visible: root.provider !== ""
                text: qsTr("Stop sending from the app")
                color: hovered ? Theme.colorText2 : Theme.colorText3
                onClicked: { passwordRow.changing = false; passwordRow.error = ""; reportMailer.forgetSending() }
            }
        }
        Text {
            objectName: "studioSendHelp"
            visible: text.length > 0
            text: root.provider === "gmail"
                  ? qsTr("Gmail needs an app password, not your normal password. Turn on 2-Step Verification, then make one at myaccount.google.com/\u2060apppasswords and paste it below. If that page says the setting isn't available, your account only uses security keys or Advanced Protection, and Gmail won't allow it.")
                  : root.provider === "icloud"
                    ? qsTr("iCloud needs an app-specific password: account.apple.com → Sign-In and Security → App-Specific Passwords. Your login is your iCloud email address.")
                    : root.provider === "microsoft"
                      ? qsTr("Works while your organisation allows SMTP sign-in for this mailbox. Microsoft is switching it off for many accounts from the end of 2026; if sending fails, ask whoever runs your email.")
                      : ""
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
            color: Theme.colorText2; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }

    // Other: the server, its port and how the connection is secured, committed together.
    SettingsRow {
        id: serverRow
        objectName: "studioSendServer"
        visible: root.provider === "other"
        spacing: Theme.gap(8)
        function commit(security) {
            const host = hostField.text.trim()
            const port = parseInt(portField.text, 10) || 0
            const sec  = security || reportMailer.security
            if (host !== reportMailer.host || port !== reportMailer.port || sec !== reportMailer.security)
                reportMailer.setServer(host, port, sec)
            hostField.text = Qt.binding(function () { return reportMailer.host })
            portField.text = Qt.binding(function () { return reportMailer.port > 0 ? String(reportMailer.port) : "" })
        }
        SettingsRowTitle {
            title: qsTr("Mail server")
            subtitle: qsTr("The outgoing (SMTP) server, its port, and how the connection is secured. Your mail provider's help pages give all three.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(8)
            PpTextField {
                id: hostField
                objectName: "studioSendHostField"
                Layout.fillWidth: true
                text: root.hasMailer ? reportMailer.host : ""
                placeholderText: qsTr("e.g. smtp.example.com")
                onEditingFinished: serverRow.commit("")
            }
            PpTextField {
                id: portField
                objectName: "studioSendPortField"
                Layout.preferredWidth: Theme.sp(80)
                text: root.hasMailer && reportMailer.port > 0 ? String(reportMailer.port) : ""
                placeholderText: "587"
                validator: IntValidator { bottom: 1; top: 65535 }
                onEditingFinished: serverRow.commit("")
            }
            Item { width: Theme.gap(8); height: 1 }
            PpChipGroup {
                objectName: "studioSendSecurity"
                options: root.securityLabels
                selected: root.hasMailer && root.securityKeys.indexOf(reportMailer.security) >= 0
                          ? root.securityLabels[root.securityKeys.indexOf(reportMailer.security)] : ""
                onSelectionChanged: (v) => {
                    const sec = root.securityKeys[root.securityLabels.indexOf(v)]
                    // The usual port goes with the usual security, unless the coach typed another.
                    if (sec === "tls" && portField.text === "587") portField.text = "465"
                    else if (sec === "starttls" && portField.text === "465") portField.text = "587"
                    serverRow.commit(sec)
                }
            }
        }
    }

    FieldRow {
        objectName: "setting_sendLogin"
        visible: root.provider !== ""
        title: qsTr("Login")
        subtitle: root.provider === "other"
                  ? qsTr("Usually your email address. Leave the login empty only if your internet provider gives you a mail server that needs none.")
                  : qsTr("Your email address. Reports are sent from it, and replies go to the coach's email above.")
        placeholder: root.provider === "gmail" ? qsTr("e.g. alex.morgan@gmail.com")
                   : root.provider === "icloud" ? qsTr("e.g. alex.morgan@icloud.com")
                   : qsTr("e.g. alex@northfieldgolf.co.uk")
        value: root.hasMailer ? reportMailer.username : ""
        onCommit: (t) => { if (t.trim() !== reportMailer.username) reportMailer.setUsername(t) }
    }

    // The password goes to the system's store and is never shown again: "Saved in Keychain", or the
    // field. A change of login or server forgets it (hasPassword goes false), so the field comes back.
    SettingsRow {
        id: passwordRow
        objectName: "setting_sendPassword"
        visible: root.provider !== "" && !(root.provider === "other" && root.hasMailer && reportMailer.username === "")
        spacing: Theme.gap(8)
        property bool   changing: false
        property string error: ""
        readonly property bool saved: root.hasMailer && reportMailer.hasPassword
        function commit() {
            const t = pwField.text
            if (t.length === 0) { if (passwordRow.saved) passwordRow.changing = false; return }
            const res = reportMailer.setPassword(t)
            pwField.text = ""
            if (res && res.ok) { passwordRow.error = ""; passwordRow.changing = false }
            else passwordRow.error = (res && res.error) || qsTr("The password could not be saved.")
        }
        SettingsRowTitle {
            title: root.provider === "gmail" ? qsTr("App password")
                 : root.provider === "icloud" ? qsTr("App-specific password")
                 : qsTr("Password")
            subtitle: qsTr("Kept in %1 on this computer, never in the settings file.").arg(root.passwordStore)
        }
        RowLayout {
            objectName: "studioPasswordSaved"
            visible: passwordRow.saved && !passwordRow.changing
            spacing: Theme.gap(12)
            Text {
                text: qsTr("Saved in %1").arg(root.passwordStore)
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody; font.weight: Theme.fontBodyWeight
                color: Theme.colorText2
            }
            PpLink {
                objectName: "studioPasswordChange"
                text: qsTr("Change")
                onClicked: { passwordRow.changing = true; passwordRow.error = ""; pwField.forceActiveFocus() }
            }
        }
        PpTextField {
            id: pwField
            objectName: "studioPasswordField"
            visible: !passwordRow.saved || passwordRow.changing
            Layout.fillWidth: true
            echoMode: TextInput.Password
            hasError: passwordRow.error !== ""
            placeholderText: root.provider === "gmail" ? qsTr("Paste the app password from Google")
                           : root.provider === "icloud" ? qsTr("Paste the app-specific password from Apple")
                           : qsTr("The password for this login")
            onEditingFinished: passwordRow.commit()
        }
        Text {
            objectName: "studioPasswordError"
            visible: passwordRow.error !== ""
            text: passwordRow.error
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro; color: Theme.colorWarn
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }

    SettingsRow {
        objectName: "setting_sendCopy"
        visible: root.provider !== ""
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(16)
            SettingsRowTitle {
                title: qsTr("Send me a copy of every report")
                subtitle: qsTr("A blind copy to you, because most mail servers keep nothing in Sent when an app sends for you.")
            }
            PpSwitch {
                objectName: "studioSendCopyToggle"
                Layout.alignment: Qt.AlignVCenter
                checked: root.hasMailer && reportMailer.copyToSelf
                onToggled: (v) => reportMailer.setCopyToSelf(v)
            }
        }
    }

    // The test: a short message with no attachment to the address reports come from.
    SettingsRow {
        objectName: "setting_sendTest"
        visible: root.provider !== ""
        spacing: Theme.gap(4)
        RowLayout {
            spacing: Theme.gap(14)
            PpLink {
                objectName: "studioSendTest"
                readonly property bool active: root.sendReady && !reportMailer.sending
                text: qsTr("Send a test email")
                interactive: active
                onClicked: root.sendTestNow()
            }
            Text {
                objectName: "studioSendTestResult"
                visible: text.length > 0
                text: root.testState === "running" ? qsTr("Sending…")
                    : root.testState === "ok" ? qsTr("Sent to %1 at %2. Check your inbox.").arg(root.testTo)
                                                  .arg(Qt.formatTime(new Date(root.testAtMs), "HH:mm"))
                    : !root.sendReady && root.hasMailer ? (reportMailer.host === "" ? qsTr("Fill in the mail server first.")
                                                                   : qsTr("Fill in the login and password first."))
                    : ""
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
        }
        Text {
            objectName: "studioSendTestError"
            visible: root.testState === "failed" && text.length > 0
            text: root.testError
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; color: Theme.colorWarn
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
    }
    Item { Layout.preferredHeight: Theme.sp(8) }
}
