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

// THE SESSIONS SCREEN'S BULK JOBS (docs/design/sessions_screen_redesign.md §2.4, §2.6, §8) — one queue
// over a list of session dirs, for one verb at a time:
//
//   make      sessionReport.make(dir, {note: the last note})               → made(dir, ok, pdf, error)
//   send      make first when the PDF is missing or out of date, then ONE email per session (S-D3):
//             reportMailer.send(…) → sendFinished(dir, ok, method, error), or the mail-app hand-off
//             (compose) until sending is set up; each recorded with sessionReport.recordSend(dir, method)
//   export    swingExporter.exportShots(swingDirsForSession(dir), videos, json) → exportFinished(ok, zip, error);
//             one ~/<session>.zip each
//   archive   archiveController.archiveSession(dir)   } no ok signal: the queue waits for `busy` to fall,
//   restore   archiveController.restoreSession(dir)   } then reads `status` ("… was not archived: why")
//   trash     sessionReviewController.trashSession(dir) → bool (the library's .pinpoint-trash)
//
// Every backend is a single-job controller already, so this runs STRICTLY ONE ITEM AT A TIME, newest
// session first. cancel() stops BETWEEN items: the one in flight finishes (the exporter has no cancel,
// and half an archive is worse than a whole one). A row that cannot take the verb is SKIPPED, counted
// apart from the failures: make/send — not canReport, live or archived; archive — archived already or
// live; restore — not archived; trash and export — live. The live session is never queued to be
// archived or trashed, whatever the caller asks (the C++ refuses it too).
//
// `status` is per dir — "queued" | "working" | "done" | "skipped" | "failed: <why>" — reassigned whole so
// the rows' Report cells re-read it; it stays until the next start(), so a failure stays in its row.
// After each archive, restore or trash the list model is told to rescan (it does not hear of them).
//
// The backends are properties defaulting to the app's context objects, so a test hands in stand-ins.

import QtQuick

