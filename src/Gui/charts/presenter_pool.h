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

// PresenterPool — a chart presenter's background jobs, on a pool the presenter does NOT own.
//
// ⚠ DESTROYING A PRESENTER NEVER WAITS FOR ITS JOB. Each presenter used to own a QThreadPool, and
// ~QThreadPool waits for the job it is running. PpModeStage rebuilds the chart panels on EVERY mode
// switch, so flipping Replay ↔ Analyse blocked the GUI thread until a job whose result nobody would
// read had finished: 427 of 574 main-thread samples sat in ~ChartsRelationsPresenter →
// QThreadPool::waitForDone, a 1.4 s freeze and a missed PPCP heartbeat (#173 follow-up, 11 Nov 2026).
//
// Now the jobs run on one pool shared by every presenter. It belongs to the application object, so
// quitting still joins it in order, before any static is torn down — a chart job is bounded, and none
// may outlive ORT or the app log. A presenter destroyed mid-job returns at once; the job finishes
// into nothing, because its result goes through a Poster, which delivers on the owner's thread only
// while the owner is alive. waitForDone() waits for THIS presenter's jobs only — the synchronous
// switch and the tests' waitForIdle() mean exactly what they meant with a private pool.
//
// THE RULE FOR A JOB BODY: it must not touch the presenter except inside the function it hands to
// the poster — the presenter may be gone by the time the job ends. Everything it reads is captured
// by value (the inputs, the worker and cache shared_ptrs), as every presenter's job already was.
//
//     auto post = m_pool.poster();
//     m_pool.start([post, w, in, this]() {
//         auto r = compute(*w, in);
//         post([this, r]() { apply(r); });     // runs on the GUI thread, and only if `this` lives
//     });

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

class PresenterPool
{
    struct State {
        std::mutex              m;
        std::condition_variable cv;
        int                     running = 0;
    };

public:
    // `owner` is the presenter; it must live on the thread its results are delivered on (the GUI
    // thread), which is also where start(), waitForDone() and poster() are called.
    explicit PresenterPool(QObject *owner) : m_owner(owner), m_state(std::make_shared<State>())
    {
        postBox();   // made here, on the owner's thread: results are delivered on the thread it lives in
    }
    ~PresenterPool() = default;   // never waits: a job still running finishes into nothing
    PresenterPool(const PresenterPool &) = delete;
    PresenterPool &operator=(const PresenterPool &) = delete;

    // Delivers a job's result on the owner's thread, and only while the owner is alive. Made on
    // the owner's thread (poster()), copied into the job, called from the worker.
    class Poster {
    public:
        template <class F>
        void operator()(F &&deliver) const
        {
            QMetaObject::invokeMethod(postBox(), [owner = m_owner, d = std::forward<F>(deliver)]() mutable {
                if (owner) d();
            }, Qt::QueuedConnection);
        }
    private:
        friend class PresenterPool;
        explicit Poster(QPointer<QObject> owner) : m_owner(std::move(owner)) {}
        QPointer<QObject> m_owner;
    };
    Poster poster() const { return Poster(m_owner); }

    // Run `job` on the shared pool.
    template <class F>
    void start(F &&job)
    {
        auto st = m_state;
        {
            std::lock_guard<std::mutex> lk(st->m);
            ++st->running;
        }
        shared()->start([st, j = std::forward<F>(job)]() mutable {
            j();
            std::lock_guard<std::mutex> lk(st->m);
            --st->running;
            st->cv.notify_all();
        });
    }

    // Wait until none of THIS presenter's jobs is running — forever with -1, else up to `msTimeout`.
    // True when none is. A result already posted is still queued; process events to receive it.
    bool waitForDone(int msTimeout = -1)
    {
        std::unique_lock<std::mutex> lk(m_state->m);
        const auto idle = [this] { return m_state->running == 0; };
        if (msTimeout < 0) {
            m_state->cv.wait(lk, idle);
            return true;
        }
        return m_state->cv.wait_for(lk, std::chrono::milliseconds(msTimeout), idle);
    }

private:
    // One pool for every presenter, owned by the application object (joined in ~QCoreApplication's
    // teardown, before statics). Re-made if a test makes a second application. Called on the GUI
    // thread only.
    static QThreadPool *shared()
    {
        static QPointer<QThreadPool> pool;
        if (!pool) {
            pool = new QThreadPool(QCoreApplication::instance());
            pool->setObjectName(QStringLiteral("ChartsPresenters"));
            pool->setMaxThreadCount(std::max(2, QThread::idealThreadCount()));
        }
        return pool;
    }
    // Where results are posted: an object of its own, never destroyed, so a job that ends while the
    // application is being torn down posts into a queue nobody reads rather than into a dying object.
    static QObject *postBox()
    {
        static QObject *box = new QObject;
        return box;
    }

    QPointer<QObject>      m_owner;
    std::shared_ptr<State> m_state;
};
