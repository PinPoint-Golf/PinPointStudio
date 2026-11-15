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

struct SessionRow {
    QString dir, name, athlete;
    qint64  dateMs = 0;
    qint64  bytes  = 0;
    int     swings = 0;
    bool    archived = false;
};

using pinpoint::SessionArchiver;

// Every session folder of the library that holds a swing, as the session repository reads it (step 2.6):
// its day is its SessionIdentity's — the start time's local day from its session file, or, for a session
// older than step 2, the day its folder's name says (SessionIdentity reads the name, nobody else). A folder
// that cannot be named as a session falls back to its modification time, as before.
QList<SessionRow> scanSessions(const QString &root)
{
    QList<SessionRow> out;
    pinpoint::SessionArchiveRun run;   // this scan's own (a worker thread: never the app's identity map)
    const QDir r(root);
    for (const QString &athlete : r.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir a(r.filePath(athlete));
        for (const QString &session : a.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString dir = a.filePath(session);
            const int swings = int(pinpoint::SwingPaths::swingFolders(dir).size());
            if (swings == 0) continue;
            SessionRow s;
            s.dir = QDir::cleanPath(dir);
            s.name = session;
            s.athlete = athlete;
            s.dateMs   = QFileInfo(dir).lastModified().toMSecsSinceEpoch();
            s.archived = SessionArchiver::isArchived(dir);
            if (const auto e = run.sessions().open(run.openSession(dir))) {
                if (e->identity.localDay().isValid())
                    s.dateMs = e->identity.localDay().startOfDay().toMSecsSinceEpoch();
                s.archived = e->archived;
            }
            s.bytes = SessionArchiver::folderBytes(dir);
            s.swings = swings;
            out << s;
        }
    }
    std::sort(out.begin(), out.end(), [](const SessionRow &x, const SessionRow &y) {
        return x.dateMs != y.dateMs ? x.dateMs > y.dateMs : x.dir > y.dir;
    });
    return out;
}

// The trash entries: <root>/.pinpoint-trash/<athlete>/<stamp>_<name>, each a session or a swing.
QStringList listTrash(const QString &root)
{
    QStringList out;
    const QDir t(root + QStringLiteral("/.pinpoint-trash"));
    for (const QString &athlete : t.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir a(t.filePath(athlete));
        for (const QString &e : a.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
            out << a.filePath(e);
    }
    return out;
}

// When an entry was trashed: the yyyyMMdd-HHmmss stamp SwingPaths::trashPath puts on its name.
QDateTime trashedAt(const QString &entry)
{
    const QDateTime t = QDateTime::fromString(QFileInfo(entry).fileName().left(15),
                                              QStringLiteral("yyyyMMdd-HHmmss"));
    return t.isValid() ? t : QFileInfo(entry).lastModified();
}

bool removeEntry(const QString &p)
{
    const QFileInfo fi(p);
    return fi.isDir() ? QDir(p).removeRecursively() : QFile::remove(p);
}

// THE ONE PURGE (MD-08: DEC-137): the library trash's entries — sessions, swings, and whole athletes
// (step 2.3's `<athlete>/<stamp>_<athlete>`) — trashed before `cutoff` (an invalid cutoff: every entry),
// deleted for good. The only code that ends a trashed thing; "Empty now" and the retention pass both call it.
struct Purged { int removed = 0, failed = 0; };
Purged purgeTrash(const QString &root, const QDateTime &cutoff)
{
    Purged p;
    for (const QString &e : listTrash(root)) {
        if (cutoff.isValid() && !(trashedAt(e) < cutoff)) continue;
        (removeEntry(e) ? p.removed : p.failed)++;
    }
    return p;
}

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
        for (const QString &e : listTrash(root)) {
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
                                 std::function<QString(ArchiveController *, std::shared_ptr<std::atomic_bool>)> job,
                                 const QString &restoredDir)
{
    if (m_busy) return false;
    m_busy = true;
    m_progress = 0.0;
    m_status = what;
    emit changed();

    auto abort = std::make_shared<std::atomic_bool>(false);
    m_abort = abort;
    m_watcher.disconnect();
    connect(&m_watcher, &QFutureWatcher<QString>::finished, this, [this, restoredDir] {
        const QString outcome = m_watcher.result();
        m_busy = false;
        m_progress = 1.0;
        m_status = outcome;
        ppInfo() << "[Archive]" << outcome;
        emit changed();
        if (!restoredDir.isEmpty() && !pinpoint::SessionArchiver::isArchived(restoredDir))
            emit sessionRestored(restoredDir);
        refresh();
    });
    m_watcher.setFuture(QtConcurrent::run([this, job, abort]() -> QString { return job(this, abort); }));
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

// One session archived through its repository, by SessionId (brief §4.12), in a run of its own. The
// repository refuses one its session file says is in progress — here or on another computer.
pinpoint::SessionArchiveResult archiveOne(const QString &sessionDir, const QString &location, bool keepRaw,
                                          const pinpoint::SessionArchiveProgress &progress)
{
    pinpoint::SessionArchiveRun run;
    pinpoint::SessionArchiveOptions opt;
    opt.archiveRoot = location;
    opt.keepRaw     = keepRaw;
    return run.sessions().archive(sessionFor(run, sessionDir), opt, progress);
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
            return QObject::tr("%1 was not archived: %2").arg(name, r.error);
        }
        return QObject::tr("%1 archived: %2 swings, %3 MB freed in the library.")
            .arg(name).arg(r.swings).arg(qint64(r.bytesFreed / (1024 * 1024)));
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
            return QObject::tr("%1 was not restored: %2").arg(name, r.error);
        }
        return QObject::tr("%1 restored: %2 swings are back in the library.").arg(name).arg(r.swings);
    }, sessionDir);
}

