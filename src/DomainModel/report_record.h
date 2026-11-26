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

#pragma once

// THE SESSION REPORT'S TWO RECORDS, AS VALUES (refactor step 5.6a; governance/artefacts/information_model.md FILE-SESS-07,
// FILE-SESS-13). The report's metadata is split in two files:
//
//   ReportInput   FILE-SESS-13, <session>/report/report_input.json: the report's SOURCE facts — the coach's note, the
//                 send log, the coach's options for a make and the make history. No rebuild can recover them. Written
//                 only by the Session's commands for them (Session::recordReportMake, recordReportSend).
//   ReportRecord  FILE-SESS-07, <session>/report/report_meta.json: the report's DERIVED facts — the fingerprint and the
//                 PDF's name, the latest prepared time, the PDF's SHA-256 and size, and whether photos went in. A cache
//                 the report command (Session::saveReport) rebuilds at each make; that rebuild never writes or deletes
//                 FILE-SESS-13.
//
// An older report_meta.json held both halves. SessionRepository reads that shape (ReportRead::legacy) and the Session
// migrates it when it is first opened (Session::migrateReport): FILE-SESS-13 first, read back, then FILE-SESS-07 as
// derived only. The file work is src/Stores/session_report_io.h. Values only: no file is touched here.
//
// Domain model ring (src/DomainModel).

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace pinpoint {

// One entry of the send log. Never an address: the session folder lives on the share.
struct ReportSend {
    qint64  ms = 0;
    QString method;   // "smtp" | "mail-app" | "copied" | "revealed"
    QString sha256;   // of the PDF that went
};

// FILE-SESS-13: the source facts.
struct ReportInput {
    QString             note;              // the coach's note, as typed at the latest make
    QVariantMap         options;           // the coach's options for the latest make
    QVector<qint64>     preparedHistory;   // every make, oldest first
    QVector<ReportSend> sends;

    bool operator==(const ReportInput &o) const
    {
        if (note != o.note || options != o.options || preparedHistory != o.preparedHistory || sends.size() != o.sends.size())
            return false;
        for (int i = 0; i < sends.size(); ++i)
            if (sends[i].ms != o.sends[i].ms || sends[i].method != o.sends[i].method || sends[i].sha256 != o.sends[i].sha256)
                return false;
        return true;
    }
};

// FILE-SESS-07: the derived facts.
struct ReportRecord {
    QString fingerprint;
    QString pdfName;            // file name inside <session>/report/
    qint64  preparedMs = 0;     // the latest make
    QString sha256;             // of the PDF (the repository computes it from the PDF it names)
    qint64  sizeBytes = 0;      // likewise
    bool    photosIncluded = false;
};

// Both files as read (SessionRepository::readReport). R-PERS-3: a file of a newer schema is read for what this build
// knows and never written; an older report_meta.json (it holds the source keys) is read whole, its source facts
// presented as `input` while report_input.json is not there.
struct ReportRead {
    enum class State { Missing, Read, Newer, Unparseable };
    State        inputState  = State::Missing;   // report_input.json
    State        recordState = State::Missing;   // report_meta.json
    ReportInput  input;
    ReportRecord record;
    bool         legacy = false;       // report_meta.json is the older shape (holds the note, options, history or sends)
    bool         pdfPresent = false;   // the PDF the record names is on disk
    QString      error;

    bool hasInput()  const { return inputState == State::Read || inputState == State::Newer || (legacy && inputState == State::Missing); }
    bool hasRecord() const { return recordState == State::Read || recordState == State::Newer; }
    bool any()       const { return hasInput() || hasRecord(); }
    // A newer file is never written by this build, nor an unparseable report_input.json (the source facts may be
    // recoverable by hand); an unparseable report_meta.json is a cache with nothing readable in it.
    bool inputWritable()  const { return inputState == State::Missing || inputState == State::Read; }
    bool recordWritable() const { return recordState != State::Newer; }
};

// THE "CURRENT" RULE (session_report_design.md §8.2): a report is current only when its PDF is still there and was
// made from what the session holds now. An archive's restamp (SessionArchiveRun::keepReportCurrent) asks this;
// report_meta.h's reportState states the same rule over the report's flat view (and says None / Stale apart).
inline bool reportIsCurrent(const ReportRead &r, const QString &currentFingerprint)
{
    return r.hasRecord() && !r.record.pdfName.isEmpty() && r.pdfPresent && r.record.fingerprint == currentFingerprint;
}

// THE RESTAMP'S RULE (sessions_screen_redesign.md S-D6; refactor step 5.6b): after a rewrite that changes how the swings
// are stored and not what they say (an archive, a restore), the record of a report that was current BEFORE it, re-stamped
// with the fingerprint the session has NOW — FILE-SESS-07's fingerprint only, every other derived fact kept, never
// FILE-SESS-13. None when there is no record, it cannot be written (a newer file), or the fingerprint did not move. The one
// rule for both of its callers: the Session's archive and restore commands (Session::archiveLanded, restoreLanded) and,
// for a folder no Athlete holds, the run's own repository (session_report_io.h keepCurrent).
inline std::optional<ReportRecord> restampedRecord(const ReportRead &now, const QString &fingerprint)
{
    if (!now.hasRecord() || !now.recordWritable() || now.record.fingerprint == fingerprint)
        return std::nullopt;
    ReportRecord rec = now.record;
    rec.fingerprint = fingerprint;
    return rec;
}

// ONE MAKE, AS THE REPORT COMMAND SAVES IT (Session::makeReport; refactor step 5.6b; FILE-SESS-07, 08, 13). The print
// host writes the PDF to `printedPdf`, outside the library (Session::reportPrintTarget); the command moves it into the
// session's report folder as `record.pdfName` (FILE-SESS-08), then records the make (FILE-SESS-13: the note, the options,
// the make in the history) and its derived facts (FILE-SESS-07). The stills (FILE-SESS-09) were saved during the make
// through the Session's still writer.
struct ReportMake {
    QString      note;
    QVariantMap  options;
    qint64       preparedMs = 0;
    QString      printedPdf;
    ReportRecord record;
};

// A session's report fingerprint as the report computes it now (SessionReportModel::fingerprintFunction): its inputs
// copied on the report's thread, so the function touches only the file system and an archive worker may call it.
using ReportFingerprintFn = std::function<QString(const QString &sessionDir)>;

// A STILL'S WRITE (FILE-SESS-09), handed out by the Session for one make (Session::reportStillsWriter): writes one
// still's bytes into that make's stills folder under `fileName`, making the folder. Safe from any thread (file work only,
// no domain object). Returns the path written, or "" with `why` set.
using ReportStillWriter = std::function<QString(const QString &fileName, const QByteArray &bytes, QString *why)>;

} // namespace pinpoint
