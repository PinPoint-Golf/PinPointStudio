/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "event_buffer.h"
#include "format_descriptor.h"
#include "source_descriptor.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#ifdef __linux__
  #include <fstream>
  #include <string>
#endif

using namespace pinpoint;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static SourceDescriptor makeImu(uint32_t hz = 200,
                                 std::chrono::milliseconds win = 1000ms) {
    SourceDescriptor d;
    d.name             = "test_imu";
    d.window_duration  = win;
    d.expected_interarrival_us = std::chrono::microseconds(1'000'000 / hz);
    ImuFormat f;
    f.device          = DeviceKind::IMU_WitMotion;
    f.sample_rate_hz  = hz;
    f.packet_bytes    = 32;
    d.format.device   = DeviceKind::IMU_WitMotion;
    d.format.format   = f;
    return d;
}

// Zero-reorder config so events emit immediately in most tests.
static EventBufferConfig zeroReorder() {
    EventBufferConfig cfg;
    cfg.reorder_window_us = 0;
    return cfg;
}

// Write n events to a source, spaced interval_us apart starting at base_us.
static void writeEvents(EventBuffer& buf, SourceId id,
                         int n, int64_t base_us, int64_t interval_us) {
    for (int i = 0; i < n; ++i) {
        auto slot = buf.acquireWriteSlot(id);
        if (!slot.valid) return;
        *slot.timestamp_us  = base_us + i * interval_us;
        *slot.bytes_written = 4;
        buf.publish(id, slot.sequence);
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

TEST(EventBuffer, RegisterSources) {
    EventBuffer buf;
    SourceId a = buf.registerSource(makeImu());
    SourceId b = buf.registerSource(makeImu());
    EXPECT_NE(a, b);
    EXPECT_NE(a, kInvalidSourceId);
    EXPECT_NE(b, kInvalidSourceId);
}

// ---------------------------------------------------------------------------
// Basic end-to-end with two IMU producers
// ---------------------------------------------------------------------------

TEST(EventBuffer, EndToEnd) {
    EventBufferConfig cfg = zeroReorder();
    EventBuffer buf(cfg);
    SourceId a = buf.registerSource(makeImu());
    SourceId b = buf.registerSource(makeImu());

    buf.start();
    EXPECT_TRUE(buf.isCapturing());

    auto sub = buf.subscribe();

    // Write events sequentially, interleaved, so timestamps are deterministic
    // and the merger can guarantee global ordering with reorder_window_us=0.
    constexpr int EVENTS = 200;
    int64_t base  = 1'000'000LL;
    int64_t step  = 10'000LL; // 10ms between interleaved events
    for (int i = 0; i < EVENTS; ++i) {
        auto sa = buf.acquireWriteSlot(a);
        if (sa.valid) {
            *sa.timestamp_us  = base + i * step;
            *sa.bytes_written = 4;
            buf.publish(a, sa.sequence);
        }
        auto sb = buf.acquireWriteSlot(b);
        if (sb.valid) {
            *sb.timestamp_us  = base + i * step + step / 2; // offset by half step
            *sb.bytes_written = 4;
            buf.publish(b, sb.sequence);
        }
    }

    // Let the merger finish emitting (all events should drain within 500ms).
    std::this_thread::sleep_for(500ms);

    // Drain remaining events via pause.
    buf.pause();

    // Read everything via snapshot (timeline is frozen, no ordering races).
    auto all = buf.snapshot(INT64_MIN, INT64_MAX);
    buf.stop();

    EXPECT_GE(all.size(), static_cast<size_t>(2 * EVENTS - 4));

    // Global ordering: merger must have emitted in timestamp order.
    for (size_t i = 1; i < all.size(); ++i)
        EXPECT_LE(all[i-1].timestamp_us, all[i].timestamp_us);

    // Both sources must appear.
    bool saw_a = false, saw_b = false;
    for (auto& e : all) {
        if (e.source_id == a) saw_a = true;
        if (e.source_id == b) saw_b = true;
    }
    EXPECT_TRUE(saw_a);
    EXPECT_TRUE(saw_b);
}

// ---------------------------------------------------------------------------
// Reorder window test
// ---------------------------------------------------------------------------

TEST(EventBuffer, ReorderWindow) {
    EventBufferConfig cfg;
    cfg.reorder_window_us = 5000; // 5ms
    EventBuffer buf(cfg);

    SourceId a = buf.registerSource(makeImu());
    SourceId b = buf.registerSource(makeImu());
    buf.start();

    auto sub = buf.subscribe();

    // Source A: t = 0, 10ms, 20ms, 30ms, ...
    // Source B: t = 5ms, 15ms, 25ms, ...
    // Expected interleaved order.
    constexpr int N = 20;
    int64_t base = EventBuffer::nowMicros();

    // Write all events synchronously.
    for (int i = 0; i < N; ++i) {
        auto sa = buf.acquireWriteSlot(a);
        if (sa.valid) {
            *sa.timestamp_us  = base + i * 10'000LL;
            *sa.bytes_written = 4;
            buf.publish(a, sa.sequence);
        }
        auto sb = buf.acquireWriteSlot(b);
        if (sb.valid) {
            *sb.timestamp_us  = base + 5000LL + i * 10'000LL;
            *sb.bytes_written = 4;
            buf.publish(b, sb.sequence);
        }
    }

    // Give the merger time to process (must respect 5ms reorder window).
    std::this_thread::sleep_for(100ms);

    std::vector<IndexEntry> received;
    IndexEntry entry;
    while (sub.tryNext(entry))
        received.push_back(entry);

    buf.stop();

    EXPECT_FALSE(received.empty());
    for (size_t i = 1; i < received.size(); ++i)
        EXPECT_LE(received[i-1].timestamp_us, received[i].timestamp_us);
}

// ---------------------------------------------------------------------------
// State machine transitions
// ---------------------------------------------------------------------------

TEST(EventBuffer, StateMachine) {
    EventBuffer buf(zeroReorder());
    buf.registerSource(makeImu());

    EXPECT_EQ(buf.state(), BufferState::Idle);
    EXPECT_FALSE(buf.isCapturing());

    buf.start();
    EXPECT_EQ(buf.state(), BufferState::Capturing);
    EXPECT_TRUE(buf.isCapturing());

    buf.pause();
    EXPECT_EQ(buf.state(), BufferState::Paused);
    EXPECT_FALSE(buf.isCapturing());

    // acquireWriteSlot in Paused state must return valid=false.
    SourceId id = 0;
    auto slot = buf.acquireWriteSlot(id);
    EXPECT_FALSE(slot.valid);

    buf.resume();
    EXPECT_EQ(buf.state(), BufferState::Capturing);
    EXPECT_TRUE(buf.isCapturing());

    buf.stop();
    EXPECT_EQ(buf.state(), BufferState::Idle);
    EXPECT_FALSE(buf.isCapturing());
}

// ---------------------------------------------------------------------------
// Multi-cycle: start/pause/resume repeated
// ---------------------------------------------------------------------------

TEST(EventBuffer, MultiCycle) {
    EventBufferConfig cfg = zeroReorder();
    EventBuffer buf(cfg);
    SourceId a = buf.registerSource(makeImu());
    SourceId b = buf.registerSource(makeImu());

    // start() only once; subsequent cycles use pause()/resume() not stop()/start().
    buf.start();

    for (int cycle = 0; cycle < 10; ++cycle) {
        int64_t base = static_cast<int64_t>(cycle) * 1'000'000LL + 500'000LL;
        writeEvents(buf, a, 20, base,          5000LL);
        writeEvents(buf, b, 20, base + 2500LL, 5000LL);

        std::this_thread::sleep_for(50ms); // let merger drain
        buf.pause();

        // Timeline has events from this cycle.
        EXPECT_GT(buf.snapshot(INT64_MIN, INT64_MAX).size(), 0u);

        buf.resume();
        // After resume, rings and timeline are reset — no events from old cycle.
        EXPECT_EQ(buf.snapshot(INT64_MIN, INT64_MAX).size(), 0u);
    }

    buf.stop();
}

// ---------------------------------------------------------------------------
// acquireReadHandle cross-source
// ---------------------------------------------------------------------------

TEST(EventBuffer, AcquireReadHandle) {
    EventBuffer buf(zeroReorder());
    SourceId id = buf.registerSource(makeImu());
    buf.start();

    uint8_t payload[32];
    for (int i = 0; i < 32; ++i) payload[i] = static_cast<uint8_t>(i);

    auto slot = buf.acquireWriteSlot(id);
    ASSERT_TRUE(slot.valid);
    std::memcpy(slot.data, payload, 32);
    *slot.bytes_written = 32;
    *slot.timestamp_us  = 12345LL;
    uint64_t seq = slot.sequence;
    buf.publish(id, seq);

    buf.pause();

    auto h = buf.acquireReadHandle(id, seq);
    ASSERT_NE(h.data, nullptr);
    EXPECT_EQ(h.bytes, 32u);
    EXPECT_EQ(h.timestamp_us, 12345LL);

    // Buffer is paused (frozen) so the payload is stable — validate via data.
    uint8_t readback[32]{};
    h.copyBytesRacy(readback, 32);
    EXPECT_EQ(std::memcmp(readback, payload, 32), 0);

    buf.stop();
}

// ---------------------------------------------------------------------------
// Soak: 30-second stress with no RSS growth
// ---------------------------------------------------------------------------

#ifdef __linux__
static long readRssKb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            long kb = 0;
            sscanf(line.c_str(), "VmRSS: %ld kB", &kb);
            return kb;
        }
    }
    return 0;
}
#endif

