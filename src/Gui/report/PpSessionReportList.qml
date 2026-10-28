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

// THE GOLFER'S SESSIONS, EACH WITH ITS REPORT (docs/design/session_report_design.md §8.0;
// sessions_screen_redesign.md §2) — the table of the rail's Sessions screen (ScreenSessions): one
// PpSessionsTableRow per session, over SessionReportListModel (`sessionReportList`), in a PpCard table.
//
// THE SELECTION (§2.3) lives here: `selection` holds session dirs, `_anchor` the range anchor.
//   click                 toggles the row and makes it the anchor        toggleRow(dir)
//   shift-click           ADDS every shown row from the anchor to here   extendTo(dir)
//   Ctrl/⌘-click          toggles (as a click)
//   Space                 toggles the focused row;  Up / Down move the focus
//   Ctrl/⌘-A              every row the filters show                     selectAllShown()
//   Esc                   clears                                         clearSelection()
//   double-click / Enter  opens the row in Live and leaves the selection as it was
// A row the filters hide (shownRank < 0) is pruned from the selection, so the screen's count is always
// what can be seen selected. The ⋯ menu (PpSessionRowMenu, one for the list) acts on its row only:
// openRowMenu(i) / closeRowMenu(), i the row's place among those shown. Each row is "sessionsRow_<i>".
//
// SELECTION DIFFERS FROM THE SWING MODEL TABLE ON PURPOSE (S-D1, Mark 28 Oct). In
// diagnosticmodel/ModelTable.qml a plain click replaces the selection and shift-click replaces it with a
// range; here a plain click toggles and shift-click adds, because Sessions is a list acted on in batches
// with no "current item" pane for a click to drive. Do not "fix" one to match the other.
//
// THIS OWNS what the rows only ask for:
//   · the report sheet (PpReportSheet), one for the whole list, opened on the row that asked
//   · the hand-off: the subject and message from Settings → Studio's templates ({first} {date} {coach}
//     {studio}), dated by the SESSION (§8.4) — and "today's session" becomes "your session on 20 October"
//     when it goes on a later day — then reportMailer.compose(), then the send log
//     (sessionReport.recordSend, with the method that really happened: "mail-app", or "dry-run" under
//     PINPOINT_MAIL_DRYRUN so a probe's sends never read "Opened in Mail")
//   · "Email" on a session with no PDF yet: the sheet makes it, and the hand-off follows on made()
//   · SENDING IT ITSELF (§8.3, "Changed (Mark, 21 October)"), once Settings → Studio → Sending reports is
//     set up (reportMailer.sendConfigured): Email opens a one-line confirm under that row's links ("Send to
//     nick@… · 7.4 MB  Send  Cancel" — in place, never a popup); Send hands the same subject and message to
//     reportMailer.send(); the row reads "Sending to Nick… 45%" while it goes, and every row's Email waits;
//     sendFinished records the send (method "smtp", so the row then says "Sent 21 Oct 18:40") or puts the
//     reason in the row's error line, with a link to Settings → Studio when the reason is the set-up. A
//     session with no PDF is made first and then shows the confirm — it is never sent unasked. Until sending
//     is set up, everything is the mail-app hand-off above, unchanged.
// The golfer's greeting and address are read once for every row (athleteController.reportDetails) and
// never stored here; the address goes to the mail app and nowhere else.
//
// THE BULK ACTIONS (§2.4–§2.6) — the selection bar's buttons and the ⋯ menu's Export / Archive / Restore /
// Delete — all come through askBulk(verb, dirs): the menu with its one row, the bar with the selection.
// It raises the confirm (one PpConfirm for the list) or the export sheet (PpExportOptionsSheet over the
// union of the sessions' cameras), then queues the verb on `jobs` (PpSessionBulkJobs), which runs one
// session at a time and puts each row's state in its Report cell. Make needs no confirm; the single Make
// on a row is still the report sheet. The live session is never offered to archive or delete, and the
// queue skips it besides. Archive and Restore need an archive folder (appSettings.archiveLocation). When
// a job ends the outcome is a notice() for the screen's toast — the zips' paths for an export.
//
// Signals bubble to the host screen: Review (open in Live), and the links to where a missing detail is
// entered (Settings → Studio; the golfer's own page).
//
// THE FILTERS are the model's (device, report state, club, archived, search) and their controls are the
// screen's; rows they leave out are hidden in place (shownRank -1).

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

