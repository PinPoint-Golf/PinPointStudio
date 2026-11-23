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

// THE SESSION LIST FOLLOWS ITS ATHLETES' EVENTS (refactor step 5.7a; R-QML-6; session_list_presenter.h). Apart from the rows
// so that a test of the rows (report_list_test) links no Athlete, Session or Shot.

#include "session_list_presenter.h"

#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"

namespace pinpoint {

void SessionListPresenter::follow(Athlete *athlete)
{
    if (!athlete) return;
    const AthleteId id = athlete->id();
    if (m_followed.value(id) == athlete && m_groups.contains(id)) return;
    QPointer<Athlete> a(athlete);
    setSource(id, [a] { return a ? a->index() : QList<SessionIndexEntry>{}; });
    m_followed.insert(id, a);
    // EVT-17, EVT-23: an entry added, a status changed.
    connect(athlete, &Athlete::sessionStarted, this, [this, id] { entriesChanged(id); });
    connect(athlete, &Athlete::sessionResumed, this, [this, id] { entriesChanged(id); });
    // EVT-20 and the move out: the row goes, and the index is read again, so a reload in flight (its entries listed before
    // the trash) lands without it, a first load too (review B1). Both are emitted after the store moved the folder.
    // The move in: it joins this athlete's rows.
    connect(athlete, &Athlete::sessionTrashed,  this, [this, id](const SessionId &s) { forget(s); entriesChanged(id); });
    connect(athlete, &Athlete::sessionMovedOut, this, [this, id](const SessionId &s) { forget(s); entriesChanged(id); });
    connect(athlete, &Athlete::sessionMovedIn,  this, [this, id] { entriesChanged(id); });
    // EVT-18, EVT-19 and a session's shots are the Session's: heard from every Session the Athlete holds, now and later.
    connect(athlete, &Athlete::sessionHeld, this, [this, id](Session *s) { followSession(id, s); });
    for (Session *s : athlete->loadedSessions()) followSession(id, s);
    connect(athlete, &QObject::destroyed, this, [this, id] { dropSource(id); });
}

void SessionListPresenter::followSession(const AthleteId &athlete, Session *session)
{
    if (!session || m_followedSessions.contains(session)) return;
    m_followedSessions.insert(session);
    connect(session, &QObject::destroyed, this, [this, session] { m_followedSessions.remove(session); });
    const SessionId sid = session->id();
    const auto entryAndFacts = [this, athlete, sid] { entriesChanged(athlete); reread(sid); };
    connect(session, &Session::sessionArchived, this, entryAndFacts);   // EVT-18
    connect(session, &Session::sessionRestored, this, entryAndFacts);   // EVT-19
    connect(session, &Session::sessionEnded, this, [this, athlete] { entriesChanged(athlete); });   // EVT-09: the status
    connect(session, &Session::shotAdded,   this, [this, sid] { reread(sid); });   // EVT-01
    connect(session, &Session::shotTrashed, this, [this, sid] { reread(sid); });   // EVT-03
    connect(session, &Session::shotChanged, this, [this, sid] { reread(sid); });   // a club corrected, and the rest
}

} // namespace pinpoint
