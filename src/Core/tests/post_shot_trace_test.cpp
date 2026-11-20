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

// PostShotTrace (src/Core/post_shot_trace.h; refactor 3.7-S / 3.7-I): the [PostShot] line the cabin run reads.
// The format (nesting, `timed` = the top-level sum, one decimal under 10), a begin/Scope/end flow, the thread gate,
// the 256-step cap, the quiet end, and a window closed by the next begin() while a step still runs (written, marked
// cut — never dropped).

#include "post_shot_trace.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <thread>

using pinpoint::PostShotTrace;

namespace {

struct Captured {
    QStringList           lines;
    QList<PostShotTrace::Window> windows;
    PostShotTrace::Sink sink()
    {
        return [this](const QString &line, const PostShotTrace::Window &w) { lines << line; windows << w; };
    }
};

bool waitFor(const std::function<bool()> &done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

} // namespace

TEST(PostShotTrace, FormatNestsAndSumsTopLevel)
{
    PostShotTrace::Window w;
    w.shot   = QStringLiteral("[PostShot] s/1");
    w.spanNs = 620'000'000;
    w.steps  = {
        { "join", 0, 482'000'000 },
        { "evt01", 1, 0 },
        { "announce", 1, 396'000'000 },
        { "lm.write", 2, 266'400'000 },
        { "lock", 3, 1'040'000 },
        { "settle", 0, 17'000'000 },
        { "rebuild", 1, 4'100'000 },
    };
    EXPECT_EQ(PostShotTrace::format(w),
              QStringLiteral("[PostShot] s/1 span 620 ms, timed 499 ms: join 482 (evt01 0.0, announce 396 (lm.write 266 "
                             "(lock 1.0))), settle 17 (rebuild 4.1)"));
}

TEST(PostShotTrace, BeginScopeEndWritesOneLine)
{
    Captured c;
    PostShotTrace::begin(QStringLiteral("[PostShot] s/2"), c.sink());
    {
        PostShotTrace::Scope a("join");
        { PostShotTrace::Scope b("evt01"); }
        { PostShotTrace::Scope b("notify"); }
    }
    { PostShotTrace::Scope a("settle"); }
    PostShotTrace::end();
    ASSERT_EQ(c.lines.size(), 1);
    const PostShotTrace::Window &w = c.windows.first();
    ASSERT_EQ(w.steps.size(), 4);
    EXPECT_STREQ(w.steps[0].name, "join");
    EXPECT_EQ(w.steps[1].depth, 1);
    EXPECT_EQ(w.steps[3].depth, 0);
    EXPECT_GE(w.spanNs, w.steps[0].ns + w.steps[3].ns);
    EXPECT_TRUE(c.lines.first().startsWith(QStringLiteral("[PostShot] s/2 span ")));
    EXPECT_TRUE(c.lines.first().contains(QStringLiteral("join ")));
    EXPECT_TRUE(c.lines.first().contains(QStringLiteral(" (evt01 ")));
    EXPECT_TRUE(c.lines.first().contains(QStringLiteral("settle ")));
    EXPECT_FALSE(PostShotTrace::recording());
    { PostShotTrace::Scope late("after"); }   // no window: records nothing, writes nothing
    EXPECT_EQ(c.lines.size(), 1);
}

TEST(PostShotTrace, AnotherThreadRecordsNothing)
{
    Captured c;
    PostShotTrace::begin(QStringLiteral("[PostShot] s/3"), c.sink());
    bool workerRecording = true;
    std::thread worker([&workerRecording] {
        workerRecording = PostShotTrace::recording();
        PostShotTrace::Scope s("commit");   // a worker's atomicWrite
    });
    worker.join();
    { PostShotTrace::Scope s("join"); }
    PostShotTrace::end();
    EXPECT_FALSE(workerRecording);
    ASSERT_EQ(c.windows.size(), 1);
    ASSERT_EQ(c.windows.first().steps.size(), 1);
    EXPECT_STREQ(c.windows.first().steps.first().name, "join");
}

TEST(PostShotTrace, CapsAt256StepsAndCountsTheRest)
{
    Captured c;
    PostShotTrace::begin(QStringLiteral("[PostShot] s/4"), c.sink());
    for (int i = 0; i < PostShotTrace::kMaxSteps + 1; ++i) { PostShotTrace::Scope s("x"); }
    PostShotTrace::end();
    ASSERT_EQ(c.windows.size(), 1);
    EXPECT_EQ(c.windows.first().steps.size(), PostShotTrace::kMaxSteps);
    EXPECT_EQ(c.windows.first().dropped, 1);
    EXPECT_TRUE(c.lines.first().endsWith(QStringLiteral(" (+1 steps not kept)")));
}

TEST(PostShotTrace, QuietEndClosesTheLastShotsWindow)
{
    Captured c;
    PostShotTrace::begin(QStringLiteral("[PostShot] s/5"), c.sink(), /*quietMs=*/50);
    { PostShotTrace::Scope s("join"); }
    EXPECT_TRUE(c.lines.isEmpty());   // not yet: the window waits for quiet
    EXPECT_TRUE(waitFor([&c] { return !c.lines.isEmpty(); }, 2000));
    EXPECT_EQ(c.lines.size(), 1);
    EXPECT_FALSE(PostShotTrace::recording());
}

TEST(PostShotTrace, NextBeginWritesTheOpenWindowFirst)
{
    Captured c;
    PostShotTrace::begin(QStringLiteral("[PostShot] s/6"), c.sink());
    { PostShotTrace::Scope s("join"); }
    PostShotTrace::begin(QStringLiteral("[PostShot] s/7"), c.sink());   // the next shot lands
    ASSERT_EQ(c.lines.size(), 1);
    EXPECT_TRUE(c.lines.first().startsWith(QStringLiteral("[PostShot] s/6 ")));
    EXPECT_FALSE(c.lines.first().contains(QStringLiteral("window cut")));
    PostShotTrace::end();
    EXPECT_EQ(c.lines.size(), 2);
}

TEST(PostShotTrace, BeginInsideARunningStepWritesItMarkedCut)
{
    Captured c;
    PostShotTrace::begin(QStringLiteral("[PostShot] s/8"), c.sink());
    {
        PostShotTrace::Scope outer("settle");   // e.g. a nested event loop runs the next shot's handler
        PostShotTrace::begin(QStringLiteral("[PostShot] s/9"), c.sink());
        ASSERT_EQ(c.lines.size(), 1);
        EXPECT_TRUE(c.lines.first().startsWith(QStringLiteral("[PostShot] s/8 ")));
        EXPECT_TRUE(c.lines.first().endsWith(QStringLiteral(" (window cut: 1 steps still running)")));
        PostShotTrace::Scope inner("join");
    }   // the old window's `settle` ends here: it records nothing into the new window
    PostShotTrace::end();
    ASSERT_EQ(c.windows.size(), 2);
    ASSERT_EQ(c.windows.last().steps.size(), 1);
    EXPECT_STREQ(c.windows.last().steps.first().name, "join");
    EXPECT_EQ(c.windows.last().cut, 0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // the quiet end is a QTimer
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
