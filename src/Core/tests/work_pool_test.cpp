// Standalone tests for the one worker pool, its serial lanes and the post-to-owner receiver
// (src/Core/work_pool.h; R-CON-5; refactor step 3.1, D15; build/refactor/step-3/BRIEF.md §4.4 and §6).
//
// What it must do (§6's row 3.1): a lane keeps order; two lanes do not block each other; a cancelled lane
// drops its waiting jobs; the receiver drops a result whose session, instance number or shot it cannot
// resolve (and a superseded one), and keeps only an exact match; the counter never repeats a number across
// sessions. Also: priorities and the foreground lane (D24), a Low job at the OS's low thread priority, and a
// posted result arriving on the receiver's thread.
//
//   cmake --build build/tests --target work_pool_test -j 6
//   ctest --test-dir build/tests -R '^work_pool_test$' --output-on-failure

#include "../work_pool.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QThread>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace pinpoint;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// A one-shot gate a job can wait on, opened from the test thread.
class Gate
{
public:
    void open()
    {
        { std::lock_guard<std::mutex> lk(m); opened = true; }
        cv.notify_all();
    }
    bool wait(int ms = 5000)
    {
        std::unique_lock<std::mutex> lk(m);
        return cv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return opened; });
    }
private:
    std::mutex              m;
    std::condition_variable cv;
    bool                    opened = false;
};

