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
#include "../Diagnostics/derived/club_distances.h"   // kClubDistanceWindowSessions
#include "../Core/pp_debug.h"

#include <QDir>
#include <QPointer>
#include <QSet>
#include <QHash>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace pinpoint {

AthleteCatchUp::AthleteCatchUp(AthleteCallables callables, QObject *parent)
    : QObject(parent), m_callables(std::move(callables))
{
    m_releaseTimer.setInterval(30'000);
    connect(&m_releaseTimer, &QTimer::timeout, this, &AthleteCatchUp::retryReleases);
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

// The order an athlete's asked sessions are taken in (refactor 3.7-e round 2; review R1-B1, J-49): FIRST the newest
// kClubDistanceWindowSessions sessions — the ones a session about to be played reads its club baseline from, so the next
// live session's baseline is computable within seconds of a launch or a rule bump, not after the whole library — oldest
// first among them; THEN the rest, oldest first (each session's own baseline is read from its predecessors). A session
// that already holds a frozen baseline does not depend on the order.
static void baselineFirst(QList<SessionIndexEntry> &entries)
{
    std::stable_sort(entries.begin(), entries.end(), [](const SessionIndexEntry &x, const SessionIndexEntry &y) {
        const QDate dx = x.identity.localDay(), dy = y.identity.localDay();
        if (dx.isValid() != dy.isValid()) return dx.isValid();
        if (dx != dy) return dx < dy;
        if (x.identity.nOfDay() != y.identity.nOfDay()) return x.identity.nOfDay() < y.identity.nOfDay();
        return x.identity.folderName() < y.identity.folderName();
    });
    const int newest = std::min<int>(int(entries.size()), analysis::kClubDistanceWindowSessions);
    QList<SessionIndexEntry> ordered = entries.mid(entries.size() - newest);
    ordered += entries.mid(0, entries.size() - newest);
    entries = ordered;
}

void AthleteCatchUp::runSessions(const AthleteId &id, const QList<SessionId> &sessions, const QSet<SessionId> &baselineOnly)
{
    if (id.isNull() || !m_callables.loadAthlete)
        return;
    Athlete *a = m_callables.loadAthlete(id);
    const bool was = running();
    // In place of this athlete's still queued (the scan that asked again knows better), but for those a live session's
    // baseline is waiting on (catchUpForBaseline); the one in hand stays in hand.
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&id](const Item &i) { return i.athlete == id && !i.forLive; }),
                  m_queue.end());
    QHash<SessionId, SessionIndexEntry> byId;
    if (a)
        for (const SessionIndexEntry &e : a->index())
            if (!e.id().isNull()) byId.insert(e.id(), e);
    QList<SessionIndexEntry> entries;
    QList<SessionId> notHere;
    for (const SessionId &sid : sessions) {
        const auto it = byId.constFind(sid);
        if (it == byId.constEnd()) notHere << sid;
        else                       entries.append(it.value());
    }
    baselineFirst(entries);
    const bool inHand = m_current && m_item.athlete == id;
    for (const SessionIndexEntry &e : std::as_const(entries)) {
        if (inHand && m_item.session == e.id()) continue;   // already in hand: answered when it is let go
        if (std::any_of(m_queue.begin(), m_queue.end(), [&e](const Item &q) { return q.session == e.id(); })) continue;
        Item it;
        it.athlete      = id;
        it.session      = e.id();
        it.baselineOnly = baselineOnly.contains(e.id());
        m_queue.append(it);
    }
    // The last of this athlete's asks marks it (its AthleteDiagnostics told once the queue has passed it).
    for (int i = m_queue.size() - 1; i >= 0; --i)
        if (m_queue[i].athlete == id) { m_queue[i].last = true; break; }
    for (const SessionId &sid : std::as_const(notHere)) {
        ++m_report.skipped;
        m_report.why << QStringLiteral("not a session of this athlete's index: ") + sid.toString();
        emit sessionCaughtUp(sid, QStringLiteral("not this athlete's session (no Session holds it)"));
    }
    if (running() != was)
        emit runningChanged();
    if (!running()) {
        if (a && !entries.isEmpty()) finishAthlete(id);
        emit finished();
        return;
    }
    step();
}

