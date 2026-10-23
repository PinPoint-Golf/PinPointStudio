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

// ONE SESSION, WITH ITS REPORT (docs/design/session_report_design.md §8.0) — a row of PpSessionReportList,
// on Home and on the golfer page's Sessions tab alike.
//
//   Tue 20 Oct 2026 · Session 3                    7 iron · 10 swings  (Face-on) (DTL) (GC Quad)
//   Work-on 1: Keep the flex in your trail knee                    Opened in Mail 20 Oct 18:40
//   Open session    Report · 1.4 MB    Email to Nick    Copy address    Show in Finder
//
// THE LINKS. Open session always. A session of fewer than three swings, or the one still running, has
// nothing else: its "report" would be the one-page note, or out of date as soon as it went.
//   Report         the PDF, in the system's viewer, when it is current, with a quiet "Remake" beside it
//                  (always on offer: a model change leaves a report current but out of step); "Make report" when there is none
//                  (opens the sheet); "Remake report" when it is out of date, with "open the old one"
//                  beside it
//   Email to Nick  hands the PDF to the coach's mail app. Shown INACTIVE, with the reason in place as a
//                  link to where it is fixed, when there is no PDF ("make the report first", which makes
//                  it and then hands it off) or no address ("add Nick's email", the golfer page). Once
//                  sending is set up in Settings → Studio it opens the confirm line below instead, and
//                  waits (inactive) while any report is being sent
//   Copy address / Show in Finder   option C, always beside it, quiet (§8.3)
//
// THE CONFIRM (sending set up, §8.3): one line under the links, in place —
//   Send to nick@example.com · 7.4 MB    Send    Cancel
// then "Sending to Nick… 45%" while it goes. A refusal is the error line, with "Open Settings → Studio"
// under it when the words send the coach there (a login, password or server to fix).
//
// THE STATE WORDS are plain text in ink3 — never a badge, never colour alone. While this session's
// report is being made they read "Making report…" with the page count.
//
// THE DEVICE TAGS are the carousel drawer's (PpDeviceTags): what recorded the session, the ones the list's
// "Recorded with" filter asked for in the accent.
//
// The row only says what it was given and emits what was pressed: the list owns the sheet and the
// hand-off, so the two pages that show rows share one of each.

import QtQuick
import PinPointStudio

