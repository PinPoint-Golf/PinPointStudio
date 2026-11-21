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

// The pose inference thread (src/Pose/inference_thread.h; step 3.7, J-43, DEC-194), with fakes:
//   - every task runs on ONE thread, not the caller's, whichever thread submits, in FIFO order;
//   - a task's exception reaches its caller, and the thread survives it;
//   - a task that submits to its own thread runs the work directly (no deadlock);
//   - stop() finishes what is queued, joins, refuses new work, and is idempotent;
//   - PoseRunner's pattern: an estimator bound to the thread before load(), held in the session
//     cache (InstanceCache) and driven by two runs on two fresh std::threads (as analysis_dag.h's
//     per-run pool does), is loaded and inferred on the one thread only. An unbound estimator, the
//     control, runs on the callers' threads (the bug: a new calling thread per run).
// PoseRunner::run itself takes no fake: it constructs the real ViTPose estimator, and this suite
// carries no ONNX Runtime. Its real path is covered by the 5-swing parity and the cabin bench
// (build/refactor/step-3/3.7/NOTES-P.md).

#include "../inference_thread.h"
#include "../pose_pipeline.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using pinpoint::pose::InferenceThread;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

namespace {

// Runs f on a thread of its own and fails the test (and ends it) if f has not finished within
// `seconds`: a deadlock must fail, not hang the suite.
template <class F>
void withinSeconds(const char *label, int seconds, F f)
{
    std::packaged_task<void()> task(std::move(f));
    std::future<void> done = task.get_future();
    std::thread t(std::move(task));
    if (done.wait_for(std::chrono::seconds(seconds)) != std::future_status::ready) {
        std::printf("  [FAIL] %s: still running after %d s (deadlock)\n", label, seconds);
        std::fflush(stdout);
        std::_Exit(1);
    }
    t.join();
    done.get();
}

struct CustomError : std::runtime_error {
    int code;
    explicit CustomError(int c) : std::runtime_error("custom"), code(c) {}
};

// PoseEstimatorViTPose's routing, verbatim in shape (pose_estimator_vitpose.cpp: load(),
// inferPrepared(), inferBatch()): bound and off the thread, hand the call to the thread and wait.
class FakeEstimator {
public:
    void bindInferenceThread(InferenceThread *t) { m_thread = t; }
    void load()
    {
        if (m_thread && !m_thread->onThread()) {
            m_thread->call([this] { load(); });
            return;
        }
        record();
        m_ready = true;
    }
    bool inferBatch(int frames)
    {
        if (m_thread && !m_thread->onThread())
            return m_thread->call([&] { return inferBatch(frames); });
        record();
        m_frames += frames;
        return true;
    }
    bool isReady() const { return m_ready; }
    int  frames() const { return m_frames; }
    std::set<std::thread::id> sessionThreads() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_sessionThreads;
    }

private:
    void record()   // "the ORT session was touched on this thread"
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_sessionThreads.insert(std::this_thread::get_id());
    }
    InferenceThread          *m_thread = nullptr;
    bool                      m_ready  = false;
    int                       m_frames = 0;
    mutable std::mutex        m_mutex;
    std::set<std::thread::id> m_sessionThreads;
};

using Key   = std::tuple<std::string, std::string, int>;   // PoseRunner's key shape
using Cache = pinpoint::analysis::InstanceCache<Key, FakeEstimator>;

// One "PoseRunner::run" as the analysis DAG makes it: on a NEW std::thread, lease the cached
// estimator (built, bound and loaded through the one door when absent), a batch-1 crop scan,
// then batches of 16 and a remainder. Returns once the run is done; its thread then waits for
// `exitGate`, so two runs' threads are alive together and their ids cannot be reused.
struct RunThread {
    std::thread     thread;
    std::thread::id id;
};
RunThread oneRun(Cache &cache, InferenceThread *bindTo, std::shared_future<void> exitGate)
{
    std::promise<std::thread::id> done;
    std::future<std::thread::id>  doneF = done.get_future();
    std::thread th([&cache, bindTo, exitGate, done = std::move(done)]() mutable {
        {
            Cache::Lease lease = cache.acquire(
                Key{ "vitpose-b", "cpu", 0 },
                [&] {
                    auto e = std::make_unique<FakeEstimator>();
                    e->bindInferenceThread(bindTo);   // acquireCached's bind, before load()
                    e->load();
                    return e;
                },
                [](const FakeEstimator &e) { return e.isReady(); });
            for (int i = 0; i < 12; ++i)
                lease->inferBatch(1);
            for (int b = 0; b < 9; ++b)
                lease->inferBatch(16);
            lease->inferBatch(7);
        }   // the lease is released: the run is over
        done.set_value(std::this_thread::get_id());
        exitGate.wait();
    });
    const std::thread::id id = doneF.get();
    return RunThread{ std::move(th), id };
}

} // namespace

