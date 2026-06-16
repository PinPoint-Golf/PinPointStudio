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

// Contract tests for pp_gpu_metrics.  These assert the *graceful-degradation*
// behaviour that holds on every host regardless of whether a supported GPU is
// present — which is exactly the part verifiable on a CPU-only / non-NVIDIA dev
// box.  When a real GPU backend resolves (CUDA / DXGI / Metal box) the same
// assertions still hold and additionally exercise the populated path.

#include "pp_gpu_metrics.h"

#include <gtest/gtest.h>

using namespace pinpoint::gpumetrics;

// sample() must be safe to call before init() and never crash.
TEST(GpuMetrics, SampleBeforeInitIsSafe)
{
    const GpuSample s = sample();          // implicit lazy init inside
    if (!s.available) {
        EXPECT_TRUE(s.backend.empty());
        EXPECT_EQ(s.deviceTotalBytes, 0u);
        EXPECT_EQ(s.deviceUsedBytes,  0u);
        EXPECT_EQ(s.processUsedBytes, 0u);
        EXPECT_EQ(s.peakProcessBytes, 0u);
        EXPECT_FALSE(s.unified);
    } else {
        // A resolved backend must name itself.
        EXPECT_FALSE(s.backend.empty());
    }
}

// init() is idempotent and agrees with sample().available.
TEST(GpuMetrics, InitIdempotentAndConsistent)
{
    const bool a = init();
    const bool b = init();
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, sample().available);
}

// processGpuBytes() mirrors availability: 0 when no backend, and folds into peak.
TEST(GpuMetrics, ProcessBytesMatchesAvailability)
{
    const bool avail = init();
    const uint64_t p = processGpuBytes();
    if (!avail)
        EXPECT_EQ(p, 0u);
    // The peak watermark must be at least the latest reading.
    const GpuSample s = sample();
    EXPECT_GE(s.peakProcessBytes, s.processUsedBytes);
}

// reset() drops the peak watermark and is safe to call repeatedly.
TEST(GpuMetrics, ResetClearsPeak)
{
    init();
    reset();
    const GpuSample s = sample();           // a fresh sample re-seeds the peak
    EXPECT_GE(s.peakProcessBytes, s.processUsedBytes);
    reset();
    reset();                                 // double reset must not crash
    EXPECT_EQ(sample().available, init());
}

// Hammer the public API from a tight loop — guards against use-after-free /
// double-free in the (no-)backend teardown and the mutex discipline.
TEST(GpuMetrics, RepeatedCallsAreStable)
{
    for (int i = 0; i < 200; ++i) {
        (void)sample();
        (void)processGpuBytes();
    }
    SUCCEED();
}
