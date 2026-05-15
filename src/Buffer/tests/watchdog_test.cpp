/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "event_buffer.h"
#include "format_descriptor.h"
#include "source_descriptor.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>

using namespace pinpoint;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Config tuned for fast watchdog tests:
//   expected_interarrival_us = 5000 (200 Hz)
//   stall threshold = 5 × 5ms = 25ms
//   watchdog_interval_ms = 10ms
static EventBufferConfig watchdogConfig() {
    EventBufferConfig cfg;
    cfg.reorder_window_us    = 0;
    cfg.watchdog_interval_ms = 10;
    cfg.stall_threshold_mult = 5;
    return cfg;
}

static SourceDescriptor makeImu200(const char* name = "test_imu") {
    SourceDescriptor d;
    d.name                    = name;
    d.window_duration         = 1000ms;
    d.expected_interarrival_us = std::chrono::microseconds(5000); // 200 Hz
    ImuFormat f;
    f.device         = DeviceKind::IMU_WitMotion;
    f.sample_rate_hz = 200;
    f.packet_bytes   = 32;
    d.format.device  = DeviceKind::IMU_WitMotion;
    d.format.format  = f;
    return d;
}

// Write n events with timestamps near nowMicros(), spaced interval_us apart.
static void writeRecentEvents(EventBuffer& buf, SourceId id,
                               int n, int64_t interval_us = 5000) {
    int64_t base = EventBuffer::nowMicros();
    for (int i = 0; i < n; ++i) {
        auto slot = buf.acquireWriteSlot(id);
        if (!slot.valid) return;
        *slot.timestamp_us  = base + static_cast<int64_t>(i) * interval_us;
        *slot.bytes_written = 4;
        buf.publish(id, slot.sequence);
    }
}

// Scan timeline entries and count SourceStalled markers for a given source.
static int countStalledMarkers(const std::vector<IndexEntry>& entries, SourceId id) {
    int count = 0;
    for (const auto& e : entries)
        if (e.source_id == id && (e.flags & IndexEntryFlags::SourceStalled))
            ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Test: stalled source emits SourceStalled marker
// ---------------------------------------------------------------------------

TEST(Watchdog, StalledSourceEmitsMarker) {
    EventBuffer buf(watchdogConfig());
    SourceId id = buf.registerSource(makeImu200());
    buf.start();

    auto sub = buf.subscribe();

    // Write 5 events at the correct rate.
    writeRecentEvents(buf, id, 5);

    // Stop writing — source goes silent.
    // Stall threshold = 5 × 5ms = 25ms.
    // Watchdog interval = 10ms.
    // Maximum detection time ≈ 25ms + 10ms + margin = ~45ms.
    // We wait up to 150ms for generous CI headroom.

    auto deadline = std::chrono::steady_clock::now() + 150ms;
    bool found_stall = false;
    IndexEntry entry;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sub.waitNext(entry, 5ms)) {
            if (entry.source_id == id &&
                (entry.flags & IndexEntryFlags::SourceStalled)) {
                found_stall = true;
                break;
            }
        }
    }

    EXPECT_TRUE(found_stall) << "SourceStalled marker was not emitted within 150ms";
    EXPECT_FALSE(buf.stalledSources().empty());
    EXPECT_EQ(buf.stalledSources()[0], id);

    buf.stop();
}

// ---------------------------------------------------------------------------
// Test: non-stalled source does not emit marker
// ---------------------------------------------------------------------------

TEST(Watchdog, NonStalledSourceNoMarker) {
    EventBuffer buf(watchdogConfig());
    SourceId id = buf.registerSource(makeImu200());
    buf.start();

    auto sub = buf.subscribe();

    // Write events continuously at 200 Hz for 200ms.
    auto end = std::chrono::steady_clock::now() + 200ms;
    while (std::chrono::steady_clock::now() < end) {
        auto slot = buf.acquireWriteSlot(id);
        if (slot.valid) {
            *slot.timestamp_us  = EventBuffer::nowMicros();
            *slot.bytes_written = 4;
            buf.publish(id, slot.sequence);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(5000));
    }

    // Drain and collect all timeline entries.
    buf.pause();
    auto all = buf.snapshot(INT64_MIN, INT64_MAX);
    buf.stop();

    EXPECT_EQ(countStalledMarkers(all, id), 0)
        << "SourceStalled marker appeared for a non-stalled source";
    EXPECT_TRUE(buf.stalledSources().empty());
}

// ---------------------------------------------------------------------------
// Test: multiple sources — only stalled one is marked
// ---------------------------------------------------------------------------

