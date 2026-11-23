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

#pragma once

// THE CHARTS PRESENTERS' ONE BASE (refactor 5.5a; F-08; DUTIES-5.5 §C P1–P6): what the six presenters (Traces,
// Relations, Wrist, Posture, Club delivery, Consistency) and the Trust presenter share, in one place, where each used to
// carry its own drifted copy:
//   • THE MODEL: a typed SeriesSource (the session's own series, or a test's), never a QObject resolved by qobject_cast
//     on every read (F-12); its snapshotChanged the one trigger.
//   • THE SNAPSHOT'S PRECEDENCE (P2): a snapshot set directly (a test, the report) wins; else the model's. Two of the six
//     had it the other way round.
//   • THE IDIOM (P4): a job on the presenter's ChartsLane (charts_work.h: the app's one WorkPool, its value through the
//     one WorkReceiver), its typed value handed back on the GUI thread only while the presenter lives — askLane() below.
//     The stills run on a lane of their own, so a compute never queues behind a decode (P10).
//   • THE SCHEDULER (P3): every presenter coalesces — one job in flight; a change meanwhile marks it dirty, and ONE more
//     job runs with the latest inputs when it lands — and is busy from the moment a job is asked for until its result
//     lands (P11: Consistency's `settling` reads the series' busy and then this one's, with no gap between them).
//   • THE SYNCHRONOUS SEAM (tests, the report's payloads): each presenter keeps its own read-time contract.
//   • THE ★ REFERENCE (refactor 5.5c): read through the ★ swing's own session's series, never by folder (referencePickOf).

#include "charts_work.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QMetaObject>
#include <QPointer>

#include <any>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace pinpoint::charts {

// The snapshot a presenter's next job reads (P2).
inline std::shared_ptr<const SessionSnapshot> chosenSnapshot(const std::shared_ptr<const SessionSnapshot> &direct,
                                                             const SeriesSource *model)
{
    if (direct) return direct;
    return model ? model->snapshot() : nullptr;
}

// Run `compute` (values only: no `this`) on `lane`; hand its value to `deliver` on the GUI thread (P4).
template <class Compute, class Deliver>
void askLane(ChartsLane &lane, Compute compute, Deliver deliver)
{
    using R = std::decay_t<decltype(compute())>;
    lane.ask([c = std::move(compute)]() mutable { return std::any(c()); },
             [d = std::move(deliver)](std::any &&v) mutable { d(std::any_cast<R>(std::move(v))); });
}

// ── THE ONE SCHEDULER (refactor 5.5c; P3, P5; review 5.5b m5) ──────────────────────────────────────────────────────
// The bookkeeping each of the six presenters used to carry its own copy of — `schedule`, the start's guard, the landing's
// re-run, the synchronous flush and `waitForIdle` — in one place. Coalesced: one job in flight; a change meanwhile marks
// the presenter dirty, and ONE more job runs with the latest inputs when it lands. The mechanics are moved, not changed,
// so each presenter keeps the policy it had:
//   Queued       Traces, Wrist, Posture, Club delivery. A change posts ONE start; the start does nothing while a job is in
//                flight (the landing re-runs a dirty presenter). Busy while a job is in flight. Synchronous (tests, the
//                report): nothing is posted, and a read of an output runs a dirty job inline (takeFlush).
//   BusyFromAsk  Relations, Consistency. Busy from the ask, before the posted start runs (P11: Consistency's `settling`
//                reads the series' busy and then this one's, with no gap between them). Synchronous: the start runs at once.
// The presenter keeps its own start (what it copies into the job) and its own apply; it calls begin() at the top of the
// start, land() and afterLanding() around its apply, and emits its own busyChanged after begin() launches.
class ChartsScheduler
{
public:
    enum class Policy { Queued, BusyFromAsk };

    // `start` is the presenter's own start (GUI thread); `busyChanged` emits its signal.
    ChartsScheduler(QObject *owner, Policy policy, std::function<void()> start, std::function<void()> busyChanged)
        : m_owner(owner), m_policy(policy), m_start(std::move(start)), m_busyChanged(std::move(busyChanged)),
          m_dirty(policy == Policy::Queued) {}   // a Queued presenter starts dirty: its first read or start computes
    ChartsScheduler(const ChartsScheduler &) = delete;
    ChartsScheduler &operator=(const ChartsScheduler &) = delete;

    // An input changed.
    void schedule()
    {
        if (m_policy == Policy::BusyFromAsk) {
            if (m_synchronous) { m_start(); return; }
            m_dirty = true;
            if (m_running || m_scheduled) return;
            m_scheduled = true;
            m_busyChanged();
            QMetaObject::invokeMethod(m_owner, [this] {
                m_scheduled = false;
                if (!m_running) m_start();
            }, Qt::QueuedConnection);
            return;
        }
        m_dirty = true;
        if (m_synchronous || m_scheduled) return;
        m_scheduled = true;
        QMetaObject::invokeMethod(m_owner, [this] { m_start(); }, Qt::QueuedConnection);
    }

    // The top of the presenter's start: true when it launches a job now. Its inputs are then clean, and (unless
    // synchronous) a job is in flight. Queued: false while synchronous or while a job is in flight.
    bool begin()
    {
        if (m_policy == Policy::Queued) {
            m_scheduled = false;
            if (m_synchronous || m_running) return false;
            m_dirty = false;
            m_running = true;
            return true;
        }
        m_dirty = false;
        if (!m_synchronous) m_running = true;
        return true;
    }

