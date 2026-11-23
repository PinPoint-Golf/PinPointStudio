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

// Housekeeping — THE ONE APPLICATION SERVICE THAT ENDS THINGS FOR GOOD (refactor step 3.8; build/refactor/step-3/BRIEF.md
// §4.11; docs/developer/information_model.md §5.4, DEC-80, DEC-137, DEC-167, DEC-175, DEC-188). Application ring
// (src/App: DEC-11); it includes no repository.
//
//   purge(root, cutoff)   THE ONE PURGE (MD-08: DEC-80, DEC-137): every entry of the library's own trash (FILE-LIB-01)
//                 trashed before `cutoff` (invalid: every entry) deleted for good — sessions, swings and whole athletes
//                 alike. "Empty now" and the retention pass both call it. Carried out in Export (SwingPaths::purgeTrash)
//                 through the composition root's callable (D18 (c): DEC-167). What the app moved to the OS trash is the
//                 OS's: the OS emptying it is a permanent delete OUTSIDE the app (today's trash behaviour kept: Mark,
//                 15 Nov 2026; DEC-175)
//   run(plan, walk, owner)   the housekeeping pass, every part of it OFF until its setting is set, ON A WORKER: the trash
//                 past SET-10's retention (the purge); then, over today's walk of the library (newest first; `walk.scan`),
//                 the sessions older than SET-11's days archived; then — while the library's volume has less free than
//                 SET-12 — the oldest sessions archived until it has; into SET-13, keeping raw frames per SET-14 (off: a
//                 permanent delete of raw frames, said by the setting's subtitle: DEC-137, J-48). Never archived: today's
//                 sessions (one may still be recording), the live one, one already archived, one in progress.
//                 HOW EACH IS ARCHIVED. A session folder one of the library's Athletes holds is archived through ITS
//                 Session, the app's one instance (DEC-190, LIVE-2), reached through the composition root's athlete
//                 callables (DEC-167) on `owner`'s thread (the GUI thread in the app: P8): Session::archiveJob there, the
//                 job (values only) here on the worker, and the result posted back to the Session, which emits EVT-18
//                 (R-CON-3). A folder no Athlete holds — the no-golfer `unknown` folder, an athlete this machine does not
//                 list — keeps today's path (Mark's N1): `walk.archiveUnheld`, the repository's archive in a run of its
//                 own. Both kinds are in the one date order, so SET-12's "oldest first" is the whole library's
//   libraryBytes(root)   the library's size for the settings page (DEC-188): every file under it, hidden ones included; -1
//                 when aborted part-way. librarySize() hands the same door out, to keep and call from any thread
//
// It keeps no state: a copy is the same service, so a worker job may carry one. In the app the pass and the purge run on
// the Archiving tab's worker (ArchiveController forwards to this, and hands in the walk: today's scan and today's
// archive of a folder, which stay its own).

#include "app.h"   // AthleteCallables; Athlete, Session (SessionArchiveOptions / Result / Job)

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

class QObject;

namespace pinpoint {

// What the one purge did.
struct TrashPurge {
    int removed = 0;
    int failed  = 0;
};

// The composition root's callables (D18 (c); DEC-167). `athletes` are the app's own, over its one athlete repository
// (listAthletes and loadAthlete are used, on the athletes' thread only). The purge and the size are carried out in Export
// and are safe from any thread; each takes the library root it acts on (SET-03, read by the caller).
struct HousekeepingDoors {
    AthleteCallables                                                                  athletes;
    std::function<TrashPurge(const QString &libraryRoot, const QDateTime &cutoff)>    purgeTrash;   // THE ONE PURGE
    std::function<qint64(const QString &libraryRoot, const std::atomic_bool *abort)>  libraryBytes;
};

// Today's walk of the library, handed in by the caller (the Archiving tab's, which owns it): its session folders, and the
// archive of a folder no Athlete holds (N1). Both safe on the worker.
struct LibraryWalk {
    struct Folder {
        QString folder;          // cleaned
        qint64  dateMs = 0;      // its day's start (SessionIdentity's), else the folder's modification time
        bool    archived = false;
    };
    std::function<QList<Folder>(const QString &libraryRoot)>                                   scan;   // newest first
    std::function<SessionArchiveResult(const QString &folder, const SessionArchiveOptions &)>  archiveUnheld;
};

class Housekeeping
{
public:
    explicit Housekeeping(HousekeepingDoors doors);

    // THE ONE PURGE (header).
    TrashPurge purge(const QString &libraryRoot, const QDateTime &cutoff = QDateTime()) const;

    // The library's size (DEC-188); -1 when `abort` stopped it, or with no door.
    qint64 libraryBytes(const QString &libraryRoot, const std::atomic_bool *abort = nullptr) const;
    using LibrarySizer = std::function<qint64(const QString &libraryRoot, const std::atomic_bool *abort)>;
    LibrarySizer librarySize() const { return m_doors.libraryBytes; }