Column {
    id: root
    objectName: "sessionReportList"

    // Passed in rather than reached for, so a harness can drive it.
    property var model:   (typeof sessionReportList !== "undefined") ? sessionReportList : null
    property var reports: (typeof sessionReport !== "undefined") ? sessionReport : null
    property var mailer:  (typeof reportMailer !== "undefined") ? reportMailer : null
    property var studio:  (typeof studioProfile !== "undefined") ? studioProfile : null
    property var athletes: (typeof athleteController !== "undefined") ? athleteController : null
    property var exporter: (typeof swingExporter !== "undefined") ? swingExporter : null
    property var archiver: (typeof archiveController !== "undefined") ? archiveController : null
    property var reviewer: (typeof sessionReviewController !== "undefined") ? sessionReviewController : null
    property var settings: (typeof appSettings !== "undefined") ? appSettings : null

    // Archive and Restore need somewhere to put the videos (Settings → Archiving).
    readonly property bool archiveReady: !!(root.settings && root.settings.archiveLocation)
    // The bulk queue; the rows read its per-dir status.
    property alias jobs: bulk

    // A row's Review. "" for the live session: resume it rather than load it for review.
    signal reviewSessionRequested(string dir)
    signal studioSettingsRequested()
    signal golferDetailsRequested()
    // A job's outcome, for the screen's toast (PpToast: glyph, words, "info" | "warn" | "error", copy text).
    signal notice(string glyph, string message, string severity, string copyText)

    // A harness may hand a plain array of rows (each with its own shownRank) for the model.
    readonly property int total: !root.model ? 0
                               : root.model.count !== undefined ? root.model.count : (root.model.length || 0)
    // What the filters leave; every row when they are off.
    readonly property int shown: !root.model ? 0
                               : root.model.shownCount !== undefined ? root.model.shownCount
                               : Array.isArray(root.model) ? root.model.filter(r => r.shownRank === undefined || r.shownRank >= 0).length
                               : root.total

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

    // Sending it from the app: set up, the session whose confirm is open, and the one this list is sending
    // (a list sends one at a time; any other list leaves its result alone).
    readonly property bool sendReady: !!(root.mailer && root.mailer.sendConfigured)
    readonly property bool sendBusy:  !!(root.mailer && root.mailer.sending)
    property string _confirmDir: ""
    property string _sendingDir: ""

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
    // One email for a session — who it goes to, and the words — as the single send and the bulk send both use.
    function composeFor(dir, sessionMs) {
        const w = root.composeWords(sessionMs || 0, Date.now())
        return { to: root.addresses, replyTo: root.studio ? (root.studio.coachEmail || "") : "",
                 fromName: root.studio ? (root.studio.coachName || root.studio.studioName || "") : "",
                 subject: w.subject, body: w.body }
    }
    // Hands one session's PDF to the mail app. The row's own facts are passed in (the PDF, the session's
    // start) so the same call serves a row the model drew and a harness's row alike.
    function sendEmail(dir, pdfPath, sessionMs) {
        root._setError(dir, "")
        if (!root.mailer) { root._setError(dir, qsTr("The mail hand-off is not available in this build.")); return null }
        const w = root.composeFor(dir, sessionMs)
        const res = root.mailer.compose(w.to, w.replyTo, w.subject, w.body, pdfPath)
        if (!res || !res.ok) { root._setError(dir, (res && res.error) || qsTr("The mail app could not be opened.")); return res }
        if (root.reports) root.reports.recordSend(dir, res.method)
        return res
    }
    // Email pressed: the confirm once sending is set up, else the mail-app hand-off as ever.
    function emailPressed(dir, pdfPath, sessionMs) {
        if (!root.sendReady) return root.sendEmail(dir, pdfPath, sessionMs)
        root._setError(dir, "")
        root._confirmDir = root._confirmDir === dir ? "" : dir
        return null
    }
    // The confirm's Send: the same words the hand-off uses, straight to the coach's mail server.
    function sendReport(dir, pdfPath, sessionMs) {
        root._setError(dir, "")
        root._confirmDir = ""
        if (!root.mailer) { root._setError(dir, qsTr("Sending is not available in this build.")); return null }
        const w = root.composeFor(dir, sessionMs)
        const res = root.mailer.send(dir, w.to, w.replyTo, w.fromName, w.subject, w.body, pdfPath)
        if (!res || !res.ok) { root._setError(dir, (res && res.error) || qsTr("The report could not be sent.")); return res }
        root._sendingDir = dir
        return res
    }
    Connections {
        target: root.mailer
        ignoreUnknownSignals: true
        function onSendFinished(dir, ok, method, error) {
            if (dir === "" || dir !== root._sendingDir) return
            root._sendingDir = ""
            if (ok) { if (root.reports) root.reports.recordSend(dir, method) }
            else root._setError(dir, error || qsTr("The report could not be sent."))
        }
    }
    function openPdf(path) {
        if (path) Qt.openUrlExternally("file://" + (path.startsWith("/") ? "" : "/") + path)
    }
    function openSheet(dir, thenEmail) {
        const r = (root.model && typeof root.model.rowFor === "function") ? root.model.rowFor(dir) : ({})
        sheet.openFor(dir, r || ({}), thenEmail)
    }

    Connections {
        target: root.reports
        ignoreUnknownSignals: true
        function onMade(dir, ok, path, error) {
            if (dir !== root._emailAfterMake) return
            root._emailAfterMake = ""
            if (!ok) return
            // Set up to send: the confirm, never a send the coach did not press.
            if (root.sendReady) { root._setError(dir, ""); root._confirmDir = dir; return }
            const r = (root.model && typeof root.model.rowFor === "function") ? root.model.rowFor(dir) : ({})
            root.sendEmail(dir, path, (r && r.sessionMs) || 0)
        }
    }

    // ── The selection (§2.3, S-D1) ───────────────────────────────────────────
    property var    selection: []          // session dirs
    property string _anchor:   ""          // shift-click's other end
    property int    focusIndex: -1         // the keyboard's row, in shown order
    // What the selection holds, for the screen's bar: { count, bytes, stale, swings, reportable, live,
    // archived }. reportable = can have a report now (canReport, not live, not archived).
    property var    selectionInfo: ({ count: 0, bytes: 0, stale: 0, swings: 0, reportable: 0, live: 0, archived: 0 })
    // Set the first time anything is selected (the screen hides its hint line after).
    signal selectedOnce()

    // The delegates, and those shown in shown order.
    function _items() {
        const out = []
        for (let i = 0; i < rows.count; ++i) { const it = rows.itemAt(i); if (it) out.push(it) }
        return out
    }
    function _shownItems() { return root._items().filter(it => it.rank >= 0).sort((a, b) => a.rank - b.rank) }
    function _itemFor(dir) { return root._items().find(it => it.dir === dir) || null }
    function rowAt(i) { const s = root._shownItems(); return (i >= 0 && i < s.length) ? s[i] : null }

    function _setSelection(next) {
        root.selection = next
        root._recount()
        if (next.length > 0) root.selectedOnce()
    }
    function _recount() {
        let bytes = 0, stale = 0, swings = 0, reportable = 0, live = 0, archived = 0
        for (const d of root.selection) {
            const it = root._itemFor(d)
            if (!it) continue
            bytes += it.sessionBytes || 0
            swings += it.swings || 0
            if (it.reportState === "stale") stale++
            if (it.isLive) live++
            if (it.archived) archived++
            if (it.canReport && !it.isLive && !it.archived) reportable++
        }
        root.selectionInfo = { count: root.selection.length, bytes: bytes, stale: stale, swings: swings,
                               reportable: reportable, live: live, archived: archived }
    }
    // A session's facts, from its row (a harness's rows have no model to ask), else the model.
    function facts(dir) {
        const it = root._itemFor(dir)
        if (it) return { dir: dir, canReport: it.canReport, isLive: it.isLive, archived: it.archived,
                         sessionMs: it.r.sessionMs || 0, state: it.reportState, pdfPath: it.pdfPath,
                         sizeBytes: it.sizeBytes, swings: it.swings, sessionBytes: it.sessionBytes,
                         dateLabel: it.dateLabel, sessionOfDay: it.sessionOfDay }
        const r = (root.model && typeof root.model.rowFor === "function") ? root.model.rowFor(dir) : null
        return r ? Object.assign({}, r, { state: r.state }) : ({})
    }
    function isSelected(dir) { return root.selection.indexOf(dir) >= 0 }
    function toggleRow(dir) {
        if (!dir) return
        const next = root.selection.slice()
        const at = next.indexOf(dir)
        if (at >= 0) next.splice(at, 1); else next.push(dir)
        root._anchor = dir
        root._setSelection(next)
    }
    // Adds every shown row from the anchor to `dir`; with no anchor shown, a toggle.
    function extendTo(dir) {
        const shown = root._shownItems()
        const a = shown.findIndex(it => it.dir === root._anchor)
        const b = shown.findIndex(it => it.dir === dir)
        if (a < 0 || b < 0) { root.toggleRow(dir); return }
        const next = root.selection.slice()
        for (let i = Math.min(a, b); i <= Math.max(a, b); ++i)
            if (next.indexOf(shown[i].dir) < 0) next.push(shown[i].dir)
        root._setSelection(next)
    }
    function selectAllShown() { root._setSelection(root._shownItems().map(it => it.dir).filter(d => d !== "")) }
    function clearSelection() { root._anchor = ""; root._setSelection([]) }

    // A hidden row is never selected; a row that went is forgotten. Run after any change to what is shown.
    function _prune() {
        const shown = root._shownItems().map(it => it.dir)
        const next = root.selection.filter(d => shown.indexOf(d) >= 0)
        if (next.length !== root.selection.length) root.selection = next
        if (root._anchor !== "" && shown.indexOf(root._anchor) < 0) root._anchor = ""
        if (root.focusIndex >= shown.length) root.focusIndex = shown.length - 1
        root._recount()
    }
    function _schedulePrune() { Qt.callLater(root._prune) }

    // A press on a row (not on its links, Review or ⋯). The selection before it is kept for a moment,
    // so a double-click — which arrives as a click on that row and then the double-click — can put it
    // back. A "double-click" on another row than the click before it is two quick clicks: a click.
    property var    _beforeClick: null
    function rowPressed(dir, modifiers) {
        root._beforeClick = { dir: dir, selection: root.selection.slice(), anchor: root._anchor }
        const it = root._itemFor(dir)
        if (it) root.focusIndex = it.rank
        root.forceActiveFocus()
        if (modifiers & Qt.ShiftModifier) root.extendTo(dir)
        else                              root.toggleRow(dir)
    }
    // `scenePos` is where the second press landed: a double-click is delivered to the row that took the
    // first press, so a second press on ANOTHER row inside the interval is that other row's click.
    function rowDoubleClicked(dir, modifiers, scenePos) {
        if (scenePos !== undefined) {
            const hit = root._shownItems().find(it => {
                const p = it.mapFromItem(null, scenePos.x, scenePos.y)
                return p.y >= 0 && p.y < it.height
            })
            if (hit && hit.dir !== dir) { root.rowPressed(hit.dir, modifiers || 0); return }
        }
        const before = root._beforeClick
        root._beforeClick = null
        if (!before || before.dir !== dir) { root.rowPressed(dir, modifiers || 0); return }
        root.selection = before.selection
        root._anchor   = before.anchor
        root._recount()
        root.review(dir)
    }
    // Review: open in Live. The running session goes as "" (resume it rather than load it for review).
    function review(dir) {
        const it = root._itemFor(dir)
        root.reviewSessionRequested(it && it.isLive ? "" : dir)
    }

    // ── The ⋯ menu ───────────────────────────────────────────────────────────
    property string _menuDir: ""
    function openRowMenu(i) {
        const it = root.rowAt(i)
        if (!it) return
        root._menuDir = it.dir
        rowMenu.dir         = it.dir
        rowMenu.isLive      = it.isLive
        rowMenu.archived    = it.archived
        rowMenu.canReport   = it.canReport
        rowMenu.reportState = it.reportState
        rowMenu.pdfPath     = it.pdfPath
        rowMenu.making      = it.making
        rowMenu.hasSwings   = it.swings > 0
        // Under the row's ⋯, right edges aligned.
        const more = it.moreItem
        const p = more.mapToItem(root, more.width, more.height)
        rowMenu.x = Math.round(p.x - rowMenu.width)
        rowMenu.y = Math.round(p.y + Theme.sp(4))
        rowMenu.open()
    }
    function closeRowMenu() { rowMenu.close() }

    // ── The bulk actions (§2.4–§2.6) ─────────────────────────────────────────
    function bytesWords(b) {
        if (!(b > 0)) return "0 MB"
        const gb = b / (1024 * 1024 * 1024)
        if (gb >= 1) return gb.toFixed(1) + " GB"
        return Math.max(1, Math.round(b / (1024 * 1024))) + " MB"
    }
    function _plural(n, one, many) { return n === 1 ? one.arg(n) : many.arg(n) }
    // "Mon 5 Oct" this year, "Mon 5 Oct 2025" before; "· 2" for a second session that day.
    function _dayWords(f) {
        let d = String(f.dateLabel || "")
        const y = String(new Date().getFullYear())
        if (d.endsWith(" " + y)) d = d.slice(0, -5)
        return (d || qsTr("this session")) + ((f.sessionOfDay || 0) >= 2 ? " · " + f.sessionOfDay : "")
    }
    // The words of a row's Report cell while a job has it.
    function _jobWords(status, verb, making, archiveProgress, sendProgress) {
        if (status === "") return ""
        if (status.indexOf("failed: ") === 0) return status.slice(8)
        if (status === "queued") return ({ make: qsTr("Report queued"), send: qsTr("Email queued"),
                                           export: qsTr("Export queued"), archive: qsTr("Archive queued"),
                                           restore: qsTr("Restore queued"), trash: qsTr("Delete queued") })[verb] || ""
        if (status !== "working" || making) return ""      // making: the cell's own "Making report… page n"
        const pc = p => Math.round((p || 0) * 100) + "%"
        switch (verb) {
        case "send":    return qsTr("Sending… %1").arg(pc(sendProgress))
        case "export":  return qsTr("Exporting…")
        case "archive": return qsTr("Archiving… %1").arg(pc(archiveProgress))
        case "restore": return qsTr("Restoring… %1").arg(pc(archiveProgress))
        case "trash":   return qsTr("Moving to the trash…")
        }
        return ""
    }

    // The confirm (or the export sheet) for a verb over `dirs`, then the queue. Rows that cannot take
    // it are left out and the confirm says so; nothing at all to do does nothing.
    property var _pending: null
    function askBulk(verb, dirs) {
        if (bulk.running || !dirs || dirs.length === 0) return false
        const all = dirs.map(d => Object.assign({ dir: d }, root.facts(d)))
        const ok = all.filter(f => bulk.canTake(verb, f.dir, f))
        const left = all.length - ok.length
        if (ok.length === 0) return false
        const one = ok.length === 1 ? ok[0] : null
        const sum = (k) => ok.reduce((a, f) => a + (Number(f[k]) || 0), 0)
        const liveLeft = all.some(f => f.isLive)
        const leftOut = left === 0 ? ""
            : liveLeft && left === 1 ? " " + qsTr("The live session is left out.")
            : " " + root._plural(left, qsTr("%1 session that cannot is left out."), qsTr("%1 sessions that cannot are left out."))

        if (verb === "make") return bulk.start("make", ok.map(f => f.dir), {})

        if (verb === "export") {
            let swings = [], perSession = 0
            for (const f of ok) {
                const s = root.reviewer ? (root.reviewer.swingDirsForSession(f.dir) || []) : []
                if (s.length > 0) perSession++
                swings = swings.concat(s)
            }
            if (swings.length === 0) { root.notice("ℹ", qsTr("No saved shots to export"), "info", ""); return false }
            exportSheet.dirs        = ok.map(f => f.dir)
            exportSheet.swingDirs   = swings
            exportSheet.cameras     = root.exporter ? (root.exporter.camerasForShots(swings) || []) : []
            exportSheet.shotCount   = swings.length
            exportSheet.caption     = perSession === 1
                ? qsTr("%1 swings · zipped to your home folder").arg(swings.length)
                : qsTr("%1 sessions, %2 swings · one zip each in your home folder").arg(perSession).arg(swings.length)
            exportSheet.heading     = ok.length === 1 ? qsTr("Export session") : qsTr("Export sessions")
            exportSheet.includeJson = true
            exportSheet.open()
            return true
        }

        let title = "", body = "", confirmText = "", tone = "attention"
        if (verb === "send") {
            const needMake = ok.filter(f => {
                const st = (root.reports && typeof root.reports.state === "function") ? (root.reports.state(f.dir) || {}) : { state: f.state }
                return st.state !== "current"
            }).length
            const bytes = ok.reduce((a, f) => a + (f.state === "current" ? (Number(f.sizeBytes) || 0) : 0), 0)
            title = one ? qsTr("Email %1's report?").arg(root._dayWords(one))
                        : qsTr("Email %1 reports to %2?").arg(ok.length).arg(root.greeting || qsTr("the golfer"))
            body = qsTr("Send %1 to %2").arg(root._plural(ok.length, qsTr("%1 report"), qsTr("%1 reports")))
                       .arg(root.addresses.join(", "))
                   + (bytes > 0 && needMake === 0 ? " · " + root.bytesWords(bytes) : "")
                   + (ok.length > 1 ? qsTr(", one email per session.") : ".")
                   + (needMake > 0 ? " " + root._plural(needMake, qsTr("%1 report has to be made first."),
                                                         qsTr("%1 reports have to be made first.")) : "")
                   + (root.sendReady ? "" : " " + qsTr("Each opens in your mail app to send."))
                   + leftOut
            confirmText = one ? qsTr("Send") : qsTr("Send %1").arg(ok.length)
        } else if (verb === "archive") {
            title = one ? qsTr("Archive %1?").arg(root._dayWords(one))
                        : qsTr("Archive %1 sessions?").arg(ok.length)
            body = qsTr("Archiving keeps the summaries, scores, metrics and the report here, and moves the videos "
                        + "and raw data to the archive folder, freeing up to %1 in the library.").arg(root.bytesWords(sum("sessionBytes")))
                   + " " + qsTr("Opening an archived session restores it.") + leftOut
            confirmText = qsTr("Archive")
        } else if (verb === "restore") {
            title = one ? qsTr("Restore %1?").arg(root._dayWords(one))
                        : qsTr("Restore %1 sessions?").arg(ok.length)
            body = qsTr("Restoring brings the videos and raw data back from the archive folder into the library, "
                        + "which needs the space archiving freed. The summaries, scores, metrics and the report stay "
                        + "as they are.") + leftOut
            confirmText = qsTr("Restore")
        } else if (verb === "trash") {
            const days = root.settings ? (root.settings.trashRetentionDays || 0) : 0
            const reports = ok.filter(f => f.state === "current" || f.state === "stale").length
            const kept = days > 0 ? (one ? qsTr("It is kept in the library's trash for %1 days.").arg(days)
                                         : qsTr("They are kept in the library's trash for %1 days.").arg(days))
                                  : (one ? qsTr("It is kept in the library's trash until you empty it.")
                                         : qsTr("They are kept in the library's trash until you empty it."))
            if (one) {
                title = qsTr("Move %1 to the library trash?").arg(root._dayWords(one))
                body = (reports > 0 ? root._plural(one.swings || 0, qsTr("Its %1 swing and report go with it."),
                                                   qsTr("Its %1 swings and report go with it."))
                                    : root._plural(one.swings || 0, qsTr("Its %1 swing goes with it."),
                                                   qsTr("Its %1 swings go with it.")))
                       + " " + kept + leftOut
            } else {
                title = qsTr("Move %1 sessions to the library trash?").arg(ok.length)
                body = qsTr("Their %1 swings (%2)%3 go with them.").arg(sum("swings")).arg(root.bytesWords(sum("sessionBytes")))
                           .arg(reports > 0 ? qsTr(" and reports") : "")
                       + " " + kept + leftOut
            }
            confirmText = qsTr("Move to trash")
            tone = "error"
        } else {
            return false
        }
        root._pending = { verb: verb, dirs: ok.map(f => f.dir) }
        confirm.title = title
        confirm.body = body
        confirm.confirmText = confirmText
        confirm.tone = tone
        confirm.open()
        return true
    }

    // The outcome, once a job ends.
    function _outcome(verb, okCount, failCount) {
        const n = okCount + failCount
        const sev = failCount === 0 ? "info" : okCount === 0 ? "error" : "warn"
        const why = failCount > 0 && bulk.lastError ? ": " + bulk.lastError : ""
        if (verb === "export") {
            const zips = Object.keys(bulk.outputs).map(d => bulk.outputs[d])
            if (zips.length === 1 && failCount === 0)
                root.notice("✓", qsTr("Exported %1").arg(zips[0].split('/').pop()), "info", zips[0])
            else if (n > 0)
                root.notice(failCount === 0 ? "✓" : "⚠",
                            qsTr("Exported %1 of %2 sessions to your home folder").arg(zips.length).arg(n) + why,
                            sev, zips.join("\n"))
            return
        }
        if (n === 0) return
        const done = ({ make:    qsTr("Made %1 of %2 reports"),        send:  qsTr("Sent %1 of %2 reports"),
                        archive: qsTr("Archived %1 of %2 sessions"),   restore: qsTr("Restored %1 of %2 sessions"),
                        trash:   qsTr("Moved %1 of %2 sessions to the library trash") })[verb] || ""
        const glyph = verb === "trash" ? "🗑" : failCount === 0 ? "✓" : "⚠"
        root.notice(glyph, done.arg(okCount).arg(n) + why, sev, "")
    }

    activeFocusOnTab: true
    Keys.onPressed: (event) => {
        const shown = root._shownItems()
        const mod = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)
        const cur = (root.focusIndex >= 0 && root.focusIndex < shown.length) ? shown[root.focusIndex] : null
        if (event.key === Qt.Key_Escape) {
            if (root.selection.length > 0) { root.clearSelection(); event.accepted = true }
        } else if (event.key === Qt.Key_A && mod) {
            root.selectAllShown(); event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            root.focusIndex = Math.min(shown.length - 1, root.focusIndex + 1); event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            root.focusIndex = Math.max(0, root.focusIndex - 1); event.accepted = true
        } else if (event.key === Qt.Key_Space && cur) {
            root.toggleRow(cur.dir); event.accepted = true
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && cur) {
            root.review(cur.dir); event.accepted = true
        }
    }

    spacing: 0
    width: parent ? parent.width : implicitWidth

    // ── Nothing matched the filters ──────────────────────────────────────────
    Text {
        objectName: "sessionListNoMatch"
        visible: root.total > 0 && root.shown === 0
        width:   root.width
        height:  visible ? Theme.sp(40) : 0
        verticalAlignment: Text.AlignVCenter
        text: qsTr("No sessions match those filters")
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
        font.weight: Theme.fontBodyWeight
        color: Theme.colorText3
    }

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

    // ── The table ────────────────────────────────────────────────────────────
    PpCard {
        id: table
        objectName: "sessionsTable"
        table: true
        visible: root.shown > 0
        width: root.width
        // Fixed columns in design pixels (they scale with the text); Work-on takes what is left. Sized at
        // Mark's text scale (1.35) to their widest content: Date holds "Tue 18 Aug 2026 · 2" beside its
        // Review chip; Club "7 iron, Driver"; Recorded with three tags (Face-on HackMotion GC Quad) with
        // a pixel or two to spare; Report "Ready · 8.1 MB  Email".
        columnGap: Theme.gap(10)
        columns: [
            { title: qsTr("Date ▾"),        width: Theme.sp(202) },
            { title: qsTr("Club"),          width: Theme.sp(80) },
            { title: qsTr("Swings"),        width: Theme.sp(44), align: "right" },
            { title: qsTr("Recorded with"), width: Theme.sp(202) },
            { title: qsTr("Work-on"),       fill: true },
            { title: qsTr("Report"),        width: Theme.sp(138) },
            { title: "",                    width: Theme.sp(24), align: "right" }
        ]

        Repeater {
            id: rows
            model: root.model
            onItemAdded:   root._schedulePrune()
            onItemRemoved: root._schedulePrune()

            PpSessionsTableRow {
                id: trow
                // The model's roles through `model`: one of them is `state`, which as a property of its
                // own would shadow Item.state. A JS array (a harness's rows) hands each row as modelData.
                required property int index
                required property var model
                readonly property var r: (model.modelData !== undefined && model.modelData !== null
                                          && typeof model.modelData === "object") ? model.modelData : model

                readonly property int rank: r.shownRank !== undefined ? r.shownRank : index
                readonly property real sessionBytes: r.sessionBytes || 0
                objectName: rank >= 0 ? "sessionsRow_" + rank : "sessionsRowHidden_" + index
                visible: rank >= 0
                height:  visible ? implicitHeight : 0
                onRankChanged:         root._schedulePrune()
                onReportStateChanged:  root._schedulePrune()
                onSessionBytesChanged: root._schedulePrune()

                dir:          r.dir || ""
                dateLabel:    r.dateLabel || ""
                sessionOfDay: r.sessionOfDay || 0
                clubs:        r.clubs || ""
                swings:       r.swings || 0
                devices:      r.devices || []
                deviceTokens: r.deviceTokens || []
                selectedDevices: root.model && root.model.deviceFilter ? root.model.deviceFilter : []
                workOn1:      r.workOn1 || ""
                workOnKnown:  !!r.workOnKnown
                isLive:       !!r.isLive
                archived:     !!r.archived
                canReport:    !!r.canReport
                reportState:  r.state || ""
                stateShort:   r.stateShort || ""
                stateWords:   r.stateWords || ""
                pdfPath:      r.pdfPath || ""
                sizeBytes:    r.sizeBytes || 0

                greeting: root.greeting
                email:    root.email
                making:   root.reports ? (root.reports.busySession === trow.dir && trow.dir !== "") : false
                progressPage:  root.reports ? root.reports.progressPage : 0
                progressPages: root.reports ? root.reports.progressPages : 0
                error: root._errors[trow.dir] || ""
                confirming:   root.sendReady && root._confirmDir === trow.dir && trow.dir !== ""
                confirmTo:    root.addresses.join(", ")
                sendBusy:     root.sendBusy
                sendingHere:  root.sendBusy && root._sendingDir === trow.dir && trow.dir !== ""
                sendProgress: root.mailer ? (root.mailer.sendProgress || 0) : 0
                // A bulk job's state for this row: "queued" | "working" | "failed: …" ("" otherwise).
                readonly property string jobStatus: bulk.status[trow.dir] || ""
                jobWords:  root._jobWords(trow.jobStatus, bulk.verb, trow.making,
                                          root.archiver ? (root.archiver.progress || 0) : 0,
                                          root.mailer ? (root.mailer.sendProgress || 0) : 0)
                jobFailed: trow.jobStatus.indexOf("failed: ") === 0

                selected: root.selection.indexOf(trow.dir) >= 0
                keyFocus: root.activeFocus && root.focusIndex === trow.rank
                menuOpen: rowMenu.opened && root._menuDir === trow.dir

                onRowPressed: (modifiers) => root.rowPressed(trow.dir, modifiers)
                onRowDoubleClicked: (modifiers, scenePos) => root.rowDoubleClicked(trow.dir, modifiers, scenePos)
                onReviewRequested:        root.review(trow.dir)
                onMenuRequested:          { if (rowMenu.opened && root._menuDir === trow.dir) root.closeRowMenu()
                                            else root.openRowMenu(trow.rank) }
                onOpenReportRequested:    root.openPdf(trow.pdfPath)
                onMakeReportRequested: function(thenEmail) { root.openSheet(trow.dir, thenEmail) }
                onEmailRequested:         root.emailPressed(trow.dir, trow.pdfPath, trow.r.sessionMs || 0)
                onSendConfirmed:          root.sendReport(trow.dir, trow.pdfPath, trow.r.sessionMs || 0)
                onSendCancelled:          root._confirmDir = ""
                onStudioSettingsRequested: root.studioSettingsRequested()
            }
        }
    }

    PpSessionRowMenu {
        id: rowMenu
        greeting: root.greeting
        email:    root.email
        sendBusy: root.sendBusy
        archiveReady: root.archiveReady
        jobRunning:   bulk.running
        onClosed: root._menuDir = ""
        // The menu closes itself before it signals, so these read the row by dir, not _menuDir.
        onReviewRequested:      root.review(rowMenu.dir)
        onMakeReportRequested:  root.openSheet(rowMenu.dir, false)
        onOpenReportRequested:  root.openPdf(rowMenu.pdfPath)
        onOpenOldReportRequested: root.openPdf(rowMenu.pdfPath)
        onEmailRequested: {
            const it = root._itemFor(rowMenu.dir)
            root.emailPressed(rowMenu.dir, rowMenu.pdfPath, it ? (it.r.sessionMs || 0) : 0)
        }
        onGolferDetailsRequested: root.golferDetailsRequested()
        onCopyAddressRequested:   { if (root.mailer) root.mailer.copyText(root.email) }
        onRevealRequested:        { if (root.mailer) root.mailer.reveal(rowMenu.pdfPath !== "" ? rowMenu.pdfPath : rowMenu.dir) }
        onExportRequested:        root.askBulk("export", [rowMenu.dir])
        onArchiveRequested:       root.askBulk(rowMenu.archived ? "restore" : "archive", [rowMenu.dir])
        onDeleteRequested:        root.askBulk("trash", [rowMenu.dir])
    }

    // ── The bulk queue, its confirm, and the export sheet ───────────────────
    PpSessionBulkJobs {
        id: bulk
        reports:   root.reports
        mailer:    root.mailer
        exporter:  root.exporter
        archiver:  root.archiver
        review:    root.reviewer
        listModel: root.model
        rowFor:    (dir) => root.facts(dir)
        compose:   (dir, sessionMs) => root.composeFor(dir, sessionMs)
        onFinished: (verb, okCount, failCount) => root._outcome(verb, okCount, failCount)
    }

    PpConfirm {
        id: confirm
        objectName: "sessionsBulkConfirm"
        onConfirmed: {
            const p = root._pending
            root._pending = null
            if (p) bulk.start(p.verb, p.dirs, {})
        }
        onCancelled: root._pending = null
    }

    PpExportOptionsSheet {
        id: exportSheet
        objectName: "sessionsExportSheet"
        property var dirs: []
        parent: Overlay.overlay
        anchors.centerIn: parent
        onConfirmed: (videoFiles, includeJson) =>
            bulk.start("export", exportSheet.dirs, { videoFiles: videoFiles, includeJson: includeJson })
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