TEST(Watchdog, MultipleSourcesOnlyOneStalls) {
    EventBuffer buf(watchdogConfig());
    SourceId id0 = buf.registerSource(makeImu200("imu0"));
    SourceId id1 = buf.registerSource(makeImu200("imu1"));
    buf.start();

    // Write 10 events to both sources.
    writeRecentEvents(buf, id0, 10);
    writeRecentEvents(buf, id1, 10);

    // Source 0 keeps writing; source 1 stops.
    // Run for 150ms — id1 stall (threshold 25ms + watchdog interval 10ms = 35ms)
    // fires well before the loop ends.
    // Check stalledSources() IMMEDIATELY after the loop while id0 just wrote
    // (< 25ms elapsed since last write, so id0 must not appear as stalled).
    auto end = std::chrono::steady_clock::now() + 150ms;
    while (std::chrono::steady_clock::now() < end) {
        auto slot = buf.acquireWriteSlot(id0);
        if (slot.valid) {
            *slot.timestamp_us  = EventBuffer::nowMicros();
            *slot.bytes_written = 4;
            buf.publish(id0, slot.sequence);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(5000));
    }

    // No extra sleep — check before the stall threshold (25ms) can expire for id0.
    auto stalled = buf.stalledSources();
    buf.stop();

    // id1 must appear as stalled; id0 must not.
    bool id1_stalled = false;
    bool id0_stalled = false;
    for (SourceId s : stalled) {
        if (s == id1) id1_stalled = true;
        if (s == id0) id0_stalled = true;
    }
    EXPECT_TRUE(id1_stalled)  << "Stalled source (id1) was not detected";
    EXPECT_FALSE(id0_stalled) << "Active source (id0) was incorrectly marked stalled";
}

// ---------------------------------------------------------------------------
// Test: stall clears after recovery
// ---------------------------------------------------------------------------

TEST(Watchdog, StallClearsAfterRecovery) {
    EventBuffer buf(watchdogConfig());
    SourceId id = buf.registerSource(makeImu200());
    buf.start();

    auto sub = buf.subscribe();

    // Write initial events, then let the source stall.
    writeRecentEvents(buf, id, 5);

    // Wait for stall detection (up to 150ms).
    auto deadline = std::chrono::steady_clock::now() + 150ms;
    bool stall_detected = false;
    IndexEntry entry;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sub.waitNext(entry, 5ms)) {
            if (entry.source_id == id &&
                (entry.flags & IndexEntryFlags::SourceStalled)) {
                stall_detected = true;
                break;
            }
        }
    }
    ASSERT_TRUE(stall_detected) << "Prerequisite: stall not detected, cannot test recovery";

    // Resume writing at the correct rate — timestamps must be recent.
    writeRecentEvents(buf, id, 5);

    // Wait for watchdog_interval_ms × 2 = 20ms for the stall to clear.
    std::this_thread::sleep_for(30ms);

    EXPECT_TRUE(buf.stalledSources().empty())
        << "Stall flag was not cleared after recovery";

    // Verify no second SourceStalled marker was emitted for the same stall event.
    buf.pause();
    auto all = buf.snapshot(INT64_MIN, INT64_MAX);
    buf.stop();

    EXPECT_EQ(countStalledMarkers(all, id), 1)
        << "Expected exactly one SourceStalled marker, got more";
}

// ---------------------------------------------------------------------------
// Test: watchdog does not fire during Paused state
// ---------------------------------------------------------------------------

TEST(Watchdog, NoMarkerDuringPause) {
    EventBuffer buf(watchdogConfig());
    SourceId id = buf.registerSource(makeImu200());
    buf.start();

    // Write events with recent timestamps so no stall is pending.
    writeRecentEvents(buf, id, 10);

    // Allow the merger to emit the normal events.
    std::this_thread::sleep_for(20ms);

    // Pause immediately — stall threshold (25ms) has not elapsed yet.
    buf.pause();

    // Note the timeline size at pause time.
    auto before_pause = buf.snapshot(INT64_MIN, INT64_MAX);
    int stall_count_before = countStalledMarkers(before_pause, id);

    // Wait well past the stall threshold while in Paused state.
    // Watchdog must not run while merger is blocked.
    std::this_thread::sleep_for(200ms);

    // Timeline must not have grown with stall markers.
    auto after_wait = buf.snapshot(INT64_MIN, INT64_MAX);
    int stall_count_after = countStalledMarkers(after_wait, id);

    buf.stop();

    EXPECT_EQ(stall_count_before, 0)
        << "Unexpected SourceStalled marker before pause";
    EXPECT_EQ(stall_count_after, 0)
        << "SourceStalled marker appeared during Paused state";
}