Item {
    id: row

    // ── The session (the list model's roles) ─────────────────────────────────
    property string dir:          ""
    property string dateLabel:    ""
    property int    sessionOfDay: 0
    property string clubs:        ""
    property int    swings:       0
    property var    devices:      []        // labels, for the tags
    property var    deviceTokens: []        // their tokens, same order
    property var    selectedDevices: []     // the list's device filter
    property string workOn1:      ""
    property bool   workOnKnown:  false
    property bool   isLive:       false
    property bool   canReport:    false
    property string reportState:  ""        // "none" | "current" | "stale"; "" until asked
    property string stateWords:   ""
    property string pdfPath:      ""
    property real   sizeBytes:    0

    // ── The golfer (the list's, one lookup for every row) ────────────────────
    property string greeting: ""
    property string email:    ""

    // ── The report being made, if it is this one ─────────────────────────────
    property bool   making:        false
    property int    progressPage:  0
    property int    progressPages: 0
    // A hand-off that failed, in plain words, shown under the links until the next press.
    property string error: ""

    // ── Sending it from the app (the list's) ─────────────────────────────────
    property bool   confirming:   false     // Email pressed, sending set up: the confirm line shows
    property string confirmTo:    ""        // every address, joined with ", "
    property bool   sendBusy:     false     // a report is going from some row: Email waits
    property bool   sendingHere:  false     // …and it is this one
    property real   sendProgress: 0         // 0..1 of the upload

    signal openSessionRequested()
    signal openReportRequested()          // the current PDF
    signal openOldReportRequested()       // the out-of-date one, still there
    signal makeReportRequested(bool thenEmail)
    signal emailRequested()
    signal golferDetailsRequested()
    signal copyAddressRequested()
    signal revealRequested()
    signal sendConfirmed()
    signal sendCancelled()
    signal studioSettingsRequested()

    readonly property bool _reportable: row.canReport && !row.isLive
    readonly property bool _hasPdf:     row.pdfPath !== "" && (row.reportState === "current" || row.reportState === "stale")
    readonly property string _who:      row.greeting !== "" ? row.greeting : qsTr("the golfer")
    readonly property string _revealWords: Qt.platform.os === "osx"     ? qsTr("Show in Finder")
                                         : Qt.platform.os === "windows" ? qsTr("Show in Explorer")
                                         :                                qsTr("Show folder")
    // The refusal sends the coach to the set-up (a login, password or server): offer the way there.
    readonly property bool _errorNeedsSettings: row.error.indexOf(qsTr("Settings → Studio")) >= 0

    function sizeWords(b) {
        if (!(b > 0)) return ""
        if (b >= 1024 * 1024) return (b / (1024 * 1024)).toFixed(1) + " MB"
        return Math.max(1, Math.round(b / 1024)) + " KB"
    }

    implicitHeight: body.implicitHeight + Theme.gap(14) * 2
    width: parent ? parent.width : implicitWidth

    // The drawer's hairline between rows.
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color:  Theme.colorBorder
    }

    Column {
        id: body
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: Theme.gap(5)

        // ── Line 1: when — what ─────────────────────────────────────────────
        Item {
            width:  parent.width
            height: Math.max(when.implicitHeight, what.implicitHeight)

            Row {
                id: whenRow
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                spacing: Theme.gap(7)
                Rectangle {     // the drawer's live dot
                    visible: row.isLive
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.sp(7); height: Theme.sp(7); radius: Theme.sp(3.5)
                    color: Theme.colorError
                }
                Text {
                    id: when
                    objectName: "sessionRowWhen"
                    text: row.dateLabel
                          + (row.sessionOfDay > 0 ? " · " + qsTr("Session %1").arg(row.sessionOfDay) : "")
                          + (row.isLive ? qsTr(" · live") : "")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                    color: Theme.colorText
                }
            }
            Text {
                id: what
                objectName: "sessionRowWhat"
                anchors { right: tags.left; rightMargin: tags.visible ? Theme.gap(8) : 0
                          verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, parent.width - whenRow.width - tags.width - Theme.gap(28))
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                text: {
                    const parts = []
                    if (row.clubs !== "") parts.push(row.clubs)
                    parts.push(row.swings === 1 ? qsTr("1 swing") : qsTr("%1 swings").arg(row.swings))
                    return parts.join(" · ")
                }
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }
            PpDeviceTags {
                id: tags
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                width: visible ? implicitWidth : 0
                tokens:   row.deviceTokens
                labels:   row.devices
                selected: row.selectedDevices
            }
        }

        // ── Line 2: work-on 1 — the report's state ──────────────────────────
        Item {
            visible: workOn.text !== "" || stateText.text !== ""
            width:   parent.width
            height:  visible ? Math.max(workOn.implicitHeight, stateText.implicitHeight) : 0

            Text {
                id: workOn
                objectName: "sessionRowWorkOn"
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: Math.min(implicitWidth, parent.width - stateText.implicitWidth - Theme.gap(20))
                elide: Text.ElideRight
                // Said only from a record under the current rule; a record being re-derived says nothing
                // rather than quote a work-on the new rule may not pick. "No habits" only where there were
                // swings enough for one to show: one or two swings cannot make a habit, so say nothing.
                text: !row.workOnKnown ? ""
                      : row.workOn1 !== "" ? qsTr("Work-on 1: %1").arg(row.workOn1)
                      : row.swings >= 3 ? qsTr("No habits to work on") : ""
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText2
            }
            Text {
                id: stateText
                objectName: "sessionRowState"
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: row.making
                      ? (row.progressPages > 0 ? qsTr("Making report… page %1 of %2").arg(row.progressPage).arg(row.progressPages)
                                               : qsTr("Making report…"))
                      : row._reportable ? row.stateWords : ""
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
        }

        // ── Line 3: the links ───────────────────────────────────────────────
        Flow {
            width:   parent.width
            spacing: Theme.gap(18)

            PpLink {
                objectName: "linkOpenSession"
                text: qsTr("Open session")
                pressMargin: Theme.sp(3)
                onClicked: row.openSessionRequested()
            }

            // Report / Make report / Remake report, the size beside a PDF that is there.
            Row {
                visible: row._reportable
                spacing: Theme.gap(6)
                PpLink {
                    id: reportLink
                    objectName: "linkReport"
                    enabled: !row.making
                    text: row.reportState === "current" ? qsTr("Report")
                        : row.reportState === "stale"   ? qsTr("Remake report")
                        :                                 qsTr("Make report")
                    color: row.making ? Theme.colorText3 : (hovered ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent)
                    interactive: !row.making
                    pressMargin: Theme.sp(3)
                    onClicked: {
                        if (row.reportState === "current") row.openReportRequested()
                        else                               row.makeReportRequested(false)
                    }
                }
                Text {
                    visible: row._hasPdf && row.sizeBytes > 0
                    anchors.baseline: reportLink.baseline
                    text: "· " + row.sizeWords(row.sizeBytes)
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText3
                }
                // Remake a CURRENT report too (Mark, 22 Oct 2026: "I'd like to always have the option").
                // A report goes stale only when the session's own data changes; a change to the model,
                // its norms or its words leaves it "current" while it says something the app no longer
                // would. Quiet, like "open the old one": the PDF is still the main link.
                Text {
                    visible: remakeLink.visible
                    anchors.baseline: reportLink.baseline
                    text: "·"
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText3
                }
                PpLink {
                    id: remakeLink
                    objectName: "linkRemakeReport"
                    visible: row.reportState === "current" && !row.making
                    anchors.baseline: reportLink.baseline
                    text: qsTr("Remake")
                    color: hovered ? Theme.colorText2 : Theme.colorText3
                    pressMargin: Theme.sp(3)
                    onClicked: row.makeReportRequested(false)
                }
                PpLink {
                    objectName: "linkOpenOldReport"
                    visible: row.reportState === "stale" && row.pdfPath !== ""
                    anchors.baseline: reportLink.baseline
                    text: qsTr("open the old one")
                    color: hovered ? Theme.colorText2 : Theme.colorText3
                    pressMargin: Theme.sp(3)
                    onClicked: row.openOldReportRequested()
                }
            }

            // Email to Nick — or why not, with the fix as a link.
            Row {
                visible: row._reportable
                spacing: Theme.gap(6)
                readonly property bool active: row._hasPdf && row.email !== "" && !row.making && !row.sendBusy
                PpLink {
                    id: emailLink
                    objectName: "linkEmail"
                    text: qsTr("Email to %1").arg(row._who)
                    interactive: parent.active
                    color: parent.active ? (hovered ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent)
                                         : Theme.colorText3
                    pressMargin: Theme.sp(3)
                    onClicked: row.emailRequested()
                }
                PpLink {
                    objectName: "linkEmailReason"
                    visible: !parent.active && !row.making && !row.sendBusy
                    anchors.baseline: emailLink.baseline
                    // The address first: making a PDF that cannot then be sent is a step the coach did not ask for.
                    text: row.email === "" ? qsTr("— add %1's email").arg(row._who)
                                           : qsTr("— make the report first")
                    pressMargin: Theme.sp(3)
                    onClicked: {
                        if (row.email === "") row.golferDetailsRequested()
                        else                  row.makeReportRequested(true)
                    }
                }
            }

            // Option C, quiet: the address, and the file where it lives.
            PpLink {
                objectName: "linkCopyAddress"
                visible: row._reportable && row.email !== ""
                text: qsTr("Copy address")
                color: hovered ? Theme.colorText2 : Theme.colorText3
                pressMargin: Theme.sp(3)
                onClicked: row.copyAddressRequested()
            }
            PpLink {
                objectName: "linkReveal"
                visible: row._reportable && row._hasPdf
                text: row._revealWords
                color: hovered ? Theme.colorText2 : Theme.colorText3
                pressMargin: Theme.sp(3)
                onClicked: row.revealRequested()
            }
        }

        // ── The confirm, in place, then the send going ──────────────────────
        Row {
            objectName: "sessionRowConfirm"
            visible: row.confirming && !row.sendingHere
            spacing: Theme.gap(18)
            Text {
                id: confirmWords
                objectName: "sessionRowConfirmWords"
                width: Math.min(implicitWidth, body.width - sendLink.implicitWidth - cancelLink.implicitWidth - Theme.gap(18) * 2)
                elide: Text.ElideMiddle
                text: qsTr("Send to %1").arg(row.confirmTo)
                      + (row.sizeBytes > 0 ? " · " + row.sizeWords(row.sizeBytes) : "")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText
            }
            PpLink {
                id: sendLink
                objectName: "linkSend"
                anchors.baseline: confirmWords.baseline
                text: qsTr("Send")
                interactive: !row.sendBusy
                pressMargin: Theme.sp(3)
                onClicked: row.sendConfirmed()
            }
            PpLink {
                id: cancelLink
                objectName: "linkSendCancel"
                anchors.baseline: confirmWords.baseline
                text: qsTr("Cancel")
                color: hovered ? Theme.colorText2 : Theme.colorText3
                pressMargin: Theme.sp(3)
                onClicked: row.sendCancelled()
            }
        }
        Text {
            objectName: "sessionRowSending"
            visible: row.sendingHere
            text: qsTr("Sending to %1… %2%").arg(row._who).arg(Math.round(row.sendProgress * 100))
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            font.weight: Theme.fontBodyWeight
            color: Theme.colorText2
        }

        Text {
            objectName: "sessionRowError"
            visible: row.error !== ""
            width:   parent.width
            text:    row.error
            wrapMode: Text.WordWrap
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorWarn
        }
        PpLink {
            objectName: "linkErrorSettings"
            visible: row.error !== "" && row._errorNeedsSettings
            text: qsTr("Open Settings → Studio")
            pressMargin: Theme.sp(3)
            onClicked: row.studioSettingsRequested()
        }
    }
}
