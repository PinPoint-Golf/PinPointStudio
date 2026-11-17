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

// THE ONE WORKER POOL, A SERIAL LANE PER OWNER, AND THE POST-TO-OWNER RECEIVER (R-CON-5; refactor step
// 3.1; Mark, 15 Nov 2026: D15, IM-S3-8). Platform ring (a per-file entry in the architecture lint's ring
// table, RING-PLATFORM): it knows nothing domain-shaped. An owner is named by text and numbers, never by a
// type of an outer ring, so any ring may use it. Design: build/refactor/step-3/BRIEF.md §4.4.
//
// The parts:
//   nextWorkNumber()  the ONE process-wide counter (64-bit; 1, 2, 3, …; never 0, never repeats in a
//                     process). A Session instance takes its number from it when it is loaded, and every ask
//                     (a shot's fire, a session's diagnosis) takes the next one. Because all numbers come from
//                     one counter, a result asked by a released instance can never equal anything the
//                     reloaded instance asks (REVIEW-2's race; 3.5's test T1).
//   WorkPool          a fixed set of worker threads running jobs from SERIAL LANES. A lane is keyed by its
//                     owner's token (a Session instance's number): one lane's jobs run one at a time, in the
//                     order they were submitted; different lanes run side by side, so a catch-up or a sweep
//                     never blocks the live session. cancelLane() drops a lane's waiting jobs (a release); the
//                     one running finishes and its result is dropped at the receiver. Priorities: a job's
//                     WorkPriority picks which lane's head runs next (higher first) and, for Low, runs the
//                     job at the OS's low thread priority, so capture and analysis threads win the CPU
//                     (D24: firing runs at low priority). Among equal priorities a FOREGROUND lane (the live
//                     session's) runs first, then the job submitted earliest.
//   WorkReceiver      the long-lived receiver on the owner (GUI) thread, held by the composition root. A
//                     job's result is POSTED to it by address — (owner id, owner instance number, item id,
//                     ask number) — and the receiver resolves that address at delivery, through ONE function
//                     the composition root hands it (its identity map; no interface is added for it, U9). It
//                     checks, in order: the owner gone → dropped; a different instance of it → dropped; the
//                     item gone → dropped, the owner told (the batch settles); the ask not EXACTLY the item's
//                     latest → dropped as superseded, the owner told; otherwise kept: handed to the owner.
//   postToOwner()     submits a job on a lane whose value is posted to the receiver under its address.
//
// The rules a job keeps (BRIEF.md §4.4; R-CON-2, R-CON-3):
//   - A JOB HOLDS VALUES ONLY: ids, numbers, immutable snapshots (shared_ptr<const …>), a thread-safe reader.
//     Never a QObject, a `this`, or a reference to a live member. The type system cannot check a lambda's
//     captures, so this is the rule every caller keeps and its reviewer checks. The one exception is
//     postToOwner()'s own: the job it builds holds the WorkReceiver it posts to, which is long-lived and
//     whose post() any thread may call.
//   - A worker never writes a file or touches a model: it returns a value, and the owner applies it on its
//     own thread.
//   - Nothing ever waits on the pool for one owner: a released owner cancels its lane and goes; whatever
//     was running is dropped at the receiver by its instance number. Deferred delete follows from this: an
//     owner may delete an item (a trashed Shot, with deleteLater()) at any time, because the receiver holds
//     no pointer to it and resolves the address afresh when the result arrives.
//   - Every result the receiver can still route to its owner reaches it, kept or dropped (Kept, ItemGone,
//     Superseded), so a batch that counts its asks always settles. A job that throws still posts a result
//     (its `error` set, its value empty), so it settles too.
//   - A tool or sweep with no worker threads calls the job and WorkReceiver::accept() itself, synchronously
//     on its main thread (R-HL-2); the checks are the same.
//
// Lifetime: the composition root creates the receiver before the pool and destroys the pool first (its
// destructor drops the waiting jobs and joins the workers, whose running jobs may still post), then the
// receiver. The rule binds every caller of WorkReceiver::post(), not only pool jobs: nothing may post once
// the receiver is being destroyed. A wake still queued for the receiver's thread then is discarded by Qt.
//
// ── Domain events: the conventions (BRIEF.md §4.2; DEC-69, DEC-70; information_model.md §7.4) ──────────
// Not code in this file (3.1's row in §6 lists no events header): the rules every producer and listener of
// EVT-01…EVT-23 (less EVT-11, DEC-154) keeps, written here beside the threading they lean on.
//   - Each event is a Qt signal on the object that ran the command (`Shot::clubCorrected(ShotRef)`,
//     `Session::shotAdded(ShotRef)`, …). No event bus: that would be a new threading idiom (R-CON-5).
//   - It is emitted on the owner thread, AFTER the repository write returned ok (§5.9), never before and
//     never for a failed write; it carries only the id of what changed (P7); a listener reads anything
//     else from the owner.
//   - Listeners connect with Qt::DirectConnection: delivery is synchronous, in command order (DEC-69).
//   - A Session connects to each Shot's signals at addShot and at load (P10), and runs §7.4's chain.
//   - A chain that re-asks several items asks them all; the owner's own reduction runs once the batch
//     settles, i.e. when every ask in it has come back through the receiver kept or dropped.
//   - Work an event starts runs on the owner's lane through postToOwner(); its result comes back through
//     the receiver, never through a signal emitted on a worker.
//
//   cmake --build build/tests --target work_pool_test -j 6
//   ctest --test-dir build/tests -R '^work_pool_test$' --output-on-failure

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <any>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QThread;

