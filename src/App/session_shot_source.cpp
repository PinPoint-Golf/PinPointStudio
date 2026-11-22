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

#include "session_shot_source.h"

#include "../DomainModel/athlete.h"
#include "../DomainModel/session.h"
#include "../DomainModel/shot.h"

namespace pinpoint {

namespace {
WriteResult notHeld() { return { WriteResult::Status::Failed, QStringLiteral("That shot is not in this session.") }; }
} // namespace

SessionShotSource::SessionShotSource(Session *session)
    : m_session(session)
{
    if (!session)
        return;
    if (const std::optional<SessionIndexEntry> e = session->entry())
        m_folder = e->folder;
    session->addUse();
    m_using = true;

    QObject::connect(session, &Session::shotAdded, &m_context, [this](const ShotRef &ref) {
        hear(shotOf(ref));
        if (m_events.added) m_events.added(ref);
    });
    QObject::connect(session, &Session::shotTrashed, &m_context, [this](const ShotRef &ref) {
        if (m_events.trashed) m_events.trashed(ref);
    });
    QObject::connect(session, &Session::releasing, &m_context, [this] { goneNow(); });
    QObject::connect(session, &QObject::destroyed, &m_context, [this] { goneNow(); });
    if (Athlete *a = session->athlete()) {
        const SessionId id = session->id();
        QObject::connect(a, &Athlete::sessionTrashed, &m_context, [this, id](const SessionId &s) { if (s == id) goneNow(); });
        QObject::connect(a, &Athlete::sessionMovedOut, &m_context, [this, id](const SessionId &s) { if (s == id) goneNow(); });
    }
    for (Shot *s : session->shots())
        hear(s);
    // A Shot taken into the collection later (a capture's allocation): heard from then on.
    QObject::connect(session, &Session::shotHeld, &m_context, [this](const ShotRef &ref) { hear(shotOf(ref)); });
}

SessionShotSource::~SessionShotSource()
{
    if (m_using && m_session)
        m_session->dropUse();
}

Shot *SessionShotSource::shotOf(const ShotRef &shot) const
{
    return (m_session && !m_gone && !shot.isNull() && shot.session() == m_session->id()) ? m_session->shot(shot) : nullptr;
}

void SessionShotSource::hear(Shot *shot)
{
    if (!shot || m_heard.contains(shot))
        return;
    m_heard.insert(shot);
    const auto plain = [this](const ShotRef &ref) { if (m_events.changed) m_events.changed(ref, false); };
    const auto rewritten = [this](const ShotRef &ref) { if (m_events.changed) m_events.changed(ref, true); };
    QObject::connect(shot, &Shot::declaredClubChanged, &m_context, plain);   // EVT-21
    QObject::connect(shot, &Shot::clubCorrected,       &m_context, plain);   // EVT-04
    QObject::connect(shot, &Shot::readingExcluded,     &m_context, plain);   // EVT-05
    QObject::connect(shot, &Shot::labelCorrected,      &m_context, plain);   // EVT-06
    QObject::connect(shot, &Shot::shotIntentChanged,   &m_context, plain);   // EVT-07
    QObject::connect(shot, &Shot::captureCompleted,    &m_context, rewritten);   // EVT-22
    QObject::connect(shot, &Shot::reanalysed,          &m_context, rewritten);   // EVT-02
    QObject::connect(shot, &QObject::destroyed, &m_context, [this, shot] { m_heard.remove(shot); });
}

void SessionShotSource::goneNow()
{
    if (m_gone)
        return;
    m_gone = true;
    if (m_events.gone) m_events.gone();
}

QVector<ShotRef> SessionShotSource::shots()
{
    QVector<ShotRef> out;
    if (!m_session || m_gone)
        return out;
    for (Shot *s : m_session->shots())   // by swing number
        out.append(s->ref());
    return out;
}

const ShotSummary *SessionShotSource::summary(const ShotRef &shot, SummaryRead how)
{
    Shot *s = shotOf(shot);
    if (!s)
        return nullptr;
    switch (how) {
    case SummaryRead::KeptOnly:   return s->summaryKept() ? &s->summary() : nullptr;
    case SummaryRead::Kept:       return &s->summary();
    case SummaryRead::Again:      return &s->rereadSummary(/*parseIfUnindexed=*/true);
    case SummaryRead::AgainCheap: return &s->rereadSummary(/*parseIfUnindexed=*/false);
    }
    return nullptr;
}

QString SessionShotSource::swingFolder(const ShotRef &shot) const
{
    const Shot *s = shotOf(shot);
    return s ? s->folder() : QString();
}

WriteResult SessionShotSource::rate(const ShotRef &shot, int stars)
{
    Shot *s = shotOf(shot);
    return s ? s->rate(stars) : notHeld();
}

WriteResult SessionShotSource::note(const ShotRef &shot, const QString &text)
{
    Shot *s = shotOf(shot);
    return s ? s->note(text) : notHeld();
}

WriteResult SessionShotSource::setDeclaredClub(const ShotRef &shot, const QString &club)
{
    Shot *s = shotOf(shot);
    return s ? s->setDeclaredClub(club) : notHeld();
}

WriteResult SessionShotSource::correctClub(const ShotRef &shot, const QString &club)
{
    Shot *s = shotOf(shot);
    return s ? s->correctClub(club) : notHeld();
}

WriteResult SessionShotSource::excludeReading(const ShotRef &shot, bool excluded)
{
    Shot *s = shotOf(shot);
    return s ? s->excludeReading(excluded) : notHeld();
}

WriteResult SessionShotSource::trash(const ShotRef &shot, QString *where)
{
    if (!shotOf(shot))
        return notHeld();
    return m_session->trashShot(shot, where);   // EVT-03, from the Session (the row leaves on it)
}

std::unique_ptr<ShotSource> shotSourceOf(Session *session)
{
    if (!session)
        return nullptr;
    return std::make_unique<SessionShotSource>(session);
}

} // namespace pinpoint