    // The pass's settings, read by the caller (SET-10…SET-14) and the facts of the moment.
    struct Plan {
        QString   libraryRoot;            // SET-03
        int       trashRetentionDays = 0; // SET-10: 0 = never purged by age
        int       archiveAfterDays   = 0; // SET-11: 0 = never archived by age
        int       archiveFloorGb     = 0; // SET-12: 0 = never archived for space
        QString   archiveLocation;        // SET-13: an existing folder (the caller checks), else nothing is archived
        bool      keepRaw = true;         // SET-14
        QString   liveFolder;             // the running session's folder (cleaned), never archived; empty when none
        QDate     today;                  // invalid: QDate::currentDate()
        QDateTime now;                    // invalid: QDateTime::currentDateTime()
        std::function<qint64()> freeBytes;   // the library volume's bytes available now (SET-12's rule)
        // The report's fingerprint now (SessionReportModel::fingerprintFunction), so a report current before a held
        // session's archive stays current: given to its Session's archiveJob, whose landing re-stamps it (S-D6; refactor
        // 5.6b, option a). A folder no Athlete holds keeps the caller's `wrap` (run, below).
        ReportFingerprintFn     keepReport;

        bool canArchive() const { return !libraryRoot.isEmpty() && !archiveLocation.isEmpty()
                                         && (archiveAfterDays > 0 || archiveFloorGb > 0); }
        // Nothing switched on: the pass does nothing at all (not even a status line).
        bool anythingOn() const { return !libraryRoot.isEmpty() && (trashRetentionDays > 0 || canArchive()); }
    };

    // Each archive of a folder NO ATHLETE HOLDS, wrapped by the caller on the worker (the app keeps a report that was
    // current, current: SessionArchiveRun's keepReportCurrent, S-D6; a held session's is its Session's: Plan::keepReport).
    // `archive` runs the archive and returns its result.
    using ArchiveWrap = std::function<SessionArchiveResult(const QString &sessionFolder,
                                                           const std::function<SessionArchiveResult()> &archive)>;

    struct Report {
        int         trashRemoved   = 0;   // purged past the retention
        int         trashFailed    = 0;
        int         archived       = 0;   // by age and for space together, both kinds
        int         archivedUnheld = 0;   // …of which folders no Athlete holds (N1's path)
        int         archiveFailed  = 0;   // refused or failed (logged, as today)
        int         inProgress     = 0;   // left alone: still in progress (a crash may have left one)
        bool        stopped        = false;   // `abort` ended it early
        QStringList why;                  // one line per failure
    };

    // ONE SESSION ARCHIVED OR RESTORED BY HAND (the Archiving tab; refactor step 5.6b): as the pass archives one — through
    // its held Session (Session::archiveJob or restoreJob on `owner`'s thread, the job on the caller's thread with
    // `progress`, the result landed on the Session: EVT-18 for an archive), a report current before it kept current by
    // that Session's own command (`keepReport`, S-D6). A folder no Athlete holds — rare since step 5.1 — runs `unheld`, the
    // caller's own path (the run's repository, its restamp wrapped by the caller). Call it off `owner`'s thread (a worker),
    // or on it (a tool, a test: every step in line).
    using UnheldAct = std::function<SessionArchiveResult()>;
    SessionArchiveResult archiveSession(const QString &folder, const SessionArchiveOptions &options, QObject *owner,
                                        const std::atomic_bool *abort, const SessionArchiveProgress &progress,
                                        const ReportFingerprintFn &keepReport, const UnheldAct &unheld) const;
    SessionArchiveResult restoreSession(const QString &folder, QObject *owner, const std::atomic_bool *abort,
                                        const SessionArchiveProgress &progress, const ReportFingerprintFn &keepReport,
                                        const UnheldAct &unheld) const;

    // The pass (header). `owner`: an object on the athletes' thread (the GUI thread in the app), through which the pass
    // reaches the Athletes and their Sessions; the pass waits for each such step, giving up when `abort` is set. Called on
    // `owner`'s own thread (a tool, a test), every step runs in line. `abort` is asked between sessions.
    Report run(const Plan &plan, const LibraryWalk &walk, QObject *owner, const std::atomic_bool *abort = nullptr,
               const ArchiveWrap &wrap = {}) const;

private:
    SessionArchiveResult oneSession(const QString &folder, QObject *owner, const std::atomic_bool *abort,
                                    const SessionArchiveProgress &progress, const UnheldAct &unheld,
                                    const std::function<SessionArchiveJob(Session *)> &ask, bool restore) const;

    HousekeepingDoors m_doors;
};

} // namespace pinpoint
