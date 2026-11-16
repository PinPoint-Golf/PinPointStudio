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

// Session — the session aggregate root (refactor step 3.4a; build/refactor/step-3/BRIEF.md §2.2, §4.1, §4.2,
// §4.8; docs/developer/information_model.md OBJ-SESSION, CLS-SESSION, LC-SESSION, EVT-01, EVT-03, EVT-09,
// DEC-142, DEC-143, DEC-168).
//
//   id()          its SessionId (ID-SESSION)
//   entry()       what the session repository's identity map holds for it — identity (folder, start, local
//                 day, n-of-day, type), status, start and end times — read from the map on every call, so
//                 the Session never keeps a second copy of its status (DEC-41: the status is persisted)
//   shots(), shot(ref)   its collection: one Shot per swing number, its child, the ONLY way to a Shot (P10).
//                 Listed from the repository when first asked; addShot and trashShot keep it after that
//   addShot()     allocate → create → returns the Shot (DEC-142; "create" here is the Shot object: DEC-142's
//                 wording is JUDGEMENT-CALLS J-15's): the next swing number and its folder through
//                 SessionRepository::allocateSwing (the one allocator: DEC-157), then the Shot, added to the
//                 collection. Capture then writes the shot's document through RecordingRepository::create, as
//                 it does today. The allocation (its folder and names) is handed back to the capture service,
//                 which writes the shot's media into that folder itself until the platform refactor (capture)
//   shotDocumentWritten(ref)   EVT-01, AT THE MOMENT D19 PICKS (DEC-168): when capture's document write has
//                 landed, not at the allocation. Capture reports it, on this thread; the first report for
//                 a shot in the collection emits shotAdded once, any later one nothing
//   trashShot(ref)   the swing to the trash, its number kept as the high-water, both through
//                 SessionRepository (J-24); EVT-03; the Shot taken out of the collection and deleted LATER
//                 (deleteLater), so a view still bound to it never touches freed memory. A swing number is
//                 never reissued (DEC-91)
//   end()         Ended with its end time; a session this run's Athlete::startSession made deliberately (not
//                 on demand) that holds no swing is then trashed by its Athlete (Athlete::trashSession,
//                 EVT-20: DEC-143), else EVT-09. THE EMPTY DISCARD IS THIS RULE ALONE (DEC-93): the session
//                 repository's end never discards (refactor step 3.4a-ii)
//
//   setSessionIntent(edit)   THE SESSION'S INTENT (OBJ-SESSION-INTENT; FILE-SESS-02; refactor step 3.4b-i): the
//                 declared focus, the declared miss, or one screen's answer, written FIELD BY FIELD against the
//                 file as it stands (SessionRepository::readSessionInput / writeSessionInput by id); EVT-10
//   setIntentRun(runs)   the "from here on" intent run (R-INPUT-2), the same way; EVT-08
//                 Both take `held`: the caller's value of every field the session's file does not hold yet (a
//                 session whose input is still in diagnostics.json's older keys until migration M3: J-33), so
//                 the first write puts all four fields in the file, exactly as the diagnostics model's own write
//                 did before (step 2.5)
//
// IT HEARS ITS SHOTS (P10; DEC-69; refactor step 3.4b-i): every Shot it holds is connected (DirectConnection)
// at the moment it is held, and each of the Shot's events is heard here, synchronously and in order. Nothing
// consumes them for a re-fire until step 3.5 (SessionDiagnostics); the app log says what was heard. It also
// hands each Shot to its Athlete, which appends the tuning log on EVT-06 (DEC-43).
//
// ITS EVENTS (DEC-69; src/Core/work_pool.h's conventions): Qt signals carrying only the id, emitted on this
// thread after the repository write returned ok, in command order; listeners connect with
// Qt::DirectConnection. shotAdded has no consumer in step 3.4a (D19's moment is wired; step 3.5's
// SessionDiagnostics listens).
//
// ONE INSTANCE PER SessionId: made by its Athlete only (Athlete::startSession, resumeSession, sessionAt,
// session, sessions), which holds the sessions it has loaded as its children; the session repository holds
// their records (LIVE-2; Mark, 15 Nov 2026: DEC-190).
//
// ITS CALLERS (step 3.4a-ii): capture (ShotProcessor, the capture service, which holds the Session it captures
// into: BRIEF §4.8) starts, resumes and ends through its Athlete and this Session, and allocates every shot —
// a camera shot's and the launch monitor's — through addShot; capture and the launch monitor report each
// shot's first document (shotDocumentWritten). The carousel trashes a swing of a session its Athlete holds through
// trashShot (refactor step 3.4b-ii), so the collection sees it; a session no Athlete holds keeps today's trash (N1).
//
// Domain model ring (src/DomainModel): names no store type.

