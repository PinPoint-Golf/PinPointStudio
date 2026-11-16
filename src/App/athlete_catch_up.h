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

// AthleteCatchUp — THE CATCH-UP, an application service (refactor step 3.6; build/refactor/step-3/BRIEF.md §4.6, §3.5;
// docs/developer/information_model.md CLS-ATHLETE-DIAG's "the athlete catch-up (an application service) asks it to
// recompute and persist its caches"; DEC-60, DEC-167, DEC-190).
//
// WHAT IT DOES: for an athlete, every ENDED session, OLDEST FIRST by its day and place in the day (SessionIdentity's),
// so each session's club baseline is frozen from predecessors already summed up (DEC-46, DEC-172): it asks the
// session's Session for its diagnostics (loading the Session runs them: SessionDiagnostics re-asks exactly the shots
// whose stamp is stale, freezes the baseline at first need, reduces), waits until they settle, asks them to write
// what they keep (diagnostics.json and the session summary, work_ons.json: SessionDiagnostics their one writer), and
// RELEASES the Session (DEC-190's follow-up, Q-18: it holds one at a time, a use added while it works, released
// after). Then it asks the athlete's AthleteDiagnostics to persist (nothing of its own until 3.7: WorkOnsController writes FILE-ATH-02/03 there) and marks it
// stale.
// It writes nothing itself and includes no repository (D18 (c)): it reaches the athletes through the composition
// root's callables (AthleteCallables) and their sessions through the Athlete (its index, then session(id) one at a
// time — Athlete::sessions() would hold every session at once).
//
// ITS WRITE BLOCKS ARE REPORTED, NEVER ONLY LOGGED (04 step 3 item 8; DEC-126): every failed write of a session's files
// (its diagnostics, its summary, its club baseline) while the session is in hand — its load, its settle, and its release,
// which writes what it kept even when a pause or a cancel lets it go unfinished (review CN-2) — is emitted as
// writeBlocked(what, where, why) for the caller's one notice route (today WorkOnsController::storageProblem →
// NotificationCenter), and the session counted once in the report.
//
// PAUSED AND CANCELLED (audit D-15): setPaused(true) — a session is live — lets go of the session in hand AT ONCE
// (released: what it kept is written, its waiting jobs cancelled) and puts it back at the front of the queue;
// cancel() — the athlete switched — drops the queue too. Neither waits for the session in hand to finish.
//
// NOT IN THE APP YET: the composition root wires it at the catch-up's cut-over (step 3.7), when SessionDiagnostics
// persists there; until then WorkOnsController's own catch-up (its private model) drives Home. athlete_dump
// --engine new and athlete_diagnostics_test drive it headless (DiagnosticsDoors::synchronous).
//
// Application ring (src/App: DEC-11). GUI thread.

#include "app.h"   // AthleteCallables

#include "../Identity/identity.h"

#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace pinpoint {

class Athlete;
class Session;
class SessionDiagnostics;

class AthleteCatchUp : public QObject
{
    Q_OBJECT
public:
    explicit AthleteCatchUp(AthleteCallables callables, QObject *parent = nullptr);
    ~AthleteCatchUp() override;

    // Queue every ended session of `athlete` (oldest first), and start. A second ask for an athlete already queued
    // changes nothing. runAll: every athlete the library lists.
    void run(const AthleteId &athlete);
    void runAll();
    void cancel();
    void setPaused(bool on);
    bool paused() const { return m_paused; }
    bool running() const { return m_current != nullptr || !m_queue.isEmpty(); }

    struct Report {
        int sessions  = 0;   // sessions taken in hand
        int written   = 0;   // whose diagnostics wrote what they keep (or had nothing new to write)
        int skipped   = 0;   // nothing hit in it, not this athlete's, or its diagnostics not wired (no doors)
        int blocked   = 0;   // a write of its files failed (reported)
        int released  = 0;   // let go after (Athlete::releaseSession)
        int interrupted = 0; // let go unfinished (a pause or a cancel)
        QStringList why;     // one line per skip or block
    };
    const Report &report() const { return m_report; }

signals:
    // DEC-126: what could not be written, where, and why — for the one notice route.
    void writeBlocked(const QString &what, const QString &where, const QString &why);
    void runningChanged();
    void finished();   // the queue is empty (not after a cancel)

private:
    struct Item { AthleteId athlete; SessionId session; bool last = false; };
    void step();
    void takeNext();
    void settleCurrent();
    void letGo(bool done);
    void reportBlock(const QString &why);
    void finishAthlete(const AthleteId &athlete);

    AthleteCallables     m_callables;
    QList<Item>          m_queue;
    bool                 m_paused = false;
    bool                 m_stepping = false;
    bool                 m_stepQueued = false;
    Item                 m_item;
    QPointer<Athlete>    m_athlete;
    QPointer<Session>    m_current;
    QPointer<SessionDiagnostics> m_diagnostics;
    QList<QMetaObject::Connection> m_waits;
    QMetaObject::Connection m_failWatch;   // its writeFailed, heard while in hand and through its release (CN-2)
    bool                 m_blockedThis = false;
    QString              m_where;
    Report               m_report;
};

} // namespace pinpoint
