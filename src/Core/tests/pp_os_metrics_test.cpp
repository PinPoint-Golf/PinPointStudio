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

#include "pp_os_metrics.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace pinpoint::osmetrics;

namespace {

void burn(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    volatile double x = 0.0;
    while (std::chrono::steady_clock::now() < end) x += 1.0;
    (void)x;
}

bool hasThread(const std::vector<ThreadSample> &rows, const std::string &name)
{
    for (const auto &t : rows)
        if (t.name == name) return true;
    return false;
}

} // namespace

TEST(OsMetrics, ProcessGauge)
{
    reset();

    const ProcessSample s0 = sampleProcess();
    EXPECT_DOUBLE_EQ(s0.cpu_percent, 0.0);   // first after reset() seeds, reports 0
    EXPECT_GT(s0.rss_bytes, 0u);

    burn(60);
    const ProcessSample s1 = sampleProcess();
    EXPECT_GT(s1.cpu_percent, 0.0);          // busy interval → positive CPU%
    EXPECT_GT(s1.rss_bytes, 0u);
    EXPECT_GE(s1.peak_rss_bytes, s0.peak_rss_bytes);   // watermark non-decreasing
    EXPECT_GE(s1.peak_rss_bytes, s1.rss_bytes);
}

TEST(OsMetrics, ThreadCpuNonDecreasing)
{
    const uint64_t a = threadCpuNowNs();
    burn(20);
    const uint64_t b = threadCpuNowNs();
    EXPECT_GE(b, a);
}

TEST(OsMetrics, ThreadRegistrationAndSampling)
{
    reset();

    std::atomic<bool> run{true};
    std::thread worker([&] {
        registerThread("Worker");
        while (run.load()) burn(5);
        unregisterThread();
    });

    // Seed the thread table, let the worker accrue CPU, then sample the interval.
    burn(10);
    sampleThreads();
    burn(60);
    const auto rows = sampleThreads();

    bool found = false;
    for (const auto &t : rows) {
        if (t.name == "Worker") {
            found = true;
            EXPECT_GE(t.cpu_percent, 0.0);
        }
    }
    EXPECT_TRUE(found);

    run.store(false);
    worker.join();
}

TEST(OsMetrics, UnregisterDropsRow)
{
    reset();

    std::thread worker([] {
        registerThread("Temp");
        unregisterThread();          // gone before we sample
    });
    worker.join();

    const auto rows = sampleThreads();
    EXPECT_FALSE(hasThread(rows, "Temp"));
}
