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

// ChartsLane (charts/charts_work.h; refactor 5.5a, re-pointed from presenter_pool_test): the charts' one threading
// idiom — a lane per owner on the one WorkPool, results through the one WorkReceiver.
//   A  destroying the owner while its job runs returns at once — the job is still blocked when it does;
//   B  that job's result, landing after, is never applied to the dead owner ("owner gone");
//   C  a live owner's result is applied once, on the owner's (GUI) thread;
//   D  waitForDone() waits for THIS lane's jobs only: another lane's blocked job does not hold it;
//   E  an older ask answered after a newer one was asked is dropped ("superseded"): only the latest is applied;
//   F  an installed pool and receiver are the ones used (the app's), and the resolver finds the lane by its address.
// Latches, not timings: every check is decided by the order things happen in, never by how long they took.

#include "charts_work.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>

using pinpoint::charts::ChartsLane;

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

// A presenter stand-in: a QObject owning a ChartsLane, as the chart presenters do.
struct Owner : QObject {
    ChartsLane lane{ this };
    int delivered = 0;
    int lastValue = 0;
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

static void run()
{
    std::printf("\nA/B — the owner goes while its job is blocked\n");
    {
        auto gate = std::make_shared<Latch>();
        auto started = std::make_shared<Latch>();
        auto jobDone = std::make_shared<std::atomic<bool>>(false);
        auto appliedToDead = std::make_shared<std::atomic<int>>(0);
        auto *o = new Owner;
        o->lane.ask([gate, started, jobDone]() {
            started->release();
            gate->wait();                                   // held until after the owner is gone
            jobDone->store(true);
            return std::any(1);
        }, [appliedToDead](std::any &&) { appliedToDead->fetch_add(1); });
        started->wait();
        delete o;                                           // must not wait for the blocked job
        check(!jobDone->load(), "A  the owner was destroyed while its job was still running");
        gate->release();
        for (int i = 0; i < 200 && !jobDone->load(); ++i) QThread::msleep(5);
        drain(100);
        check(jobDone->load(), "   the job finished afterwards, on its own");
        check(appliedToDead->load() == 0, "B  and its result was not applied to the dead owner");
    }

    std::printf("\nC — a live owner gets its result, on its own thread\n");
    {
        Owner o;
        Owner *op = &o;
        o.lane.ask([]() { return std::any(QThread::currentThreadId() != nullptr); },
                   [op](std::any &&) { ++op->delivered; op->deliveredOn = QThread::currentThreadId(); });
        check(o.lane.waitForDone(5000), "   waitForDone: the job ran and came back");
        check(o.delivered == 1, "C  the result was applied once");
        check(o.deliveredOn == QThread::currentThreadId(), "   on the owner's (this) thread");
    }

    std::printf("\nD — waitForDone waits for this lane's jobs only\n");
    {
        auto gate = std::make_shared<Latch>();
        auto started = std::make_shared<Latch>();
        Owner busy, idle;
        busy.lane.ask([gate, started]() { started->release(); gate->wait(); return std::any(0); }, [](std::any &&) {});
        started->wait();
        check(idle.lane.waitForDone(0), "D  an owner with nothing in flight is idle while another's job is blocked");
        check(!busy.lane.waitForDone(20), "   the blocked owner is not");
        gate->release();
        check(busy.lane.waitForDone(5000), "   and is once its job ends");
    }

    std::printf("\nE — only the latest ask is applied\n");
    {
        auto gate = std::make_shared<Latch>();
        auto started = std::make_shared<Latch>();
        Owner o;
        Owner *op = &o;
        o.lane.ask([gate, started]() { started->release(); gate->wait(); return std::any(1); },
                   [op](std::any &&v) { ++op->delivered; op->lastValue = std::any_cast<int>(v); });
        started->wait();
        o.lane.ask([]() { return std::any(2); }, [op](std::any &&v) { ++op->delivered; op->lastValue = std::any_cast<int>(v); });
        gate->release();
        check(o.lane.waitForDone(5000), "   both came back");
        check(o.delivered == 1 && o.lastValue == 2, "E  the older answer was dropped, the newer applied");
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("charts_work_test — the charts' one idiom: a lane on the one pool, results through the one receiver\n");
    std::printf("\n-- the charts' own pool (nothing installed: a test, a tool) --\n");
    run();

    std::printf("\nF — an installed pool and receiver (the app's composition root)\n");
    {
        int resolved = 0;
        pinpoint::WorkReceiver receiver([&resolved](const pinpoint::WorkAddress &a) {
            if (ChartsLane::owns(a)) { ++resolved; return ChartsLane::target(a); }
            return pinpoint::WorkTarget{};
        });
        pinpoint::WorkPool pool(2);
        ChartsLane::install(&pool, &receiver);
        run();
        check(resolved > 0 && receiver.count(pinpoint::WorkVerdict::Kept) > 0,
              "F  the installed receiver resolved the lanes' results through ChartsLane::target");
        ChartsLane::uninstall();
    }

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