namespace pinpoint {

// ── The process-wide counter ─────────────────────────────────────────────────────────────────────────
// The next number: 1 the first time, then 2, 3, …; never 0 ("no number"), never the same twice in one
// process, whichever thread asks. The design takes numbers on the owner thread only; the counter is atomic
// anyway, so a tool's threads cannot break it either.
quint64 nextWorkNumber() noexcept;

// ── The pool and its lanes ───────────────────────────────────────────────────────────────────────────
enum class WorkPriority : int { Low = 0, Normal = 1, High = 2 };

// A lane's key: its owner's token. A Session's lane is keyed by its instance number, so a released and
// reloaded session gets a new lane, and the old one's cancelled jobs can never join it.
using LaneToken = quint64;

class WorkPool
{
public:
    using Job = std::function<void()>;

    // `threads` workers; 0 → defaultThreadCount(). They start at once and wait for work.
    explicit WorkPool(int threads = 0);
    // Drops every waiting job (unrun), then waits for the running ones to finish and joins the workers.
    ~WorkPool();
    WorkPool(const WorkPool &)            = delete;
    WorkPool &operator=(const WorkPool &) = delete;

    // At least two, so one lane cannot hold every worker; about half the physical cores, so the pool
    // never takes the machine from capture and pose inference (cpu_topology.h).
    static int defaultThreadCount();

    // Queue `job` at the back of `lane`. It runs after every job queued on that lane before it, and
    // never at the same time as another job of that lane. False (and nothing queued) for an empty job,
    // a zero token or a pool being destroyed. Any thread.
    bool submit(LaneToken lane, Job job, WorkPriority priority = WorkPriority::Normal);

    // Drop `lane`'s waiting jobs, unrun (their captured values are released here). A job of the lane
    // that is running finishes; the lane is forgotten (with its foreground mark) once it has. Returns how
    // many were dropped. A later submit with the same token starts the lane again, behind any running job.
    int cancelLane(LaneToken lane);

    // A foreground lane's head runs before a background lane's head of the same priority (D24: the live
    // session first). Remembered until the lane is cancelled.
    void setForeground(LaneToken lane, bool foreground);

    int  threadCount() const noexcept { return int(m_threads.size()); }
    int  waiting(LaneToken lane) const;   // queued, not yet started
    bool isRunning(LaneToken lane) const; // one of its jobs is on a worker now

    // Block until nothing is waiting or running (or `msecs` pass; < 0 waits for ever). For tests and an
    // orderly teardown; the app never waits on the pool for one owner.
    bool waitForDone(int msecs = -1);

private:
    struct Entry
    {
        Job          job;
        WorkPriority priority = WorkPriority::Normal;
        quint64      seq      = 0;   // submission order across the pool: the last tie-break
    };
    struct Lane
    {
        std::deque<Entry>  waiting;      // front = next
        bool               running    = false;
        bool               foreground = false;
        bool               cancelled  = false;   // forget the lane when its running job returns
    };

    void workerLoop();
    // Under m_mutex: the lane whose head should run next, or end().
    std::map<LaneToken, Lane>::iterator pickLocked();