// THE LIVE SESSION'S CLUB BASELINE AT START (refactor 3.7-e round 2; review R1-B1, J-49): when the session just started
// here cannot freeze its baseline because earlier sessions are not summed up by this build yet, exactly those are taken
// in hand first — and taken even while the catch-up is paused for the live session — and the live session's owner is
// then asked to freeze again. True when a freeze was asked now or the predecessors were queued for it.
bool AthleteCatchUp::catchUpForBaseline(const AthleteId &id, const SessionId &live)
{
    Athlete *a = m_callables.loadAthlete ? m_callables.loadAthlete(id) : nullptr;
    AthleteDiagnostics *ad = AthleteDiagnostics::of(a);
    if (!ad)
        return false;
    const ClubBaselineComputation c = ad->clubBaselineFor(live);
    if (c.computable) {
        if (Session *s = a->loadedSession(live))
            if (SessionDiagnostics *d = s->diagnostics()) d->freezeClubBaselineIfNeeded();
        emit liveBaselineSettled(live);
        return true;
    }
    if (c.waitingOn.isEmpty())
        return false;   // not computable for another reason (no day): nothing a catch-up can do
    const bool was = running();
    QList<Item> front;
    for (int k = c.waitingOn.size() - 1; k >= 0; --k) {   // oldest first (waitingOn is newest first)
        const SessionId sid = c.waitingOn.at(k);
        if (m_current && m_item.session == sid) { m_item.forLive = true; continue; }
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&sid](const Item &q) { return q.session == sid; }),
                      m_queue.end());
        Item it;
        it.athlete = id;
        it.session = sid;
        it.forLive = true;
        front << it;
    }
    m_queue = front + m_queue;
    m_liveAthlete = id;
    m_liveSession = live;
    ppInfo() << "[CatchUp] the live session's club baseline waits on" << c.waitingOn.size()
             << "earlier session(s): summing them up first";
    if (running() != was)
        emit runningChanged();
    step();
    return true;
}

