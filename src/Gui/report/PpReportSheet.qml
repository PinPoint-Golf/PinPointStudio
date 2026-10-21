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

// THE REPORT SHEET (docs/design/session_report_design.md §8.1) — Make report / Remake report open it. An
// in-app sheet over the window, in the popover card's family; no native dialog.
//
//   the coach's note     optional, prefilled from this session's last report, about 80 words at most —
//                        it sits on the cover, which has room for that and no more
//   the photo line       whether swing photos go in, and the reason when they do not ("Nick is under 18 —
//                        no parent or guardian approval recorded"), linked to the golfer's page
//   missing details      each as a link to where it is entered ("No studio name yet — add it in
//                        Settings → Studio")
//   Make the PDF         sessionReport.make(dir, {note}); then "Page 4 of 10" while it prints, and on
//                        made() either close (the row shows the new state) or the error, in place
//
// The technical appendix is always included (Mark, 20 October), so there is no option for it; the sheet
// says so in one quiet line. Everything said here comes from sessionReport.preflight(dir) — the sheet
// makes no judgement of its own about what the report can hold.

import QtQuick
import QtQuick.Controls
import PinPointStudio

Popup {
    id: sheet
    objectName: "reportSheet"

    property var    reports:  null
    property string greeting: ""

    // The session it is open on, and its row as the list had it.
    property string sessionDir: ""
    property var    sessionRow: ({})
    // Email follows the make (the row's "make the report first").
    property bool   thenEmail:  false

    property var    preflight: ({})
    property string error:     ""
    // Make pressed: waiting for the model to go busy, or for made().
    property bool   requested: false

    signal studioSettingsRequested()
    signal golferDetailsRequested()
    signal makeStarted(string dir, bool thenEmail)

    readonly property int maxWords: 80
    readonly property int words: {
        const t = noteArea.text.trim()
        return t === "" ? 0 : t.split(/\s+/).length
    }
    readonly property bool busyHere: sheet.reports !== null && sheet.reports.busySession === sheet.sessionDir
                                     && sheet.sessionDir !== ""
    readonly property bool working: sheet.requested || sheet.busyHere
    readonly property bool canReport: sheet.preflight.canReport === undefined ? true : !!sheet.preflight.canReport
    readonly property var  photos:   sheet.preflight.photos || ({})
    readonly property var  missing:  sheet.preflight.missing || []
    readonly property var  reportState: (sheet.reports && sheet.sessionDir !== "") ? (sheet._stateRev >= 0 ? sheet.reports.state(sheet.sessionDir) : ({})) : ({})
    property int _stateRev: 0

    function openFor(dir, row, thenEmail) {
        sheet.sessionDir = dir || ""
        sheet.sessionRow = row || ({})
        sheet.thenEmail  = !!thenEmail
        sheet.error      = ""
        sheet.requested  = false
        sheet.preflight  = sheet.reports ? (sheet.reports.preflight(dir) || ({})) : ({})
        noteArea.text    = sheet.preflight.lastNote || ""
        sheet._stateRev++
        visible = true
    }
    function make() {
        if (!sheet.reports || sheet.working) return
        sheet.error = ""
        sheet.requested = true
        sheet.makeStarted(sheet.sessionDir, sheet.thenEmail)
        sheet.reports.make(sheet.sessionDir, { note: noteArea.text.trim() })
    }

    Connections {
        target: sheet.reports
        ignoreUnknownSignals: true
        function onMade(dir, ok, path, error) {
            if (dir !== sheet.sessionDir) return
            sheet.requested = false
            if (ok) sheet.close()
            else    sheet.error = error || qsTr("The report could not be made.")
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    // While the PDF is being made the sheet stays: it is where the progress is.
    closePolicy: sheet.working ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    padding: Theme.gap(22)
    width: Math.min(Theme.sp(520), (parent ? parent.width : Theme.sp(520)) - Theme.sp(48))

    background: PpPopoverCard {}

    contentItem: Column {
        spacing: Theme.gap(12)

        // ── What it is a report of ──────────────────────────────────────────
        Column {
            width: parent.width
            spacing: Theme.gap(4)
            PpMicro { text: Theme.caps(sheet.reportState.state === "stale" || sheet.reportState.state === "current"
                                       ? qsTr("Remake report") : qsTr("Make report")) }
            Text {
                objectName: "reportSheetSession"
                width: parent.width
                text: (sheet.sessionRow.dateLabel || "")
                      + (sheet.sessionRow.sessionOfDay > 0 ? " · " + qsTr("Session %1").arg(sheet.sessionRow.sessionOfDay) : "")
                      + (sheet.sessionRow.swings > 0 ? " · " + qsTr("%1 swings").arg(sheet.sessionRow.swings) : "")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzHeading
                color: Theme.colorText
                elide: Text.ElideRight
            }
        }

        // ── The coach's note ────────────────────────────────────────────────
        Column {
            width: parent.width
            spacing: Theme.gap(5)
            Item {
                width: parent.width
                height: noteLabel.implicitHeight
                Text {
                    id: noteLabel
                    text: sheet.greeting !== "" ? qsTr("Your note for %1 (optional)").arg(sheet.greeting)
                                                : qsTr("Your note (optional)")
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                    color: Theme.colorText2
                }
                Text {
                    objectName: "reportSheetWordCount"
                    anchors.right: parent.right
                    text: sheet.words > sheet.maxWords
                          ? qsTr("%1 words — about %2 fit on the cover").arg(sheet.words).arg(sheet.maxWords)
                          : qsTr("%1 of %2 words").arg(sheet.words).arg(sheet.maxWords)
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                    color: sheet.words > sheet.maxWords ? Theme.colorWarn : Theme.colorText3
                }
            }
            TextArea {
                id: noteArea
                objectName: "reportSheetNote"
                width: parent.width
                enabled: !sheet.working
                placeholderText: qsTr("A line or two in your own words. It goes on the cover.")
                placeholderTextColor: Theme.colorText3
                wrapMode: TextArea.Wrap
                color: Theme.colorText
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                padding: Theme.gap(10)
                implicitHeight: Math.max(Theme.sp(84), contentHeight + Theme.sp(20))
                background: Rectangle {
                    color: Theme.colorSurface
                    radius: Theme.radius
                    border.width: 1
                    border.color: noteArea.activeFocus ? Theme.colorAccent : Theme.colorBorderMid
                }
            }
        }

        // ── Photos ──────────────────────────────────────────────────────────
        Flow {
            width: parent.width
            spacing: Theme.gap(6)
            visible: sheet.photos.included !== undefined
            Text {
                objectName: "reportSheetPhotos"
                width: Math.min(implicitWidth, parent.width)
                wrapMode: Text.WordWrap
                text: sheet.photos.included ? qsTr("Swing photos are included.")
                      : (sheet.photos.reason ? qsTr("No swing photos: %1.").arg(String(sheet.photos.reason).replace(/\.$/, ""))
                                             : qsTr("No swing photos."))
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText2
            }
            PpLink {
                objectName: "reportSheetPhotosLink"
                text: sheet.greeting !== "" ? qsTr("%1's details").arg(sheet.greeting) : qsTr("The golfer's details")
                pressMargin: Theme.sp(3)
                onClicked: { sheet.close(); sheet.golferDetailsRequested() }
            }
        }

        // ── Missing details, each a link to where it is entered ─────────────
        Repeater {
            model: sheet.missing
            PpLink {
                required property var modelData
                required property int index
                objectName: "reportSheetMissing_" + index
                width: Math.min(implicitWidth, sheet.availableWidth)
                wrapMode: Text.WordWrap
                text: modelData.text || ""
                onClicked: {
                    sheet.close()
                    if (modelData.where === "studio") sheet.studioSettingsRequested()
                    else                              sheet.golferDetailsRequested()
                }
            }
        }

        // After a send, a remake is a different PDF from the one the golfer has (§8.3).
        Text {
            objectName: "reportSheetSent"
            visible: !!(sheet.reportState.lastSend && sheet.reportState.lastSend.ms > 0
                        && sheet.reportState.lastSend.method === "mail-app")
            width: parent.width
            wrapMode: Text.WordWrap
            text: visible ? qsTr("A copy was opened in Mail on %1. The new PDF will differ from the one sent.")
                              .arg(Qt.formatDateTime(new Date(sheet.reportState.lastSend.ms), "d MMM HH:mm")) : ""
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorText2
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("The technical appendix is included, from a fresh page.")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
            color: Theme.colorText3
        }

        Text {
            objectName: "reportSheetCannot"
            visible: !sheet.canReport
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("A report needs at least three swings from a session that has ended.")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorWarn
        }

        Text {
            objectName: "reportSheetError"
            visible: sheet.error !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: sheet.error
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorWarn
        }

        // ── Make the PDF ────────────────────────────────────────────────────
        Item {
            width: parent.width
            height: makeButton.implicitHeight

            Text {
                objectName: "reportSheetProgress"
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                visible: sheet.working
                text: (sheet.busyHere && sheet.reports.progressPages > 0)
                      ? qsTr("Page %1 of %2").arg(sheet.reports.progressPage).arg(sheet.reports.progressPages)
                      : qsTr("Making the report…")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.gap(10)
                PpButton {
                    objectName: "reportSheetCancel"
                    visible: !sheet.working
                    label: qsTr("Cancel")
                    onClicked: sheet.close()
                }
                PpButton {
                    id: makeButton
                    objectName: "reportSheetMake"
                    primary: true
                    enabled: sheet.canReport && !sheet.working && sheet.words <= sheet.maxWords && sheet.reports !== null
                    label: sheet.thenEmail ? qsTr("Make the PDF and email it") : qsTr("Make the PDF")
                    onClicked: sheet.make()
                }
            }
        }
    }
}
