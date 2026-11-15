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

#include "session.h"
#include "athlete.h"
#include "shot.h"

#include "../Core/pp_debug.h"

namespace pinpoint {

namespace {
WriteResult failedWith(const QString &why) { return { WriteResult::Status::Failed, why }; }
}

Session::Session(Athlete *athlete, SessionRepository *sessions, const ShotDoors &doors, const SessionId &id,
                 bool startedHere)
    : QObject(athlete), m_athlete(athlete), m_sessions(sessions), m_doors(doors), m_id(id),
      m_startedHere(startedHere), m_runningHere(startedHere)
{
}

std::optional<SessionIndexEntry> Session::entry() const
{
    return m_sessions ? m_sessions->open(m_id) : std::nullopt;
}

SessionStatus Session::status() const
{
    const auto e = entry();
    return e ? e->status : SessionStatus::Ended;
}

// ── The collection ───────────────────────────────────────────────────────────────────────────────

Shot *Session::hold(const ShotRef &ref, const std::optional<recording::ShotKind> &kind)
{
    if (Shot *s = m_shots.value(ref.swingNumber()))
        return s;
    Shot *s = new Shot(ref, kind, m_doors, this);
    m_shots.insert(ref.swingNumber(), s);
    return s;
}

void Session::loadShots()
{
    if (m_shotsLoaded || !m_sessions)
        return;
    m_shotsLoaded = true;
    // The swings on disk now, each a Shot of unknown kind until asked (a document read). A swing whose
    // document capture is still writing is listed too: it is in the collection from its allocation
    // (LC-SHOT: "capturing"), whoever allocated it.
    for (const ShotRef &ref : m_sessions->shots(m_id))
        hold(ref, std::nullopt);
}

QList<Shot *> Session::shots()
{
    loadShots();
    return m_shots.values();   // QMap: by swing number
}

Shot *Session::shot(const ShotRef &ref)
{
    if (ref.isNull() || ref.session() != m_id)
        return nullptr;
    loadShots();
    return m_shots.value(ref.swingNumber());
}

// ── The commands ─────────────────────────────────────────────────────────────────────────────────

Shot *Session::addShot(WriteResult *result)
{
    return addShot(recording::ShotKind::Captured, result);
}

Shot *Session::addShot(recording::ShotKind kind, WriteResult *result)
{
    if (!m_sessions) {
        if (result)
            *result = failedWith(QStringLiteral("No session store."));
        return nullptr;
    }
    // DEC-142's order. 1. ALLOCATE: the next swing number past the high-water, its folder made, the
    // high-water raised — one write under the session's lock (SessionRepository::allocateSwing, today's
    // allocator, so the folders and numbers are today's). 2. CREATE the Shot, in the collection from now
    // (LC-SHOT: capturing). 3. RETURN it; capture writes the document into it next. No event yet: EVT-01
    // is the document write's (D19), shotDocumentWritten.
    const SwingAllocation a = m_sessions->allocateSwing(m_id);
    if (result)
        *result = a.result;
    if (!a.ok())
        return nullptr;
    loadShots();   // the listing (if first) sees the new folder; hold() keeps the one instance either way
    Shot *s = hold(a.shot, kind);
    s->m_kind = kind;   // the caller says what it allocated for
    return s;
}

bool Session::shotDocumentWritten(const ShotRef &ref)
{
    Shot *s = shot(ref);
    if (!s || m_announced.contains(ref.swingNumber()))
        return false;
    m_announced.insert(ref.swingNumber());
    emit shotAdded(ref);   // EVT-01
    return true;
}

WriteResult Session::trashShot(const ShotRef &ref, QString *where)
{
    Shot *s = shot(ref);
    if (!s)
        return failedWith(QStringLiteral("That shot is not in this session."));
    const WriteResult r = m_sessions->trashShot(ref, where);
    if (!r.ok())
        return r;   // nothing changed: no event
    m_shots.remove(ref.swingNumber());
    m_announced.remove(ref.swingNumber());
    emit shotTrashed(ref);   // EVT-03
    s->deleteLater();        // a view still bound to it reads a live object until the event loop runs
    return r;
}

WriteResult Session::end()
{
    return endSession(/*discardIfEmpty=*/true);
}

WriteResult Session::endSession(bool discardIfEmpty)
{
    if (!m_sessions)
        return failedWith(QStringLiteral("No session store."));
    const auto e = entry();
    if (!e)
        return failedWith(QStringLiteral("That session is not known."));
    if (e->status != SessionStatus::InProgress) {
        m_runningHere = false;
        return { WriteResult::Status::Unchanged, {} };
    }
    // ⚠ THE EMPTY DISCARD TAKES ONLY WHAT THIS RUN'S START MADE, and only while it holds no swing (DEC-93,
    // unchanged; DEC-143). A resumed session is somebody's earlier work, whatever it holds.
    const bool empty = discardIfEmpty && m_startedHere && m_sessions->shots(m_id).isEmpty();

    // Ended first, with its end time (FILE-SESS-01): Athlete::trashSession refuses a session in progress
    // (J-46), so the empty session is ended, then trashed.
    const WriteResult r = m_sessions->end(m_id, /*discardIfEmpty=*/false);
    if (!r.ok())
        return r;   // still in progress, as its file says (DEC-73): no event
    m_startedHere = false;
    m_runningHere = false;

    if (empty && m_athlete) {
        const WriteResult t = m_athlete->trashSession(m_id);   // EVT-20, from the Athlete (DEC-71, DEC-143)
        if (t.ok())
            return t;   // this Session is deleted later; touch nothing more
        // As today: a discard that cannot be made leaves the session ended, and says so in the log.
        ppWarn() << "[Session] could not trash the empty session" << m_id.toString() << "-" << t.error;
    }
    emit sessionEnded(m_id);   // EVT-09
    return r;
}

} // namespace pinpoint
