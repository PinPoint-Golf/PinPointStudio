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

// The one worker pool, its serial lanes and the post-to-owner receiver. The rules are in work_pool.h.

#include "work_pool.h"

#include "cpu_topology.h"

#include <QCoreApplication>
#include <QEvent>
#include <QThread>

#include <atomic>
#include <chrono>
#include <exception>
#include <tuple>
#include <utility>

namespace pinpoint {

// ── The counter ──────────────────────────────────────────────────────────────────────────────────────
quint64 nextWorkNumber() noexcept
{
    static std::atomic<quint64> counter { 0 };
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

// ── The pool ─────────────────────────────────────────────────────────────────────────────────────────
int WorkPool::defaultThreadCount()
{
    const int half = physicalCoreCount() / 2;
    return half > 2 ? half : 2;
}

WorkPool::WorkPool(int threads)
{
    const int n = threads > 0 ? threads : defaultThreadCount();
    m_threads.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        QThread *t = QThread::create([this] { workerLoop(); });
        t->setObjectName(QStringLiteral("pp-work-%1").arg(i));
        t->start(QThread::NormalPriority);   // explicit, so a worker's priority is known (workerLoop)
        m_threads.push_back(t);
    }
}

WorkPool::~WorkPool()
{
    std::vector<Entry> dropped;   // destroyed outside the lock (a capture's destructor may do anything)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
        for (auto &[token, lane] : m_lanes) {
            for (Entry &e : lane.waiting) dropped.push_back(std::move(e));
            lane.waiting.clear();
        }
        m_queued = 0;
    }
    dropped.clear();   // the unrun jobs' captured values are released now, before the joins
    m_work.notify_all();
    {
        // Every worker says it has left its loop, under the pool's own mutex, before the threads are
        // joined: what a job did is then ordered before the destructor by a std::mutex, not only by
        // QThread::wait()'s internals.
        std::unique_lock<std::mutex> lk(m_mutex);
        // Only the workers that started can say so (a QThread that failed to start would hang this).
        int started = 0;
        for (QThread *t : m_threads) started += (t->isRunning() || t->isFinished()) ? 1 : 0;
        m_idle.wait(lk, [this, started] { return m_exited >= started; });
    }
    for (QThread *t : m_threads) {
        t->wait();
        delete t;
    }
    m_threads.clear();
}

bool WorkPool::submit(LaneToken lane, Job job, WorkPriority priority, quint64 tag)
{
    if (!job || lane == 0) return false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_stop) return false;
        Lane &l      = m_lanes[lane];
        l.cancelled  = false;
        l.waiting.push_back(Entry { std::move(job), priority, ++m_seq, tag });
        ++m_queued;
    }
    m_work.notify_one();
    return true;
}

int WorkPool::cancelLane(LaneToken lane, std::vector<quint64> *droppedTags)
{
    std::deque<Entry> dropped;   // destroyed after the lock is let go
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_lanes.find(lane);
        if (it == m_lanes.end()) return 0;
        dropped.swap(it->second.waiting);
        m_queued -= int(dropped.size());
        if (droppedTags)
            for (const Entry &e : dropped) droppedTags->push_back(e.tag);
        if (it->second.running) {
            it->second.cancelled  = true;
            it->second.foreground = false;
        } else {
            m_lanes.erase(it);
        }
    }
    m_idle.notify_all();
    return int(dropped.size());
}

void WorkPool::setForeground(LaneToken lane, bool foreground)
{
    if (lane == 0) return;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_stop) return;
        auto it = m_lanes.find(lane);
        if (it == m_lanes.end()) {
            if (!foreground) return;   // a background lane needs no entry
            it = m_lanes.emplace(lane, Lane {}).first;
        }
        it->second.foreground = foreground;
    }
    m_work.notify_one();   // the order of what waits may have changed
}

int WorkPool::waiting(LaneToken lane) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_lanes.find(lane);
    return it == m_lanes.end() ? 0 : int(it->second.waiting.size());
}

bool WorkPool::isRunning(LaneToken lane) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_lanes.find(lane);
    return it != m_lanes.end() && it->second.running;
}

bool WorkPool::waitForDone(int msecs)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    auto done = [this] { return m_running == 0 && m_queued == 0; };
    if (msecs < 0) {
        m_idle.wait(lk, done);
        return true;
    }
    return m_idle.wait_for(lk, std::chrono::milliseconds(msecs), done);
}

std::map<LaneToken, WorkPool::Lane>::iterator WorkPool::pickLocked()
{
    // Higher priority first; then a foreground lane; then the job submitted earliest. A lane with a job
    // on a worker is not a candidate: that is what makes it serial.
    auto best = m_lanes.end();
    std::tuple<int, int, quint64> bestKey { -1, -1, 0 };
    for (auto it = m_lanes.begin(); it != m_lanes.end(); ++it) {
        const Lane &l = it->second;
        if (l.running || l.waiting.empty()) continue;
        const Entry &head = l.waiting.front();
        // seq is inverted so that "larger key" means "submitted earlier".
        const std::tuple<int, int, quint64> key { int(head.priority), l.foreground ? 1 : 0, ~head.seq };
        if (best == m_lanes.end() || key > bestKey) {
            best    = it;
            bestKey = key;
        }
    }
    return best;
}

