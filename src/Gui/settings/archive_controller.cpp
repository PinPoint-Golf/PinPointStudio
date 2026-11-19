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

#include "archive_controller.h"

#include "../app/app_settings.h"
#include "../../Core/pp_debug.h"
#include "../../Export/session_archiver.h"
#include "../../Export/swing_paths.h"
#include "../report/report_meta.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {

// The library's session folders, as the Archiving tab lists them: SessionArchiveRun::scanLibrary (moved there unchanged
// in refactor step 3.8, so housekeeping's walk and this list are one scan).
using SessionRow = pinpoint::SessionArchiveRun::LibrarySession;
using pinpoint::SessionArchiver;

QList<SessionRow> scanSessions(const QString &root) { return pinpoint::SessionArchiveRun::scanLibrary(root); }

} // namespace

struct ArchiveController::Scan {
    QList<SessionRow> sessions;
    double freeBytes = 0, swingBytes = 0, trashBytes = 0;
    int    swingsMeasured = 0, trashEntries = 0;
};

ArchiveController::ArchiveController(AppSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
}

ArchiveController::~ArchiveController()
{
    if (m_abort) m_abort->store(true);
    m_watcher.disconnect();
    m_scanWatcher.disconnect();
    if (m_watcher.isRunning())     m_watcher.waitForFinished();
    if (m_scanWatcher.isRunning()) m_scanWatcher.waitForFinished();
}

double ArchiveController::hoursLeft() const
{
    if (m_swingBytes <= 0 || m_freeBytes <= 0) return -1.0;
    return m_freeBytes / m_swingBytes / 60.0;   // one swing a minute
}

void ArchiveController::applyScan(const Scan &scan)
{
    m_sessions.clear();
    for (const SessionRow &s : scan.sessions)
        m_sessions << QVariantMap{
            { QStringLiteral("dir"), s.dir }, { QStringLiteral("name"), s.name },
            { QStringLiteral("athlete"), s.athlete }, { QStringLiteral("dateMs"), double(s.dateMs) },
            { QStringLiteral("bytes"), double(s.bytes) }, { QStringLiteral("swings"), s.swings },
            { QStringLiteral("archived"), s.archived } };
    m_freeBytes      = scan.freeBytes;
    m_swingBytes     = scan.swingBytes;
    m_swingsMeasured = scan.swingsMeasured;
    m_trashBytes     = scan.trashBytes;
    m_trashEntries   = scan.trashEntries;
    emit changed();
}

void ArchiveController::refresh()
{
    if (!m_settings || m_scanWatcher.isRunning()) return;
    const QString root = m_settings->athleteLibraryPath();
    if (root.isEmpty()) return;
    auto result = std::make_shared<Scan>();
    m_scanWatcher.disconnect();
    connect(&m_scanWatcher, &QFutureWatcher<void>::finished, this, [this, result] { applyScan(*result); });
    m_scanWatcher.setFuture(QtConcurrent::run([root, result] {
        Scan &s = *result;
        s.sessions = scanSessions(root);
        const QStorageInfo si(root);
        s.freeBytes = si.isValid() ? double(si.bytesAvailable()) : 0.0;
        // What a swing costs HERE: the mean of the most recent swings still in the library, raw
        // frames and all — the settings in force when they were recorded, not a table.
        qint64 bytes = 0;
        for (const SessionRow &row : s.sessions) {
            if (row.archived) continue;
            QStringList swings = pinpoint::SwingPaths::swingFolders(row.dir);
            std::sort(swings.begin(), swings.end());
            std::reverse(swings.begin(), swings.end());
            for (const QString &sw : swings) {
                if (s.swingsMeasured >= 30) break;
                bytes += SessionArchiver::folderBytes(row.dir + QLatin1Char('/') + sw);
                ++s.swingsMeasured;
            }
            if (s.swingsMeasured >= 30) break;
        }
        s.swingBytes = s.swingsMeasured > 0 ? double(bytes) / s.swingsMeasured : 0.0;
        for (const QString &e : pinpoint::SwingPaths::trashEntries(root)) {
            ++s.trashEntries;
            s.trashBytes += QFileInfo(e).isDir() ? double(SessionArchiver::folderBytes(e)) : double(QFileInfo(e).size());
        }
    }));
}

