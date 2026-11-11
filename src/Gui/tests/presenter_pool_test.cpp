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

// PresenterPool (charts/presenter_pool.h, #173 follow-up): a chart presenter's jobs on a pool it does not own.
//   A  destroying the owner while its job runs returns at once — the job is still blocked when it does;
//   B  that job's result, posted after, is never delivered to the dead owner;
//   C  a live owner's result is delivered, on the owner's thread;
//   D  waitForDone() waits for THIS owner's jobs only: another owner's blocked job does not hold it.
// Latches, not timings: every check is decided by the order things happen in, never by how long they took.

#include "presenter_pool.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>

static int g_fail = 0;
static void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

struct Latch {
    std::mutex m;
    std::condition_variable cv;
    bool open = false;
    void release() { { std::lock_guard<std::mutex> lk(m); open = true; } cv.notify_all(); }
    void wait() { std::unique_lock<std::mutex> lk(m); cv.wait(lk, [this] { return open; }); }
};

// A presenter stand-in: a QObject owning a PresenterPool, as the chart presenters do.
struct Owner : QObject {
    PresenterPool pool{ this };
    int delivered = 0;
    Qt::HANDLE deliveredOn = nullptr;
};

static void drain(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("presenter_pool_test — destroying a presenter never waits for its job (#173)\n");

    std::printf("\nA/B — the owner goes while its job is blocked\n");
    {
        auto gate = std::make_shared<Latch>();
        auto started = std::make_shared<Latch>();
        auto jobDone = std::make_shared<std::atomic<bool>>(false);
        auto deliveredToDead = std::make_shared<std::atomic<int>>(0);
        auto *o = new Owner;
        auto post = o->pool.poster();
        o->pool.start([post, gate, started, jobDone, deliveredToDead]() {
            started->release();
            gate->wait();                                   // held until after the owner is gone
            jobDone->store(true);
            post([deliveredToDead]() { deliveredToDead->fetch_add(1); });
        });
        started->wait();
        delete o;                                           // must not wait for the blocked job
        check(!jobDone->load(), "A  the owner was destroyed while its job was still running");
        gate->release();
        for (int i = 0; i < 200 && !jobDone->load(); ++i) QThread::msleep(5);
        drain(100);
        check(jobDone->load(), "   the job finished afterwards, on its own");
        check(deliveredToDead->load() == 0, "B  and its result was not delivered to the dead owner");
    }

    std::printf("\nC — a live owner gets its result, on its own thread\n");
    {
        Owner o;
        auto post = o.pool.poster();
        Owner *op = &o;
        o.pool.start([post, op]() {
            post([op]() { ++op->delivered; op->deliveredOn = QThread::currentThreadId(); });
        });
        check(o.pool.waitForDone(5000), "   waitForDone: the job ran");
        drain(100);
        check(o.delivered == 1, "C  the result was delivered once");
        check(o.deliveredOn == QThread::currentThreadId(), "   on the owner's (this) thread");
    }

    std::printf("\nD — waitForDone waits for this owner's jobs only\n");
    {
        auto gate = std::make_shared<Latch>();
        auto started = std::make_shared<Latch>();
        Owner busy, idle;
        busy.pool.start([gate, started]() { started->release(); gate->wait(); });
        started->wait();
        check(idle.pool.waitForDone(0), "D  an owner with nothing running is idle while another's job is blocked");
        check(!busy.pool.waitForDone(20), "   the blocked owner is not");
        gate->release();
        check(busy.pool.waitForDone(5000), "   and is once its job ends");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
