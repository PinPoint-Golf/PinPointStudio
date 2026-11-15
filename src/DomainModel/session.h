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
//   addShot()     allocate → create → returns the Shot (DEC-142): the next swing number and its folder
//                 through SessionRepository::allocateSwing (the one allocator: DEC-157), then the Shot,
//                 added to the collection. Capture then writes the shot's document through
//                 RecordingRepository::create, as it does today
//   shotDocumentWritten(ref)   EVT-01, AT THE MOMENT D19 PICKS (DEC-168): when capture's document write has
//                 landed, not at the allocation. Capture reports it, on this thread; the first report for
//                 a shot in the collection emits shotAdded once, any later one nothing
//   trashShot(ref)   the swing to the trash, its number kept as the high-water, both through
//                 SessionRepository (J-24); EVT-03; the Shot taken out of the collection and deleted LATER
//                 (deleteLater), so a view still bound to it never touches freed memory. A swing number is
//                 never reissued (DEC-91)
//   end()         Ended with its end time; a session this run's Athlete::startSession made that holds no
//                 swing is then trashed by its Athlete (Athlete::trashSession, EVT-20: DEC-143), else EVT-09
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
// INTERIM (step 3.4a, run i): the objects exist and are tested, but no caller has moved yet — capture, the
// launch monitor, the phone-clip filer and AthleteController still start, end and allocate through
// SessionRepository directly (run 3.4a-ii moves them), and the carousel still trashes a swing itself (step
// 3.4b). A Session already holding its shots does not see a swing those paths add or trash until then.
//
// Domain model ring (src/DomainModel): names no store type.

#include "athlete.h"   // ShotDoors
#include "session_repository.h"
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
    bool        shotDocumentWritten(const ShotRef &ref);
    WriteResult trashShot(const ShotRef &ref, QString *where = nullptr);
    WriteResult end();

    // This process started or resumed it, and has not ended it: what Athlete::startSession and resumeSession
    // end first (DEC-78, DEC-134). A session a crash left in progress is not running here: the repository's
    // Start ends it, with its end inferred (J-18), as today.
    bool runningHere() const { return m_runningHere; }

signals:
    void shotAdded(const pinpoint::ShotRef &shot);        // EVT-01 (D19: when capture's document write lands)
    void shotTrashed(const pinpoint::ShotRef &shot);      // EVT-03
    void sessionEnded(const pinpoint::SessionId &session);   // EVT-09

private:
    friend class Athlete;   // one instance per SessionId: made by its Athlete only
    // `startedHere`: made by this run's Athlete::startSession (the empty discard may take it, DEC-93).
    Session(Athlete *athlete, SessionRepository *sessions, const ShotDoors &doors, const SessionId &id,
            bool startedHere);

    // end(), and the end a start or a resume makes of the session running here (Athlete::endRunningSessions),
    // which never discards an empty one — as today, where only End itself discards (DEC-93).
    WriteResult endSession(bool discardIfEmpty);
    void  loadShots();
    Shot *hold(const ShotRef &ref, const std::optional<recording::ShotKind> &kind);

    Athlete            *m_athlete  = nullptr;
    SessionRepository  *m_sessions = nullptr;
    ShotDoors           m_doors;
    SessionId           m_id;
    bool                m_startedHere = false;
    bool                m_runningHere = false;
    bool                m_shotsLoaded = false;
    QMap<int, Shot *>   m_shots;       // by swing number; each a child of this Session
    QSet<int>           m_announced;   // swing numbers whose EVT-01 has been emitted
};

} // namespace pinpoint