    // The job's value is being applied (GUI thread): nothing is in flight now. Whether something was.
    bool land()
    {
        const bool was = m_running;
        m_running = false;
        return was;
    }
    // After the apply: one more job when the inputs moved meanwhile; else, when a job had been in flight, busy is over.
    void afterLanding(bool wasRunning)
    {
        if (m_dirty && !m_synchronous) m_start();
        else if (wasRunning) m_busyChanged();
    }

    // Synchronous: a read runs a dirty job inline. True once per change (the presenter then computes and applies).
    bool takeFlush()
    {
        if (!m_synchronous || !m_dirty) return false;
        m_dirty = false;
        return true;
    }

    bool busy() const { return m_running || (m_policy == Policy::BusyFromAsk && m_scheduled); }
    bool idle() const { return !m_running && !m_scheduled && !m_dirty; }
    bool synchronous() const { return m_synchronous; }
    void setSynchronousFlag(bool on) { m_synchronous = on; }
    bool dirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }

    // Spin the GUI thread until nothing is posted, dirty or in flight (one predicate for all six: P5). Synchronous: the
    // presenter's flush, at once. False on timeout. Tests, probes and the report.
    bool waitForIdle(ChartsLane &lane, int msTimeout, const std::function<void()> &flush = {})
    {
        if (m_synchronous) { if (flush) flush(); return true; }
        QDeadlineTimer deadline(msTimeout);
        while (!idle() && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            lane.waitForDone(20);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return idle();
    }

private:
    QObject              *m_owner;
    Policy                m_policy;
    std::function<void()> m_start;
    std::function<void()> m_busyChanged;
    bool m_dirty;
    bool m_scheduled   = false;
    bool m_running     = false;
    bool m_synchronous = false;
};

// ── THE ★ REFERENCE (refactor 5.5c; J-58, J-ii3) ─────────────────────────────────────────────────────────────────
// The ★ is a swing of any of the golfer's sessions ("it stays your reference across sessions"). A presenter is handed
// two series: its own (`model`) and the ★ swing's session's (`referenceModel`: the same one when the ★ is one of this
// session's shots; else, in QML, ChartsSeries.of(app.sessionAt(<the ★'s session>)), as a Trust view finds a swing in
// another session). The ★ is picked on the GUI thread when a job is asked for, and handed to the job by value, so no
// presenter reads another session's swing document.
//   • One of THIS session's shots: the snapshot's own shot, as before (its place and findings with it).
//   • Another session's: that session's shot as its series read it, WITHOUT its place or findings, so the ★ draws its Δ
//     only and is never graded, as when it was read from its document.
//   • None: why, in the golfer's words (the three sentences the presenters have always said).
struct ReferencePick {
    std::shared_ptr<const ShotSeries> shot;   // null: none (why says why)
    QString                           why;
    bool                              pending = false;   // the ★'s session is still being read: its words come next
};

// The ★ copy the presenter keeps between jobs, so another session's shot is copied once per change of that shot.
struct ReferenceMemo {
    std::shared_ptr<const ShotSeries> source;
    std::shared_ptr<const ShotSeries> copy;
};

inline QString chartsCleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

inline ReferencePick referencePickOf(const QString &referenceSwingDir, const SeriesSource *referenceModel,
                                     const std::shared_ptr<const SessionSnapshot> &own, ReferenceMemo &memo)
{
    ReferencePick p;
    const QString dir = chartsCleanDir(referenceSwingDir);
    if (dir.isEmpty()) {
        p.why = QStringLiteral("No reference swing is set: ★ marks one, and it stays your reference across sessions.");
        return p;
    }
    if (own)
        for (const auto &s : own->shots)
            if (s && chartsCleanDir(s->swingDir) == dir) { p.shot = s; return p; }
    const std::shared_ptr<const SessionSnapshot> theirs = referenceModel ? referenceModel->snapshot() : nullptr;
    if (theirs && theirs != own)
        for (const auto &s : theirs->shots) {
            if (!s || chartsCleanDir(s->swingDir) != dir) continue;
            if (memo.source != s) {
                auto c = std::make_shared<ShotSeries>(*s);
                c->ordinal     = 0;                     // not one of this session's places
                c->diagnostics = ChartShotFindings();   // its Δ only: never graded here (J-ii3)
                memo.source = s;
                memo.copy   = std::move(c);
            }
            p.shot = memo.copy;
            return p;
        }
    memo = ReferenceMemo();
    const QString skipped = referenceModel ? referenceModel->skipReasonOf(dir) : QString();
    if (!skipped.isEmpty() && skipped != QLatin1String("no swing document")) {
        p.why = QStringLiteral("The reference swing cannot be read: %1.").arg(skipped);
    } else if (referenceModel && referenceModel->busy() && skipped.isEmpty()) {
        p.pending = true;
        p.why = QStringLiteral("The reference swing is still being read.");
    } else {
        // no session holds its folder, its swing has gone, or it has no document
        p.why = QStringLiteral("The reference swing is no longer on disk.");
    }
    return p;
}

} // namespace pinpoint::charts
