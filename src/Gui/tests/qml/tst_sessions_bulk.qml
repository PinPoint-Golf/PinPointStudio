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
import QtTest
import PinPointStudio

// The Sessions screen's bulk jobs (docs/design/sessions_screen_redesign.md §2.4–§2.6, §8):
// PpSessionBulkJobs over FIXTURE rows with stand-in backends — reports, mailer, exporter, archiver, the
// review controller and the list model — that log what they are asked and answer on the next turn, as
// the real ones do. No session folder is read; nothing is made, sent, archived or trashed.
//
//   bulk make skips the rows that cannot report and makes the rest newest first · Cancel stops between
//   items · the live row is never queued to trash or archive · archive then restore round-trips
//   `archived` · send makes a missing PDF first, then sends, one send recorded per session · the
//   selection bar's buttons are live by what is selected, and say why not
Item {
    id: probe
    width: 1400; height: 800

    // s0 is live, s1 too short to report, s2–s5 can; s5 is the newest.
    function fixture() {
        const out = []
        for (let i = 0; i < 6; ++i)
            out.push({ dir: "/fx/s" + i, dateLabel: "Tue " + (10 + i) + " Oct 2026", sessionOfDay: 1,
                       clubs: "7 iron", swings: i === 1 ? 2 : 10 + i, devices: ["Face-on"], deviceTokens: ["faceon"],
                       workOn1: "Keep the flex in your trail knee", workOnKnown: true, isLive: i === 0,
                       archived: false, canReport: i >= 2, state: "none", stateShort: "No report",
                       stateWords: "No report", pdfPath: "", sizeBytes: 0, sessionBytes: 1e9,
                       sessionMs: 1000 * (i + 1), shownRank: i })
        return out
    }
    property var rows: fixture()
    function rowOf(d) { return probe.rows.find(r => r.dir === d) || ({}) }
    function setRow(dir, patch) { probe.rows = probe.rows.map(r => r.dir === dir ? Object.assign({}, r, patch) : r) }

    // ── Stand-in backends ────────────────────────────────────────────────────
    QtObject { id: reports
        property bool busy: false; property string busySession: ""
        property int progressPage: 0; property int progressPages: 0
        property var states: ({})            // dir → {state, pdfPath}
        property var madeLog: []
        property var sendRecords: []
        property bool autoFinish: true
        property string _pending: ""
        signal made(string dir, bool ok, string path, string error)
        function preflight(d) { return { lastNote: "" } }
        function state(d) { return reports.states[d] || { state: "none", pdfPath: "" } }
        function make(d, opts) {
            reports.madeLog = reports.madeLog.concat([d])
            reports.busy = true; reports.busySession = d; reports._pending = d
            if (reports.autoFinish) Qt.callLater(reports.finishPending)
        }
        function finishPending() {
            const d = reports._pending
            if (d === "") return
            reports._pending = ""
            const s = Object.assign({}, reports.states); s[d] = { state: "current", pdfPath: d + "/report.pdf" }
            reports.states = s
            reports.busy = false; reports.busySession = ""
            reports.made(d, true, d + "/report.pdf", "")
        }
        function recordSend(d, method) { reports.sendRecords = reports.sendRecords.concat([{ dir: d, method: method }]) }
        function reset() { states = ({}); madeLog = []; sendRecords = []; autoFinish = true; _pending = ""; busy = false; busySession = "" }
    }
    QtObject { id: mailer
        property bool sendConfigured: true; property bool sending: false; property real sendProgress: 0
        property var sendLog: []
        signal sendFinished(string sessionDir, bool ok, string method, string error)
        function send(dir, to, replyTo, fromName, subject, body, file) {
            mailer.sendLog = mailer.sendLog.concat([{ dir: dir, to: to, file: file, subject: subject }])
            Qt.callLater(() => mailer.sendFinished(dir, true, "dry-run", ""))
            return { ok: true, error: "" }
        }
        function compose(to, replyTo, subject, body, file) { return { ok: true, method: "dry-run" } }
        function copyText(t) {} function reveal(p) {}
        function reset() { sendLog = [] }
    }
    QtObject { id: exporter
        property bool busy: false
        property var log: []
        signal exportFinished(bool ok, string zipPath, string error)
        function camerasForShots(d) { return [{ file: "face_on.mp4", alias: "Face-on" }] }
        function exportShots(dirs, videos, json) {
            exporter.log = exporter.log.concat([dirs])
            Qt.callLater(() => exporter.exportFinished(true, "/home/" + dirs[0].split("/")[2] + ".zip", ""))
        }
    }
    QtObject { id: archiver
        property bool busy: false; property string status: ""; property real progress: 0
        property var archivedSet: ({})
        property var log: []
        property bool failNext: false          // the next job ends "… was not archived: no space"
        signal changed()
        function _run(d, archive) {
            if (archiver.busy) return false
            archiver.log = archiver.log.concat([(archive ? "archive " : "restore ") + d])
            archiver.busy = true; archiver.status = "Working…"; archiver.changed()
            Qt.callLater(() => {
                archiver.busy = false
                if (archiver.failNext) {
                    archiver.failNext = false
                    archiver.status = d + " was not archived: no space"
                } else {
                    const s = Object.assign({}, archiver.archivedSet); s[d] = archive
                    archiver.archivedSet = s
                    archiver.status = d + (archive ? " archived: 10 swings, 900 MB freed in the library."
                                                   : " restored: 10 swings are back in the library.")
                }
                archiver.changed()
            })
            return true
        }
        function archiveSession(d) { return archiver._run(d, true) }
        function restoreSession(d) { archiver._run(d, false) }
        function reset() { archivedSet = ({}); log = []; status = ""; busy = false; failNext = false }
    }
    QtObject { id: review
        property string liveSessionDir: ""
        property var trashed: []
        function swingDirsForSession(d) { return [d + "/swing_0001", d + "/swing_0002"] }
        function trashSession(d) { review.trashed = review.trashed.concat([d]); return true }
        function reset() { trashed = []; liveSessionDir = "" }
    }
    // The list model: refresh() reads the archiver's outcome back into the rows, as a rescan would.
    QtObject { id: listModel
        property int refreshes: 0
        function refresh() {
            listModel.refreshes++
            probe.rows = probe.rows.map(r => archiver.archivedSet[r.dir] === undefined ? r
                                             : Object.assign({}, r, { archived: archiver.archivedSet[r.dir] }))
        }
    }

    PpSessionBulkJobs {
        id: jobs
        reports: reports; mailer: mailer; exporter: exporter; archiver: archiver; review: review
        listModel: listModel
        rowFor: (d) => probe.rowOf(d)
        compose: (d, ms) => ({ to: ["nick@example.com"], replyTo: "", fromName: "Alex", subject: "Report " + d, body: "Hi" })
    }
    SignalSpy { id: finishedSpy; target: jobs; signalName: "finished" }

    // ── A list and its bar, for the buttons ──────────────────────────────────
    QtObject { id: athletes
        property string currentUuid: "fx"; property string currentName: "Nick Henn"
        property string email: "nick@example.com"
        signal athletesChanged()
        function reportDetails(u) { return { greeting: "Nick", email: athletes.email } } }
    QtObject { id: studio
        property string coachName: "Alex"; property string studioName: "Studio"
        property string coachEmail: ""; property string emailSubject: ""; property string emailBody: "" }
    QtObject { id: settings
        property string archiveLocation: "/archive"; property int trashRetentionDays: 30 }

    PpSessionReportList {
        id: list
        x: 20; y: 20; width: 1300
        model: probe.rows
        reports: reports; studio: studio; athletes: athletes; mailer: mailer
        exporter: exporter; archiver: archiver; reviewer: review; settings: settings
    }
    PpSessionsSelectionBar {
        id: bar
        list: list
        x: 20; y: 700; width: 1360; height: implicitHeight
    }

    TestCase {
        name: "SessionsBulk"
        when: windowShown

        function find(item, name) {
            if (!item) return null
            if (item.objectName === name) return item
            const k = (item.data || item.children || [])
            for (let i = 0; i < k.length; ++i) { const f = find(k[i], name); if (f) return f }
            return null
        }
        function runToEnd(verb, dirs, options) {
            finishedSpy.clear()
            verify(jobs.start(verb, dirs, options || {}), "started " + verb)
            verify(jobs.running)
            finishedSpy.wait(3000)
            verify(!jobs.running)
            return finishedSpy.signalArguments[0]
        }
        function all() { return probe.rows.map(r => r.dir) }

        function init() {
            probe.rows = probe.fixture()
            reports.reset(); mailer.reset(); archiver.reset(); review.reset()
            athletes.email = "nick@example.com"; athletes.athletesChanged()
            settings.archiveLocation = "/archive"
            list.clearSelection()
            finishedSpy.clear()
            tryVerify(() => !jobs.running)
        }

        function test_make_skips_rows_that_cannot_report_and_goes_newest_first() {
            const a = runToEnd("make", all())
            compare(reports.madeLog, ["/fx/s5", "/fx/s4", "/fx/s3", "/fx/s2"])
            compare(jobs.status["/fx/s0"], "skipped")          // live
            compare(jobs.status["/fx/s1"], "skipped")          // two swings
            compare(jobs.status["/fx/s5"], "done")
            compare(jobs.skipped, 2)
            compare(jobs.total, 4)
            compare(jobs.done, 4)
            compare(a[0], "make"); compare(a[1], 4); compare(a[2], 0)
        }

        function test_cancel_stops_between_items() {
            reports.autoFinish = false
            finishedSpy.clear()
            verify(jobs.start("make", ["/fx/s2", "/fx/s3", "/fx/s4", "/fx/s5"], {}))
            tryVerify(() => reports.madeLog.length === 1)
            compare(jobs.current, "/fx/s5")
            jobs.cancel()
            verify(jobs.running, "the item in flight finishes")
            verify(jobs.cancelling)
            compare(jobs.status["/fx/s4"], undefined)          // no longer queued
            wait(50)
            compare(reports.madeLog.length, 1)
            reports.finishPending()
            finishedSpy.wait(3000)
            compare(reports.madeLog, ["/fx/s5"])
            compare(jobs.status["/fx/s5"], "done")
            compare(finishedSpy.signalArguments[0][1], 1)
            compare(finishedSpy.signalArguments[0][2], 0)
        }

        function test_the_live_row_is_never_trashed_or_archived() {
            runToEnd("trash", all())
            verify(review.trashed.indexOf("/fx/s0") < 0)
            compare(review.trashed.length, 5)
            compare(jobs.status["/fx/s0"], "skipped")

            probe.rows = probe.fixture()
            runToEnd("archive", all())
            verify(archiver.log.indexOf("archive /fx/s0") < 0)
            compare(archiver.log.length, 5)
            compare(jobs.status["/fx/s0"], "skipped")

            // A row the model has not marked live yet, but the review controller has: still skipped.
            probe.rows = probe.fixture()
            review.liveSessionDir = "/fx/s3"
            review.trashed = []
            runToEnd("trash", ["/fx/s3", "/fx/s4"])
            compare(review.trashed, ["/fx/s4"])
        }

        function test_archive_then_restore_round_trips() {
            runToEnd("archive", ["/fx/s2"])
            compare(probe.rowOf("/fx/s2").archived, true)
            compare(jobs.status["/fx/s2"], "done")
            verify(listModel.refreshes > 0)
            // Restore takes the archived row and skips the rest.
            const a = runToEnd("restore", ["/fx/s2", "/fx/s3"])
            compare(probe.rowOf("/fx/s2").archived, false)
            compare(jobs.status["/fx/s3"], "skipped")
            compare(archiver.log, ["archive /fx/s2", "restore /fx/s2"])
            compare(a[1], 1)
        }

        function test_a_refused_archive_fails_with_the_reason() {
            archiver.failNext = true
            const a = runToEnd("archive", ["/fx/s2"])
            compare(a[2], 1)
            verify(String(jobs.status["/fx/s2"]).indexOf("failed: ") === 0)
            verify(jobs.lastError.indexOf("no space") >= 0)
        }

        function test_send_makes_the_missing_pdf_first_and_records_one_send_each() {
            reports.states = { "/fx/s2": { state: "current", pdfPath: "/fx/s2/report.pdf" } }
            const a = runToEnd("send", ["/fx/s2", "/fx/s3", "/fx/s1"])
            compare(reports.madeLog, ["/fx/s3"])                      // s2's PDF was current
            compare(mailer.sendLog.map(s => s.dir), ["/fx/s3", "/fx/s2"])
            compare(mailer.sendLog[0].file, "/fx/s3/report.pdf")
            compare(mailer.sendLog[0].to, ["nick@example.com"])
            compare(reports.sendRecords.length, 2)
            compare(reports.sendRecords.filter(r => r.dir === "/fx/s3").length, 1)
            compare(reports.sendRecords.filter(r => r.dir === "/fx/s2").length, 1)
            compare(reports.sendRecords[0].method, "dry-run")
            compare(jobs.status["/fx/s1"], "skipped")
            compare(a[1], 2); compare(a[2], 0)
        }

        function test_export_makes_one_zip_per_session() {
            runToEnd("export", ["/fx/s2", "/fx/s3"], { videoFiles: ["face_on.mp4"], includeJson: true })
            compare(exporter.log.length, 2)
            compare(exporter.log[0], ["/fx/s3/swing_0001", "/fx/s3/swing_0002"])
            compare(Object.keys(jobs.outputs).length, 2)
            compare(jobs.outputs["/fx/s3"], "/home/s3.zip")
        }

        // ── The bar ──────────────────────────────────────────────────────────
        function btn(name) { const b = find(bar, name); verify(b, name); return b }

        function test_the_bar_buttons_follow_the_selection() {
            verify(!bar.visible)
            list.toggleRow("/fx/s0")                                  // the live row alone
            tryVerify(() => bar.visible)
            verify(!btn("sessionsBulkMake").can)
            compare(btn("sessionsBulkMake").why, "The live session is still recording")
            verify(!btn("sessionsBulkEmail").can)
            verify(!btn("sessionsBulkArchive").can)
            verify(!btn("sessionsBulkDelete").can)
            compare(btn("sessionsBulkDelete").why, "The live session cannot be deleted")
            verify(!btn("sessionsBulkExport").can)

            list.toggleRow("/fx/s2")                                  // and one that can take all of it
            verify(btn("sessionsBulkMake").can)
            verify(btn("sessionsBulkEmail").can)
            compare(btn("sessionsBulkEmail").label, "Email to Nick")
            verify(btn("sessionsBulkExport").can)
            verify(btn("sessionsBulkArchive").can)
            compare(btn("sessionsBulkArchive").label, "Archive")
            verify(btn("sessionsBulkDelete").can)
            compare(find(bar, "sessionsSelectedCount").text, "2 selected")

            // No archive folder: Archive is off and says where to set one.
            settings.archiveLocation = ""
            verify(!btn("sessionsBulkArchive").can)
            compare(btn("sessionsBulkArchive").why, "Set an archive folder in Settings → Archiving")
            settings.archiveLocation = "/archive"

            // No address: Email is off and says so.
            athletes.email = ""; athletes.athletesChanged()
            verify(!btn("sessionsBulkEmail").can)
            verify(btn("sessionsBulkEmail").why.indexOf("email") >= 0)
            athletes.email = "nick@example.com"; athletes.athletesChanged()

            // A short session only: no report can be made.
            list.clearSelection()
            list.toggleRow("/fx/s1")
            verify(!btn("sessionsBulkMake").can)
            compare(btn("sessionsBulkMake").why, "A report needs at least three swings")
            verify(btn("sessionsBulkDelete").can)

            // Every selected row archived: Restore.
            probe.setRow("/fx/s3", { archived: true })
            tryVerify(() => list.rowAt(3) && list.rowAt(3).archived)
            list.clearSelection()
            list.toggleRow("/fx/s3")
            compare(btn("sessionsBulkArchive").label, "Restore")
            verify(btn("sessionsBulkArchive").can)
            verify(!btn("sessionsBulkMake").can)
        }

        function test_the_delete_confirm_names_the_session_and_the_retention() {
            const c = find(list, "sessionsBulkConfirm")
            verify(c)
            verify(list.askBulk("trash", ["/fx/s0", "/fx/s4"]))       // the live row is left out
            tryVerify(() => c.opened)
            compare(c.title, "Move Tue 14 Oct to the library trash?")
            verify(c.body.indexOf("Its 14 swings go with it.") === 0, c.body)
            verify(c.body.indexOf("kept in the library's trash for 30 days") >= 0, c.body)
            verify(c.body.indexOf("The live session is left out.") >= 0, c.body)
            compare(c.tone, "error")
            c.close()
            tryVerify(() => !c.opened)
            compare(review.trashed.length, 0)                          // cancelled: nothing moved

            settings.trashRetentionDays = 0
            verify(list.askBulk("trash", ["/fx/s4", "/fx/s5"]))
            tryVerify(() => c.opened)
            compare(c.title, "Move 2 sessions to the library trash?")
            verify(c.body.indexOf("until you empty it") >= 0, c.body)
            c.confirmed()
            tryVerify(() => !list.jobs.running && review.trashed.length === 2)
            compare(review.trashed, ["/fx/s5", "/fx/s4"])
            settings.trashRetentionDays = 30
            c.close()
        }

        function test_the_bar_shows_the_job_and_the_rows_their_state() {
            reports.autoFinish = false
            list.toggleRow("/fx/s2"); list.toggleRow("/fx/s3")
            verify(list.askBulk("make", list.selection))
            tryVerify(() => reports.madeLog.length === 1)
            verify(find(bar, "sessionsBulkProgress").visible)
            compare(find(bar, "sessionsBulkProgressWords").text, "Making reports · 1 of 2")
            // The queued row says so in its Report cell.
            const r2 = list.rowAt(2)
            compare(r2.jobWords, "Report queued")
            compare(find(r2, "sessionsJob").visible, true)
            reports.finishPending()
            tryVerify(() => reports.madeLog.length === 2)
            compare(find(bar, "sessionsBulkProgressWords").text, "Making reports · 2 of 2")
            reports.finishPending()
            tryVerify(() => !list.jobs.running)
            verify(!find(bar, "sessionsBulkProgress").visible)
        }
    }
}
