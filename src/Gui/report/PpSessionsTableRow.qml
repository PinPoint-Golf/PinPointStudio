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

// ONE SESSION, ONE TABLE ROW (docs/design/sessions_screen_redesign.md §2.1, §8) — a row of
// PpSessionReportList on the rail's Sessions screen, on its PpCard's column template:
//
//   Date [Review]   Club   Swings   Recorded with   Work-on…   Report · action   ⋯
//
//   Date        the day in the body face, emphasised; "· 2" for a second session that day. The Review
//               chip (PpRowAction) rides at the cell's right edge on hover, focus or previewHover:
//               "Review" (Enter), "Live" on the running session, "Restore and review" on an
//               archived one (opening it restores it first, main.cpp archivedSessionOpened)
//   Work-on     work-on 1's goal, elided; "No habits to work on" with three swings or more; "—"
//   Report      the short state ("No report" muted, "Out of date" in the attention colour, "Ready",
//               "Sent 21 Oct") and its ONE next action: Make / Remake / Email. The full words
//               (stateWords: "Sent 21 Oct 18:40", "Opened in Mail…") are the cell's tooltip. No action
//               on a session that cannot report (under three swings, or the live one). An archived row
//               is dimmed and reads "Archived". Making, the send confirm, sending and a refusal each
//               REPLACE the cell in place, as does a bulk job's state for the row (jobWords: "Archive
//               queued", "Archiving… 40%", or why it failed, in the warn colour, until the next job);
//               the rest is on the ⋯ menu (PpSessionRowMenu).
//   ⋯           the row menu, which the list opens
//
// SELECTED: a 3-px accent bar on the left edge over a colorAccentLight wash, which hover deepens.
// The row only draws and reports: a press anywhere but a link, the chip or ⋯ goes to the list
// (rowPressed with its modifiers), which owns the selection (S-D1).

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

