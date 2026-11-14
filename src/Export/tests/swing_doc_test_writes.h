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
// before step 1.4, through THE ONE DOOR (RecordingRepository, implemented by
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
#include "../swing_store_repository.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace pinpoint::testdoc {

inline RecordingRepository &repo()
{
    static SwingStoreRecordingRepository r;
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
    return report(repo().create(swingDir, rec, o), error);
}

inline bool updateReview(const QString &swingDir, int rating, const QString &note,
                         const QString &club, QString *error = nullptr)
{
    ReviewEdit e;
    e.rating = rating;
    e.note   = note;
    e.club   = club;
    return report(repo().editReview(swingDir, e), error);
}

inline bool updateShotAnnotations(const QString &swingDir, bool excluded, const QString &clubOverride,
                                  QString *error = nullptr)
{
    ReviewEdit e;
    e.excluded     = excluded;
    e.clubOverride = clubOverride;
    return report(repo().editReview(swingDir, e), error);
}

inline bool updateLaunchMonitor(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                QString *error = nullptr, const QJsonObject &captureClub = {},
                                bool *clubReplaced = nullptr)
{
    return report(repo().completeCapture(swingDir, reading, captureClub, clubReplaced), error);
}

inline bool refreshLaunchMonitorRows(const QString &swingDir, bool *changed = nullptr,
                                     QString *error = nullptr, bool dryRun = false)
{
    PersistOptions o;
    o.dryRun = dryRun;
    const WriteResult w = repo().writeAnalysis(swingDir, AnalysisOutcome{}, o);
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
    return report(repo().completeCapture(swingDir, a), error);
}

inline bool writeDeviceOnlySwing(const QString &swingDir, const lm::LaunchMonitorReading &reading,
                                 const DeviceOnlyMeta &meta, QString *error = nullptr)
{
    return report(repo().create(swingDir, reading, meta), error);
}

} // namespace pinpoint::testdoc
