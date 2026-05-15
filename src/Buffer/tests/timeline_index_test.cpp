/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "timeline_index.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using namespace pinpoint;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(TimelineIndex, PowerOfTwoCapacity) {
    TimelineIndex idx(16);
    EXPECT_EQ(idx.capacity(), 16u);
    EXPECT_EQ(idx.latestSequence(), 0u);
    EXPECT_EQ(idx.writeSequence(), 0u);
}

TEST(TimelineIndex, NonPowerOfTwoAsserts) {
    EXPECT_THROW({ TimelineIndex idx(7); }, std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Append and read
// ---------------------------------------------------------------------------

TEST(TimelineIndex, AppendAndRead) {
    TimelineIndex idx(64);

    for (int i = 0; i < 10; ++i) {
        IndexEntry e;
        e.timestamp_us    = 1000LL * i;
        e.source_id       = 0;
        e.source_sequence = static_cast<uint64_t>(i);
        e.flags           = 0;
        uint64_t seq = idx.append(e);
        EXPECT_EQ(seq, static_cast<uint64_t>(i));
    }

    EXPECT_EQ(idx.latestSequence(), 9u);
    EXPECT_EQ(idx.writeSequence(), 10u);

    for (int i = 0; i < 10; ++i) {
        IndexEntry out{};
        EXPECT_TRUE(idx.tryRead(static_cast<uint64_t>(i), out));
        EXPECT_EQ(out.timestamp_us, 1000LL * i);
        EXPECT_EQ(out.global_sequence, static_cast<uint64_t>(i));
    }
}

// ---------------------------------------------------------------------------
// Unread slot
// ---------------------------------------------------------------------------

TEST(TimelineIndex, ReadBeforeWrite) {
    TimelineIndex idx(64);
    IndexEntry out{};
    EXPECT_FALSE(idx.tryRead(0, out)); // nothing written yet
}

// ---------------------------------------------------------------------------
// Overrun detection
// ---------------------------------------------------------------------------

TEST(TimelineIndex, OverrunDetected) {
    TimelineIndex idx(4); // tiny ring
    for (int i = 0; i < 100; ++i) {
        IndexEntry e;
        e.timestamp_us = 1000LL * i;
        e.source_id    = 0;
        idx.append(e);
    }
    // Sequence 0 should be overwritten (ring has only 4 slots, 100 appends).
    IndexEntry out{};
    EXPECT_FALSE(idx.tryRead(0, out));
    // Recent entries should still be readable.
    EXPECT_TRUE(idx.tryRead(99, out));
    EXPECT_EQ(out.timestamp_us, 1000LL * 99);
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

TEST(TimelineIndex, Snapshot) {
    TimelineIndex idx(128);
    for (int i = 0; i < 50; ++i) {
        IndexEntry e;
        e.timestamp_us = static_cast<int64_t>(i) * 1'000'000LL; // 0..49 seconds
        e.source_id    = 0;
        idx.append(e);
    }

    auto results = idx.snapshot(2'000'000LL, 4'000'000LL);
    ASSERT_EQ(results.size(), 3u); // t=2s, 3s, 4s
    EXPECT_EQ(results[0].timestamp_us, 2'000'000LL);
    EXPECT_EQ(results[1].timestamp_us, 3'000'000LL);
    EXPECT_EQ(results[2].timestamp_us, 4'000'000LL);
    // Verify ordering
    for (size_t i = 1; i < results.size(); ++i)
        EXPECT_LE(results[i-1].timestamp_us, results[i].timestamp_us);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST(TimelineIndex, Reset) {
    TimelineIndex idx(64);
    for (int i = 0; i < 10; ++i) {
        IndexEntry e; e.timestamp_us = i;
        idx.append(e);
    }
    EXPECT_EQ(idx.writeSequence(), 10u);

    idx.reset();
    EXPECT_EQ(idx.writeSequence(), 0u);
    EXPECT_EQ(idx.latestSequence(), 0u);

    // Slot 0 should appear unwritten after reset.
    IndexEntry out{};
    EXPECT_FALSE(idx.tryRead(0, out));

    // Write again from sequence 0.
    for (int i = 0; i < 10; ++i) {
        IndexEntry e; e.timestamp_us = 100LL + i;
        uint64_t seq = idx.append(e);
        EXPECT_EQ(seq, static_cast<uint64_t>(i));
    }
    EXPECT_TRUE(idx.tryRead(0, out));
    EXPECT_EQ(out.timestamp_us, 100LL);
}

// ---------------------------------------------------------------------------
// SPSC stress — appender and reader, TSAN suppressed
// ---------------------------------------------------------------------------

TEST(TimelineIndex, SpscStress) {
    constexpr int N = 100'000;
    TimelineIndex idx(131072); // large enough to avoid overrun

    std::thread writer([&] {
        for (int i = 0; i < N; ++i) {
            IndexEntry e;
            e.timestamp_us    = static_cast<int64_t>(i);
            e.source_id       = 0;
            e.source_sequence = static_cast<uint64_t>(i);
            idx.append(e);
        }
    });

    std::thread reader([&] {
        int64_t last_ts = -1;
        uint64_t next   = 0;
        while (next < static_cast<uint64_t>(N)) {
            IndexEntry out{};
            if (idx.tryRead(next, out)) {
                EXPECT_GE(out.timestamp_us, last_ts);
                last_ts = out.timestamp_us;
                ++next;
            }
        }
    });

    writer.join();
    reader.join();
}