QtObject {
    id: jobs

    // ── The backends ─────────────────────────────────────────────────────────
    property var reports:  (typeof sessionReport !== "undefined") ? sessionReport : null
    property var mailer:   (typeof reportMailer !== "undefined") ? reportMailer : null
    property var exporter: (typeof swingExporter !== "undefined") ? swingExporter : null
    property var archiver: (typeof archiveController !== "undefined") ? archiveController : null
    property var review:   (typeof sessionReviewController !== "undefined") ? sessionReviewController : null
    // Told to rescan after an archive, restore or trash (anything with refresh()).
    property var listModel: (typeof sessionReportList !== "undefined") ? sessionReportList : null
    // A session's facts: {canReport, isLive, archived, sessionMs, …}. The host's rows, else the model's.
    property var rowFor: function (dir) {
        return (jobs.listModel && typeof jobs.listModel.rowFor === "function") ? (jobs.listModel.rowFor(dir) || {}) : {}
    }
    // One email's words for a session: {to: [addresses], replyTo, fromName, subject, body}. The host's,
    // so the bulk send says exactly what the single send says.
    property var compose: null

    // ── What it is doing ─────────────────────────────────────────────────────
    property bool   running:   false
    property string verb:      ""
    property int    done:      0          // items finished, either way
    property int    total:     0          // items queued (skipped rows are not)
    property int    skipped:   0
    property string current:   ""         // the dir in flight
    property var    status:    ({})
    property string lastError: ""
    property bool   cancelling: false
    // export: {dir: zip path} of the zips written.
    property var    outputs:   ({})

    signal finished(string verb, int okCount, int failCount)

    readonly property var verbs: ["make", "send", "export", "archive", "restore", "trash"]

    property var    _queue:  []
    property var    _wait:   null         // {kind, dir}: the completion the item in flight waits for
    property var    _opts:   ({})
    property int    _ok:     0
    property int    _failed: 0

    function _same(a, b) {
        const n = s => String(s || "").replace(/\/+/g, "/").replace(/\/$/, "")
        return n(a) === n(b)
    }
    function _isLive(f, dir) {
        return !!f.isLive || (!!jobs.review && !!jobs.review.liveSessionDir && jobs._same(jobs.review.liveSessionDir, dir))
    }
    // Whether a session can take the verb.
    function canTake(verb, dir, f) {
        f = f || jobs.rowFor(dir) || {}
        const live = jobs._isLive(f, dir)
        switch (verb) {
        case "make": case "send": return !!f.canReport && !live && !f.archived
        case "archive":           return !f.archived && !live
        case "restore":           return !!f.archived
        case "trash": case "export": return !live
        }
        return false
    }
    function _setStatus(dir, s) {
        const m = Object.assign({}, jobs.status)
        if (s === "") delete m[dir]; else m[dir] = s
        jobs.status = m
    }

    // Queues `dirs` for `verb`; false when a job is running or the verb is not one of ours.
    // options: make {note}; export {videoFiles, includeJson}.
    function start(verb, dirs, options) {
        if (jobs.running || jobs.verbs.indexOf(verb) < 0) return false
        const st = {}, queue = []
        let skipped = 0
        for (const d of (dirs || [])) {
            if (!d || st[d] !== undefined) continue
            const f = jobs.rowFor(d) || {}
            if (jobs.canTake(verb, d, f)) { st[d] = "queued"; queue.push({ dir: d, ms: Number(f.sessionMs) || 0 }) }
            else                          { st[d] = "skipped"; skipped++ }
        }
        // Newest first; the folder name (date-led) breaks a tie.
        queue.sort((a, b) => (b.ms - a.ms) || (b.dir < a.dir ? -1 : b.dir > a.dir ? 1 : 0))
        jobs.verb = verb
        jobs._opts = options || {}
        jobs._queue = queue.map(q => q.dir)
        jobs.status = st
        jobs.outputs = ({})
        jobs.done = 0
        jobs.total = queue.length
        jobs.skipped = skipped
        jobs.lastError = ""
        jobs.cancelling = false
        jobs._ok = 0
        jobs._failed = 0
        jobs._wait = null
        jobs.running = true
        Qt.callLater(jobs._next)
        return true
    }

    // Stops after the item in flight; what was still queued is left alone.
    function cancel() {
        if (!jobs.running) return
        jobs.cancelling = true
        const m = Object.assign({}, jobs.status)
        for (const d of jobs._queue) delete m[d]
        jobs.status = m
        jobs._queue = []
        if (!jobs._wait) Qt.callLater(jobs._next)
    }

    function _next() {
        if (!jobs.running || jobs._wait) return
        if (jobs.cancelling || jobs._queue.length === 0) { jobs._finish(); return }
        const q = jobs._queue.slice()
        const dir = q.shift()
        jobs._queue = q
        jobs.current = dir
        jobs._setStatus(dir, "working")
        switch (jobs.verb) {
        case "make":    jobs._make(dir); break
        case "send":    jobs._send(dir); break
        case "export":  jobs._export(dir); break
        case "archive": jobs._archive(dir); break
        case "restore": jobs._restore(dir); break
        case "trash":   jobs._trash(dir); break
        }
    }

    function _itemDone(dir, ok, error, skipped) {
        jobs._wait = null
        if (skipped)  { jobs._setStatus(dir, "skipped"); jobs.skipped++ ; jobs.total-- }
        else if (ok)  { jobs._setStatus(dir, "done"); jobs._ok++; jobs.done++ }
        else {
            const why = error || qsTr("it did not work")
            jobs._setStatus(dir, "failed: " + why)
            jobs.lastError = why
            jobs._failed++
            jobs.done++
        }
        if (["archive", "restore", "trash"].indexOf(jobs.verb) >= 0
                && jobs.listModel && typeof jobs.listModel.refresh === "function")
            jobs.listModel.refresh()
        Qt.callLater(jobs._next)
    }

    function _finish() {
        jobs.running = false
        jobs.current = ""
        jobs._wait = null
        jobs.finished(jobs.verb, jobs._ok, jobs._failed)
    }

    // ── make ─────────────────────────────────────────────────────────────────
    function _make(dir) {
        if (!jobs.reports) { jobs._itemDone(dir, false, qsTr("Reports are not available in this build.")); return }
        // The sheet's defaults: the note the coach wrote last time stays on the remake.
        const pre = typeof jobs.reports.preflight === "function" ? (jobs.reports.preflight(dir) || {}) : {}
        const opts = Object.assign({ note: pre.lastNote || "" }, jobs._opts.make || {})
        jobs._wait = { kind: "made", dir: dir }
        jobs.reports.make(dir, opts)              // a refusal comes back as made(…, false) at once
    }
    property Connections _madeConn: Connections {
        target: jobs.reports
        ignoreUnknownSignals: true
        function onMade(dir, ok, path, error) {
            const w = jobs._wait
            if (!w || w.kind !== "made" || !jobs._same(w.dir, dir)) return
            if (jobs.verb === "send" && ok) { jobs._wait = null; jobs._sendPdf(w.dir, path); return }
            jobs._itemDone(w.dir, ok, error)
        }
    }

    // ── send ─────────────────────────────────────────────────────────────────
    function _send(dir) {
        if (!jobs.reports || !jobs.mailer) { jobs._itemDone(dir, false, qsTr("Sending is not available in this build.")); return }
        const st = typeof jobs.reports.state === "function" ? (jobs.reports.state(dir) || {}) : {}
        if (st.state === "current" && st.pdfPath) jobs._sendPdf(dir, st.pdfPath)
        else                                     jobs._make(dir)
    }
    function _sendPdf(dir, pdf) {
        const f = jobs.rowFor(dir) || {}
        const w = typeof jobs.compose === "function" ? (jobs.compose(dir, Number(f.sessionMs) || 0) || {}) : {}
        if (jobs.mailer.sendConfigured) {
            jobs._wait = { kind: "sent", dir: dir }
            const res = jobs.mailer.send(dir, w.to || [], w.replyTo || "", w.fromName || "", w.subject || "", w.body || "", pdf)
            if (!res || !res.ok) jobs._itemDone(dir, false, (res && res.error) || qsTr("The report could not be sent."))
            return
        }
        // Not set up to send: the mail app, as the single Email does.
        const res = jobs.mailer.compose(w.to || [], w.replyTo || "", w.subject || "", w.body || "", pdf)
        if (res && res.ok) jobs.reports.recordSend(dir, res.method)
        jobs._itemDone(dir, !!(res && res.ok), (res && res.error) || qsTr("The mail app could not be opened."))
    }
    property Connections _sentConn: Connections {
        target: jobs.mailer
        ignoreUnknownSignals: true
        function onSendFinished(dir, ok, method, error) {
            const w = jobs._wait
            if (!w || w.kind !== "sent" || !jobs._same(w.dir, dir)) return
            if (ok && jobs.reports) jobs.reports.recordSend(w.dir, method)
            jobs._itemDone(w.dir, ok, error)
        }
    }

    // ── export ───────────────────────────────────────────────────────────────
    function _export(dir) {
        if (!jobs.exporter || !jobs.review) { jobs._itemDone(dir, false, qsTr("Export is not available in this build.")); return }
        const swings = jobs.review.swingDirsForSession(dir) || []
        if (swings.length === 0) { jobs._itemDone(dir, true, "", true); return }     // no saved shots
        if (jobs.exporter.busy) { jobs._itemDone(dir, false, qsTr("Another export is running.")); return }
        jobs._wait = { kind: "exported", dir: dir }
        jobs.exporter.exportShots(swings, jobs._opts.videoFiles || [], jobs._opts.includeJson !== false)
    }
    property Connections _exportConn: Connections {
        target: jobs.exporter
        ignoreUnknownSignals: true
        function onExportFinished(ok, zip, error) {
            const w = jobs._wait
            if (!w || w.kind !== "exported") return
            if (ok) { const o = Object.assign({}, jobs.outputs); o[w.dir] = zip; jobs.outputs = o }
            jobs._itemDone(w.dir, ok, error)
        }
    }

    // ── archive / restore ────────────────────────────────────────────────────
    function _archive(dir) {
        if (!jobs.archiver) { jobs._itemDone(dir, false, qsTr("Archiving is not available in this build.")); return }
        if (jobs.archiver.busy) { jobs._itemDone(dir, false, qsTr("Archiving is busy with something else.")); return }
        jobs._wait = { kind: "archived", dir: dir }
        if (!jobs.archiver.archiveSession(dir)) jobs._itemDone(dir, false, qsTr("It could not be archived."))
    }
    function _restore(dir) {
        if (!jobs.archiver) { jobs._itemDone(dir, false, qsTr("Archiving is not available in this build.")); return }
        // Busy with something else (housekeeping): its end would read as ours.
        if (jobs.archiver.busy) { jobs._itemDone(dir, false, qsTr("Archiving is busy with something else.")); return }
        jobs._wait = { kind: "archived", dir: dir }
        jobs.archiver.restoreSession(dir)
        if (!jobs.archiver.busy && jobs._wait && jobs._wait.dir === dir) jobs._settleArchive()   // never started
    }
    function _settleArchive() {
        const w = jobs._wait
        const st = String(jobs.archiver.status || "")
        const failed = /was not (archived|restored)/.test(st)
        jobs._itemDone(w.dir, !failed, failed ? st : "")
    }
    property Connections _archiveConn: Connections {
        target: jobs.archiver
        ignoreUnknownSignals: true
        function onChanged() {
            const w = jobs._wait
            if (w && w.kind === "archived" && !jobs.archiver.busy) jobs._settleArchive()
        }
    }

    // ── trash ────────────────────────────────────────────────────────────────
    function _trash(dir) {
        if (!jobs.review) { jobs._itemDone(dir, false, qsTr("Delete is not available in this build.")); return }
        const ok = !!jobs.review.trashSession(dir)
        jobs._itemDone(dir, ok, ok ? "" : qsTr("It could not be moved to the library trash."))
    }
}