TEST(EventBufferSoak, ThirtySecondStressNoBloat) {
    EventBufferConfig cfg;
    cfg.reorder_window_us = 0; // immediate emit so consumer can keep up
    EventBuffer buf(cfg);

    SourceId a = buf.registerSource(makeImu(200, 5000ms));
    SourceId b = buf.registerSource(makeImu(200, 5000ms));
    buf.start();

    auto sub = buf.subscribe();

#ifdef __linux__
    long rss_start = readRssKb();
#endif

    constexpr int DURATION_S = 30;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(DURATION_S);

    // Two producer threads at 200 Hz each.
    std::atomic<bool> running{true};
    std::atomic<uint64_t> produced_a{0}, produced_b{0};

    auto producer = [&](SourceId id, std::atomic<uint64_t>& count) {
        while (running.load(std::memory_order_relaxed)) {
            int64_t ts = EventBuffer::nowMicros();
            auto slot = buf.acquireWriteSlot(id);
            if (slot.valid) {
                *slot.timestamp_us  = ts;
                *slot.bytes_written = 32;
                buf.publish(id, slot.sequence);
                count.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(5000)); // 200 Hz
        }
    };

    std::thread ta(producer, a, std::ref(produced_a));
    std::thread tb(producer, b, std::ref(produced_b));

    // Consumer thread.
    std::atomic<uint64_t> consumed{0};
    uint64_t sub_overruns = 0;
    std::thread tc([&] {
        IndexEntry entry;
        while (std::chrono::steady_clock::now() < deadline) {
            if (sub.waitNext(entry, 10ms))
                consumed.fetch_add(1, std::memory_order_relaxed);
        }
        sub_overruns = sub.overrunsSinceLastRead();
    });

    // Wait for the full duration.
    std::this_thread::sleep_for(std::chrono::seconds(DURATION_S));
    running.store(false, std::memory_order_relaxed);

    ta.join(); tb.join(); tc.join();
    buf.stop();

#ifdef __linux__
    long rss_end    = readRssKb();
    long growth_kb  = rss_end - rss_start;
    // TSAN shadow memory adds significant per-test overhead; relax limit there.
#if defined(__SANITIZE_THREAD__)
    long limit_kb = 50 * 1024; // 50MB under TSAN
#else
    long limit_kb = 5 * 1024;  // 5MB otherwise
#endif
    EXPECT_LT(growth_kb, limit_kb)
        << "RSS grew by " << growth_kb << " KB";
#endif

    // No subscription overruns — consumer kept up with the index.
    EXPECT_EQ(sub_overruns, 0u)
        << "Subscription overran " << sub_overruns << " entries";

    EXPECT_GT(consumed.load(), 0u);
}