void ArchiveController::emptyTrash()
{
    if (!m_settings) return;
    const QString root = m_settings->athleteLibraryPath();
    if (root.isEmpty()) return;
    startJob(tr("Emptying the trash…"), [root](ArchiveController *, std::shared_ptr<std::atomic_bool>) {
        const Purged p = purgeTrash(root, QDateTime());
        return p.failed == 0 ? QObject::tr("Trash emptied: %1 items removed.").arg(p.removed)
                             : QObject::tr("Trash: %1 removed, %2 could not be.").arg(p.removed).arg(p.failed);
    });
}

void ArchiveController::cancel()
{
    if (m_abort) m_abort->store(true);
}

void ArchiveController::runHousekeeping()
{
    if (!m_settings) return;
    const QString root     = m_settings->athleteLibraryPath();
    const QString location = m_settings->archiveLocation();
    const int  retention   = m_settings->trashRetentionDays();
    const int  afterDays   = m_settings->archiveAfterDays();
    const int  floorGb     = m_settings->archiveFloorGb();
    const bool keepRaw     = m_settings->archiveKeepRaw();
    if (root.isEmpty()) return;
    const bool canArchive = !location.isEmpty() && QFileInfo(location).isDir();
    const QString liveDir  = m_liveDir;
    const pinpoint::report::ReportFingerprintFn fp = canArchive ? reportFingerprint() : pinpoint::report::ReportFingerprintFn();
    if (retention <= 0 && (!canArchive || (afterDays <= 0 && floorGb <= 0))) {
        refresh();
        return;   // nothing is switched on: not even a status line
    }

    startJob(tr("Housekeeping…"), [=](ArchiveController *, std::shared_ptr<std::atomic_bool> abort) {
        QStringList done;
        // 1. The trash, past its retention.
        if (retention > 0) {
            const int removed = purgeTrash(root, QDateTime::currentDateTime().addDays(-retention)).removed;
            if (removed) done << QObject::tr("%1 trash items older than %2 days removed").arg(removed).arg(retention);
        }
        if (canArchive && (afterDays > 0 || floorGb > 0)) {
            const auto archiveDir = [&](const QString &dir) {
                return pinpoint::report::keepReportCurrent(dir, fp, [&] {
                    return archiveOne(dir, location, keepRaw, {});
                }).ok;
            };
            // Today's sessions are never touched: one may still be recording.
            const qint64 today = QDate::currentDate().startOfDay().toMSecsSinceEpoch();
            QList<SessionRow> sessions = scanSessions(root);   // newest first
            int archived = 0;
            // 2. By age.
            if (afterDays > 0) {
                const qint64 cutoff = QDate::currentDate().addDays(-afterDays).startOfDay().toMSecsSinceEpoch();
                for (SessionRow &s : sessions) {
                    if (abort->load()) break;
                    if (s.archived || s.dateMs >= today || s.dateMs >= cutoff || s.dir == liveDir) continue;
                    if (archiveDir(s.dir)) { s.archived = true; ++archived; }
                }
            }
            // 3. Below the floor: the oldest first, until there is room again or nothing is left.
            if (floorGb > 0) {
                for (auto it = sessions.rbegin(); it != sessions.rend(); ++it) {
                    if (abort->load()) break;
                    QStorageInfo si(root);
                    if (si.bytesAvailable() >= qint64(floorGb) * 1024 * 1024 * 1024) break;
                    if (it->archived || it->dateMs >= today || it->dir == liveDir) continue;
                    if (archiveDir(it->dir)) { it->archived = true; ++archived; }
                }
            }
            if (archived) done << QObject::tr("%1 sessions archived").arg(archived);
        }
        return done.isEmpty() ? QObject::tr("Housekeeping: nothing to do.")
                              : QObject::tr("Housekeeping: %1.").arg(done.join(QStringLiteral("; ")));
    });
}
