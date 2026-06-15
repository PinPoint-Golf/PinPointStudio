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

#include <atomic>
#include <thread>
#include <vector>

using namespace pinpoint::profiling;

namespace {

// Stable string literals so internScope's stored char* never dangles.
const char *kPerThread[8] = {
    "C.t0", "C.t1", "C.t2", "C.t3", "C.t4", "C.t5", "C.t6", "C.t7"
};

} // namespace

// M threads hammer one shared scope + a distinct per-thread scope + a paired
// MEM add/sub, while another thread snapshots continuously. Asserts exact counts
// and net-zero memory — and, under TSan, no data races.
TEST(ProfilerConcurrency, HammerWithConcurrentSnapshot)
{
    auto &p = Profiler::instance();
    p.reset();

    constexpr int M = 8;
    constexpr int K = 4000;

    std::atomic<bool> stopSnap{false};
    std::thread snapper([&] {
        while (!stopSnap.load(std::memory_order_relaxed)) {
            const auto s = p.snapshot();
            (void)s;
        }
    });

    std::vector<std::thread> workers;
    workers.reserve(M);
    for (int m = 0; m < M; ++m) {
        workers.emplace_back([&p, m] {
            ScopeRecord *mine = p.internScope(kPerThread[m]);
            for (int k = 0; k < K; ++k) {
                { PP_PROFILE_SCOPE("C.shared"); }
                mine->calls.fetch_add(1, std::memory_order_relaxed);
                PP_PROFILE_MEM_ADD("C.mem", 16);
                PP_PROFILE_MEM_SUB("C.mem", 16);
            }
        });
    }
    for (auto &t : workers) t.join();
    stopSnap.store(true);
    snapper.join();

    const auto snap = p.snapshot();

    uint64_t shared = 0;
    int64_t  memCur = 0;
    bool     haveMem = false;
    std::vector<uint64_t> perThread(M, 0);
    for (const auto &s : snap.scopes) {
        if (s.name == "C.shared") shared = s.calls;
        for (int m = 0; m < M; ++m)
            if (s.name == kPerThread[m]) perThread[m] = s.calls;
    }
    for (const auto &mm : snap.memory)
        if (mm.name == "C.mem") { memCur = mm.current_bytes; haveMem = true; }

    EXPECT_EQ(shared, uint64_t(M) * K);
    for (int m = 0; m < M; ++m)
        EXPECT_EQ(perThread[m], uint64_t(K));
    ASSERT_TRUE(haveMem);
    EXPECT_EQ(memCur, 0);            // equal adds/subs → net zero
}
