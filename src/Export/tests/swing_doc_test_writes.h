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

// TEST HELPER: the swing-document writes the tests make, in the call shapes they made them in
// before step 1.4 (a folder; translated to its ShotRef here, step 2.4), through THE ONE DOOR (RecordingRepository, implemented by
// SwingStoreRecordingRepository). SwingDocWriter's writes are private now; a test that writes a
// fixture or exercises a write goes through the same repository the app does, so it tests what the
// app runs. Each is a thin forward: the test's expectations are the repository's behaviour.
//
//   writeSwingJson         → create (the first write; the header, the analysis, the review seed)
//   updateReview           → editReview { rating, note, club }
//   updateShotAnnotations  → editReview { excluded, clubOverride }
//   updateLaunchMonitor    → completeCapture (a reading, maybe a followed club)
//   refreshLaunchMonitorRows → writeAnalysis with no analysis (dryRun honoured)
//   updateStreamOrigin     → completeCapture (a stream arrival)
//   writeDeviceOnlySwing   → create (a launch-monitor-only shot)

#include "../../DomainModel/recording_repository.h"
#include "../../DomainModel/session_repository.h"
#include "../swing_store_repository.h"

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVector>

namespace pinpoint::testdoc {

// THE TESTS' SESSION REPOSITORY (step 2.4): the swing door is keyed by ShotRef and resolves a shot's
// folder through a SessionRepository. A test's swing is a temp folder of any name, in no session, so
// this one makes ANY folder a shot: the first time a folder is named it gets a ShotRef of its own (a
// fresh session id, swing 1), and that ref resolves back to it — exactly the translate-once-at-the-edge
// contract the app's SessionStore keeps for real swing folders (whose test is session_repository_test
// and recording_repository_test's "a legacy ShotRef resolves"). Every other method is inert.
class FolderShots final : public SessionRepository
{
public:
    SessionStartResult start(const SessionOwner &, const QString &, const QString &) override { return {}; }
    SessionId   todaysSession(const SessionOwner &, const QString &) override { return {}; }
    WriteResult resume(const SessionId &, const QString &) override { return {}; }
    WriteResult end(const SessionId &, bool) override { return {}; }
    SessionStartResult restartInLibrary(const SessionId &, const SessionOwner &, const QString &, QString *) override
    {
        return {};
    }
    QList<SessionIndexEntry>         enumerate(const SessionOwner &) override { return {}; }
    std::optional<SessionIndexEntry> open(const SessionId &) const override { return std::nullopt; }
    SessionId       openFolder(const QString &, const AthleteId &) override { return {}; }
    SwingAllocation allocateSwing(const SessionId &) override { return {}; }
    QList<ShotRef>  shots(const SessionId &) const override { return {}; }

    ShotRef openSwingFolder(const QString &swingFolder, const AthleteId & = {}) override
    {
        if (swingFolder.isEmpty())
            return {};
        const QString   key = QDir::cleanPath(swingFolder);
        const QMutexLocker guard(&m_mutex);
        auto it = m_refs.constFind(key);
        if (it != m_refs.constEnd())
            return *it;
        const ShotRef ref(SessionId::create(), 1);
        m_refs.insert(key, ref);
        m_folders.insert(ref, key);
        return ref;
    }
    QString swingFolder(const ShotRef &shot) const override
    {
        const QMutexLocker guard(&m_mutex);
        return m_folders.value(shot);
    }

private:
    mutable QMutex           m_mutex;
    QHash<QString, ShotRef>  m_refs;
    QHash<ShotRef, QString>  m_folders;
};

inline FolderShots &shots()
{
    static FolderShots s;
    return s;
}

// A test's folder as the shot the door is keyed by (the test's edge translation).
inline ShotRef ref(const QString &swingDir) { return shots().openSwingFolder(swingDir); }

inline RecordingRepository &repo()
{
    static SwingStoreRecordingRepository r(shots());
    return r;
}

inline bool report(const WriteResult &w, QString *error)
{
    if (!w.ok() && error)
        *error = w.error;
    return w.ok();
}

inline bool writeSwingJson(const QString &swingDir, const QJsonObject &rawManifest,
                           const analysis::SwingAnalysis *analysis, QString *error = nullptr,
                           const QString &club = QString(), bool savePose = true)
{
    NewRecording rec;
    rec.header     = rawManifest;
    rec.analysis   = analysis;
    rec.reviewClub = club;
    PersistOptions o;
    o.savePose = savePose;
    return report(repo().create(ref(swingDir), rec, o), error);
}

inline bool updateReview(const QString &swingDir, int rating, const QString &note,
                         const QString &club, QString *error = nullptr)
{
    ReviewEdit e;
    e.rating = rating;
    e.note   = note;
    e.club   = club;
    return report(repo().editReview(ref(swingDir), e), error);
}

inline bool updateShotAnnotations(const QString &swingDir, bool excluded, const QString &clubOverride,
                                  QString *error = nullptr)
{
    ReviewEdit e;
    e.excluded     = excluded;
    e.clubOverride = clubOverride;
    return report(repo().editReview(ref(swingDir), e), error);
}

inline bool updateLaunchMonitor(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                QString *error = nullptr, const QJsonObject &captureClub = {},
                                bool *clubReplaced = nullptr)
{
    return report(repo().completeCapture(ref(swingDir), reading, captureClub, clubReplaced), error);
}

inline bool refreshLaunchMonitorRows(const QString &swingDir, bool *changed = nullptr,
                                     QString *error = nullptr, bool dryRun = false)
{
    PersistOptions o;
    o.dryRun = dryRun;
    const WriteResult w = repo().writeAnalysis(ref(swingDir), AnalysisOutcome{}, o);
    if (changed)
        *changed = w.changed();
    return report(w, error);
}

inline bool updateStreamOrigin(const QString &swingDir, const QString &alias, const StreamOrigin &origin,
                               QString *error = nullptr, const QString &fileName = QString(),
                               const QVector<qint64> &frameTUs = {}, double playbackFps = 0.0,
                               const QString &framesClock = QString())
{
    StreamArrival a;
    a.alias       = alias;
    a.origin      = origin;
    a.fileName    = fileName;
    a.frameTUs    = frameTUs;
    a.playbackFps = playbackFps;
    a.framesClock = framesClock;
    return report(repo().completeCapture(ref(swingDir), a), error);
}

inline bool writeDeviceOnlySwing(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                 const DeviceOnlyMeta &meta, QString *error = nullptr)
{
    return report(repo().create(ref(swingDir), reading, meta), error);
}

} // namespace pinpoint::testdoc