void WorkPool::workerLoop()
{
    QThread *self = QThread::currentThread();
    for (;;) {
        std::unique_lock<std::mutex> lk(m_mutex);
        auto it = m_lanes.end();
        m_work.wait(lk, [&] {
            if (m_stop) return true;
            it = pickLocked();
            return it != m_lanes.end();
        });
        if (m_stop) {
            ++m_exited;
            lk.unlock();
            m_idle.notify_all();
            return;
        }

        const LaneToken token = it->first;
        Entry entry           = std::move(it->second.waiting.front());
        it->second.waiting.pop_front();
        it->second.running = true;
        --m_queued;
        ++m_running;
        lk.unlock();

        // D24: a Low job runs at the OS's low thread priority, so capture and analysis win the CPU.
        // (Honoured on Windows and macOS; Linux's default scheduler ignores it.) Only the JOB runs low: the
        // worker is back at normal priority before it takes m_mutex again, so a starved low thread can
        // never hold the lock the GUI thread's submit / cancelLane need (Windows' SRW locks have no
        // priority inheritance).
        const bool low = entry.priority == WorkPriority::Low;
        if (low) self->setPriority(QThread::LowPriority);

        try {
            entry.job();
        } catch (...) {
            // A bare job that throws must not take the worker with it. postToOwner() catches first and
            // posts the error, so an owner's batch still settles.
        }
        entry.job = nullptr;   // release the job's captured values before the lane can move on
        if (low) self->setPriority(QThread::NormalPriority);

        lk.lock();
        --m_running;
        bool more = false;
        auto again = m_lanes.find(token);
        if (again != m_lanes.end()) {
            again->second.running = false;
            more                  = !again->second.waiting.empty();
            if (again->second.cancelled && !more) m_lanes.erase(again);
        }
        lk.unlock();
        m_idle.notify_all();
        if (more) m_work.notify_one();
    }
}

// ── The receiver ─────────────────────────────────────────────────────────────────────────────────────
const char *workVerdictName(WorkVerdict v) noexcept
{
    switch (v) {
    case WorkVerdict::Kept: return "kept";
    case WorkVerdict::OwnerGone: return "owner gone";
    case WorkVerdict::OtherInstance: return "another instance";
    case WorkVerdict::ItemGone: return "item gone";
    case WorkVerdict::Superseded: return "superseded";
    }
    return "?";
}

namespace {
QEvent::Type drainEventType()
{
    static const QEvent::Type t = QEvent::Type(QEvent::registerEventType());
    return t;
}
} // namespace

class WorkReceiver::Context : public QObject
{
public:
    explicit Context(WorkReceiver *rx) : m_rx(rx) {}
    bool event(QEvent *e) override
    {
        if (e->type() == drainEventType()) {
            m_rx->drain();
            return true;
        }
        return QObject::event(e);
    }
private:
    WorkReceiver *m_rx;
};

WorkReceiver::WorkReceiver(WorkResolver resolve)
    : m_resolve(std::move(resolve))
    , m_context(std::make_unique<Context>(this))
{
    drainEventType();   // registered here, on the receiver's thread, before any worker posts
}

WorkReceiver::~WorkReceiver() = default;   // Qt discards a wake still queued for the context

void WorkReceiver::post(WorkResult result)
{
    bool wake = false;
    {
        std::lock_guard<std::mutex> lk(m_inboxMutex);
        m_inbox.push_back(std::move(result));
        wake          = !m_wakePending;
        m_wakePending = true;
    }
    if (wake) QCoreApplication::postEvent(m_context.get(), new QEvent(drainEventType()));
}

void WorkReceiver::drain()
{
    // One result at a time, taken under the mutex. An owner's deliver() may spin an event loop; a drain
    // nested in it then carries on with the NEXT result, so delivery stays first in, first out (a lane's
    // results in lane order). The wake is cleared only when the inbox is found empty, under the same
    // mutex, so a post after that wakes the thread again.
    for (;;) {
        WorkResult r;
        {
            std::lock_guard<std::mutex> lk(m_inboxMutex);
            if (m_inbox.empty()) {
                m_wakePending = false;
                return;
            }
            r = std::move(m_inbox.front());
            m_inbox.pop_front();
        }
        accept(std::move(r));
    }
}

WorkVerdict WorkReceiver::accept(WorkResult result)
{
    const WorkAddress &a = result.address;
    WorkTarget         t = m_resolve ? m_resolve(a) : WorkTarget {};

    WorkVerdict v;
    if (t.instance == 0 || !t.deliver) v = WorkVerdict::OwnerGone;
    else if (t.instance != a.instance) v = WorkVerdict::OtherInstance;
    else if (!t.itemFound) v = WorkVerdict::ItemGone;
    else if (a.ask == 0 || t.latestAsk != a.ask) v = WorkVerdict::Superseded;   // exact match only
    else v = WorkVerdict::Kept;

    ++m_counts[int(v)];
    if (v == WorkVerdict::Kept || v == WorkVerdict::ItemGone || v == WorkVerdict::Superseded)
        t.deliver(v, std::move(result));
    return v;
}

quint64 WorkReceiver::count(WorkVerdict v) const noexcept
{
    const int i = int(v);
    return i >= 0 && i < 5 ? m_counts[i] : 0;
}

bool postToOwner(WorkPool &pool, WorkReceiver &receiver, LaneToken lane, WorkAddress address,
                 std::function<std::any()> work, WorkPriority priority)
{
    if (!work) return false;
    WorkReceiver *rx = &receiver;
    const quint64 tag = address.ask;   // read before the address is moved into the job
    return pool.submit(
        lane,
        [rx, address = std::move(address), work = std::move(work)]() mutable {
            WorkResult r;
            r.address = address;
            try {
                r.value = work();
            } catch (const std::exception &e) {
                r.value.reset();
                r.error = QString::fromUtf8(e.what());
                if (r.error.isEmpty()) r.error = QStringLiteral("the job threw");
            } catch (...) {
                r.value.reset();
                r.error = QStringLiteral("the job threw");
            }
            work = nullptr;   // the values are released on the worker, not on the receiver's thread
            rx->post(std::move(r));
        },
        priority, tag);
}

} // namespace pinpoint
