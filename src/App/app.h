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

// App — the application's one object for the views, `app` in QML (refactor step 3.4a; build/refactor/step-3/
// BRIEF.md §4.10; D1: DEC-164; IM-S3-7). Application ring (src/App: DEC-11).
//
//   sessionAt(folder)   THE ONE INTERIM MEMBER (D1): the Session whose folder a view still chooses itself
//                       (the diagnostics panel's sessionDir, until step 4's sessionOnScreen replaces the
//                       choice), asked of the CURRENT Athlete, which translates the folder at the edge
//                       (Athlete::sessionAt; DEC-90). Null with no current athlete, or for a folder that is
//                       not one of its sessions. The Session stays its Athlete's child: QML never owns it
//
//   ShotCommandDoor     (refactor step 3.4b-i; shot_command_door.h) how today's writers of a shot's facts, which
//                       still hold a folder, reach the Shot's and the Session's commands: through sessionAt, then
//                       the Session's own Shot (P10); and (3.4b-ii) how the carousel's rows read a held shot's
//                       summary and trash it, and how re-analysis reads and writes it back. C++ only; no QML calls it
//
// Step 4 grows it (app.session, app.athlete, app.state: R-QML-1); nothing is re-pointed twice. It includes no
// repository (P11): the current athlete comes from the composition root as a function.
//
// THE COMPOSITION ROOT'S CALLABLES (D18 (c); DEC-167) are declared here, beside it, for the application
// services and AthleteController that take them (AthleteController from run 3.4a-ii; steps 3.6, 3.8): functions over the
// athlete repository that main.cpp makes and hands out, so no service includes a repository and no new domain
// object stands between them (no Library object: IM-S3-11 not needed).

#include "../DomainModel/athlete.h"   // Athlete, AthleteRecord, WriteResult
#include "../DomainModel/session.h"   // Session (sessionAt's type, whole for moc)
#include "../Identity/identity.h"
#include "shot_command_door.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace pinpoint {

// D18 (c): how the application services and AthleteController reach the athletes they act on. Made once by
// the composition root over its one athlete repository; the repository reads the library setting (SET-03) on
// every call, so a library re-pointed needs no new callables. AthleteController takes them from run 3.4a-ii;
// the trash's purge joins them in step 3.8.
struct AthleteCallables {
    std::function<QList<AthleteId>()>                              listAthletes;    // the library's athletes
    std::function<Athlete *(const AthleteId &)>                    loadAthlete;     // the one instance (DEC-52)
    std::function<Athlete *(const AthleteRecord &, WriteResult *)> createAthlete;   // mints the AthleteId
    // An athlete's activity (its sessions with a swing, the latest one's start: DEC-89), read from its
    // SessionIndex. SAFE FROM ANY THREAD (AthleteRepository::readActivity): the picker's read runs on a pool.
    std::function<AthleteActivity(const SessionOwner &)>           readActivity;
};

class App : public QObject, public ShotCommandDoor
{
    Q_OBJECT
public:
    // `currentAthlete`: the athlete the app is on (today AthleteController's; step 4's app.athlete).
    explicit App(std::function<Athlete *()> currentAthlete, QObject *parent = nullptr);

    Q_INVOKABLE pinpoint::Session *sessionAt(const QString &folder) const;

    // ── ShotCommandDoor (refactor step 3.4b-i) ──────────────────────────────────────────────────────────────
    ForwardResult rate(const QString &sessionFolder, const ShotRef &shot, int stars) override;
    ForwardResult note(const QString &sessionFolder, const ShotRef &shot, const QString &text) override;
    ForwardResult setDeclaredClub(const QString &sessionFolder, const ShotRef &shot, const QString &club) override;
    ForwardResult correctClub(const QString &sessionFolder, const ShotRef &shot, const QString &club) override;
    ForwardResult excludeReading(const QString &sessionFolder, const ShotRef &shot, bool excluded) override;
    ForwardResult completeCapture(const QString &sessionFolder, const ShotRef &shot,
                                  const lm::LaunchMonitorReading &reading, const QJsonObject &followedClub,
                                  bool *clubReplaced) override;
    ForwardResult completeCapture(const QString &sessionFolder, const ShotRef &shot,
                                  const StreamArrival &arrival) override;
    ForwardResult setMarkupTruth(const QString &sessionFolder, const ShotRef &shot, TruthView view,
                                 const QJsonObject &truth) override;
    bool setShotIntent(const QString &sessionFolder, const ShotRef &shot, const QString &intent, bool keepEmpty,
                       const SaveLater &later) override;
    bool correctLabel(const QString &sessionFolder, const ShotRef &shot, const QVector<LabelCorrection> &corrections,
                      const LabelCorrectionNote &note, bool keepEmpty, const SaveLater &later) override;
    ForwardResult flagForTuning(const QString &sessionFolder, const ShotRef &shot,
                                const TuningFlagNote &note) override;
    ForwardResult setSessionIntent(const QString &sessionFolder, const SessionIntentEdit &edit,
                                   const SessionInput &held) override;
    ForwardResult setIntentRun(const QString &sessionFolder, const QVector<IntentRun> &runs,
                               const SessionInput &held) override;
    // The rows and the readers (refactor step 3.4b-ii).
    std::optional<ShotSummary> shotSummary(const QString &sessionFolder, const ShotRef &shot, SummaryRead how) override;
    std::optional<QVector<HeldShot>> sessionShots(const QString &sessionFolder) override;
    ForwardResult trashShot(const QString &sessionFolder, const ShotRef &shot, QString *where) override;
    std::optional<RecordingSource> recordingSourceOf(const QString &sessionFolder, const ShotRef &shot) override;
    ForwardResult applyAnalysis(const QString &sessionFolder, const ShotRef &shot, const AnalysisOutcome &outcome,
                                const PersistOptions &options) override;

private:
    std::function<Athlete *()> m_currentAthlete;

    // The Session's own Shot for `shot`, through the Session at `sessionFolder` (P10). Null: not held (N1).
    Shot *shotAt(const QString &sessionFolder, const ShotRef &shot) const;
};

} // namespace pinpoint
