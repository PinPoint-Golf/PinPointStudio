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

#include "athlete_catch_up.h"

#include "../DomainModel/athlete.h"
#include "../DomainModel/athlete_diagnostics.h"
#include "../DomainModel/session.h"
#include "../DomainModel/session_diagnostics.h"
#include "../Core/pp_debug.h"

#include <QTimer>

#include <algorithm>

namespace pinpoint {

AthleteCatchUp::AthleteCatchUp(AthleteCallables callables, QObject *parent)
    : QObject(parent), m_callables(std::move(callables))
{
}

AthleteCatchUp::~AthleteCatchUp()
{
    cancel();
}

void AthleteCatchUp::run(const AthleteId &id)
{
    if (id.isNull() || !m_callables.loadAthlete)
        return;
    for (const Item &i : std::as_const(m_queue))
        if (i.athlete == id) return;
    if (m_current && m_item.athlete == id) return;
    Athlete *a = m_callables.loadAthlete(id);
    if (!a)
        return;
    // Every ended session (a live one is the capture's), oldest first by its day and place in the day.
    QList<SessionIndexEntry> entries;
    for (const SessionIndexEntry &e : a->index())
        if (!e.id().isNull() && e.status != SessionStatus::InProgress)
            entries.append(e);
    std::stable_sort(entries.begin(), entries.end(), [](const SessionIndexEntry &x, const SessionIndexEntry &y) {
        const QDate dx = x.identity.localDay(), dy = y.identity.localDay();
        if (dx.isValid() != dy.isValid()) return dx.isValid();
        if (dx != dy) return dx < dy;
        if (x.identity.nOfDay() != y.identity.nOfDay()) return x.identity.nOfDay() < y.identity.nOfDay();
        return x.identity.folderName() < y.identity.folderName();
    });
    const bool was = running();
    for (int i = 0; i < entries.size(); ++i)
        m_queue.append(Item{ id, entries[i].id(), i == entries.size() - 1 });
    if (entries.isEmpty())
        finishAthlete(id);
    if (running() != was)
        emit runningChanged();
    step();
}

void AthleteCatchUp::runAll()
{
    if (!m_callables.listAthletes)
        return;
    for (const AthleteId &id : m_callables.listAthletes())
        run(id);
}

void AthleteCatchUp::cancel()
{
    const bool was = running();
    m_queue.clear();
    if (m_current)
        letGo(/*finished=*/false);
    if (was != running())
        emit runningChanged();
}

void AthleteCatchUp::setPaused(bool on)
{
    if (m_paused == on)
        return;
    m_paused = on;
    if (on && m_current) {
        const Item again = m_item;
        letGo(/*finished=*/false);
        m_queue.prepend(again);   // taken again first when the pause lifts
    }
    if (!on)
        step();
}

void AthleteCatchUp::step()
{
    // One session at a time; re-entrancy (a synchronous load settles inside takeNext) loops here instead of recursing.
    if (m_stepping) {
        m_stepQueued = true;
        return;
    }
    m_stepping = true;
    do {
        m_stepQueued = false;
        if (!m_current && !m_paused && !m_queue.isEmpty())
            takeNext();
    } while (m_stepQueued);
    m_stepping = false;
}

void AthleteCatchUp::takeNext()
{
    m_item = m_queue.takeFirst();
    m_athlete = m_callables.loadAthlete ? m_callables.loadAthlete(m_item.athlete) : nullptr;
    const auto skip = [this](const QString &why) {
        ++m_report.skipped;
        m_report.why << why;
        if (m_item.last) finishAthlete(m_item.athlete);
        if (m_queue.isEmpty()) emit finished();
        m_stepQueued = true;
    };
    if (!m_athlete) {
        skip(QStringLiteral("an athlete could not be loaded: ") + m_item.athlete.toString());
        return;
    }
    Session *s = m_athlete->session(m_item.session);
    if (!s) {
        skip(QStringLiteral("a session is not this athlete's: ") + m_item.session.toString());
        return;
    }
    ++m_report.sessions;
    m_current = s;
    s->addUse();   // held while it works (Q-18: released after)
    if (s->shots().isEmpty()) {          // nothing was hit in it: there is nothing to say (today's rule)
        ++m_report.skipped;
        letGo(/*finished=*/true);
        return;
    }
    m_diagnostics = s->diagnostics();
    if (!m_diagnostics) {
        ++m_report.skipped;
        m_report.why << QStringLiteral("no diagnostics for %1 (the composition root wired no doors)").arg(s->id().toString());
        letGo(/*finished=*/true);
        return;
    }
    // EVERY WRITE IT FAILS WHILE IT IS IN HAND IS REPORTED (review CN-2; DEC-126): heard from now until after its release
    // (the release writes what it kept: §4.4 step 8), and one that failed while it loaded (a club baseline that could not
    // be frozen) is reported now.
    m_blockedThis = false;
    {
        const auto e = s->entry();
        m_where = e ? e->folder : s->id().toString();
    }
    m_failWatch = connect(m_diagnostics, &SessionDiagnostics::writeFailed, this,
                          [this](const QString &why) { reportBlock(why); }, Qt::DirectConnection);
    if (m_diagnostics->counts().writeFailures > 0)
        reportBlock(m_diagnostics->lastWriteError());
    const auto check = [this] { settleCurrent(); };
    m_waits << connect(m_diagnostics, &SessionDiagnostics::batchSettled, this, check, Qt::QueuedConnection);
    m_waits << connect(m_diagnostics, &SessionDiagnostics::diagnosisApplied, this, check, Qt::QueuedConnection);
    settleCurrent();
}

void AthleteCatchUp::settleCurrent()
{
    if (!m_current || !m_diagnostics)
        return;
    if (!m_diagnostics->idle())
        return;   // asks or a reduction still out: the next settle comes back here
    m_diagnostics->flush();   // a failure is heard (writeFailed → reportBlock)
    if (!m_blockedThis)
        ++m_report.written;
    letGo(/*finished=*/true);
}

void AthleteCatchUp::reportBlock(const QString &why)
{
    if (!m_blockedThis) {
        m_blockedThis = true;
        ++m_report.blocked;
    }
    m_report.why << m_where + QStringLiteral(": ") + why;
    ppWarn() << "[CatchUp] a session's files were not written:" << m_where << "-" << why;
    emit writeBlocked(QStringLiteral("The session's diagnostics"), m_where, why);
}

void AthleteCatchUp::letGo(bool done)
{
    for (const QMetaObject::Connection &c : std::as_const(m_waits))
        disconnect(c);
    m_waits.clear();
    Session *s = m_current;
    Athlete *a = m_athlete;
    const Item item = m_item;
    m_current = nullptr;
    m_diagnostics = nullptr;
    if (s) {
        s->dropUse();
        QString whyNot;
        if (a && a->releaseSession(s->id(), &whyNot))   // writes what it kept first: a failure is still heard
            ++m_report.released;
        else
            ppInfo() << "[CatchUp] a session was kept (" << whyNot << "):" << s->id().toString();
    }
    disconnect(m_failWatch);
    m_failWatch = {};
    if (!done) {
        ++m_report.interrupted;
        return;
    }
    if (item.last)
        finishAthlete(item.athlete);
    if (m_queue.isEmpty()) {
        emit runningChanged();
        emit finished();
        return;
    }
    // The next one on a later turn of the event loop (a library of them in one go would stall the GUI thread); in a
    // synchronous run step() loops instead.
    if (m_stepping)
        m_stepQueued = true;
    else
        QTimer::singleShot(0, this, &AthleteCatchUp::step);
}

void AthleteCatchUp::finishAthlete(const AthleteId &id)
{
    Athlete *a = m_callables.loadAthlete ? m_callables.loadAthlete(id) : nullptr;
    AthleteDiagnostics *d = AthleteDiagnostics::of(a);
    if (!d)
        return;
    d->markStale();
    const WriteResult w = d->persist();   // nothing of its own yet (F1): Unchanged
    if (!w.ok()) {
        ++m_report.blocked;
        m_report.why << a->folder() + QStringLiteral(": ") + w.error;
        emit writeBlocked(QStringLiteral("The athlete's diagnostics"), a->folder(), w.error);
    }
}

} // namespace pinpoint
