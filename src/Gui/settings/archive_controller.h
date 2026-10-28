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
//   · archive / restore one session (SessionArchiver: verified copy, stubs, marker);
//   · space: the free space as HOURS of capture at the swing size THIS library actually records
//     (measured over its most recent swings, not assumed);
//   · housekeeping at startup, every piece of it OFF until the user sets it: empty the library's
//     .pinpoint-trash of entries older than N days; archive sessions older than N days; archive
//     the oldest sessions while free space is below a floor.
//
// Every file operation runs on a worker thread, one at a time.

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
    // The startup pass: trash retention, then archive-by-age, then archive-below-floor. Each is a
    // no-op while its setting is 0 / no archive location is set.
    Q_INVOKABLE void runHousekeeping();

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