    mutable std::mutex              m_mutex;
    std::condition_variable         m_work;   // a job became runnable, or stop
    std::condition_variable         m_idle;   // a job finished (waitForDone)
    std::map<LaneToken, Lane>       m_lanes;
    quint64                         m_seq     = 0;
    int                             m_running = 0;
    int                             m_queued  = 0;
    int                             m_exited  = 0;   // workers that have left workerLoop (the destructor)
    bool                            m_stop    = false;
    std::vector<QThread *>          m_threads;
};

// ── The receiver ─────────────────────────────────────────────────────────────────────────────────────
// Where a result is going. Text and numbers only (Platform ring): `owner` is the owner's id as text (a
// SessionId's toString()), `item` an item inside it (a ShotRef's toString(); empty for the owner's own
// work, its diagnosis), `instance` the owner instance's number and `ask` the ask's, both from
// nextWorkNumber().
struct WorkAddress
{
    QString owner;
    quint64 instance = 0;
    QString item;
    quint64 ask = 0;
};

struct WorkResult
{
    WorkAddress address;
    std::any    value;   // the job's values; the owner knows their type
    QString     error;   // non-empty: the job threw, and `value` is empty. Read by the owner as unread
};

enum class WorkVerdict : int {
    Kept,            // the ask is its item's latest: handed to the owner to keep
    OwnerGone,       // no owner of that id is loaded: dropped, nobody told
    OtherInstance,   // the owner was released and loaded again: dropped, nobody told
    ItemGone,        // the item is gone (a trashed shot): dropped, the owner told so its batch settles
    Superseded,      // a newer ask of the item is in its lane: dropped, the owner told so its batch settles
};
const char *workVerdictName(WorkVerdict v) noexcept;

// What the composition root's lookup answers for an address, resolved at delivery time.
struct WorkTarget
{
    quint64 instance  = 0;       // the loaded owner's instance number; 0: no owner of that id is loaded
    bool    itemFound = false;   // the item is still the owner's (an empty item: the owner itself)
    quint64 latestAsk = 0;       // that item's latest ask number
    // The owner's hand-off, called on the receiver's thread for Kept, ItemGone and Superseded, never for
    // OwnerGone or OtherInstance. Required whenever `instance` is non-zero.
    std::function<void(WorkVerdict, WorkResult &&)> deliver;
};
using WorkResolver = std::function<WorkTarget(const WorkAddress &)>;

class WorkReceiver
{
public:
    // Lives on the thread that constructs it (the GUI thread in the app, the main thread in a tool).
    explicit WorkReceiver(WorkResolver resolve);
    ~WorkReceiver();
    WorkReceiver(const WorkReceiver &)            = delete;
    WorkReceiver &operator=(const WorkReceiver &) = delete;

    // Any thread: queue `result` for accept() on the receiver's thread, in the order posted. Every caller,
    // not only a pool job, must have stopped posting before the receiver is destroyed.
    void post(WorkResult result);

    // The receiver's thread: resolve the address, check it, and hand the result on (see WorkVerdict).
    // Public for the synchronous path (a tool, a sweep) and the tests.
    WorkVerdict accept(WorkResult result);

    // How many results got each verdict (the receiver's thread).
    quint64 count(WorkVerdict v) const noexcept;

private:
    class Context;           // a QObject on the receiver's thread; a posted event there runs drain()
    friend class Context;
    void drain();            // the receiver's thread: accept() every result in the inbox, in order

    WorkResolver             m_resolve;
    // The hand-off from the workers: results wait here under a std::mutex, and one posted event (only
    // when none is pending) wakes the receiver's thread. The result itself never travels inside a Qt
    // event, so the hand-over is ordered by this mutex alone (one the thread sanitizer can see).
    std::mutex               m_inboxMutex;
    std::deque<WorkResult>   m_inbox;
    bool                     m_wakePending = false;
    quint64                  m_counts[5] = { 0, 0, 0, 0, 0 };
    std::unique_ptr<Context> m_context;   // declared last: destroyed first, so the wake goes before the inbox
};

// Run `work` on `lane` and post its value to `receiver` under `address`. `work` keeps the job rules above
// (values only). A `work` that throws posts a result with `error` set. False if the pool refused the job.
bool postToOwner(WorkPool &pool, WorkReceiver &receiver, LaneToken lane, WorkAddress address,
                 std::function<std::any()> work, WorkPriority priority = WorkPriority::Normal);

} // namespace pinpoint