// Run the receiver's thread's event loop until `done` or `ms` pass.
template <class F> static bool spinUntil(F done, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// ── A stand-in for the composition root's identity map ─────────────────────────────────────────────────
struct FakeSession
{
    quint64                 instance = 0;
    QHash<QString, quint64> latestAsk;   // shot id → its latest ask number
    std::vector<std::pair<WorkVerdict, WorkResult>> delivered;
};
struct FakeMap
{
    QHash<QString, std::shared_ptr<FakeSession>> sessions;   // session id → the loaded instance

    WorkResolver resolver()
    {
        return [this](const WorkAddress &a) {
            WorkTarget t;
            auto it = sessions.constFind(a.owner);
            if (it == sessions.constEnd()) return t;
            std::shared_ptr<FakeSession> s = *it;
            t.instance  = s->instance;
            t.itemFound = s->latestAsk.contains(a.item);
            t.latestAsk = s->latestAsk.value(a.item);
            t.deliver   = [s](WorkVerdict v, WorkResult &&r) { s->delivered.emplace_back(v, std::move(r)); };
            return t;
        };
    }
    std::shared_ptr<FakeSession> load(const QString &id)
    {
        auto s       = std::make_shared<FakeSession>();
        s->instance  = nextWorkNumber();
        sessions[id] = s;
        return s;
    }
};

static void laneKeepsOrder()
{
    std::printf("A lane keeps order\n");
    WorkPool pool(4);
    const LaneToken lane = nextWorkNumber();
    std::mutex       m;
    std::vector<int> ran;
    std::atomic<int> inLane { 0 }, maxInLane { 0 };
    for (int i = 0; i < 60; ++i) {
        pool.submit(lane, [&, i] {
            const int now = ++inLane;
            int prev = maxInLane.load();
            while (now > prev && !maxInLane.compare_exchange_weak(prev, now)) {}
            std::this_thread::sleep_for(std::chrono::microseconds((i * 37) % 400));
            { std::lock_guard<std::mutex> lk(m); ran.push_back(i); }
            --inLane;
        });
    }
    check(pool.waitForDone(10000), "the lane drained");
    bool inOrder = ran.size() == 60;
    for (int i = 0; inOrder && i < 60; ++i) inOrder = ran[size_t(i)] == i;
    check(inOrder, "60 jobs on four workers ran in the order they were submitted");
    check(maxInLane.load() == 1, "never two of one lane's jobs at once");
}

static void twoLanesDoNotBlock()
{
    std::printf("Two lanes do not block each other\n");
    WorkPool pool(2);
    const LaneToken a = nextWorkNumber(), b = nextWorkNumber();
    Gate             releaseA, aStarted, bDone;
    std::atomic<bool> aSecondRan { false }, bRan { false }, bRanWhileABlocked { false };
    std::atomic<bool> aBlocked { false };
    pool.submit(a, [&] { aBlocked = true; aStarted.open(); releaseA.wait(); aBlocked = false; });
    pool.submit(a, [&] { aSecondRan = true; });
    check(aStarted.wait(), "lane A's first job has started (and holds its lane until released)");
    pool.submit(b, [&] { bRan = true; bRanWhileABlocked = aBlocked.load(); bDone.open(); });

    check(bDone.wait() && bRan.load(), "lane B's job ran while lane A's first job was still running");
    check(bRanWhileABlocked.load(), "… really while A was blocked");
    check(!aSecondRan.load() && pool.waiting(a) == 1, "A's second job waits behind A's first (serial), with a worker free");
    releaseA.open();
    check(pool.waitForDone(5000), "both lanes drained");
    check(aSecondRan.load(), "A's second job ran once A's first returned");
}

static void cancelledLaneDropsWaiting()
{
    std::printf("A cancelled lane drops its waiting jobs\n");
    WorkPool pool(1);
    const LaneToken a = nextWorkNumber(), b = nextWorkNumber();
    Gate             releaseFirst, firstStarted;
    std::atomic<int> aRan { 0 };
    std::atomic<bool> firstFinished { false }, bRan { false };
    auto captured = std::make_shared<int>(7);   // a value a waiting job holds

    pool.submit(a, [&] { firstStarted.open(); releaseFirst.wait(); ++aRan; firstFinished = true; });
    for (int i = 0; i < 4; ++i) pool.submit(a, [&, captured] { ++aRan; });
    pool.submit(b, [&] { bRan = true; });
    check(firstStarted.wait(), "A's first job is running");
    check(pool.isRunning(a) && pool.waiting(a) == 4, "four of A's jobs wait behind it");
    check(captured.use_count() == 5, "the waiting jobs hold their values");

    check(pool.cancelLane(a) == 4, "cancelLane answers how many it dropped (4)");
    check(pool.waiting(a) == 0, "nothing of A waits");
    check(captured.use_count() == 1, "the dropped jobs' values are released at once");
    check(pool.isRunning(a), "the running job is not interrupted");
    releaseFirst.open();
    check(pool.waitForDone(5000), "the pool drained");
    check(firstFinished.load() && aRan.load() == 1, "the running job finished; none of the dropped ones ran");
    check(bRan.load(), "another lane's waiting job was not dropped");
    check(!pool.isRunning(a) && pool.waiting(a) == 0, "the cancelled lane is forgotten");

    std::atomic<bool> again { false };
    pool.submit(a, [&] { again = true; });
    pool.waitForDone(5000);
    check(again.load(), "a later submit with the same token runs");
    check(pool.cancelLane(nextWorkNumber()) == 0, "cancelling a lane that has nothing is 0");
}

static void submitAfterCancelWhileRunning()
{
    std::printf("A submit after cancelling a running lane waits for the running job\n");
    WorkPool pool(2);   // a second worker is free: only the lane's seriality keeps the new job back
    const LaneToken a = nextWorkNumber(), b = nextWorkNumber();
    Gate             started, release, bDone;
    std::atomic<bool> firstFinished { false }, secondRan { false }, secondSawFirstFinished { false };
    pool.submit(a, [&] { started.open(); release.wait(); firstFinished = true; });
    pool.submit(a, [] {});
    check(started.wait(), "the lane's first job is running");
    check(pool.cancelLane(a) == 1, "the cancel drops the one waiting job");
    pool.submit(a, [&] { secondSawFirstFinished = firstFinished.load(); secondRan = true; });
    // A job submitted after it on another lane: the free worker takes it. Had the new job of lane A been
    // runnable, the same worker would have taken that first (same priority, submitted earlier).
    pool.submit(b, [&] { bDone.open(); });
    check(bDone.wait(), "the free worker ran another lane's later job");
    check(!secondRan.load() && pool.waiting(a) == 1, "the new job of the cancelled lane still waits behind the running one");
    release.open();
    check(pool.waitForDone(5000), "drained");
    check(secondRan.load() && secondSawFirstFinished.load(), "it ran only after the running job returned (the lane stayed serial)");
}

static void destroyedWithJobsInFlight()
{
    std::printf("Destroying the pool with a job running and jobs waiting\n");
    auto              captured = std::make_shared<int>(1);
    Gate              started, release;
    std::atomic<bool> firstFinished { false };
    std::atomic<int>  waitingRan { 0 };
    std::thread       releaser;
    QElapsedTimer     t;
    {
        WorkPool pool(1);
        const LaneToken a = nextWorkNumber(), b = nextWorkNumber();
        pool.submit(a, [&] { started.open(); release.wait(10000); firstFinished = true; });
        for (int i = 0; i < 3; ++i) pool.submit(a, [&waitingRan, captured] { ++waitingRan; });
        pool.submit(b, [&waitingRan, captured] { ++waitingRan; });
        check(started.wait(), "one job is running, four wait (two lanes)");
        check(captured.use_count() == 5, "the waiting jobs hold their values");
        // Let the running job go only once the destructor has dropped the waiting ones (their values are
        // released before the joins), so a waiting job could only run if the destructor let it.
        releaser = std::thread([&] {
            QElapsedTimer w;
            w.start();
            while (captured.use_count() != 1 && w.elapsed() < 5000)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            release.open();
        });
        t.start();
    }   // ~WorkPool
    const qint64 took = t.elapsed();
    releaser.join();
    check(captured.use_count() == 1, "the waiting jobs' values were released");
    check(waitingRan.load() == 0, "no waiting job ran");
    check(firstFinished.load(), "the running job finished before the destructor returned");
    check(took < 5000, "the destructor returned (promptly, once the running job did)");
}

static void prioritiesAndForeground()
{
    std::printf("Priorities and the foreground lane (D24)\n");
    WorkPool pool(1);
    const LaneToken blocker = nextWorkNumber(), bgLow = nextWorkNumber(), fgLow = nextWorkNumber(),
                    normal = nextWorkNumber();
    Gate        release, started;
    std::mutex  m;
    std::vector<int> order;
    std::atomic<int> lowSeen { -1 }, normalSeen { -1 };
    auto note = [&](int who) { std::lock_guard<std::mutex> lk(m); order.push_back(who); };

    pool.submit(blocker, [&] { started.open(); release.wait(); });
    check(started.wait(), "the one worker is busy");
    pool.submit(bgLow, [&] { note(1); lowSeen = int(QThread::currentThread()->priority()); }, WorkPriority::Low);
    pool.submit(fgLow, [&] { note(2); }, WorkPriority::Low);
    pool.setForeground(fgLow, true);
    pool.submit(normal, [&] { note(3); normalSeen = int(QThread::currentThread()->priority()); }, WorkPriority::Normal);
    release.open();
    check(pool.waitForDone(5000), "drained");
    check(order == std::vector<int>({ 3, 2, 1 }),
          "Normal first; then, of two Low lanes, the foreground one, though the background one was queued first");
    // Qt's stored value: shows the call was made, not that the OS honours it.
    check(lowSeen.load() == int(QThread::LowPriority), "a Low job runs with its thread set to LowPriority (Qt's stored value)");
    check(normalSeen.load() == int(QThread::NormalPriority), "a Normal job runs with its thread at NormalPriority (Qt's stored value)");
    std::atomic<int> afterLow { -1 };
    pool.submit(normal, [&] { afterLow = int(QThread::currentThread()->priority()); });
    pool.waitForDone(5000);
    check(afterLow.load() == int(QThread::NormalPriority),
          "the worker is back at NormalPriority after a Low job (it never re-takes the lock low)");
    check(!pool.submit(0, [] {}) && !pool.submit(blocker, {}), "a zero token or an empty job is refused");
}

static void receiverChecks()
{
    std::printf("The receiver resolves by (session, instance, shot, ask) and drops what it cannot\n");
    FakeMap      map;
    WorkReceiver rx(map.resolver());

    auto s1         = map.load(QStringLiteral("S1"));
    const quint64 ask1 = nextWorkNumber();
    s1->latestAsk[QStringLiteral("S1/1")] = ask1;
    auto result = [](const QString &owner, quint64 instance, const QString &item, quint64 ask, int value) {
        WorkResult r;
        r.address = WorkAddress { owner, instance, item, ask };
        r.value   = value;
        return r;
    };

    check(rx.accept(result(QStringLiteral("S9"), s1->instance, QStringLiteral("S9/1"), ask1, 0)) == WorkVerdict::OwnerGone,
          "a session that is not loaded → dropped (owner gone)");
    check(s1->delivered.empty(), "… and nobody is told");

    check(rx.accept(result(QStringLiteral("S1"), s1->instance, QStringLiteral("S1/2"), ask1, 0)) == WorkVerdict::ItemGone,
          "a shot the session no longer has (trashed) → dropped (item gone)");
    check(s1->delivered.size() == 1 && s1->delivered.back().first == WorkVerdict::ItemGone,
          "… and the session is told, so its batch settles");

    const quint64 ask2 = nextWorkNumber();
    s1->latestAsk[QStringLiteral("S1/1")] = ask2;   // the shot was asked again
    check(rx.accept(result(QStringLiteral("S1"), s1->instance, QStringLiteral("S1/1"), ask1, 0)) == WorkVerdict::Superseded,
          "an ask that is not the shot's latest → dropped (superseded)");
    check(s1->delivered.size() == 2 && s1->delivered.back().first == WorkVerdict::Superseded, "… and the session is told");

    check(rx.accept(result(QStringLiteral("S1"), s1->instance, QStringLiteral("S1/1"), ask2, 42)) == WorkVerdict::Kept,
          "the shot's latest ask, on the loaded instance → kept");
    check(s1->delivered.size() == 3 && s1->delivered.back().first == WorkVerdict::Kept
              && std::any_cast<int>(s1->delivered.back().second.value) == 42,
          "… handed to the session with its value");

    check(rx.accept(result(QStringLiteral("S1"), s1->instance, QStringLiteral("S1/1"), 0, 0)) == WorkVerdict::Superseded,
          "ask number 0 is never a match");

    // Released and loaded again: the shot asked again by the new instance. A result of the old instance,
    // even one carrying the shot's id and a number the old instance asked, is never kept.
    const quint64 oldInstance = s1->instance;
    auto          s1b         = map.load(QStringLiteral("S1"));
    const quint64 ask3        = nextWorkNumber();
    s1b->latestAsk[QStringLiteral("S1/1")] = ask3;
    check(rx.accept(result(QStringLiteral("S1"), oldInstance, QStringLiteral("S1/1"), ask2, 0)) == WorkVerdict::OtherInstance,
          "a result of a released instance → dropped (another instance)");
    check(rx.accept(result(QStringLiteral("S1"), oldInstance, QStringLiteral("S1/1"), ask3, 0)) == WorkVerdict::OtherInstance,
          "… even with the new instance's latest ask number");
    check(s1b->delivered.empty(), "… and the new instance is told nothing");

    map.sessions.remove(QStringLiteral("S1"));
    check(rx.accept(result(QStringLiteral("S1"), s1b->instance, QStringLiteral("S1/1"), ask3, 0)) == WorkVerdict::OwnerGone,
          "a session released since the ask → dropped (owner gone)");

    check(rx.count(WorkVerdict::Kept) == 1 && rx.count(WorkVerdict::OwnerGone) == 2
              && rx.count(WorkVerdict::OtherInstance) == 2 && rx.count(WorkVerdict::ItemGone) == 1
              && rx.count(WorkVerdict::Superseded) == 2,
          "the receiver counts each verdict");

    WorkReceiver none(WorkResolver {});
    check(none.accept(result(QStringLiteral("S1"), 1, QStringLiteral("S1/1"), 1, 0)) == WorkVerdict::OwnerGone,
          "a receiver with no lookup drops everything");
}

static void drainKeepsOrderWhenReentered()
{
    std::printf("Delivery stays first in, first out when an owner spins an event loop inside deliver\n");
    std::vector<int> order;
    WorkReceiver    *self = nullptr;
    auto resolve = [&](const WorkAddress &a) {
        WorkTarget t;
        t.instance  = a.instance;
        t.itemFound = true;
        t.latestAsk = a.ask;
        t.deliver   = [&](WorkVerdict, WorkResult &&r) {
            const int v = std::any_cast<int>(r.value);
            order.push_back(v);
            if (v == 1) {   // the first result posts another, then runs a nested event loop
                WorkResult more;
                more.address = WorkAddress { QStringLiteral("S"), 1, QStringLiteral("S/1"), 1 };
                more.value   = 4;
                self->post(std::move(more));
                QCoreApplication::processEvents();
            }
        };
        return t;
    };
    WorkReceiver rx(resolve);
    self = &rx;
    for (int v = 1; v <= 3; ++v) {
        WorkResult r;
        r.address = WorkAddress { QStringLiteral("S"), 1, QStringLiteral("S/1"), 1 };
        r.value   = v;
        rx.post(std::move(r));
    }
    check(spinUntil([&] { return order.size() == 4; }), "all four delivered");
    check(order == std::vector<int>({ 1, 2, 3, 4 }), "in the order posted: 1, 2, 3, then the one posted from inside 1's deliver");
}

static void postedThroughThePool()
{
    std::printf("A job's value posted to its owner through the pool\n");
    FakeMap      map;
    WorkReceiver rx(map.resolver());
    WorkPool     pool(2);
    auto         s = map.load(QStringLiteral("S2"));

    std::atomic<bool> ranOffThread { false };
    const QThread    *main = QThread::currentThread();
    std::vector<quint64> asks;
    for (int i = 0; i < 5; ++i) {
        const quint64 ask = nextWorkNumber();
        asks.push_back(ask);
        s->latestAsk[QStringLiteral("S2/%1").arg(i)] = ask;
        postToOwner(pool, rx, s->instance, WorkAddress { QStringLiteral("S2"), s->instance, QStringLiteral("S2/%1").arg(i), ask },
                    [i, main, &ranOffThread]() -> std::any {
                        if (QThread::currentThread() != main) ranOffThread = true;
                        return i * 10;
                    },
                    WorkPriority::Low);
    }
    // One more that throws: it still comes back, so its batch settles.
    const quint64 askThrow = nextWorkNumber();
    s->latestAsk[QStringLiteral("S2/9")] = askThrow;
    postToOwner(pool, rx, s->instance, WorkAddress { QStringLiteral("S2"), s->instance, QStringLiteral("S2/9"), askThrow },
                []() -> std::any { throw std::runtime_error("no document"); });

    check(spinUntil([&] { return s->delivered.size() == 6; }), "all six results reached the session");
    check(ranOffThread.load(), "the work ran on a worker");
    bool kept = s->delivered.size() == 6;
    for (int i = 0; kept && i < 5; ++i)
        kept = s->delivered[size_t(i)].first == WorkVerdict::Kept
            && std::any_cast<int>(s->delivered[size_t(i)].second.value) == i * 10;
    check(kept, "kept in lane order, each with its value");
    const WorkResult &thrown = s->delivered.back().second;
    check(s->delivered.back().first == WorkVerdict::Kept && thrown.error == QStringLiteral("no document")
              && !thrown.value.has_value(),
          "a job that threw posts its error and no value (the owner reads it as unread)");

    // The session released while its lane still has work: waiting jobs cancelled, the running one dropped.
    Gate hold, started;
    const quint64 ask = nextWorkNumber();
    s->latestAsk[QStringLiteral("S2/0")] = ask;
    postToOwner(pool, rx, s->instance, WorkAddress { QStringLiteral("S2"), s->instance, QStringLiteral("S2/0"), ask },
                [&]() -> std::any { started.open(); hold.wait(); return 1; });
    for (int i = 0; i < 3; ++i)
        postToOwner(pool, rx, s->instance, WorkAddress { QStringLiteral("S2"), s->instance, QStringLiteral("S2/0"), ask },
                    []() -> std::any { return 2; });
    check(started.wait(), "a job of the session is running");
    const quint64 goneBefore = rx.count(WorkVerdict::OwnerGone);
    map.sessions.remove(QStringLiteral("S2"));
    check(pool.cancelLane(s->instance) == 3, "release: its three waiting jobs are cancelled");
    hold.open();
    check(spinUntil([&] { return rx.count(WorkVerdict::OwnerGone) == goneBefore + 1; }),
          "the running one finished and was dropped at the receiver (owner gone)");
    check(s->delivered.size() == 6, "the released session was told nothing more");
}

static void counterNeverRepeats()
{
    std::printf("The counter never repeats a number across sessions\n");
    // Three sessions loaded, asking interleaved; one released and loaded again; all numbers collected.
    std::vector<quint64> all;
    quint64 instA = nextWorkNumber(), instB = nextWorkNumber();
    all.push_back(instA);
    all.push_back(instB);
    for (int i = 0; i < 100; ++i) {
        all.push_back(nextWorkNumber());   // an ask of session A
        all.push_back(nextWorkNumber());   // an ask of session B
        if (i == 50) {
            instA = nextWorkNumber();      // A released and loaded again
            all.push_back(instA);
        }
    }
    all.push_back(nextWorkNumber());       // a third session
    bool increasing = true;
    for (size_t i = 1; i < all.size(); ++i) increasing = increasing && all[i] > all[i - 1];
    check(increasing && all.front() > 0, "every number is new, larger than the last, and never 0");

    // From several threads at once (a tool's threads): still no repeat.
    std::vector<std::vector<quint64>> per(4);
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&per, t] { for (int i = 0; i < 20000; ++i) per[size_t(t)].push_back(nextWorkNumber()); });
    for (auto &t : ts) t.join();
    std::set<quint64> seen(all.begin(), all.end());
    size_t total = all.size();
    for (const auto &v : per) { seen.insert(v.begin(), v.end()); total += v.size(); }
    check(seen.size() == total && !seen.count(0), "80 000 numbers from four threads: none repeats, none is 0");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    laneKeepsOrder();
    twoLanesDoNotBlock();
    cancelledLaneDropsWaiting();
    submitAfterCancelWhileRunning();
    destroyedWithJobsInFlight();
    prioritiesAndForeground();
    receiverChecks();
    postedThroughThePool();
    drainKeepsOrderWhenReentered();
    counterNeverRepeats();
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
