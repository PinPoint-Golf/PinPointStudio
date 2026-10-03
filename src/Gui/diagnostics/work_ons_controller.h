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

#include "../../Analysis/work_ons.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>

#include <memory>
#include <vector>

class SessionDiagnosticsModel;
namespace pinpoint::analysis { class ICharacteristicPackProvider; }

// WORK ONS — the home screen's list of the faults a golfer keeps producing, and the thing that
// keeps each session's record of them current (docs/design/work_ons_design.md). The context
// property `workOns`.
//
// ONE MECHANISM FOR "END OF SESSION" AND FOR CATCH-UP, because they are the same problem. A
// session's record (<session>/work_ons.json) is STALE whenever what it was derived from has
// moved: there is no record yet (every session recorded before this existed, and any the app
// quit out of), a swing arrived, left or was re-analysed, the pack or norms changed, or the
// selection rule did. refresh() finds the stale sessions and re-derives them; ending a session
// is merely the moment one becomes stale. Nothing has to remember to "close" anything, and a
// library converted, re-analysed or copied in from another machine catches up by being looked at.
//
// The derivation is SessionDiagnosticsModel's — a private, panel-less instance. Its
// activateSession() already reconciles the ledger against the swings on disk (back-fills what
// was never reduced, regrades what went stale), so a record is always derived from a CURRENT
// ledger, and the list here cannot disagree with the panel a golfer opens on the same session.
//
// The home list is aggregateWorkOns() over the records and nothing else. No running totals:
// trashing a session removes its contribution at the next refresh.
//
// THREADING. The folder scan (a stat per swing, on what may be a network share) runs on a
// private single-thread pool and is delivered back queued. Derivation is the model's own worker.
// Sessions are derived one at a time, and never while a session is live (`paused`) — detection
// reads whole swing documents and must not compete with capture.
class WorkOnsController : public QObject
{
    Q_OBJECT

    // <library>/<athlete>, or empty for none. Passed in rather than reached for — main.cpp binds
    // it to the current athlete — so a test or a probe can point it at any folder.
    Q_PROPERTY(QString athleteDir READ athleteDir WRITE setAthleteDir NOTIFY athleteDirChanged)
    // The SAME policy the session panel grades against (AppSettings::diagnosticsGradePolicy).
    Q_PROPERTY(QString gradePolicy READ gradePolicy WRITE setGradePolicy NOTIFY gradePolicyChanged)
    // A session is live. Holds derivation, not the list.
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

    // Active and easing work-ons, most persistent first; every string final.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int clearedCount READ clearedCount NOTIFY itemsChanged)
    // Sessions with a current record / sessions found. They differ while catching up.
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY itemsChanged)
    Q_PROPERTY(int sessionsFound READ sessionsFound NOTIFY itemsChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY updatingChanged)

public:
    explicit WorkOnsController(QObject *parent = nullptr);
    ~WorkOnsController() override;

    QString athleteDir() const { return m_athleteDir; }
    void    setAthleteDir(const QString &dir);
    QString gradePolicy() const { return m_gradePolicy; }
    void    setGradePolicy(const QString &name);
    bool    paused() const { return m_paused; }
    void    setPaused(bool on);

    QVariantList items() const { return m_items; }
    int  clearedCount() const { return m_clearedCount; }
    int  sessionCount() const { return int(m_records.size()); }
    int  sessionsFound() const { return m_sessionsFound; }
    bool updating() const { return m_scanning || m_deriving || !m_queue.isEmpty(); }

    // Rescan the athlete's sessions and re-derive whatever is stale. Cheap when nothing is.
    Q_INVOKABLE void refresh();

    // Test seam, as SessionDiagnosticsModel's: scan and derive inline.
    void setSynchronous(bool on) { m_synchronous = on; }
    bool waitForIdle(int msTimeout = 60000);

    static QString recordPath(const QString &sessionDir);

signals:
    void athleteDirChanged();
    void gradePolicyChanged();
    void pausedChanged();
    void itemsChanged();
    void updatingChanged();

private:
    struct Scanned {
        QString sessionDir;
        QString fingerprint;
        bool    fresh = false;
        qint64  mtimeMs = 0;
        pinpoint::analysis::SessionWorkOns record;   // valid when fresh
    };
    static std::vector<Scanned> scan(const QString &athleteDir, const QString &contentStamp);
    void applyScan(int generation, std::vector<Scanned> rows);

    void deriveNext();
    void onModelSettled();
    void finishDerive();
    void publish();
    SessionDiagnosticsModel *model();

    QString m_athleteDir;
    QString m_gradePolicy = QStringLiteral("standard");
    bool    m_paused = false;
    bool    m_synchronous = false;

    // session dir -> its current record. Rebuilt by every scan, added to by every derivation.
    QHash<QString, pinpoint::analysis::SessionWorkOns> m_records;
    QHash<QString, QString> m_fingerprints;   // session dir -> what the queue will stamp
    QHash<QString, qint64>  m_dirMtime;
    QStringList m_queue;                      // stale session dirs, newest first
    QString     m_derivingDir;
    bool        m_deriving = false;
    bool        m_scanning = false;
    bool        m_rescan   = false;
    int         m_generation = 0;
    int         m_sessionsFound = 0;

    QVariantList m_items;
    int          m_clearedCount = 0;

    SessionDiagnosticsModel *m_model = nullptr;   // created on first need; owned (child)
    std::unique_ptr<pinpoint::analysis::ICharacteristicPackProvider> m_packProv;
    QThreadPool m_pool;
};
