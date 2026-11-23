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
//                       choice), asked of the current Athlete, then of whichever library athlete's folder holds it —
//                       the Unassigned owner's included (refactor step 5.1; J-67: a session belongs to its athlete
//                       whoever is current) — which translates the folder at the edge (Athlete::sessionAt; DEC-90).
//                       Null for a folder no athlete lists. The Session stays its Athlete's child: QML never owns it
//
//   ShotCommandDoor     (refactor step 3.4b-i; shot_command_door.h) how today's writers of a shot's facts, which
//                       still hold a folder, reach the Shot's and the Session's commands: through sessionAt, then
//                       the Session's own Shot (P10); and (3.4b-ii) how the carousel's rows read a held shot's
//                       summary and trash it, and how re-analysis reads and writes it back. C++ only; no QML calls it
//
//   state               (refactor step 4-a) the AppState (app_state.h): the current athlete, the capture target, the
//                       session on screen, the mode, the selections and the replay. app.athlete and app.session are
//                       shorthand for state.currentAthlete and state.sessionOnScreen (R-QML-1; DEC-63)
//
// It includes no repository (P11): the current athlete comes from the composition root as a function.
//
// THE COMPOSITION ROOT'S CALLABLES (D18 (c); DEC-167) are declared here, beside it, for the application
// services and AthleteController that take them (AthleteController from run 3.4a-ii; steps 3.6, 3.8): functions over the
// athlete repository that main.cpp makes and hands out, so no service includes a repository and no new domain
// object stands between them (no Library object: IM-S3-11 not needed).

#include "../DomainModel/athlete.h"   // Athlete, AthleteRecord, WriteResult
#include "../DomainModel/session.h"   // Session (sessionAt's type, whole for moc)
#include "../Identity/identity.h"
#include "app_state.h"
#include "shot_command_door.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace pinpoint {

// D18 (c): how the application services and AthleteController reach the athletes they act on. Made once by
// the composition root over its one athlete repository; the repository reads the library setting (SET-03) on
// every call, so a library re-pointed needs no new callables. AthleteController takes them from run 3.4a-ii.
// Housekeeping (step 3.8) takes them too, with its purge and size doors (HousekeepingDoors, housekeeping.h); its pass
// runs on a worker and calls them on the GUI thread only.
struct AthleteCallables {
    std::function<QList<AthleteId>()>                              listAthletes;    // the library's athletes
    std::function<Athlete *(const AthleteId &)>                    loadAthlete;     // the one instance (DEC-52)
    std::function<Athlete *(const AthleteRecord &, WriteResult *)> createAthlete;   // mints the AthleteId
    // An athlete's activity (its sessions with a swing, the latest one's start: DEC-89), read from its
    // SessionIndex. SAFE FROM ANY THREAD (AthleteRepository::readActivity): the picker's read runs on a pool.
    std::function<AthleteActivity(const SessionOwner &)>           readActivity;
    // THE UNASSIGNED OWNER (refactor step 5.1; information_model.md §5.11; DEC-198; J-67): AthleteRepository::unassigned
    // — `create` true writes its record at the first need (a capture's Start, a move); false hands it only when listed.
    std::function<Athlete *(bool create, WriteResult *result)>    unassigned;
};

class App : public QObject, public ShotCommandDoor
{
    Q_OBJECT
    Q_PROPERTY(pinpoint::AppState *state   READ state   CONSTANT)
    Q_PROPERTY(pinpoint::Athlete  *athlete READ athlete NOTIFY athleteChanged)
    Q_PROPERTY(pinpoint::Session  *session READ session NOTIFY sessionChanged)
public:
    // `currentAthlete`: the athlete the app is on (AthleteController's; app.athlete).
    explicit App(std::function<Athlete *()> currentAthlete, QObject *parent = nullptr);

    AppState *state() { return &m_state; }
    Athlete  *athlete() const { return m_state.currentAthlete(); }
    Session  *session() const { return m_state.sessionOnScreen(); }

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
    std::optional<QJsonObject> markupTruth(const QString &sessionFolder, const ShotRef &shot, TruthView view) override;
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
    ShotRef shotOfFolder(const QString &swingFolder) override;
    std::optional<bool> isDeviceOnly(const QString &sessionFolder, const ShotRef &shot) override;

    // THE LIBRARY'S ATHLETES (refactor step 5.1; J-67): a session belongs to its athlete whoever is current, so
    // sessionAt finds a folder through whichever of them holds it — the Unassigned owner included. Every athlete the
    // repository has handed out (AthleteStore::heldAthletes: the listed ones, and Unassigned once capture made it).
    void setAthletes(std::function<QList<Athlete *>()> athletes) { m_athletes = std::move(athletes); }

signals:
    void athleteChanged();
    void sessionChanged();

private:
    std::function<Athlete *()> m_currentAthlete;
    std::function<QList<Athlete *>()> m_athletes;
    AppState                   m_state;

    // The Session's own Shot for `shot`, through the Session at `sessionFolder` (P10). Null: not held (N1).
    Shot *shotAt(const QString &sessionFolder, const ShotRef &shot) const;
};

} // namespace pinpoint