void ArchiveController::setProgress(double p, const QString &status)
{
    m_progress = p;
    m_status = status;
    emit changed();
}

void ArchiveController::setLiveSessionDir(const QString &dir)
{
    m_liveDir = dir.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
}

bool ArchiveController::isLive(const QString &sessionDir) const
{
    return !m_liveDir.isEmpty() && QDir::cleanPath(QFileInfo(sessionDir).absoluteFilePath()) == m_liveDir;
}

pinpoint::report::ReportFingerprintFn ArchiveController::reportFingerprint() const
{
    return m_fingerprint ? m_fingerprint() : pinpoint::report::ReportFingerprintFn();
}

bool ArchiveController::startJob(const QString &what,
                                 std::function<JobOutcome(ArchiveController *, std::shared_ptr<std::atomic_bool>)> job,
                                 const QString &restoredDir)
{
    if (m_busy) return false;
    m_busy = true;
    m_progress = 0.0;
    m_status = what;
    m_failed = false;
    emit changed();

    auto abort = std::make_shared<std::atomic_bool>(false);
    m_abort = abort;
    m_watcher.disconnect();
    connect(&m_watcher, &QFutureWatcher<JobOutcome>::finished, this, [this, restoredDir] {
        const JobOutcome outcome = m_watcher.result();
        m_busy = false;
        m_progress = 1.0;
        m_status = outcome.words;
        m_failed = !outcome.ok;
        ppInfo() << "[Archive]" << outcome.words;
        emit changed();
        if (!restoredDir.isEmpty() && !pinpoint::SessionArchiver::isArchived(restoredDir))
            emit sessionRestored(restoredDir);
        refresh();
    });
    m_watcher.setFuture(QtConcurrent::run([this, job, abort]() -> JobOutcome { return job(this, abort); }));
    return true;
}

namespace {
// A progress callback that reports to the controller on its own thread and honours cancel.
pinpoint::SessionArchiveProgress reporter(std::shared_ptr<std::atomic_bool> abort,
                                             const QString &verb, const QString &name,
                                             std::function<void(double, QString)> post)
{
    return [abort, verb, name, post](int done, int total) {
        post(total > 0 ? double(done) / total : 0.0,
             QStringLiteral("%1 %2 — %3 of %4 files").arg(verb, name).arg(done).arg(total));
        return !abort->load();
    };
}

// THE EDGE (DEC-90): the folder a row carries, to its SessionId in the run's own repository, once.
pinpoint::SessionId sessionFor(pinpoint::SessionArchiveRun &run, const QString &sessionDir)
{
    const pinpoint::SessionId id = run.openSession(sessionDir);
    if (id.isNull())
        ppWarn() << "[Archive] the session of" << sessionDir << "cannot be named (no session file, and no"
                 << "athlete recorded for it)";
    return id;
}

// One session archived through its repository, by SessionId, in a run of its own (SessionArchiveRun::archiveInOwnRun:
// moved there unchanged in refactor step 3.8). The repository refuses one its session file says is in progress.
pinpoint::SessionArchiveResult archiveOne(const QString &sessionDir, const QString &location, bool keepRaw,
                                          const pinpoint::SessionArchiveProgress &progress)
{
    pinpoint::SessionArchiveOptions opt;
    opt.archiveRoot = location;
    opt.keepRaw     = keepRaw;
    return pinpoint::SessionArchiveRun::archiveInOwnRun(sessionDir, opt, progress);
}
} // namespace

