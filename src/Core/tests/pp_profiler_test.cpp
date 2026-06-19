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

#include "pp_profiler.h"

#include <gtest/gtest.h>

#include <chrono>

using namespace pinpoint::profiling;

namespace {

// Spin for at least `us` microseconds of wall time on the calling thread.
void burn(int us)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    volatile double x = 0.0;
    while (std::chrono::steady_clock::now() < end) x += 1.0;
    (void)x;
}

const Profiler::ScopeStat *findScope(const Profiler::Snapshot &s, const char *name)
{
    for (const auto &x : s.scopes)
        if (x.name == name) return &x;
    return nullptr;
}

const Profiler::MemStat *findMem(const Profiler::Snapshot &s, const char *name)
{
    for (const auto &x : s.memory)
        if (x.name == name) return &x;
    return nullptr;
}

} // namespace

TEST(Profiler, ScopeTiming)
{
    auto &p = Profiler::instance();
    p.reset();

    constexpr int N = 16;
    for (int i = 0; i < N; ++i) {
        PP_PROFILE_SCOPE("T.scope");
        burn(40);
    }

    const auto snap = p.snapshot();
    const auto *s = findScope(snap, "T.scope");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->calls, uint64_t(N));
    EXPECT_GT(s->wall_ns_total, 0u);
    EXPECT_LE(s->wall_ns_min, s->wall_ns_max);
    const uint64_t avg = s->wall_ns_total / N;
    EXPECT_LE(s->wall_ns_min, avg);
    EXPECT_LE(avg, s->wall_ns_max);
}

TEST(Profiler, CountOnly)
{
    auto &p = Profiler::instance();
    p.reset();

    for (int i = 0; i < 5; ++i) PP_PROFILE_COUNT("T.count");

    const auto snap = p.snapshot();
    const auto *s = findScope(snap, "T.count");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->calls, 5u);
    EXPECT_EQ(s->wall_ns_total, 0u);   // counted, never timed
}

TEST(Profiler, MemoryAddSubScope)
{
    auto &p = Profiler::instance();
    p.reset();

    PP_PROFILE_MEM_ADD("T.mem", 1000);
    {
        const auto snap = p.snapshot();
        const auto *m = findMem(snap, "T.mem");
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->current_bytes, 1000);
        EXPECT_EQ(m->peak_bytes, 1000);
    }

    PP_PROFILE_MEM_ADD("T.mem", 500);   // current 1500, peak 1500
    PP_PROFILE_MEM_SUB("T.mem", 800);   // current 700,  peak 1500 (holds)
    {
        const auto snap = p.snapshot();
        const auto *m = findMem(snap, "T.mem");
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->current_bytes, 700);
        EXPECT_EQ(m->peak_bytes, 1500);
    }

    {
        PP_PROFILE_MEM_SCOPE("T.mem", 300);   // current 1000 while in scope
        const auto snap = p.snapshot();
        const auto *m = findMem(snap, "T.mem");
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->current_bytes, 1000);
    }
    // released on scope exit → back to baseline
    {
        const auto snap = p.snapshot();
        const auto *m = findMem(snap, "T.mem");
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->current_bytes, 700);
    }
}

TEST(Profiler, ResetSemantics)
{
    auto &p = Profiler::instance();
    p.reset();

    PP_PROFILE_MEM_ADD("R.mem", 2000);
    PP_PROFILE_MEM_SUB("R.mem", 500);   // current 1500, peak 2000
    { PP_PROFILE_SCOPE("R.scope"); burn(30); }

    p.reset();

    const auto snap = p.snapshot();
    const auto *s = findScope(snap, "R.scope");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->calls, 0u);            // scope counters zeroed
    EXPECT_EQ(s->wall_ns_total, 0u);

    const auto *m = findMem(snap, "R.mem");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->current_bytes, 1500);  // current preserved (live allocation)
    EXPECT_EQ(m->peak_bytes, 1500);     // peak rebased down to current
}

TEST(Profiler, DeepTierGate)
{
    auto &p = Profiler::instance();

    Profiler::setDeepEnabled(false);
    p.reset();
    for (int i = 0; i < 3; ++i) { PP_PROFILE_SCOPE_DEEP("D.scope"); burn(40); }
    {
        const auto snap = p.snapshot();
        const auto *s = findScope(snap, "D.scope");
        ASSERT_NE(s, nullptr);          // interned (listed) but not counted
        EXPECT_EQ(s->calls, 0u);
    }

    Profiler::setDeepEnabled(true);
    p.reset();
    // Each scope must burn well past the per-thread CPU clock granularity so the
    // deep tier attributes a non-zero cpu_ns_total. Windows' GetThreadTimes only
    // advances on the scheduler tick (~15 ms), so 300 us would round to 0 there;
    // 30 ms per scope clears the tick on every platform.
    for (int i = 0; i < 3; ++i) { PP_PROFILE_SCOPE_DEEP("D.scope"); burn(30000); }
    {
        const auto snap = p.snapshot();
        const auto *s = findScope(snap, "D.scope");
        ASSERT_NE(s, nullptr);
        EXPECT_EQ(s->calls, 3u);
        EXPECT_GT(s->cpu_ns_total, 0u); // per-scope CPU attributed on a busy body
    }
    Profiler::setDeepEnabled(false);
}

TEST(Profiler, InterningIdentity)
{
    auto &p = Profiler::instance();

    ScopeRecord *a = p.internScope("I.same");
    ScopeRecord *b = p.internScope("I.same");
    EXPECT_EQ(a, b);                    // same name → same record pointer

    p.reset();
    // Two distinct call sites, same name → one snapshot row with summed calls.
    auto siteA = [] { PP_PROFILE_COUNT("I.collapse"); };
    auto siteB = [] { PP_PROFILE_COUNT("I.collapse"); };
    siteA();
    siteB();

    const auto snap = p.snapshot();
    int rows = 0;
    uint64_t calls = 0;
    for (const auto &x : snap.scopes)
        if (x.name == "I.collapse") { ++rows; calls = x.calls; }
    EXPECT_EQ(rows, 1);
    EXPECT_EQ(calls, 2u);
}
