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

#include "thread_policy.h"
#include "event_buffer.h"
#include "format_descriptor.h"
#include "source_descriptor.h"

#include <gtest/gtest.h>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace pinpoint;

// ---------------------------------------------------------------------------
// Test: apply() does not crash on any role
// ---------------------------------------------------------------------------

TEST(ThreadPolicy, ApplyDoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(ThreadPolicy::apply(ThreadRole::Capture));
    EXPECT_NO_FATAL_FAILURE(ThreadPolicy::apply(ThreadRole::Merger));
    EXPECT_NO_FATAL_FAILURE(ThreadPolicy::apply(ThreadRole::Consumer));
}

// ---------------------------------------------------------------------------
// Test: lastApplyDescription() returns non-null, non-empty string
// ---------------------------------------------------------------------------

TEST(ThreadPolicy, DescriptionNonEmpty) {
    ThreadPolicy::apply(ThreadRole::Merger);
    const char* desc = ThreadPolicy::lastApplyDescription();
    ASSERT_NE(desc, nullptr);
    ASSERT_GT(strlen(desc), 0u);
    std::cout << "[thread priority] " << desc << "\n";
}

// ---------------------------------------------------------------------------
// Test: apply() is per-thread (thread-local)
// ---------------------------------------------------------------------------

TEST(ThreadPolicy, PerThreadDescription) {
    std::string desc_a, desc_b;

    std::thread ta([&] {
        ThreadPolicy::apply(ThreadRole::Merger);
        desc_a = ThreadPolicy::lastApplyDescription();
    });
    std::thread tb([&] {
        ThreadPolicy::apply(ThreadRole::Capture);
        desc_b = ThreadPolicy::lastApplyDescription();
    });
    ta.join();
    tb.join();

    // On most platforms Merger and Capture produce different descriptions.
    // (On Linux without privileges both fall back to "default (no elevation)",
    // so descriptions may be equal. We just verify both are non-empty.)
    EXPECT_GT(desc_a.size(), 0u);
    EXPECT_GT(desc_b.size(), 0u);
    std::cout << "[Merger desc]  " << desc_a << "\n";
    std::cout << "[Capture desc] " << desc_b << "\n";
}

// ---------------------------------------------------------------------------
// Test: pinToCore() on Linux — verify no crash
// ---------------------------------------------------------------------------

#if defined(PINPOINT_PLATFORM_LINUX)
TEST(ThreadPolicy, PinToCoreNoCrash) {
    bool pinned = ThreadPolicy::pinToCore(0);
    (void)pinned;
    std::cout << "[core pin] " << (pinned ? "succeeded" : "not supported") << "\n";
}
#endif

// ---------------------------------------------------------------------------
// Test: merger thread applies priority when EventBuffer::start() is called
// ---------------------------------------------------------------------------

TEST(ThreadPolicy, MergerThreadApplyOnStart) {
    // Structural test: start and stop the buffer without crashing.
    // The merger thread calls ThreadPolicy::apply(ThreadRole::Merger) internally.
    // Its description is printed to stderr by the merger loop.
    EventBufferConfig cfg;
    cfg.reorder_window_us = 0;
    EventBuffer buf(cfg);

    ImuFormat f;
    f.device         = DeviceKind::IMU_WitMotion;
    f.sample_rate_hz = 200;
    f.packet_bytes   = 32;
    SourceDescriptor d;
    d.name                     = "test";
    d.window_duration          = std::chrono::milliseconds(1000);
    d.expected_interarrival_us = std::chrono::microseconds(5000);
    d.format.device            = DeviceKind::IMU_WitMotion;
    d.format.format            = f;
    buf.registerSource(d);

    EXPECT_NO_FATAL_FAILURE(buf.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_NO_FATAL_FAILURE(buf.stop());
}

// ---------------------------------------------------------------------------
// Test: diagnostics() returns a valid snapshot
// ---------------------------------------------------------------------------

TEST(ThreadPolicy, DiagnosticsSnapshot) {
    EventBufferConfig cfg;
    cfg.reorder_window_us = 0;
    EventBuffer buf(cfg);

    ImuFormat f;
    f.device         = DeviceKind::IMU_WitMotion;
    f.sample_rate_hz = 200;
    f.packet_bytes   = 32;
    SourceDescriptor d;
    d.name                     = "diag_imu";
    d.window_duration          = std::chrono::milliseconds(1000);
    d.expected_interarrival_us = std::chrono::microseconds(5000);
    d.format.device            = DeviceKind::IMU_WitMotion;
    d.format.format            = f;
    SourceId id = buf.registerSource(d);

    buf.start();

    auto slot = buf.acquireWriteSlot(id);
    ASSERT_TRUE(slot.valid);
    *slot.timestamp_us  = EventBuffer::nowMicros();
    *slot.bytes_written = 4;
    buf.publish(id, slot.sequence);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    buf.pause();

    auto snap = buf.diagnostics();

    EXPECT_EQ(snap.state, BufferState::Paused);
    EXPECT_EQ(snap.sources.size(), 1u);
    EXPECT_EQ(snap.sources[0].id, id);
    EXPECT_EQ(snap.sources[0].name, "diag_imu");
    EXPECT_GE(snap.sources[0].events_written, 1u);
    EXPECT_GT(snap.snapshot_timestamp_us, 0);

    buf.stop();
}
