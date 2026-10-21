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

// THE GOLFER'S SESSIONS, EACH WITH ITS REPORT (docs/design/session_report_design.md §8.0) — Home's
// Sessions section (limit 10, then "Show all N sessions") and the golfer page's Sessions tab (limit 0,
// every session). One PpSessionReportRow per session, over SessionReportListModel (`sessionReportList`).
//
// THIS OWNS what the rows only ask for:
//   · the report sheet (PpReportSheet), one for the whole list, opened on the row that asked
//   · the hand-off: the subject and message from Settings → Studio's templates ({first} {date} {coach}
//     {studio}), dated by the SESSION (§8.4) — and "today's session" becomes "your session on 20 October"
//     when it goes on a later day — then reportMailer.compose(), then the send log
//     (sessionReport.recordSend, with the method that really happened: "mail-app", or "dry-run" under
//     PINPOINT_MAIL_DRYRUN so a probe's sends never read "Opened in Mail")
//   · "Email" on a session with no PDF yet: the sheet makes it, and the hand-off follows on made()
// The golfer's greeting and address are read once for every row (athleteController.reportDetails) and
// never stored here; the address goes to the mail app and nowhere else.
//
// Signals bubble to the host screen: Open session, Show all, and the links to where a missing detail is
// entered (Settings → Studio; the golfer's own page).

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "sessionReportList"

    // 0: every session. Home uses 10.
    property int limit: 0
    // Passed in rather than reached for, so a harness can drive it.
    property var model:   (typeof sessionReportList !== "undefined") ? sessionReportList : null
    property var reports: (typeof sessionReport !== "undefined") ? sessionReport : null
    property var mailer:  (typeof reportMailer !== "undefined") ? reportMailer : null
    property var studio:  (typeof studioProfile !== "undefined") ? studioProfile : null
    property var athletes: (typeof athleteController !== "undefined") ? athleteController : null

    signal showAll()
    // A row's Open session. "" for the live session: resume it rather than load it for review.
    signal reviewSessionRequested(string dir)
    signal studioSettingsRequested()
    signal golferDetailsRequested()

    readonly property int total: root.model ? root.model.count : 0

    // ── The golfer's report details, re-read when the athlete or their record changes ──
    property int _detailsRev: 0
    Connections {
        target: root.athletes
        ignoreUnknownSignals: true
        function onCurrentAthleteChanged() { root._detailsRev++ }
        function onAthletesChanged()       { root._detailsRev++ }
    }
    onVisibleChanged: if (visible) root._detailsRev++
    readonly property var details: {
        if (root._detailsRev < 0 || !root.athletes) return ({})
        const uuid = root.athletes.currentUuid || ""
        const d = (uuid !== "" && typeof root.athletes.reportDetails === "function")
                  ? root.athletes.reportDetails(uuid) : null
        return d || ({})
    }
    readonly property string greeting: {
        const g = (root.details.greeting || "").trim()
        if (g !== "") return g
        const name = root.athletes ? (root.athletes.currentName || "") : ""
        return name.trim().split(/\s+/)[0] || ""
    }
    // As entered on the golfer page: one address, or several ("nick@…, parent@…" — a junior's parent).
    readonly property string email: (root.details.email || "").trim()
    readonly property var addresses: root.email.split(/[,;]/).map(a => a.trim()).filter(a => a !== "")

    // Per-row hand-off errors, by session dir, until that row is pressed again.
    property var _errors: ({})
    function _setError(dir, text) {
        const e = Object.assign({}, root._errors)
        if (text) e[dir] = text; else delete e[dir]
        root._errors = e
    }
    // The session whose Email asked for a PDF first: handed off when it is made.
    property string _emailAfterMake: ""

    // ── The hand-off ─────────────────────────────────────────────────────────
    function fill(template, words) {
        return String(template).replace(/\{(first|date|coach|studio)\}/g, (m, k) => words[k] !== undefined ? words[k] : m)
    }
    // {subject, body} for a session, dated by the session; `nowMs` is when it goes.
    function composeWords(sessionMs, nowMs) {
        const when = sessionMs > 0 ? new Date(sessionMs) : new Date(nowMs)
        const now  = new Date(nowMs)
        const words = {
            first:  root.greeting,
            date:   Qt.formatDate(when, "d MMMM yyyy"),
            coach:  root.studio ? (root.studio.coachName || "") : "",
            studio: root.studio ? (root.studio.studioName || "") : ""
        }
        const subjectT = (root.studio && root.studio.emailSubject) ? root.studio.emailSubject
                                                                    : qsTr("Your session report — {date}")
        const bodyT = (root.studio && root.studio.emailBody) ? root.studio.emailBody
                    : qsTr("Hi {first},\n\nHere is your report from today's session. Page 1 has the short version; "
                           + "your first work-on is on page 4.\n\n{coach}\n{studio}")
        let body = root.fill(bodyT, words)
        const sameDay = when.getFullYear() === now.getFullYear() && when.getMonth() === now.getMonth()
                        && when.getDate() === now.getDate()
        if (!sameDay) {
            const on = qsTr("your session on %1").arg(Qt.formatDate(when, "d MMMM"))
            body = body.replace(/today['’]s session/g, on).replace(/Today['’]s session/g, on.charAt(0).toUpperCase() + on.slice(1))
        }
        // A template line left empty by a missing coach or studio name goes, rather than sit blank at the foot.
        body = body.replace(/\n+$/, "")
        return { subject: root.fill(subjectT, words), body: body }
    }
    // Hands one session's PDF to the mail app. The row's own facts are passed in (the PDF, the session's
    // start) so the same call serves a row the model drew and a harness's row alike.
    function sendEmail(dir, pdfPath, sessionMs) {
        root._setError(dir, "")
        if (!root.mailer) { root._setError(dir, qsTr("The mail hand-off is not available in this build.")); return null }
        const w = root.composeWords(sessionMs || 0, Date.now())
        const replyTo = root.studio ? (root.studio.coachEmail || "") : ""
        const res = root.mailer.compose(root.addresses, replyTo, w.subject, w.body, pdfPath)
        if (!res || !res.ok) { root._setError(dir, (res && res.error) || qsTr("The mail app could not be opened.")); return res }
        if (root.reports) root.reports.recordSend(dir, res.method)
        return res
    }
    function openPdf(path) {
        if (path) Qt.openUrlExternally("file://" + (path.startsWith("/") ? "" : "/") + path)
    }
    function openSheet(dir, thenEmail) {
        const r = root.model ? root.model.rowFor(dir) : ({})
        sheet.openFor(dir, r || ({}), thenEmail)
    }

    Connections {
        target: root.reports
        ignoreUnknownSignals: true
        function onMade(dir, ok, path, error) {
            if (dir !== root._emailAfterMake) return
            root._emailAfterMake = ""
            if (!ok) return
            const r = root.model ? root.model.rowFor(dir) : ({})
            root.sendEmail(dir, path, (r && r.sessionMs) || 0)
        }
    }

    spacing: 0
    width: parent ? parent.width : implicitWidth

    // ── Nothing yet ──────────────────────────────────────────────────────────
    Text {
        objectName: "sessionListEmpty"
        visible: root.total === 0
        width:   root.width
        height:  visible ? Theme.sp(40) : 0
        verticalAlignment: Text.AlignVCenter
        text: (root.model && root.model.scanning) ? qsTr("Reading sessions…")
                                                   : qsTr("No sessions yet. Every session you record is listed here.")
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
        font.weight: Theme.fontBodyWeight
        color: Theme.colorText3
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: root.model
        PpSessionReportRow {
            // The model's roles through `model`: one of them is `state`, which as a property of its own
            // would shadow Item.state.
            required property int index
            required property var model

            objectName: "sessionRow_" + index
            visible: root.limit <= 0 || index < root.limit
            height:  visible ? implicitHeight : 0
            width:   root.width

            dir:          model.dir
            dateLabel:    model.dateLabel
            sessionOfDay: model.sessionOfDay
            clubs:        model.clubs
            swings:       model.swings
            devices:      model.devices
            workOn1:      model.workOn1
            workOnKnown:  model.workOnKnown
            isLive:       model.isLive
            canReport:    model.canReport
            reportState:  model.state
            stateWords:   model.stateWords
            pdfPath:      model.pdfPath
            sizeBytes:    model.sizeBytes

            greeting: root.greeting
            email:    root.email
            making:   root.reports ? (root.reports.busySession === model.dir && model.dir !== "") : false
            progressPage:  root.reports ? root.reports.progressPage : 0
            progressPages: root.reports ? root.reports.progressPages : 0
            error: root._errors[model.dir] || ""

            onOpenSessionRequested:   root.reviewSessionRequested(model.isLive ? "" : model.dir)
            onOpenReportRequested:    root.openPdf(model.pdfPath)
            onOpenOldReportRequested: root.openPdf(model.pdfPath)
            onMakeReportRequested: function(thenEmail) { root.openSheet(model.dir, thenEmail) }
            onEmailRequested:         root.sendEmail(model.dir, model.pdfPath, model.sessionMs)
            onGolferDetailsRequested: root.golferDetailsRequested()
            onCopyAddressRequested:   { if (root.mailer) root.mailer.copyText(root.email) }
            onRevealRequested:        { if (root.mailer) root.mailer.reveal(model.pdfPath) }
        }
    }

    // ── Show all N sessions ──────────────────────────────────────────────────
    Item {
        visible: root.limit > 0 && root.total > root.limit
        width:   root.width
        height:  visible ? Theme.sp(40) : 0
        PpLink {
            objectName: "linkShowAllSessions"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: qsTr("Show all %1 sessions").arg(root.total)
            pressMargin: Theme.sp(3)
            onClicked: root.showAll()
        }
    }

    PpReportSheet {
        id: sheet
        reports:  root.reports
        greeting: root.greeting
        onStudioSettingsRequested: root.studioSettingsRequested()
        onGolferDetailsRequested:  root.golferDetailsRequested()
        onMakeStarted: function(dir, thenEmail) { if (thenEmail) root._emailAfterMake = dir }
    }
}