bool ArchiveController::archiveSession(const QString &sessionDir)
{
    if (!m_settings) return false;
    // The running session is still being written: its swings are not to be thinned under it.
    if (isLive(sessionDir)) {
        ppWarn() << "[Archive] refused: the live session cannot be archived:" << sessionDir;
        return false;
    }
    const QString location = m_settings->archiveLocation();
    const bool keepRaw = m_settings->archiveKeepRaw();
    const QString name = QFileInfo(sessionDir).fileName();
    const pinpoint::report::ReportFingerprintFn fp = reportFingerprint();
    return startJob(tr("Archiving %1…").arg(name), [sessionDir, location, keepRaw, name, fp](ArchiveController *self,
                                                                                    std::shared_ptr<std::atomic_bool> abort) {
        QPointer<ArchiveController> p(self);
        auto post = [p](double v, QString s) {
            QMetaObject::invokeMethod(p, [p, v, s] { if (p) p->setProgress(v, s); }, Qt::QueuedConnection);
        };
        const auto r = pinpoint::report::keepReportCurrent(sessionDir, fp, [&] {
            return archiveOne(sessionDir, location, keepRaw,
                              reporter(abort, QStringLiteral("Archiving"), name, post));
        });
        if (!r.ok) {
            ppWarn() << "[Archive] archiving" << sessionDir << "failed:" << r.error;
            return JobOutcome(QObject::tr("%1 was not archived: %2").arg(name, r.error), false);
        }
        return JobOutcome(QObject::tr("%1 archived: %2 swings, %3 MB freed in the library.")
                              .arg(name).arg(r.swings).arg(qint64(r.bytesFreed / (1024 * 1024))));
    });
}

void ArchiveController::restoreSession(const QString &sessionDir)
{
    const QString name = QFileInfo(sessionDir).fileName();
    const pinpoint::report::ReportFingerprintFn fp = reportFingerprint();
    startJob(tr("Restoring %1…").arg(name), [sessionDir, name, fp](ArchiveController *self,
                                                               std::shared_ptr<std::atomic_bool> abort) {
        QPointer<ArchiveController> p(self);
        auto post = [p](double v, QString s) {
            QMetaObject::invokeMethod(p, [p, v, s] { if (p) p->setProgress(v, s); }, Qt::QueuedConnection);
        };
        const auto r = pinpoint::report::keepReportCurrent(sessionDir, fp, [&] {
            pinpoint::SessionArchiveRun run;   // this job's own (a worker thread)
            return run.sessions().restore(sessionFor(run, sessionDir),
                                          reporter(abort, QStringLiteral("Restoring"), name, post));
        });
        if (!r.ok) {
            ppWarn() << "[Archive] restoring" << sessionDir << "failed:" << r.error;
            return JobOutcome(QObject::tr("%1 was not restored: %2").arg(name, r.error), false);
        }
        return JobOutcome(QObject::tr("%1 restored: %2 swings are back in the library.").arg(name).arg(r.swings));
    }, sessionDir);
}

void ArchiveController::emptyTrash()
{
    if (!m_settings) return;
    const QString root = m_settings->athleteLibraryPath();
    if (root.isEmpty()) return;
    if (!m_housekeeping) {
        ppWarn() << "[Archive] no housekeeping: the trash was not emptied";
        return;
    }
    // THE ONE PURGE (DEC-80, DEC-137): housekeeping's, on the worker (a copy: it keeps no state).
    const pinpoint::Housekeeping housekeeping = *m_housekeeping;
    startJob(tr("Emptying the trash…"), [root, housekeeping](ArchiveController *, std::shared_ptr<std::atomic_bool>) {
        const pinpoint::TrashPurge p = housekeeping.purge(root);
        return p.failed == 0 ? QObject::tr("Trash emptied: %1 items removed.").arg(p.removed)
                             : QObject::tr("Trash: %1 removed, %2 could not be.").arg(p.removed).arg(p.failed);
    });
}