int main()
{
    std::printf("=== one thread, FIFO ===\n");
    {
        InferenceThread t;
        const std::thread::id self = std::this_thread::get_id();
        CHECK("the thread is not the creator's", t.id() != self && !t.onThread());

        std::vector<int>             order;
        std::set<std::thread::id>    ran;
        std::vector<std::future<void>> fs;
        for (int i = 0; i < 200; ++i)
            fs.push_back(t.submit([&, i] {
                order.push_back(i);   // only the inference thread writes: no lock needed
                ran.insert(std::this_thread::get_id());
            }));
        for (auto &f : fs)
            f.get();
        bool fifo = order.size() == 200;
        for (int i = 0; fifo && i < 200; ++i)
            fifo = order[size_t(i)] == i;
        CHECK("200 tasks from one submitter ran in submission order", fifo);
        CHECK("... all on the one thread", ran.size() == 1 && *ran.begin() == t.id());

        // Four submitters at once: each one's tasks keep their order, and all run on one thread.
        std::mutex                         m;
        std::vector<std::vector<int>>      seen(4);
        std::set<std::thread::id>          ranOn, callers;
        std::vector<std::thread>           subs;
        for (int s = 0; s < 4; ++s)
            subs.emplace_back([&, s] {
                {
                    std::lock_guard<std::mutex> lk(m);
                    callers.insert(std::this_thread::get_id());
                }
                std::vector<std::future<void>> mine;
                for (int i = 0; i < 100; ++i)
                    mine.push_back(t.submit([&, s, i] {
                        std::lock_guard<std::mutex> lk(m);
                        seen[size_t(s)].push_back(i);
                        ranOn.insert(std::this_thread::get_id());
                    }));
                for (auto &f : mine)
                    f.get();
            });
        for (auto &th : subs)
            th.join();
        bool perSubmitter = true;
        for (const auto &v : seen) {
            perSubmitter = perSubmitter && v.size() == 100;
            for (size_t i = 0; perSubmitter && i < v.size(); ++i)
                perSubmitter = v[i] == int(i);
        }
        CHECK("four concurrent submitters: each one's 100 tasks in its order", perSubmitter);
        CHECK("... on the one thread, none on a submitter's", ranOn.size() == 1 && *ranOn.begin() == t.id()
                                                                 && callers.count(t.id()) == 0);

        // call() from fresh threads (the DAG's per-run pool): same thread, value returned.
        std::set<std::thread::id> callRan;
        int sum = 0;
        for (int r = 0; r < 5; ++r)
            std::thread([&, r] {
                sum += t.call([&, r] {
                    callRan.insert(std::this_thread::get_id());
                    return r * 10;
                });
            }).join();
        CHECK("call() from five fresh threads returns each value", sum == 100);
        CHECK("... and runs every one on the same thread", callRan.size() == 1 && *callRan.begin() == t.id());
    }

    std::printf("=== exceptions ===\n");
    {
        InferenceThread t;
        bool caught = false;
        try {
            t.call([]() -> int { throw std::runtime_error("boom"); });
        } catch (const std::runtime_error &e) {
            caught = std::string(e.what()) == "boom";
        }
        CHECK("call(): the task's exception is rethrown in the caller", caught);

        int code = 0;
        try {
            t.submit([] { throw CustomError(42); }).get();
        } catch (const CustomError &e) {
            code = e.code;
        }
        CHECK("submit().get(): the exception keeps its type and contents", code == 42);
        CHECK("the thread survives both and keeps running tasks", t.call([] { return 7; }) == 7);
    }

    std::printf("=== re-entrancy ===\n");
    {
        InferenceThread t;
        withinSeconds("a task's own call()/submit() never waits on itself", 10, [&] {
            const int v = t.call([&] {
                const int inner = t.call([] { return 2; });           // direct
                const int viaFuture = t.submit([] { return 3; }).get();   // ready at once
                return inner + viaFuture + (t.onThread() ? 10 : 0);
            });
            CHECK("a task calling call() and submit().get() on its own thread returns", v == 15);
        });
    }

    std::printf("=== shutdown ===\n");
    {
        auto t = std::make_unique<InferenceThread>();
        std::atomic<int> ran{ 0 };
        std::vector<std::future<void>> fs;
        for (int i = 0; i < 10; ++i)
            fs.push_back(t->submit([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ++ran;
            }));
        withinSeconds("stop() returns", 10, [&] {
            std::thread other([&] { t->stop(); });   // two stoppers at once
            t->stop();
            other.join();
        });
        CHECK("stop() ran every queued task before joining", ran.load() == 10);
        bool allReady = true;
        for (auto &f : fs)
            allReady = allReady && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        CHECK("... and every caller's future is ready", allReady);
        bool refused = false;
        try {
            t->submit([] {});
        } catch (const std::runtime_error &) {
            refused = true;
        }
        CHECK("after stop(), submit() is refused with an exception", refused);
        refused = false;
        try {
            t->call([] { return 1; });
        } catch (const std::runtime_error &) {
            refused = true;
        }
        CHECK("... and so is call()", refused);
        t->stop();   // idempotent
        t.reset();   // the destructor after a stop
        CHECK("stop() twice and then the destructor: no hang, no crash", true);

        // A task that stops its own thread: new work is refused; the destructor still joins.
        auto u = std::make_unique<InferenceThread>();
        withinSeconds("a task stopping its own thread", 10, [&] {
            u->call([&] { u->stop(); });
            bool refusedAfter = false;
            try {
                u->call([] { return 0; });
            } catch (const std::runtime_error &) {
                refusedAfter = true;
            }
            CHECK("a task that calls stop() on its own thread: later work is refused", refusedAfter);
            u.reset();
        });
        CHECK("... and the destructor joins it", !u);

        // The destructor alone (never stopped) drains and joins too.
        std::atomic<int> late{ 0 };
        {
            InferenceThread v;
            for (int i = 0; i < 5; ++i)
                v.submit([&] { ++late; });
        }
        CHECK("destroying an unstopped thread runs its queue and joins", late.load() == 5);
    }

    std::printf("=== PoseRunner's pattern: cached estimator, two runs, two fresh threads ===\n");
    {
        InferenceThread poseThread;
        Cache cache;
        std::promise<void> gate;
        const std::shared_future<void> exitGate = gate.get_future().share();
        RunThread r1 = oneRun(cache, &poseThread, exitGate);   // sequential runs, as face-on then DTL,
        RunThread r2 = oneRun(cache, &poseThread, exitGate);   // or one shot after another
        gate.set_value();
        r1.thread.join();
        r2.thread.join();
        const std::thread::id run1 = r1.id, run2 = r2.id;
        Cache::Lease lease = cache.acquire(Key{ "vitpose-b", "cpu", 0 },
                                           [] { return std::unique_ptr<FakeEstimator>(); },
                                           [](const FakeEstimator &) { return true; });
        const std::set<std::thread::id> used = lease->sessionThreads();
        CHECK("the two runs came from two different threads", run1 != run2);
        CHECK("one estimator was cached and reused", cache.size() == 1 && lease.cached());
        CHECK("both runs' frames were inferred", lease->frames() == 2 * (12 + 9 * 16 + 7));
        CHECK("its load and every inference ran on ONE thread", used.size() == 1);
        CHECK("... the inference thread, neither run's own", used.size() == 1 && *used.begin() == poseThread.id()
                                                               && used.count(run1) == 0 && used.count(run2) == 0);
    }
    {
        Cache cache;   // the control: unbound, as the cache was before J-43
        std::promise<void> gate;
        const std::shared_future<void> exitGate = gate.get_future().share();
        RunThread r1 = oneRun(cache, nullptr, exitGate);   // sequential runs, as face-on then DTL,
        RunThread r2 = oneRun(cache, nullptr, exitGate);   // or one shot after another
        gate.set_value();
        r1.thread.join();
        r2.thread.join();
        const std::thread::id run1 = r1.id, run2 = r2.id;
        Cache::Lease lease = cache.acquire(Key{ "vitpose-b", "cpu", 0 },
                                           [] { return std::unique_ptr<FakeEstimator>(); },
                                           [](const FakeEstimator &) { return true; });
        const std::set<std::thread::id> used = lease->sessionThreads();
        CHECK("control, unbound: the session is touched on each run's own thread (the bug this fixes)",
              used.size() == 2 && used.count(run1) == 1 && used.count(run2) == 1);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