PpTableRow {
    id: row

    // ── The session (the list model's roles) ─────────────────────────────────
    property string dir:          ""
    property string dateLabel:    ""
    property int    sessionOfDay: 0
    property string clubs:        ""
    property int    swings:       0
    property var    devices:      []
    property var    deviceTokens: []
    property var    selectedDevices: []
    property string workOn1:      ""
    property bool   workOnKnown:  false
    property bool   isLive:       false
    property bool   archived:     false
    property bool   canReport:    false
    property string reportState:  ""        // "none" | "current" | "stale"; "" until asked
    property string stateShort:   ""
    property string stateWords:   ""
    property string pdfPath:      ""
    property real   sizeBytes:    0         // the PDF

    // ── The golfer (the list's) ──────────────────────────────────────────────
    property string greeting: ""
    property string email:    ""

    // ── The report being made, sent, or refused (the list's) ─────────────────
    property bool   making:        false
    property int    progressPage:  0
    property int    progressPages: 0
    property string error:         ""
    property bool   confirming:    false
    property string confirmTo:     ""
    property bool   sendBusy:      false
    property bool   sendingHere:   false
    property real   sendProgress:  0
    // A bulk job's words for this row (PpSessionBulkJobs via the list); "" when no job has it.
    property string jobWords:      ""
    property bool   jobFailed:     false

    // ── Selection and focus (the list's) ─────────────────────────────────────
    property bool selected:     false
    property bool keyFocus:     false      // the list's keyboard row
    property bool menuOpen:     false      // its ⋯ menu is up: hold the hover
    // Draws the row as if the pointer were on it (the render probe).
    property bool previewHover: false

    signal rowPressed(int modifiers)
    signal rowDoubleClicked(int modifiers, point scenePos)
    signal reviewRequested()
    signal menuRequested()
    signal openReportRequested()
    signal makeReportRequested(bool thenEmail)
    signal emailRequested()
    signal sendConfirmed()
    signal sendCancelled()
    signal studioSettingsRequested()

    // The ⋯ cell, for the list to open the menu under.
    readonly property Item moreItem: more
    readonly property bool shownHover: row.hovered || row.previewHover || row.menuOpen
    readonly property bool _reportable: row.canReport && !row.isLive && !row.archived
    readonly property bool _hasPdf: row.pdfPath !== "" && (row.reportState === "current" || row.reportState === "stale")
    readonly property string _who: row.greeting !== "" ? row.greeting : qsTr("the golfer")
    readonly property bool _errorNeedsSettings: row.error.indexOf(qsTr("Settings → Studio")) >= 0

    function sizeWords(b) {
        if (!(b > 0)) return ""
        if (b >= 1024 * 1024) return (b / (1024 * 1024)).toFixed(1) + " MB"
        return Math.max(1, Math.round(b / 1024)) + " KB"
    }

    // The wash: the selection's accent, deepened under the pointer; else PpTableRow's own hover.
    highlighted: row.selected || row.previewHover || row.menuOpen || row.keyFocus
    hoverColor: row.selected
                ? (row.shownHover ? Qt.rgba(Theme.colorAccent.r, Theme.colorAccent.g, Theme.colorAccent.b,
                                            Math.min(1, Theme.colorAccentLight.a * 1.8))
                                  : Theme.colorAccentLight)
                : Theme.colorBg3

    // ── Date ─────────────────────────────────────────────────────────────────
    Item {
        objectName: "sessionsCellDate"
        width:  row.columnWidth(0)
        height: row.height
        opacity: row.archived ? 0.55 : 1
        Row {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            spacing: Theme.gap(7)
            Rectangle {     // the drawer's live dot
                visible: row.isLive
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.sp(7); height: Theme.sp(7); radius: Theme.sp(3.5)
                color: Theme.colorError
            }
            Text {
                objectName: "sessionsDate"
                width: Math.min(implicitWidth, row.columnWidth(0) - (review.visible ? review.width + Theme.gap(8) : 0)
                                               - (row.isLive ? Theme.sp(14) : 0))
                elide: Text.ElideRight
                text: row.dateLabel + (row.sessionOfDay >= 2 ? " · " + row.sessionOfDay : "")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                font.weight: Font.DemiBold
                color: Theme.colorText
            }
        }
        PpRowAction {
            id: review
            objectName: "sessionsReview"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: row.shownHover || row.keyFocus
            text:    row.isLive ? qsTr("Live") : row.archived ? qsTr("Restore and review") : qsTr("Review")
            tooltip: row.isLive   ? qsTr("Go to the running session in Live · Enter")
                   : row.archived ? qsTr("Restores the archived session first, then opens it in Live · Enter")
                   :                qsTr("Review in Live · Enter")
            onClicked: row.reviewRequested()
        }
    }

    // ── Club ─────────────────────────────────────────────────────────────────
    Text {
        objectName: "sessionsClub"
        width: row.columnWidth(1)
        elide: Text.ElideRight
        text: row.clubs !== "" ? row.clubs : "—"
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        font.weight: Theme.fontBodyWeight
        color: row.clubs !== "" ? Theme.colorText : Theme.colorText3
        opacity: row.archived ? 0.55 : 1
    }

    // ── Swings ───────────────────────────────────────────────────────────────
    Text {
        objectName: "sessionsSwings"
        width: row.columnWidth(2)
        horizontalAlignment: Text.AlignRight
        text: row.swings
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
        color: Theme.colorText
        opacity: row.archived ? 0.55 : 1
    }

    // ── Recorded with ────────────────────────────────────────────────────────
    Item {
        width:  row.columnWidth(3)
        height: tags.height
        clip:   tags.implicitWidth > width
        opacity: row.archived ? 0.55 : 1
        PpDeviceTags {
            id: tags
            tokens:   row.deviceTokens
            labels:   row.devices
            selected: row.selectedDevices
        }
    }

    // ── Work-on ──────────────────────────────────────────────────────────────
    Text {
        objectName: "sessionsWorkOn"
        width: row.columnWidth(4)
        elide: Text.ElideRight
        // Said only from a record under the current rule; "No habits" only where there were swings
        // enough for one to show.
        readonly property bool said: row.workOnKnown && (row.workOn1 !== "" || row.swings >= 3)
        text: !said ? "—" : row.workOn1 !== "" ? row.workOn1 : qsTr("No habits to work on")
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        font.weight: Theme.fontBodyWeight
        color: said ? Theme.colorText2 : Theme.colorText3
        opacity: row.archived ? 0.55 : 1
    }

    // ── Report: the state and its one action, or what is happening to it ────
    Item {
        id: reportCell
        objectName: "sessionsCellReport"
        width:  row.columnWidth(5)
        height: row.height

        // Which line the cell is showing.
        readonly property string mode: row.making        ? "making"
                                     : row.jobWords !== "" ? "job"
                                     : row.archived      ? "archived"
                                     : row.sendingHere   ? "sending"
                                     : row.confirming    ? "confirm"
                                     : row.error !== ""  ? "error"
                                     :                     "state"

        // The plain state and its action.
        Row {
            objectName: "sessionsReportState"
            visible: reportCell.mode === "state" || reportCell.mode === "archived"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            spacing: Theme.gap(10)
            Text {
                id: stateText
                objectName: "sessionsStateShort"
                width: Math.min(implicitWidth, reportCell.width - (actionLink.visible ? actionLink.implicitWidth + Theme.gap(10) : 0))
                elide: Text.ElideRight
                text: reportCell.mode === "archived" ? qsTr("Archived")
                      : row.stateShort + (row.reportState === "current" && row.sizeBytes > 0
                                          ? " · " + row.sizeWords(row.sizeBytes) : "")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: reportCell.mode === "archived"     ? Theme.colorText3
                     : row.reportState === "stale"        ? Theme.colorAttention
                     : row.reportState === "current"      ? Theme.colorText
                     :                                      Theme.colorText3
                HoverHandler { id: stateHover }
                ToolTip.visible: stateHover.hovered && row.stateWords !== "" && reportCell.mode === "state"
                ToolTip.text:    row.stateWords
                ToolTip.delay:   400
            }
            PpLink {
                id: actionLink
                objectName: "sessionsReportAction"
                visible: reportCell.mode === "state" && row._reportable && row.reportState !== ""
                anchors.baseline: stateText.baseline
                // Email waits while any report is going, and needs an address (the ⋯ menu says so).
                readonly property bool emailOk: row.email !== "" && !row.sendBusy
                text: row.reportState === "current" ? qsTr("Email")
                    : row.reportState === "stale"   ? qsTr("Remake")
                    :                                 qsTr("Make")
                interactive: row.reportState !== "current" || emailOk
                color: !interactive ? Theme.colorText3
                     : hovered ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent
                pressMargin: Theme.sp(3)
                ToolTip.visible: !interactive && emailHint.hovered
                ToolTip.text: row.email === "" ? qsTr("Add %1's email first: ⋯ → Email").arg(row._who)
                                               : qsTr("Another report is being sent")
                ToolTip.delay: 400
                HoverHandler { id: emailHint; enabled: !actionLink.interactive }
                onClicked: {
                    if (row.reportState === "current") row.emailRequested()
                    else                               row.makeReportRequested(false)
                }
            }
        }

        // Making it.
        Text {
            objectName: "sessionsMaking"
            visible: reportCell.mode === "making"
            anchors.verticalCenter: parent.verticalCenter
            width: reportCell.width
            elide: Text.ElideRight
            text: row.progressPages > 0 ? qsTr("Making report… page %1 of %2").arg(row.progressPage).arg(row.progressPages)
                                        : qsTr("Making report…")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorText3
        }

        // A bulk job has it: queued, working, or why it failed (the whole of it in the tooltip).
        Text {
            id: jobText
            objectName: "sessionsJob"
            visible: reportCell.mode === "job"
            anchors.verticalCenter: parent.verticalCenter
            width: reportCell.width
            elide: Text.ElideRight
            text: row.jobWords
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            font.weight: Theme.fontBodyWeight
            color: row.jobFailed ? Theme.colorWarn : Theme.colorText2
            HoverHandler { id: jobHover }
            ToolTip.visible: jobHover.hovered && (row.jobFailed || jobText.truncated)
            ToolTip.text:    row.jobWords
            ToolTip.delay:   400
        }

        // The confirm, in place: Send to nick@… · 7.4 MB  Send  Cancel
        Row {
            objectName: "sessionsConfirm"
            visible: reportCell.mode === "confirm"
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gap(10)
            Text {
                id: confirmWords
                objectName: "sessionsConfirmWords"
                width: Math.min(implicitWidth, reportCell.width - sendLink.implicitWidth - cancelLink.implicitWidth - Theme.gap(10) * 2)
                elide: Text.ElideMiddle
                text: qsTr("Send to %1").arg(row.confirmTo)
                      + (row.sizeBytes > 0 ? " · " + row.sizeWords(row.sizeBytes) : "")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText
                HoverHandler { id: confirmHover }
                ToolTip.visible: confirmHover.hovered && confirmWords.truncated
                ToolTip.text: confirmWords.text
                ToolTip.delay: 400
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

        // Sending it.
        Text {
            objectName: "sessionsSending"
            visible: reportCell.mode === "sending"
            anchors.verticalCenter: parent.verticalCenter
            width: reportCell.width
            elide: Text.ElideRight
            text: qsTr("Sending to %1… %2%").arg(row._who).arg(Math.round(row.sendProgress * 100))
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            font.weight: Theme.fontBodyWeight
            color: Theme.colorText2
        }

        // Refused: the reason (all of it in the tooltip), and the way to the fix — Settings → Studio
        // when the words send the coach there, else Email again.
        Row {
            objectName: "sessionsError"
            visible: reportCell.mode === "error"
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gap(10)
            Text {
                id: errorWords
                objectName: "sessionsErrorWords"
                width: Math.min(implicitWidth, reportCell.width - errorLink.implicitWidth - Theme.gap(10))
                elide: Text.ElideRight
                text: row.error
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorWarn
                HoverHandler { id: errorHover }
                ToolTip.visible: errorHover.hovered
                ToolTip.text: row.error
                ToolTip.delay: 400
            }
            PpLink {
                id: errorLink
                objectName: row._errorNeedsSettings ? "linkErrorSettings" : "linkErrorRetry"
                anchors.baseline: errorWords.baseline
                text: row._errorNeedsSettings ? qsTr("Settings") : qsTr("Email")
                interactive: row._errorNeedsSettings || (row._hasPdf && row.email !== "" && !row.sendBusy)
                color: !interactive ? Theme.colorText3
                     : hovered ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent
                pressMargin: Theme.sp(3)
                onClicked: row._errorNeedsSettings ? row.studioSettingsRequested() : row.emailRequested()
            }
        }
    }

    // ── ⋯ ────────────────────────────────────────────────────────────────────
    Item {
        id: more
        objectName: "sessionsMore"
        width:  Math.max(Theme.sp(24), row.columnWidth(6))
        height: Theme.sp(24)
        Rectangle {
            anchors.centerIn: parent
            width: Theme.sp(24); height: Theme.sp(24)
            radius: Theme.radius
            color: row.menuOpen || morePress.containsMouse ? Theme.colorBg2 : "transparent"
            Text {
                anchors.centerIn: parent
                text: "⋯"
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody
                color: row.menuOpen || morePress.containsMouse ? Theme.colorText : Theme.colorText3
            }
            PpPressable {
                id: morePress
                held: row.menuOpen
                onClicked: row.menuRequested()
            }
        }
    }

    // ── Not cells (beyond the template, so left where they are put) ──────────
    // The selection's bar on the left edge.
    Rectangle {
        visible: row.selected
        x: -row.padH; y: 0
        width: 3; height: row.height
        color: Theme.colorAccent
    }
    // The row's own press: under every cell (z -1), so a link, the chip and ⋯ take theirs first.
    MouseArea {
        objectName: "sessionsRowPress"
        z: -1
        x: -row.padH; y: 0
        width: row.width; height: row.height
        acceptedButtons: Qt.LeftButton
        onClicked: (mouse) => row.rowPressed(mouse.modifiers)
        onDoubleClicked: (mouse) => row.rowDoubleClicked(mouse.modifiers, mapToItem(null, mouse.x, mouse.y))
    }
}