void ArchiveController::cancel()
{
    if (m_abort) m_abort->store(true);
}

ArchiveController::ArchiveJobs ArchiveController::archiveJobs()
{
    return [](const QString &sessionDir, const pinpoint::SessionArchiveOptions &options,
              const pinpoint::SessionArchiveProgress &progress) {
        return pinpoint::SessionArchiveRun::archiveInOwnRun(sessionDir, options, progress);
    };
}

void ArchiveController::stopJobs()
{
    if (m_abort) m_abort->store(true);
    if (m_watcher.isRunning()) m_watcher.waitForFinished();
}

void ArchiveController::runHousekeeping()
{
    if (!m_settings) return;
    pinpoint::Housekeeping::Plan plan;
    plan.libraryRoot        = m_settings->athleteLibraryPath();
    plan.trashRetentionDays = m_settings->trashRetentionDays();
    plan.archiveAfterDays   = m_settings->archiveAfterDays();
    plan.archiveFloorGb     = m_settings->archiveFloorGb();
    plan.keepRaw            = m_settings->archiveKeepRaw();
    plan.liveFolder         = m_liveDir;
    // An archive location that is not a folder here archives nothing (as before).
    const QString location = m_settings->archiveLocation();
    plan.archiveLocation = !location.isEmpty() && QFileInfo(location).isDir() ? location : QString();
    if (plan.libraryRoot.isEmpty()) return;
    if (!plan.anythingOn()) {
        refresh();
        return;   // nothing is switched on: not even a status line
    }
    if (!m_housekeeping) {
        ppWarn() << "[Archive] no housekeeping: the startup pass did not run";
        refresh();
        return;
    }
    const pinpoint::report::ReportFingerprintFn fp = plan.canArchive() ? reportFingerprint() : pinpoint::report::ReportFingerprintFn();
    const pinpoint::Housekeeping housekeeping = *m_housekeeping;   // a copy for the worker: it keeps no state
    const QString root = plan.libraryRoot;
    plan.freeBytes = [root] { return QStorageInfo(root).bytesAvailable(); };
    // Today's walk of the library and today's archive of a folder no Athlete holds (Mark's N1), both this tab's.
    pinpoint::LibraryWalk walk;
    walk.scan = [](const QString &libraryRoot) {
        QList<pinpoint::LibraryWalk::Folder> out;
        for (const SessionRow &s : pinpoint::SessionArchiveRun::scanLibrary(libraryRoot, /*withBytes=*/false))
            out.append({ s.dir, s.dateMs, s.archived });
        return out;
    };
    walk.archiveUnheld = [](const QString &folder, const pinpoint::SessionArchiveOptions &options) {
        return pinpoint::SessionArchiveRun::archiveInOwnRun(folder, options);
    };

    startJob(tr("Housekeeping…"), [plan, housekeeping, fp, walk](ArchiveController *self, std::shared_ptr<std::atomic_bool> abort) {
        // Each archive keeps a report that was current, current (S-D6).
        const pinpoint::Housekeeping::ArchiveWrap wrap =
            [fp](const QString &dir, const std::function<pinpoint::SessionArchiveResult()> &archive) {
                return pinpoint::report::keepReportCurrent(dir, fp, archive);
            };
        // The Athletes and their Sessions are reached on this controller's (the GUI) thread; the archives run here.
        const pinpoint::Housekeeping::Report r = housekeeping.run(plan, walk, self, abort.get(), wrap);
        QStringList done;
        if (r.trashRemoved)
            done << QObject::tr("%1 trash items older than %2 days removed").arg(r.trashRemoved).arg(plan.trashRetentionDays);
        if (r.archived)
            done << QObject::tr("%1 sessions archived").arg(r.archived);
        return done.isEmpty() ? QObject::tr("Housekeeping: nothing to do.")
                              : QObject::tr("Housekeeping: %1.").arg(done.join(QStringLiteral("; ")));
    });
}