#include "athlete.h"   // ShotDoors
#include "session_repository.h"
#include "shot_command_values.h"   // SessionIntentEdit
#include "../Identity/identity.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>

#include <optional>

namespace pinpoint {

class Shot;
namespace recording { enum class ShotKind : int; }   // src/Recording/recording.h

class Session : public QObject
{
    Q_OBJECT
public:
    SessionId                         id() const { return m_id; }
    std::optional<SessionIndexEntry>  entry() const;
    SessionStatus                     status() const;   // Ended when the repository does not hold it
    bool                              inProgress() const { return status() == SessionStatus::InProgress; }
    Athlete                          *athlete() const { return m_athlete; }

    // The collection, by swing number (P10).
    QList<Shot *> shots();
    Shot         *shot(const ShotRef &ref);

    // The commands.
    Shot       *addShot(WriteResult *result = nullptr);   // a captured shot
    Shot       *addShot(recording::ShotKind kind, WriteResult *result = nullptr);
    // The same, handing back the whole allocation (its result, folder and names) for the capture service,
    // which writes the shot's media into the folder itself (until the platform refactor: capture).
    Shot       *addShot(recording::ShotKind kind, SwingAllocation *allocation);
    bool        shotDocumentWritten(const ShotRef &ref);
    WriteResult trashShot(const ShotRef &ref, QString *where = nullptr);
    WriteResult end();
    // The session's intent (refactor step 3.4b-i; above). `held`: the caller's value of each field the file does
    // not hold yet (J-33's fallback); the edited field is always the edit's.
    WriteResult setSessionIntent(const SessionIntentEdit &edit, const SessionInput &held = {});   // EVT-10
    WriteResult setIntentRun(const QVector<IntentRun> &runs, const SessionInput &held = {});      // EVT-08

    // This process started or resumed it, and has not ended it: what Athlete::startSession and resumeSession
    // end first (DEC-78, DEC-134). A session a crash left in progress is not running here: the repository's
    // Start ends it, with its end inferred (J-18), as today.
    bool runningHere() const { return m_runningHere; }
    // Started on demand for a swing outside a session (ShotProcessor's capture outside one): never the empty
    // discard's, whatever ends it — as today, where only a session the golfer started is discarded (DEC-93).
    bool onDemand() const { return m_onDemand; }

signals:
    void shotAdded(const pinpoint::ShotRef &shot);        // EVT-01 (D19: when capture's document write lands)
    void shotTrashed(const pinpoint::ShotRef &shot);      // EVT-03
    void sessionEnded(const pinpoint::SessionId &session);   // EVT-09
    void intentRunChanged(const pinpoint::SessionId &session);       // EVT-08 IntentRunChanged
    void sessionIntentChanged(const pinpoint::SessionId &session);   // EVT-10 SessionIntentChanged

private:
    friend class Athlete;   // one instance per SessionId: made by its Athlete only
    // `startedHere`: made by this run's Athlete::startSession (the empty discard may take it, DEC-93, unless
    // `onDemand`).
    Session(Athlete *athlete, SessionRepository *sessions, const ShotDoors &doors, const SessionId &id,
            bool startedHere, bool onDemand = false);

    // end(), and the end a start or a resume makes of the session running here (Athlete::endRunningSessions),
    // which never discards an empty one — as today, where only End itself discards (DEC-93).
    WriteResult endSession(bool discardIfEmpty);
    void  loadShots();
    Shot *hold(const ShotRef &ref, const std::optional<recording::ShotKind> &kind);
    // P10: hears each of the Shot's events (step 3.4b-i); nothing re-fires on them until step 3.5.
    void  hear(Shot *shot);
    // The session's input as its file holds it, each field it does not hold yet taken from `held` (J-33).
    SessionInput inputWith(const SessionInput &held) const;

    Athlete            *m_athlete  = nullptr;
    SessionRepository  *m_sessions = nullptr;
    ShotDoors           m_doors;
    SessionId           m_id;
    bool                m_startedHere = false;
    bool                m_runningHere = false;
    bool                m_onDemand    = false;
    bool                m_shotsLoaded = false;
    QMap<int, Shot *>   m_shots;       // by swing number; each a child of this Session
    QSet<int>           m_announced;   // swing numbers whose EVT-01 has been emitted
};

} // namespace pinpoint
