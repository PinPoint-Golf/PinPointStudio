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
                 bool startedHere, bool onDemand)
    : QObject(athlete), m_athlete(athlete), m_sessions(sessions), m_doors(doors), m_id(id),
      m_startedHere(startedHere), m_runningHere(startedHere), m_onDemand(onDemand)
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
    hear(s);
    emit shotHeld(ref);   // its diagnostics, if any, hear it too (step 3.5-i)
    return s;
}

void Session::hear(Shot *shot)
{
    // P10 (DEC-69): the Session hears every change to its shots, synchronously and in order, and logs it. Its
    // SessionDiagnostics (step 3.5-i), when it has one, hears each Shot itself (shotHeld) and re-asks on it.
    const auto heard = [](const char *event) {
        return [event](const ShotRef &ref) { ppInfo() << "[Session] heard" << event << "for" << ref.toString(); };
    };
    connect(shot, &Shot::declaredClubChanged, this, heard("EVT-21"), Qt::DirectConnection);
    connect(shot, &Shot::clubCorrected,       this, heard("EVT-04"), Qt::DirectConnection);
    connect(shot, &Shot::readingExcluded,     this, heard("EVT-05"), Qt::DirectConnection);
    connect(shot, &Shot::labelCorrected,      this, heard("EVT-06"), Qt::DirectConnection);
    connect(shot, &Shot::shotIntentChanged,   this, heard("EVT-07"), Qt::DirectConnection);
    connect(shot, &Shot::captureCompleted,    this, heard("EVT-22"), Qt::DirectConnection);
    connect(shot, &Shot::reanalysed,          this, heard("EVT-02"), Qt::DirectConnection);   // refactor step 3.4b-ii
    // EVT-06's tuning log is the Athlete's (DEC-43): it hears the Shot itself.
    if (m_athlete)
        m_athlete->hearLabelCorrections(shot);
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
    SwingAllocation a;
    Shot *s = addShot(kind, &a);
    if (result)
        *result = a.result;
    return s;
}

Shot *Session::addShot(recording::ShotKind kind, SwingAllocation *allocation)
{
    SwingAllocation none;
    SwingAllocation &out = allocation ? *allocation : none;
    if (!m_sessions) {
        out = SwingAllocation{};
        out.result = failedWith(QStringLiteral("No session store."));
        return nullptr;
    }
    // DEC-142's order. 1. ALLOCATE: the next swing number past the high-water, its folder made, the
    // high-water raised — one write under the session's lock (SessionRepository::allocateSwing, today's
    // allocator, so the folders and numbers are today's). 2. CREATE the Shot, in the collection from now
    // (LC-SHOT: capturing). 3. RETURN it; capture writes the document into it next. No event yet: EVT-01
    // is the document write's (D19), shotDocumentWritten.
    out = m_sessions->allocateSwing(m_id);
    const SwingAllocation &a = out;
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
    ppInfo() << "[Session] EVT-01 shot added:" << ref.toString();
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
    ppInfo() << "[Session] EVT-03 shot trashed:" << ref.toString();
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
    // unchanged; DEC-143). A resumed session is somebody's earlier work, whatever it holds; a session started
    // on demand for a swing outside a session is never discarded (today's rule).
    const bool empty = discardIfEmpty && m_startedHere && !m_onDemand && m_sessions->shots(m_id).isEmpty();

    // Ended first, with its end time (FILE-SESS-01): Athlete::trashSession refuses a session in progress
    // (J-46), so the empty session is ended, then trashed.
    const WriteResult r = m_sessions->end(m_id);   // the repository ends it; it never discards
    if (!r.ok())
        return r;   // still in progress, as its file says (DEC-73): no event
    m_startedHere = false;
    m_runningHere = false;

    if (empty && m_athlete) {
        QString where;
        const WriteResult t = m_athlete->trashSession(m_id, &where);   // EVT-20, from the Athlete (DEC-71, DEC-143)
        if (t.ok()) {
            ppInfo() << "[Session] discarded the empty session" << m_id.toString() << "(to" << where << ")";
            return t;   // this Session is deleted later; touch nothing more
        }
        // As today: a discard that cannot be made leaves the session ended, and says so in the log.
        ppWarn() << "[Session] could not trash the empty session" << m_id.toString() << "-" << t.error;
    }
    ppInfo() << "[Session] EVT-09 session ended:" << m_id.toString();
    emit sessionEnded(m_id);   // EVT-09
    return r;
}

// ── The session's intent (refactor step 3.4b-i; OBJ-SESSION-INTENT, FILE-SESS-02) ─────────────────────────────

SessionInput Session::inputWith(const SessionInput &held) const
{
    // FIELD BY FIELD, against the file as it stands now (step 2.5's rule, moved here unchanged from the
    // diagnostics model's write): every field keeps what the file holds — another model on the session may have
    // written it since the caller read — and a field the file does not hold yet is the caller's, which it resolved
    // from diagnostics.json's older keys (J-33). So after the first write all four fields are in the file.
    const SessionInputRead disk = m_sessions->readSessionInput(m_id);
    SessionInput out = disk.state == SessionInputRead::State::Read ? disk.input : SessionInput();
    if (!out.declaredFocus) out.declaredFocus = held.declaredFocus;
    if (!out.declaredMiss)  out.declaredMiss  = held.declaredMiss;
    if (!out.screenAnswers) out.screenAnswers = held.screenAnswers;
    if (!out.intentRuns)    out.intentRuns    = held.intentRuns;
    return out;
}

WriteResult Session::setSessionIntent(const SessionIntentEdit &edit, const SessionInput &held)
{
    if (!m_sessions)
        return failedWith(QStringLiteral("No session store."));
    SessionInput out = inputWith(held);
    if (edit.focus)
        out.declaredFocus = *edit.focus;
    if (edit.miss)
        out.declaredMiss = *edit.miss;
    if (!edit.screenId.isEmpty()) {
        if (!out.screenAnswers)
            out.screenAnswers = QMap<QString, bool>();
        out.screenAnswers->insert(edit.screenId, edit.screenPresent);
    }
    const WriteResult w = m_sessions->writeSessionInput(m_id, out);
    if (!w.ok()) {
        ppWarn() << "[Session] the session's intent was not saved for" << m_id.toString() << "-" << w.error;
        return w;   // nothing changed: no event
    }
    if (w.changed()) {
        ppInfo() << "[Session] EVT-10 session intent changed:" << m_id.toString();
        emit sessionIntentChanged(m_id);   // EVT-10
    }
    return w;
}

WriteResult Session::setIntentRun(const QVector<IntentRun> &runs, const SessionInput &held)
{
    if (!m_sessions)
        return failedWith(QStringLiteral("No session store."));
    SessionInput out = inputWith(held);
    out.intentRuns = runs;
    const WriteResult w = m_sessions->writeSessionInput(m_id, out);
    if (!w.ok()) {
        ppWarn() << "[Session] the intent run was not saved for" << m_id.toString() << "-" << w.error;
        return w;   // nothing changed: no event
    }
    if (w.changed()) {
        ppInfo() << "[Session] EVT-08 intent run changed:" << m_id.toString();
        emit intentRunChanged(m_id);   // EVT-08
    }
    return w;
}

} // namespace pinpoint
