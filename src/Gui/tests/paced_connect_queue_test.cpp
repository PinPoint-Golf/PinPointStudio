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

// PacedConnectQueue — ImuManager's paced BLE connect (session_wizard_refactor_design.md
// §4.11, finding F10), without the device stack.
//
// ⚠ THE ORACLE IS THE BEHAVIOUR OF THE TWO QML QUEUES IT REPLACES (the IMU step of the
// retired ScreenSessionWizard.qml, PpImuPanel): first device at once, each next one a gap later, the "connecting"
// flag dropping as the LAST device is selected. Plus the three things those queues could
// not do: cancel, replace, and skip without a gap.
//
// Timing is asserted as a LOWER bound (never early — early is the defect the gap exists to
// prevent) with a generous upper bound, so a loaded machine cannot fail it. The gap is
// 80 ms rather than 2 s so the whole suite runs in about a second.

#include "imu/paced_connect_queue.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <cstdio>

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", qPrintable(label));
        ++g_fail;
    }
}

static constexpr int kGap   = 80;
static constexpr int kSlack = 400;   // upper bound only; a loaded CI box may be slow

// Records each offered id and when it was offered, relative to `clock`.
struct Recorder {
    QElapsedTimer      clock;
    QStringList        ids;
    QList<qint64>      at;
    QStringList        skip;   // ids the callback declines
    PacedConnectQueue::SelectFn fn()
    {
        return [this](const QString &id) {
            if (skip.contains(id)) return false;
            ids << id;
            at << clock.elapsed();
            return true;
        };
    }
};

static void testTiming()
{
    Recorder r;
    PacedConnectQueue q(r.fn());
    QSignalSpy active(&q, &PacedConnectQueue::activeChanged);

    r.clock.start();
    q.start({ "a", "b", "c" }, kGap);
    check(QStringLiteral("T1 the first device is selected immediately (synchronously)"),
          r.ids == QStringList{ "a" });
    check(QStringLiteral("T2 active while devices are waiting"), q.active() && active.count() == 1);

    (void)QTest::qWaitFor([&] { return r.ids.size() == 3; }, kGap * 2 + kSlack * 2);
    check(QStringLiteral("T3 all three selected, in order"), r.ids == QStringList{ "a", "b", "c" });
    if (r.at.size() == 3) {
        check(QStringLiteral("T4 b is not early (≥ gap after a, got %1 ms)").arg(r.at[1] - r.at[0]),
              r.at[1] - r.at[0] >= kGap - 2);
        check(QStringLiteral("T5 c is not early (≥ gap after b, got %1 ms)").arg(r.at[2] - r.at[1]),
              r.at[2] - r.at[1] >= kGap - 2);
        check(QStringLiteral("T6 c lands near 2·gap (got %1 ms)").arg(r.at[2]),
              r.at[2] >= 2 * kGap - 4 && r.at[2] < 2 * kGap + kSlack);
    }
    check(QStringLiteral("T7 inactive the moment the LAST device is selected"),
          !q.active() && active.count() == 2);
    QTest::qWait(kGap * 2);
    check(QStringLiteral("T8 nothing more happens afterwards"), r.ids.size() == 3 && active.count() == 2);
}

static void testCancel()
{
    Recorder r;
    PacedConnectQueue q(r.fn());
    QSignalSpy active(&q, &PacedConnectQueue::activeChanged);
    r.clock.start();
    q.start({ "a", "b", "c" }, kGap);
    (void)QTest::qWaitFor([&] { return r.ids.size() == 2; }, kGap + kSlack);
    q.cancel();
    check(QStringLiteral("X1 cancel drops the flag"), !q.active() && active.count() == 2);
    QTest::qWait(kGap * 3);
    check(QStringLiteral("X2 nothing is selected after cancel"), r.ids == QStringList{ "a", "b" });
    q.cancel();
    check(QStringLiteral("X3 a second cancel is harmless and silent"), active.count() == 2);
}

static void testReplace()
{
    Recorder r;
    PacedConnectQueue q(r.fn());
    QSignalSpy active(&q, &PacedConnectQueue::activeChanged);
    r.clock.start();
    q.start({ "a", "b", "c" }, kGap);
    QTest::qWait(kGap / 2);   // mid-gap: b has not been selected
    const qint64 replacedAt = r.clock.elapsed();
    q.start({ "x", "y" }, kGap);
    check(QStringLiteral("R1 the replacement's first device goes at once"),
          r.ids == QStringList{ "a", "x" });
    check(QStringLiteral("R2 still active, no flicker through false"),
          q.active() && active.count() == 1);
    (void)QTest::qWaitFor([&] { return r.ids.size() == 3; }, kGap + kSlack);
    QTest::qWait(kGap * 3);
    check(QStringLiteral("R3 the old remainder is gone"), r.ids == QStringList{ "a", "x", "y" });
    if (r.at.size() == 3)
        check(QStringLiteral("R4 y waits a full gap after x (got %1 ms)").arg(r.at[2] - replacedAt),
              r.at[2] - replacedAt >= kGap - 2);
    check(QStringLiteral("R5 inactive at the end"), !q.active() && active.count() == 2);
}

static void testEdges()
{
    {
        Recorder r;
        PacedConnectQueue q(r.fn());
        QSignalSpy active(&q, &PacedConnectQueue::activeChanged);
        q.start({}, kGap);
        QTest::qWait(kGap);
        check(QStringLiteral("E1 an empty list is a no-op"),
              r.ids.isEmpty() && !q.active() && active.count() == 0);
    }
    {
        Recorder r;
        PacedConnectQueue q(r.fn());
        QSignalSpy active(&q, &PacedConnectQueue::activeChanged);
        q.start({ "a" }, kGap);
        check(QStringLiteral("E2 one device: selected, and the flag never rises"),
              r.ids == QStringList{ "a" } && !q.active() && active.count() == 0);
    }
    {
        // Skipped ids (not enumerated / already connected) cost no gap.
        Recorder r;
        r.skip = { "s1", "s2" };
        PacedConnectQueue q(r.fn());
        r.clock.start();
        q.start({ "s1", "a", "s2", "b" }, kGap);
        check(QStringLiteral("E3 leading skips cost nothing: a goes at once"),
              r.ids == QStringList{ "a" });
        (void)QTest::qWaitFor([&] { return r.ids.size() == 2; }, kGap + kSlack);
        check(QStringLiteral("E4 a skip in the middle costs no extra gap (b at %1 ms)")
                  .arg(r.at.value(1, -1)),
              r.ids == QStringList{ "a", "b" } && r.at.value(1) < 2 * kGap);
        check(QStringLiteral("E5 inactive at the end"), !q.active());
    }
    {
        Recorder r;
        r.skip = { "s1", "s2" };
        PacedConnectQueue q(r.fn());
        QSignalSpy active(&q, &PacedConnectQueue::activeChanged);
        q.start({ "s1", "s2" }, kGap);
        check(QStringLiteral("E6 all skipped: nothing selected, the flag never rises"),
              r.ids.isEmpty() && !q.active() && active.count() == 0);
    }
    {
        Recorder r;
        PacedConnectQueue q(r.fn());
        q.start({ "a", "a", "b" }, kGap);
        (void)QTest::qWaitFor([&] { return r.ids.size() == 2; }, kGap + kSlack);
        QTest::qWait(kGap * 2);
        check(QStringLiteral("E7 a duplicate id is selected once"), r.ids == QStringList{ "a", "b" });
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("paced_connect_queue_test — ImuManager's paced BLE connect (design §4.11)\n");
    testTiming();
    testCancel();
    testReplace();
    testEdges();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