// ---------------------------------------------------------------------------
// Phase 5: dynamic source registration / deregistration
// ---------------------------------------------------------------------------

static SourceDescriptor makeImuDescriptor(const std::string& name, uint32_t hz = 200) {
    SourceDescriptor d = makeImu(hz);
    d.name = name;
    return d;
}

TEST(EventBuffer, StartWithZeroSources) {
    pinpoint::EventBuffer buf;
    ASSERT_NO_THROW(buf.start());
    // With no sources the buffer auto-pauses and waits for the first source.
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Paused);
    EXPECT_EQ(buf.activeSourceCount(), 0u);
    buf.stop();
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Idle);
}

TEST(EventBuffer, RegisterWhilePaused) {
    pinpoint::EventBuffer buf;
    buf.start();  // auto-pauses: no sources
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Paused);

    pinpoint::SourceId id = buf.registerSource(makeImuDescriptor("test_imu"));
    EXPECT_NE(id, pinpoint::kInvalidSourceId);
    EXPECT_EQ(buf.activeSourceCount(), 1u);
    // Caller controls resume.
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Paused);
    buf.resume();
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Capturing);

    buf.stop();
}

TEST(EventBuffer, DeregisterFreesSlotForReuse) {
    pinpoint::EventBuffer buf;
    buf.start();  // auto-pauses: no sources

    auto id0 = buf.registerSource(makeImuDescriptor("imu_0"));
    auto id1 = buf.registerSource(makeImuDescriptor("imu_1"));
    EXPECT_EQ(buf.activeSourceCount(), 2u);

    buf.deregisterSource(id0);
    EXPECT_EQ(buf.activeSourceCount(), 1u);

    // New registration must reuse slot 0.
    auto id2 = buf.registerSource(makeImuDescriptor("imu_new"));
    EXPECT_EQ(id2, id0);
    EXPECT_EQ(buf.activeSourceCount(), 2u);

    buf.resume();
    buf.stop();
    (void)id1;
}

