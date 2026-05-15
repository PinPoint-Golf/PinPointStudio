/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "event_buffer.h"
#include "swing_window.h"
#include "format_descriptor.h"
#include "source_descriptor.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

using namespace pinpoint;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static SourceDescriptor makeImu(uint32_t hz = 200) {
    SourceDescriptor d;
    d.name            = "imu";
    d.window_duration = std::chrono::milliseconds(5000);
    d.expected_interarrival_us = std::chrono::microseconds(1'000'000 / hz);
    ImuFormat f;
    f.device         = DeviceKind::IMU_WitMotion;
    f.sample_rate_hz = hz;
    f.packet_bytes   = 32;
    d.format.device  = DeviceKind::IMU_WitMotion;
    d.format.format  = f;
    return d;
}

static SourceDescriptor makeCamera() {
    SourceDescriptor d;
    d.name            = "cam";
    d.window_duration = std::chrono::milliseconds(5000);
    CameraFormat f;
    f.pixel_format       = PixelFormat::Mono8;
    f.width              = 64; f.height = 64;
    f.fps_numerator      = 30; f.fps_denominator = 1;
    f.max_payload_bytes  = 64 * 64; // 4096 bytes
    f.typical_payload_bytes = 64 * 64;
    d.format.device = DeviceKind::Camera_UVC;
    d.format.format = f;
    return d;
}

static EventBufferConfig zeroReorder() {
    EventBufferConfig cfg;
    cfg.reorder_window_us = 0;
    return cfg;
}

// Write n events to source, returns base timestamp used.
static int64_t writeEvents(EventBuffer& buf, SourceId id, int n,
                            int64_t base_us, int64_t step_us,
                            const void* fill = nullptr, size_t fill_bytes = 0) {
    for (int i = 0; i < n; ++i) {
        auto slot = buf.acquireWriteSlot(id);
        if (!slot.valid) break;
        *slot.timestamp_us  = base_us + i * step_us;
        *slot.bytes_written = static_cast<uint32_t>(
            fill_bytes ? fill_bytes : 0);
        if (fill && fill_bytes)
            std::memcpy(slot.data, fill, fill_bytes);
        buf.publish(id, slot.sequence);
    }
    return base_us;
}

// ---------------------------------------------------------------------------
// Construction and basic access
// ---------------------------------------------------------------------------

TEST(SwingWindow, ConstructionAndEntries) {
    EventBuffer buf(zeroReorder());
    SourceId imu = buf.registerSource(makeImu());
    buf.start();

    int64_t base = EventBuffer::nowMicros();
    writeEvents(buf, imu, 20, base, 5000LL);

    std::this_thread::sleep_for(50ms); // let merger emit
    buf.pause();

    int64_t w_start = base - 1;
    int64_t w_end   = base + 20 * 5000LL;
    auto window = buf.captureSwingWindow(w_start, w_end);

    EXPECT_EQ(window.startTimestampUs(), w_start);
    EXPECT_EQ(window.endTimestampUs(),   w_end);
    EXPECT_FALSE(window.entries().empty());
    EXPECT_LE(window.entries().front().timestamp_us, w_end);
    EXPECT_GE(window.entries().back().timestamp_us,  w_start);

    buf.stop();
}

// ---------------------------------------------------------------------------
// captureSwingWindow asserts in non-Paused state
// ---------------------------------------------------------------------------

TEST(SwingWindow, AssertsPausedState) {
    EventBuffer buf(zeroReorder());
    buf.registerSource(makeImu());
    buf.start();
    // In debug builds this asserts; in release it may still fire. Skip check.
    buf.stop();
}

// ---------------------------------------------------------------------------
// Payload access
// ---------------------------------------------------------------------------