// After the last of a live session's predecessors: ask its owner to freeze now.
void AthleteCatchUp::settleLiveBaseline()
{
    if (m_liveSession.isNull())
        return;
    if (m_current && m_item.forLive)
        return;
    for (const Item &q : std::as_const(m_queue))
        if (q.forLive) return;
    const SessionId live = std::exchange(m_liveSession, SessionId());
    Athlete *a = m_callables.loadAthlete ? m_callables.loadAthlete(m_liveAthlete) : nullptr;
    if (Session *s = a ? a->loadedSession(live) : nullptr)
        if (SessionDiagnostics *d = s->diagnostics()) d->freezeClubBaselineIfNeeded();
    emit liveBaselineSettled(live);
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
    if (on && m_current && !m_item.forLive) {   // a live session's baseline waits on the one in hand: it finishes
        const Item again = m_item;
        letGo(/*finished=*/false);
        // Taken again first when the pause lifts — but AFTER any session a live session's baseline waits on (review R2-B1:
        // Start asks catchUpForBaseline before its pause arrives, so those may already be queued in front).
        int at = 0;
        while (at < m_queue.size() && m_queue.at(at).forLive)
            ++at;
        m_queue.insert(at, again);
    }
    // Paused or not: a live session's waited-on predecessors are taken (step), whatever stands at the front.
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
        if (!m_current && !m_queue.isEmpty()) {
            if (m_paused) {
                // WHILE PAUSED, ONLY WHAT A LIVE SESSION'S BASELINE WAITS ON, wherever it stands in the queue (review R2-B1).
                const auto it = std::find_if(m_queue.begin(), m_queue.end(), [](const Item &q) { return q.forLive; });
                if (it == m_queue.end())
                    break;
                const Item live = *it;
                m_queue.erase(it);
                m_queue.prepend(live);
            }
            takeNext();
        }
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
        emit sessionCaughtUp(m_item.session, why);
        if (m_item.last) finishAthlete(m_item.athlete);
        settleLiveBaseline();
        if (m_queue.isEmpty()) emit finished();
        m_stepQueued = true;
    };
    if (!m_athlete) {
        skip(QStringLiteral("an athlete could not be loaded: ") + m_item.athlete.toString());
        return;
    }
    // NOT THE LIVE ONE (it is the capture's): a session recorded here and still in progress is left alone.
    if (Session *live = m_athlete->loadedSession(m_item.session); live && live->runningHere() && live->inProgress()) {
        skip(QStringLiteral("the session is being recorded: ") + m_item.session.toString());
        return;
    }
    // A NEWER BUILD'S SESSION (R-PERS-3; refactor 4-e, REVIEW-e B2 and §5): its summary was saved by a newer build, so it
    // is not taken — loading it would re-grade its ledger and write this build's diagnostics.json over the newer one, on
    // every launch. The athlete level counts it as a newer build's (notSummedUp's newerFolders), and Home says so.
    if (AthleteDiagnostics *ad = AthleteDiagnostics::of(m_athlete); ad && ad->summaryFromNewerBuild(m_item.session)) {
        skip(QStringLiteral("its summary is a newer build's, read-only here: ") + m_item.session.toString());
        return;
    }
    // A SESSION THAT WANTS ONLY ITS BASELINE is not loaded while the freeze is not computable: loading it would re-diagnose
    // and write the same files again, each time the scan asks (the freeze is the one thing it lacks).
    if (m_item.baselineOnly && !m_athlete->loadedSession(m_item.session)) {
        AthleteDiagnostics *ad = AthleteDiagnostics::of(m_athlete);
        const ClubBaselineComputation c = ad ? ad->clubBaselineFor(m_item.session) : ClubBaselineComputation{};
        if (!c.computable) {
            skip(QStringLiteral("its club baseline is not computable yet (") + c.why + QStringLiteral("): ")
                 + m_item.session.toString());
            return;
        }
    }
    m_loadedHere = !m_athlete->loadedSession(m_item.session);
    Session *s = m_athlete->session(m_item.session);
    if (!s) {
        skip(QStringLiteral("a session is not this athlete's: ") + m_item.session.toString());
        return;
    }
    ++m_report.sessions;
    m_current = s;
    s->addUse();   // held while it works (Q-18: released after, when this service loaded it)
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
    // ONE FREEZE RULE (J-a8; CARRY 3.7-0): a session held since before its predecessors were summed up — the live one at its
    // End, one a screen opened — is asked for its baseline at first need again (a no-op once it holds one).
    m_diagnostics->freezeClubBaselineIfNeeded();
    // A SESSION ALREADY HELD IN THIS PROCESS (a screen opened it: J-e8) is re-checked against its documents on disk, as a
    // load would (review R1-m2 (c)): one re-analysed elsewhere is judged again before its summary is written.
    if (!m_loadedHere)
        m_diagnostics->recheckDocuments();
    const auto check = [this] { settleCurrent(); };
    // THE SESSION TRASHED WHILE IN HAND (refactor 3.7-e: the catch-up now runs in the app, beside the Sessions list's trash):
    // Athlete::trashSession deletes the Session whatever its uses, so the session in hand can go from under it. It is
    // answered (gone) and the next one taken, rather than the queue waiting for ever on a settle that cannot come.
    const SessionId inHand = s->id();
    m_waits << connect(s, &QObject::destroyed, this, [this, inHand] {
        if (m_current || m_item.session != inHand) return;
        m_report.why << QStringLiteral("the session went away while in hand: ") + inHand.toString();
        letGo(/*finished=*/true);
    }, Qt::QueuedConnection);
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
    // What it keeps, and its summary even when nothing is owed: the session was asked for because its summary on disk is
    // stale (3.7-e). A failure is heard (writeFailed → reportBlock).
    m_diagnostics->flushWithSummary();
    // TAKEN BEFORE ITS PREDECESSORS (the newest sessions go first: J-49) and so with no baseline it could freeze: asked once
    // more at the end of the pass, by when they are summed up, to freeze it then (and write again).
    if (m_diagnostics->clubBaselineWaiting() && !m_item.again) {
        Item later = m_item;
        later.baselineOnly = true;
        later.again        = true;
        later.forLive      = false;
        later.last         = false;
        m_queue.append(later);
    }
    if (m_diagnostics->writesHeld()) {   // an archive is out (or was made from this hold): nothing written (R1-m7)
        ++m_report.skipped;
        m_report.why << m_where + QStringLiteral(": not written, its archive is out");
    } else if (!m_blockedThis) {
        ++m_report.written;
    }
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
    const bool loadedHere = std::exchange(m_loadedHere, false);
    m_current = nullptr;
    m_diagnostics = nullptr;
    if (s) {
        s->dropUse();
        QString whyNot;
        if (!loadedHere) {
            ppInfo() << "[CatchUp] a session it did not load stays loaded:" << s->id().toString();
        } else if (a && a->releaseSession(s->id(), &whyNot)) {   // writes what it kept first: a failure is still heard
            ++m_report.released;
        } else {
            // Held by someone else for now (housekeeping's archive, a report): released once nobody holds it (R1-m7).
            ppInfo() << "[CatchUp] a session it loaded is held (" << whyNot << "): released once free:" << s->id().toString();
            releaseWhenFree(item.athlete, s->id());
        }
    }
    disconnect(m_failWatch);
    m_failWatch = {};
    if (done)
        emit sessionCaughtUp(item.session, QString());   // a pause takes it again later; a cancel's asker has moved on
    settleLiveBaseline();
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

