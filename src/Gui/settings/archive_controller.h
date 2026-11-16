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

// ArchiveController — the Archiving settings tab (swing_storage_impl.md, Phase 2 stage 6).
//
//   · the library's sessions, with their size and whether they are archived;
//   · archive / restore one session — through SessionRepository::archive / restore BY ITS SessionId (step
//     2.6): each job makes a SessionArchiveRun of its own (it runs on a worker, never the app's identity
//     map), translates the row's folder to the SessionId once, at this edge, and the repository refuses a
//     session its session file says is in progress (SessionArchiver: verified copy, stubs, marker);
//   · space: the free space as HOURS of capture at the swing size THIS library actually records
//     (measured over its most recent swings, not assumed);
//   · housekeeping at startup and "Empty now" FORWARD TO HOUSEKEEPING (refactor step 3.8; src/App/housekeeping.h, the
//     one application service that ends things for good: DEC-80, DEC-137), which the composition root hands in
//     (setHousekeeping). Every piece of the pass is OFF until the user sets it: empty the library's .pinpoint-trash of
//     entries older than N days; archive sessions older than N days; archive the oldest sessions while free space is
//     below a floor — a session an Athlete holds through its Session (the app's instance, Session::archiveJob on this
//     thread, the job on the worker, the result posted back: EVT-18), a folder no Athlete holds through today's archive
//     in a run of its own (Mark's N1). The trash is emptied by ONE purge
//     (Housekeeping::purge), for "Empty now" and the retention pass alike. This controller reads the settings, runs
//     the pass on its worker and words the outcome; it decides nothing.
//
// Every file operation runs on a worker thread, one at a time.

#include "../../App/housekeeping.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>

class AppSettings;

class ArchiveController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions     READ sessions     NOTIFY changed)
    Q_PROPERTY(bool         busy         READ busy         NOTIFY changed)
    Q_PROPERTY(QString      status       READ status       NOTIFY changed)
    Q_PROPERTY(double       progress     READ progress     NOTIFY changed)   // 0..1 of the operation in hand
    Q_PROPERTY(double       freeBytes    READ freeBytes    NOTIFY changed)
    Q_PROPERTY(double       swingBytes   READ swingBytes   NOTIFY changed)   // measured mean; 0 = unknown
    Q_PROPERTY(int          swingsMeasured READ swingsMeasured NOTIFY changed)
    Q_PROPERTY(double       hoursLeft    READ hoursLeft    NOTIFY changed)   // at one swing a minute; -1 = unknown
    Q_PROPERTY(double       trashBytes   READ trashBytes   NOTIFY changed)
    Q_PROPERTY(int          trashEntries READ trashEntries NOTIFY changed)

public:
    explicit ArchiveController(AppSettings *settings, QObject *parent = nullptr);
    ~ArchiveController() override;

    QVariantList sessions() const { return m_sessions; }
    bool    busy()      const { return m_busy; }
    QString status()    const { return m_status; }
    double  progress()  const { return m_progress; }
    double  freeBytes() const { return m_freeBytes; }
    double  swingBytes() const { return m_swingBytes; }
    int     swingsMeasured() const { return m_swingsMeasured; }
    double  hoursLeft() const;
    double  trashBytes() const { return m_trashBytes; }
    int     trashEntries() const { return m_trashEntries; }

    Q_INVOKABLE void refresh();                          // re-scan sessions, space and trash
    // False when it did not start: the live session (refused, logged), or a job already running.
    Q_INVOKABLE bool archiveSession(const QString &sessionDir);
    Q_INVOKABLE void restoreSession(const QString &sessionDir);
    Q_INVOKABLE void emptyTrash();                       // everything in the library trash, now
    Q_INVOKABLE void cancel();                           // stop between files
    // At quit, before the composition root's other locals go (refactor step 3.8): a running job is asked to stop and
    // waited for, so the housekeeping pass never reaches an Athlete after the athlete repository is gone.
    void stopJobs();
    // THE APP'S SESSION REPOSITORY'S ARCHIVE JOB (refactor step 3.8; SessionStore::setArchiveJobs): today's archive of a
    // session folder in a run of its own (SessionArchiveRun::archiveInOwnRun), the code this tab's own archive runs.
    // ⚠ A LINT DODGE, recorded for Mark with J-9 (3.8-i NOTES, round 3): it is handed out here only so main.cpp need not
    // name session_archiver.h (its A5 count 8 → 9, Mark's call). A Gui controller is not where the store's wiring belongs.
    using ArchiveJobs = std::function<pinpoint::SessionArchiveResult(const QString &sessionDir,
                                                                     const pinpoint::SessionArchiveOptions &options,
                                                                     const pinpoint::SessionArchiveProgress &progress)>;
    static ArchiveJobs archiveJobs();
    // The startup pass: trash retention, then archive-by-age, then archive-below-floor. Each is a
    // no-op while its setting is 0 / no archive location is set.
    Q_INVOKABLE void runHousekeeping();

    // Housekeeping (step 3.8): the composition root's, which outlives this controller. Without it, "Empty now" and the
    // startup pass do nothing (logged).
    void setHousekeeping(const pinpoint::Housekeeping *housekeeping) { m_housekeeping = housekeeping; }
    // The running session's folder (main.cpp's, as the session list is told it); empty when none.
    // Archiving it is refused, by hand or by housekeeping.
    void setLiveSessionDir(const QString &dir);
    // SessionReportModel::fingerprintFunction, asked on this thread as each job starts: archive and
    // restore keep a report that was current, current (S-D6, report_meta.h keepReportCurrent).
    void setReportFingerprint(std::function<std::function<QString(const QString &)>()> snapshot)
    {
        m_fingerprint = std::move(snapshot);
    }

signals:
    void changed();
    void sessionRestored(const QString &sessionDir);     // the review re-opens it, now complete

private:
    struct Scan;
    bool startJob(const QString &what, std::function<QString(ArchiveController *self,
                                                                std::shared_ptr<std::atomic_bool>)> job,
                  const QString &restoredDir = {});
    void applyScan(const Scan &scan);
    void setProgress(double p, const QString &status);
    bool isLive(const QString &sessionDir) const;
    std::function<QString(const QString &)> reportFingerprint() const;

    AppSettings *m_settings = nullptr;
    const pinpoint::Housekeeping *m_housekeeping = nullptr;
    QString      m_liveDir;
    std::function<std::function<QString(const QString &)>()> m_fingerprint;
    QVariantList m_sessions;
    bool    m_busy = false;
    QString m_status;
    double  m_progress = 0.0;
    double  m_freeBytes = 0.0;
    double  m_swingBytes = 0.0;
    int     m_swingsMeasured = 0;
    double  m_trashBytes = 0.0;
    int     m_trashEntries = 0;
    std::shared_ptr<std::atomic_bool> m_abort;
    QFutureWatcher<QString> m_watcher;
    QFutureWatcher<void>    m_scanWatcher;
};