TEST(EventBuffer, DeregisterWhileCapturingAsserts) {
    pinpoint::EventBuffer buf;
    buf.start();  // auto-pauses
    auto id = buf.registerSource(makeImuDescriptor("imu"));
    buf.resume();  // caller resumes

#ifndef NDEBUG
    EXPECT_DEATH(buf.deregisterSource(id), "Paused");
#endif
    buf.stop();
}

TEST(EventBuffer, DeregisterWithLiveSwingWindowAsserts) {
    pinpoint::EventBuffer buf(zeroReorder());
    buf.start();  // auto-pauses
    auto id = buf.registerSource(makeImuDescriptor("imu"));
    buf.resume();  // caller resumes

    writeEvents(buf, id, 10, EventBuffer::nowMicros() - 100'000, 1'000);
    std::this_thread::sleep_for(20ms);
    buf.pause();

#ifndef NDEBUG
    {
        auto window = buf.captureSwingWindow(std::chrono::milliseconds(1000));
        EXPECT_DEATH(buf.deregisterSource(id), "SwingWindow");
        // window destroyed at end of scope — swing_window_live_ cleared
    }
#endif

    ASSERT_NO_THROW(buf.deregisterSource(id));
    buf.resume();
    buf.stop();
}

TEST(EventBuffer, SourceRegisterDeregisterRegisterCycle) {
    pinpoint::EventBuffer buf(zeroReorder());
    buf.start();  // auto-pauses: no sources
    EXPECT_EQ(buf.activeSourceCount(), 0u);
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Paused);

    auto id = buf.registerSource(makeImuDescriptor("imu"));
    buf.resume();  // caller controls when to start capturing

    for (int i = 0; i < 100; ++i) {
        auto slot = buf.acquireWriteSlot(id);
        ASSERT_TRUE(slot.valid);
        *slot.timestamp_us  = EventBuffer::nowMicros();
        *slot.bytes_written = 4;
        buf.publish(id, slot.sequence);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(20ms);

    EXPECT_GT(buf.statsFor(id).events_written.load(), 0u);

    buf.pause();
    buf.deregisterSource(id);
    EXPECT_EQ(buf.activeSourceCount(), 0u);

    // acquireWriteSlot with a dead id is a safe no-op.
    auto slot = buf.acquireWriteSlot(id);
    EXPECT_FALSE(slot.valid);

    // Re-register — slot 0 reused. Caller resumes when ready.
    auto id2 = buf.registerSource(makeImuDescriptor("imu_new"));
    EXPECT_EQ(id2, id);
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Paused);
    buf.resume();
    EXPECT_EQ(buf.state(), pinpoint::BufferState::Capturing);
    buf.stop();
}