// A SESSION LOADED ON DEMAND THAT SOMEONE ELSE STILL HELD WHEN ITS LOADER LET GO (review R1-m7): the catch-up's own, or the
// report make's (main.cpp). Tried again every 30 s until it is released or no longer loaded; a session in use or being
// recorded is refused by Athlete::releaseSession, as always.
void AthleteCatchUp::releaseWhenFree(const AthleteId &athlete, const SessionId &session)
{
    if (athlete.isNull() || session.isNull())
        return;
    for (const auto &p : std::as_const(m_releaseOwed))
        if (p.second == session) return;
    m_releaseOwed.append({ athlete, session });
    if (!m_releaseTimer.isActive())
        m_releaseTimer.start();
}

void AthleteCatchUp::retryReleases()
{
    for (int i = m_releaseOwed.size() - 1; i >= 0; --i) {
        const auto [athlete, session] = m_releaseOwed.at(i);
        Athlete *a = m_callables.loadAthlete ? m_callables.loadAthlete(athlete) : nullptr;
        if (!a || !a->loadedSession(session) || (m_current && m_current->id() == session && m_item.athlete == athlete)) {
            if (!a || !a->loadedSession(session)) m_releaseOwed.removeAt(i);
            continue;
        }
        if (a->releaseSession(session))
            m_releaseOwed.removeAt(i);
    }
    if (m_releaseOwed.isEmpty())
        m_releaseTimer.stop();
}

void AthleteCatchUp::followResumes(Athlete *athlete)
{
    if (!athlete || m_followed.contains(athlete))
        return;
    m_followed.insert(athlete);
    connect(athlete, &QObject::destroyed, this, [this, athlete] { m_followed.remove(athlete); });
    QPointer<Athlete> a(athlete);
    connect(athlete, &Athlete::sessionResumed, this, [this, a](const SessionId &id) {
        // A turn later: the resume's own work (the Session marked as recorded here) is done by then.
        QTimer::singleShot(0, this, [this, a, id] {
            Session *s = a ? a->loadedSession(id) : nullptr;
            SessionDiagnostics *d = s ? s->diagnostics() : nullptr;
            if (s && d && s->runningHere() && s->inProgress() && d->clubBaselineWaiting()) {
                ppInfo() << "[CatchUp] a session resumed with its club baseline still waiting: its predecessors first -" << id.toString();
                catchUpForBaseline(a->id(), id);
            }
        });
    });
}

void AthleteCatchUp::finishAthlete(const AthleteId &id)
{
    Athlete *a = m_callables.loadAthlete ? m_callables.loadAthlete(id) : nullptr;
    AthleteDiagnostics *d = AthleteDiagnostics::of(a);
    if (!d)
        return;
    d->markStale();
    // Its caches (FILE-ATH-02/03, refactor 3.7-f): what its jobs have landed and not written yet. It computes nothing here.
    const WriteResult w = d->persist();
    if (!w.ok()) {
        ++m_report.blocked;
        m_report.why << a->folder() + QStringLiteral(": ") + w.error;
        emit writeBlocked(QStringLiteral("The athlete's diagnostics"), a->folder(), w.error);
    }
}

} // namespace pinpoint
