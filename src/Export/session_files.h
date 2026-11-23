/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// SessionFiles — the names of a session folder's own files, spelled once (H-12; refactor step 2.2,
// brief §4.7). Adapters ring (src/Export), beside the repository that reads and writes them
// (session_store.h).
//
//   <session>/
//     session.json            FILE-SESS-01: the session's id, athlete, type, status, start and end
//                             times and origin (DEC-149, DEC-160: IM-S2-1). Written by SessionStore only
//     session_input.json      FILE-SESS-02: the golfer's session-level input (DEC-160: IM-S2-3).
//     diagnostics.json        FILE-SESS-03: the diagnostics cache (the ledger and the session block)
//     diagnostics.json.<tag>  FILE-SESS-12: a dated backup of it (regrade_ledger)
//     diagnostics.json.unreadable-<stamp>   one that could not be read, set aside
//     club_baseline.json      FILE-SESS-04: the frozen club baseline
//     work_ons.json           FILE-SESS-05: the session's work-ons record
//                             The four above are read and written by SessionDiagnosticsFiles only
//                             (session_diagnostics_files.h, step 2.5), and by id through the session
//                             store (step 3.4a, J-42): both over session_diagnostics_io.h's file work
//     .pinpoint-last-swing    FILE-SESS-10: the highest swing number the session has issued (DEC-91)
//     .pinpoint-lock          EXT-12: the advisory lock (FolderLock, src/Core/folder_lock.h)
//     archived.json           FILE-SESS-06: the archive marker — the session's swings are stubs, the rest is
//                             in the archive mirror it names. Written and read by the archive only
//                             (session_archiver.h, behind SessionRepository::archive / restore; step 2.6)
//     report/report_meta.json FILE-SESS-07: the report's derived facts (session_report_io.h, for SessionRepository)
//     report/report_input.json FILE-SESS-13: the report's source facts (session_report_io.h, for SessionRepository)
//     report/<date>_<name>_session-report.pdf   FILE-SESS-08: the report (the report model writes it)
//     trunk_ceremony.json     FILE-SESS-11: the trunk-sensor calibration recording, and
//     trunk_ceremony_<n>.json the earlier ceremonies of a resumed session (TrunkCeremonyRecorder writes them)
//     swing_NNNN/             one folder per shot (SwingPaths composes the name)
//   <Cache>/report-stills/<hash>/   FILE-SESS-09: the report stills, outside the library (session_report_io.h)
//
// NAMES, NOT WRITES, for the report documents and the trunk ceremony (D9: DEC-156; step 2.8b's A4 extension):
// their writers keep writing them (the report until step 5's Session command; the ceremony with Studio/Rig,
// the platform refactor) and call the path functions below. Lint A4 names this file the owner of every name
// above (IM-S2-6: DEC-160), so each is spelled here and nowhere else in src/.
//
// The swing counter's two accessors live here too, because two writers use it until step 3: the
// repository's allocation, and SwingPaths::trashPath, which records the number of a swing that
// leaves its session so it is never issued again.

#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"

#include <QFile>
#include <QString>

// Header-only, like atomic_write.h: SwingPaths (compiled by some 26 test targets) reads the swing counter,
// and a .cpp here would join every one of their link lines.

namespace pinpoint {

class SessionFiles
{
public:
    // "session.json"
    static QString sessionFileName() { return QStringLiteral("session.json"); }
    static QString sessionFile(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + sessionFileName(); }

    // ".pinpoint-last-swing"
    static QString swingHighWaterFileName() { return QStringLiteral(".pinpoint-last-swing"); }
    static QString swingHighWaterFile(const QString &sessionDir)
    {
        return sessionDir + QLatin1Char('/') + swingHighWaterFileName();
    }

    // "session_input.json", "diagnostics.json", "club_baseline.json", "work_ons.json" (step 2.5)
    static QString sessionInputFileName() { return QStringLiteral("session_input.json"); }
    static QString sessionInputFile(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + sessionInputFileName(); }
    static QString diagnosticsFileName() { return QStringLiteral("diagnostics.json"); }
    static QString diagnosticsFile(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + diagnosticsFileName(); }
    // FILE-SESS-12: "diagnostics.json.<tag>"
    static QString diagnosticsBackupFile(const QString &sessionDir, const QString &tag)
    {
        return diagnosticsFile(sessionDir) + QLatin1Char('.') + tag;
    }
    // "diagnostics.json.unreadable-<stamp>": the stem; the caller numbers a second one in the same second.
    static QString diagnosticsAsideStem(const QString &sessionDir, const QString &stamp)
    {
        return diagnosticsFile(sessionDir) + QStringLiteral(".unreadable-") + stamp;
    }
    static QString clubBaselineFileName() { return QStringLiteral("club_baseline.json"); }
    static QString clubBaselineFile(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + clubBaselineFileName(); }
    static QString workOnsFileName() { return QStringLiteral("work_ons.json"); }
    static QString workOnsFile(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + workOnsFileName(); }
    // "archived.json"
    static QString archiveMarkerFileName() { return QStringLiteral("archived.json"); }
    static QString archiveMarker(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + archiveMarkerFileName(); }

    // The report documents (FILE-SESS-07…09, 13; names only, D9): "report", "report_meta.json" (the derived
    // facts), "report_input.json" (the source facts: step 5.6a), the PDF's "_session-report.pdf" ending (the
    // report model puts the session's date and the golfer's name before it), and the stills' cache folder
    // "report-stills". Their file work: session_report_io.h.
    static QString reportFolderName() { return QStringLiteral("report"); }
    static QString reportMetaFileName() { return QStringLiteral("report_meta.json"); }
    static QString reportInputFileName() { return QStringLiteral("report_input.json"); }
    static QString reportPdfSuffix() { return QStringLiteral("_session-report.pdf"); }
    static QString reportStillsFolderName() { return QStringLiteral("report-stills"); }

    // The trunk ceremony (FILE-SESS-11; Studio/Rig's, in the session folder): "trunk_ceremony.json", and
    // "trunk_ceremony_<n>.json" for the earlier one of a resumed session, n from 2.
    static QString trunkCeremonyFileName() { return QStringLiteral("trunk_ceremony.json"); }
    static QString trunkCeremonyAsideFileName(int n) { return QStringLiteral("trunk_ceremony_%1.json").arg(n); }

    // ".pinpoint-lock": FolderLock's own (src/Core/folder_lock.h), named here so the list is complete.
    static QString lockFileName() { return FolderLock::fileName(); }

    // The highest swing number the session has recorded issuing; 0 when the file is missing or
    // unreadable.
    static int recordedSwingHighWater(const QString &sessionDir)
    {
        QFile f(swingHighWaterFile(sessionDir));
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        bool ok = false;
        const int n = QString::fromLatin1(f.read(32)).trimmed().toInt(&ok);
        return ok && n > 0 ? n : 0;
    }

    // Raise the recorded high-water to n; never lowers it (an n at or below it writes nothing and
    // answers ok). Written whole through the one atomic write (R-PERS-2): a crash mid-write leaves the
    // old number, never a truncated file that reads as 0. Into the session's existing folder only:
    // this never makes one.
    static AtomicWriteResult raiseSwingHighWater(const QString &sessionDir, int n)
    {
        if (n <= recordedSwingHighWater(sessionDir))
            return { true, {}, {} };
        return atomicWrite(swingHighWaterFile(sessionDir), QByteArray::number(n),
                           AtomicWriteOptions::intoExistingFolder());
    }
};

} // namespace pinpoint