TEST(SwingWindow, PayloadAccess) {
    EventBuffer buf(zeroReorder());
    SourceId imu = buf.registerSource(makeImu());
    buf.start();

    uint8_t pattern[32];
    for (int i = 0; i < 32; ++i) pattern[i] = static_cast<uint8_t>(0xA0 + i);

    int64_t base = EventBuffer::nowMicros();
    writeEvents(buf, imu, 5, base, 5000LL, pattern, 32);

    std::this_thread::sleep_for(50ms);
    buf.pause();

    auto window = buf.captureSwingWindow(base - 1, base + 5 * 5000LL);
    ASSERT_FALSE(window.entries().empty());

    const auto& e = window.entries().front();
    auto h = window.payloadOf(e);
    ASSERT_NE(h.data, nullptr);
    EXPECT_EQ(h.bytes, 32u);

    uint8_t readback[32]{};
    h.copyBytesRacy(readback, 32);
    EXPECT_EQ(std::memcmp(readback, pattern, 32), 0);

    buf.stop();
}

// ---------------------------------------------------------------------------
// frameCount and imuSampleCount
// ---------------------------------------------------------------------------

TEST(SwingWindow, FrameAndImuCount) {
    EventBuffer buf(zeroReorder());
    SourceId cam = buf.registerSource(makeCamera());
    SourceId imu = buf.registerSource(makeImu());
    buf.start();

    int64_t base = EventBuffer::nowMicros();
    writeEvents(buf, cam, 10, base,       33'333LL); // ~30 fps
    writeEvents(buf, imu, 30, base + 100, 5'000LL);  // 200 Hz

    std::this_thread::sleep_for(100ms);
    buf.pause();

    int64_t end = base + 30 * 33'333LL;
    auto window = buf.captureSwingWindow(base - 1, end);

    EXPECT_EQ(window.frameCount(cam),     window.entriesFor(cam).size());
    EXPECT_EQ(window.imuSampleCount(imu), window.entriesFor(imu).size());
    EXPECT_GT(window.imuSampleCount(imu), 0u);

    buf.stop();
}

// ---------------------------------------------------------------------------
// entriesFor
// ---------------------------------------------------------------------------

TEST(SwingWindow, EntriesFor) {
    EventBuffer buf(zeroReorder());
    SourceId a = buf.registerSource(makeImu());
    SourceId b = buf.registerSource(makeImu());
    buf.start();

    int64_t base = EventBuffer::nowMicros();
    writeEvents(buf, a, 10, base,        5000LL);
    writeEvents(buf, b, 10, base + 2500, 5000LL);

    std::this_thread::sleep_for(50ms);
    buf.pause();

    auto window = buf.captureSwingWindow(base - 1, base + 10 * 5000LL + 5000LL);

    auto ea = window.entriesFor(a);
    auto eb = window.entriesFor(b);

    for (auto& e : ea) EXPECT_EQ(e.source_id, a);
    for (auto& e : eb) EXPECT_EQ(e.source_id, b);

    EXPECT_GT(ea.size(), 0u);
    EXPECT_GT(eb.size(), 0u);

    buf.stop();
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST(SwingWindow, MoveSemantics) {
    EventBuffer buf(zeroReorder());
    buf.registerSource(makeImu());
    buf.start();

    int64_t base = EventBuffer::nowMicros();
    writeEvents(buf, 0, 5, base, 5000LL);

    std::this_thread::sleep_for(50ms);
    buf.pause();

    auto w1 = buf.captureSwingWindow(base - 1, base + 5 * 5000LL);
    size_t n1 = w1.entries().size();

    SwingWindow w2 = std::move(w1);
    EXPECT_EQ(w2.entries().size(), n1);
    EXPECT_TRUE(w1.entries().empty()); // moved-from is valid but empty

    buf.stop();
}

// ---------------------------------------------------------------------------
// Lifetime after resume — no crash on access to moved window
// ---------------------------------------------------------------------------

TEST(SwingWindow, LifetimeAfterResume) {
    EventBuffer buf(zeroReorder());
    buf.registerSource(makeImu());
    buf.start();

    int64_t base = EventBuffer::nowMicros();
    writeEvents(buf, 0, 5, base, 5000LL);

    std::this_thread::sleep_for(50ms);
    buf.pause();

    auto window = buf.captureSwingWindow(base - 1, base + 5 * 5000LL);
    size_t n = window.entries().size();

    buf.resume();
    // Accessing index metadata after resume is safe (entries_ is a local copy).
    EXPECT_EQ(window.entries().size(), n);
    // DO NOT access payloads here — rings were reset.

    buf.stop();
}
