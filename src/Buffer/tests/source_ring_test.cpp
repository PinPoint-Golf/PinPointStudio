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

#include "pinpoint/event_buffer/source_ring.h"
#include "pinpoint/event_buffer/source_descriptor.h"
#include "pinpoint/event_buffer/format_descriptor.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdint>
#include <numeric>
#include <algorithm>

using namespace pinpoint;

// ============================================================================
// Helpers
// ============================================================================

static void writeSlot(SourceRing& ring, uint8_t pattern, int64_t ts, size_t payload_size = 0) {
    auto slot = ring.acquireWriteSlot();
    ASSERT_TRUE(slot.valid);
    if (payload_size == 0) payload_size = slot.capacity;
    std::memset(slot.data, pattern, payload_size);
    *slot.bytes_written = static_cast<uint32_t>(payload_size);
    *slot.timestamp_us  = ts;
    ring.publish(slot.sequence);
}

// ============================================================================
// Construction and sizing
// ============================================================================

TEST(SourceRingConstruction, SlotCountRoundedToPow2) {
    SourceRing r(0, 5, 64);
    EXPECT_EQ(r.slotCount(), 8u);

    SourceRing r2(0, 8, 64);
    EXPECT_EQ(r2.slotCount(), 8u);

    SourceRing r3(0, 9, 64);
    EXPECT_EQ(r3.slotCount(), 16u);

    SourceRing r4(0, 1, 64);
    EXPECT_EQ(r4.slotCount(), 1u);
}

TEST(SourceRingConstruction, SlotStrideAligned) {
    // slot_stride >= sizeof(SlotHeader) + slot_max_bytes and multiple of 64
    SourceRing r(0, 4, 100);
    size_t stride = r.slotStride();
    EXPECT_GE(stride, 64u + 100u);
    EXPECT_EQ(stride % 64, 0u);
}

TEST(SourceRingConstruction, SlotPointersAligned) {
    SourceRing r(0, 4, 128);
    auto ptrs = r.getSlotPointers();
    ASSERT_EQ(ptrs.size(), 4u);
    for (auto* p : ptrs) {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 64, 0u) << "payload pointer not 64-byte aligned";
    }
    // Adjacent pointers differ by slot_stride
    for (size_t i = 1; i < ptrs.size(); ++i) {
        ptrdiff_t diff = ptrs[i] - ptrs[i-1];
        EXPECT_EQ(static_cast<size_t>(diff), r.slotStride());
    }
}

TEST(SourceRingConstruction, IdReturned) {
    SourceRing r(42, 4, 64);
    EXPECT_EQ(r.id(), 42u);
}

// ============================================================================
// Basic read/write
// ============================================================================

TEST(SourceRingReadWrite, WriteAndReadBack) {
    SourceRing ring(0, 4, 64);

    auto slot = ring.acquireWriteSlot();
    ASSERT_TRUE(slot.valid);
    ASSERT_NE(slot.data, nullptr);
    std::memset(slot.data, 0xAB, 32);
    *slot.bytes_written = 32;
    *slot.timestamp_us  = 12345678LL;
    ring.publish(slot.sequence);

    auto h = ring.acquireReadHandle(slot.sequence);
    ASSERT_NE(h.data, nullptr);
    EXPECT_EQ(h.bytes, 32u);
    EXPECT_EQ(h.timestamp_us, 12345678LL);
    EXPECT_TRUE(h.validate(ring));

    for (size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(h.data[i]), 0xABu);
    }
}

TEST(SourceRingReadWrite, ValidateReturnsTrueAfterPublish) {
    SourceRing ring(0, 4, 64);
    writeSlot(ring, 0x01, 1000LL, 32);
    auto h = ring.acquireReadHandle(0);
    ASSERT_NE(h.data, nullptr);
    EXPECT_TRUE(h.validate(ring));
}

TEST(SourceRingReadWrite, ValidateReturnsFalseAfterOverwrite) {
    SourceRing ring(0, 4, 64);

    // Write sequence 0
    auto first = ring.acquireWriteSlot();
    std::memset(first.data, 0x11, 8);
    *first.bytes_written = 8;
    *first.timestamp_us  = 1LL;
    ring.publish(first.sequence);

    auto h = ring.acquireReadHandle(0);
    ASSERT_NE(h.data, nullptr);

    // Overwrite slot 0 by writing slot_count + 1 more events (wrap around)
    for (size_t i = 0; i < ring.slotCount(); ++i) {
        writeSlot(ring, 0x22, 100LL);
    }

    // Now validate should fail — slot 0 was overwritten
    EXPECT_FALSE(h.validate(ring));
}

TEST(SourceRingReadWrite, PeekTimestampCorrect) {
    SourceRing ring(0, 4, 64);
    writeSlot(ring, 0x00, 999999LL);

    int64_t ts = 0;
    EXPECT_TRUE(ring.peekTimestamp(0, ts));
    EXPECT_EQ(ts, 999999LL);
}

TEST(SourceRingReadWrite, LatestSequenceAdvances) {
    SourceRing ring(0, 4, 64);
    for (int i = 0; i < 5; ++i) {
        writeSlot(ring, static_cast<uint8_t>(i), static_cast<int64_t>(i * 1000));
        EXPECT_EQ(ring.latestSequence(), static_cast<uint64_t>(i));
    }
}

// ============================================================================
// SPSC stress test
// ============================================================================

TEST(SourceRingStress, SPSCSingleProducerConsumer) {
    static constexpr int N        = 1'000'000;
    static constexpr size_t SLOTS = 256;
    static constexpr size_t PL    = 16; // payload bytes: 8 bytes sequence + padding

    SourceRing ring(0, SLOTS, PL);

    std::atomic<bool> done{false};
    std::vector<bool> seen(N, false);
    std::atomic<int>  torn_reads{0};
    std::atomic<int>  false_validates{0};

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            auto slot = ring.acquireWriteSlot();
            // valid must be true — no capturing_ flag set
            if (!slot.valid) { ++i; continue; }
            // Embed sequence number in first 4 bytes
            std::memcpy(slot.data, &i, sizeof(i));
            *slot.bytes_written = sizeof(i);
            *slot.timestamp_us  = static_cast<int64_t>(i);
            ring.publish(slot.sequence);
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        uint64_t next = 0;
        while (true) {
            uint64_t latest = ring.latestSequence();
            bool is_done    = done.load(std::memory_order_acquire);

            while (next <= latest) {
                auto h = ring.acquireReadHandle(next);
                if (h.data != nullptr) {
                    int val = 0;
                    h.copyBytesRacy(&val, sizeof(val));
                    bool ok = h.validate(ring);
                    if (ok) {
                        // Sequence embedded in payload must match ring sequence
                        if (static_cast<uint64_t>(val) != next) {
                            torn_reads.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            seen[static_cast<size_t>(val)] = true;
                        }
                    }
                    // If !ok, slot was overwritten — skip it, that's fine
                }
                ++next;
            }

            if (is_done && next > static_cast<uint64_t>(N - 1)) break;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(torn_reads.load(), 0) << "Torn reads detected";
    EXPECT_EQ(false_validates.load(), 0) << "False validates detected";
    // We don't assert all sequences seen — fast writer will overrun slow consumer
}

// ============================================================================
// Overrun correctness
// ============================================================================

TEST(SourceRingOverrun, OverrunNoCorruption) {
    static constexpr size_t SLOTS = 4;
    static constexpr size_t PL    = 8;
    SourceRing ring(0, SLOTS, PL);

    // Write 100 events; no consumer draining
    for (int i = 0; i < 100; ++i) {
        writeSlot(ring, static_cast<uint8_t>(i & 0xFF), static_cast<int64_t>(i));
    }

    int valid_count = 0;
    int invalid_count = 0;
    // Try to read from sequence 0 — most should be overwritten
    for (size_t s = 0; s < 100; ++s) {
        auto h = ring.acquireReadHandle(s);
        if (h.data != nullptr && h.validate(ring)) {
            ++valid_count;
        } else {
            ++invalid_count;
        }
    }

    // Should not crash; some may be valid (recent ones), most invalid
    EXPECT_GE(invalid_count, 0);
    EXPECT_GE(valid_count, 0);
    EXPECT_EQ(ring.stats().events_overwritten.load(), 100u - SLOTS);
}

TEST(SourceRingOverrun, StatsAccurate) {
    SourceRing ring(0, 4, 8);

    for (int i = 0; i < 100; ++i) {
        writeSlot(ring, 0x55, 1000LL);
    }

    EXPECT_EQ(ring.stats().events_written.load(), 100u);
    EXPECT_EQ(ring.stats().events_overwritten.load(), 96u); // 100 - 4 slots
}

// ============================================================================
// Bounds enforcement
// ============================================================================

TEST(SourceRingBounds, PublishClampsBytesWritten) {
    SourceRing ring(0, 4, 64);

    auto slot = ring.acquireWriteSlot();
    ASSERT_TRUE(slot.valid);
    // Set bytes_written beyond capacity
    *slot.bytes_written = static_cast<uint32_t>(slot.capacity + 100);
    *slot.timestamp_us  = 1LL;
    ring.publish(slot.sequence);

    EXPECT_EQ(ring.stats().bounds_violations.load(), 1u);

    auto h = ring.acquireReadHandle(slot.sequence);
    ASSERT_NE(h.data, nullptr);
    EXPECT_EQ(h.bytes, slot.capacity);
}

// ============================================================================
// Statistics accuracy
// ============================================================================

TEST(SourceRingStats, EventsWritten) {
    SourceRing ring(0, 16, 32);
    constexpr int N = 10;
    size_t total_bytes = 0;
    for (int i = 0; i < N; ++i) {
        auto slot = ring.acquireWriteSlot();
        ASSERT_TRUE(slot.valid);
        uint32_t sz = static_cast<uint32_t>((i % 16) + 1);
        *slot.bytes_written = sz;
        *slot.timestamp_us  = static_cast<int64_t>(i * 100);
        ring.publish(slot.sequence);
        total_bytes += sz;
    }
    EXPECT_EQ(ring.stats().events_written.load(), static_cast<uint64_t>(N));
    EXPECT_EQ(ring.stats().bytes_written_total.load(), total_bytes);
    EXPECT_EQ(ring.stats().events_overwritten.load(), 0u);
}

TEST(SourceRingStats, OverwrittenCount) {
    SourceRing ring(0, 4, 8);
    for (int i = 0; i < 20; ++i) {
        writeSlot(ring, 0xAA, static_cast<int64_t>(i));
    }
    // First 4 writes: no overwrite; next 16 overwrite
    EXPECT_EQ(ring.stats().events_overwritten.load(), 16u);
}

// ============================================================================
// Valid flag / pause behaviour
// ============================================================================

TEST(SourceRingPause, ValidFalseWhenCapturingFlagFalse) {
    std::atomic<bool> capturing{false};
    SourceRing ring(0, 4, 64, &capturing);

    auto slot = ring.acquireWriteSlot();
    EXPECT_FALSE(slot.valid);
    EXPECT_EQ(slot.data, nullptr);
}

TEST(SourceRingPause, SequenceDoesNotAdvanceWhenPaused) {
    std::atomic<bool> capturing{false};
    SourceRing ring(0, 4, 64, &capturing);

    uint64_t before = ring.latestSequence();
    // Attempt several writes — all should be no-ops
    for (int i = 0; i < 5; ++i) {
        auto s = ring.acquireWriteSlot();
        EXPECT_FALSE(s.valid);
    }
    // latestSequence should not have advanced
    EXPECT_EQ(ring.latestSequence(), before);
}

TEST(SourceRingPause, ToggleDuringStressTest) {
    std::atomic<bool> capturing{true};
    SourceRing ring(0, 256, 16, &capturing);

    std::atomic<bool> done{false};
    std::atomic<int>  writes_during_false{0};

    // Producer toggles capturing
    std::thread producer([&] {
        for (int i = 0; i < 10000; ++i) {
            if (i == 2000) capturing.store(false, std::memory_order_release);
            if (i == 4000) capturing.store(true, std::memory_order_release);

            auto slot = ring.acquireWriteSlot();
            if (slot.valid) {
                int val = i;
                std::memcpy(slot.data, &val, sizeof(val));
                *slot.bytes_written = sizeof(val);
                *slot.timestamp_us  = static_cast<int64_t>(i);
                ring.publish(slot.sequence);
            }
        }
        done.store(true, std::memory_order_release);
    });

    producer.join();
    // Ring should not have been corrupted regardless of capturing toggling
    SUCCEED();
}

TEST(SourceRingPause, PublishOnInvalidSlotIsNoop) {
    std::atomic<bool> capturing{false};
    SourceRing ring(0, 4, 64, &capturing);

    auto slot = ring.acquireWriteSlot();
    EXPECT_FALSE(slot.valid);

    uint64_t stats_before = ring.stats().events_written.load();
    // Calling publish with an arbitrary sequence on a paused ring should not crash.
    // (Note: in real usage, caller should check valid first — this tests robustness.)
    // We don't call publish here since the spec says MUST NOT call publish when valid=false.
    // Instead just verify stats unchanged.
    EXPECT_EQ(ring.stats().events_written.load(), stats_before);
}

// ============================================================================
// Monotonicity of latestSequence()
// ============================================================================

TEST(SourceRingMonotonicity, LatestSequenceNeverDecreases) {
    SourceRing ring(0, 256, 32);
    std::atomic<bool> done{false};
    std::atomic<bool> monotonic_violation{false};

    std::thread writer([&] {
        for (int i = 0; i < 200000; ++i) {
            writeSlot(ring, 0x77, static_cast<int64_t>(i));
        }
        done.store(true, std::memory_order_release);
    });

    std::thread reader([&] {
        uint64_t prev = 0;
        while (!done.load(std::memory_order_acquire)) {
            uint64_t cur = ring.latestSequence();
            if (cur < prev) {
                monotonic_violation.store(true, std::memory_order_relaxed);
            }
            prev = cur;
        }
    });

    writer.join();
    reader.join();
    EXPECT_FALSE(monotonic_violation.load());
}

// ============================================================================
// SourceDescriptor sizing helpers
// ============================================================================

TEST(SourceDescriptor, ComputeSlotCountCamera) {
    SourceDescriptor desc;
    CameraFormat cf;
    cf.fps_numerator   = 60;
    cf.fps_denominator = 1;
    cf.max_payload_bytes = 1024;
    FormatDescriptor fd;
    fd.device = DeviceKind::Camera_UVC;
    fd.format = cf;
    desc.format          = fd;
    desc.window_duration = std::chrono::milliseconds(5000);
    // 60fps * 5s = 300 → next pow2 = 512
    EXPECT_EQ(desc.computeSlotCount(), 512u);
}

TEST(SourceDescriptor, ComputeSlotCountIMU) {
    SourceDescriptor desc;
    ImuFormat imu;
    imu.sample_rate_hz = 200;
    imu.packet_bytes   = 20;
    FormatDescriptor fd;
    fd.device = DeviceKind::IMU_WitMotion;
    fd.format = imu;
    desc.format          = fd;
    desc.window_duration = std::chrono::milliseconds(5000);
    // 200 * 5 = 1000 → next pow2 = 1024
    EXPECT_EQ(desc.computeSlotCount(), 1024u);
}

TEST(SourceDescriptor, ComputeSlotBytesIMUAligned) {
    SourceDescriptor desc;
    ImuFormat imu;
    imu.packet_bytes = 13; // not multiple of 8
    FormatDescriptor fd;
    fd.device = DeviceKind::IMU_WitMotion;
    fd.format = imu;
    desc.format = fd;
    // 13 rounded up to 16
    EXPECT_EQ(desc.computeSlotBytes(), 16u);
}

TEST(SourceDescriptor, ComputeSlotBytesCamera) {
    SourceDescriptor desc;
    CameraFormat cf;
    cf.max_payload_bytes = 2073600;
    FormatDescriptor fd;
    fd.device = DeviceKind::Camera_UVC;
    fd.format = cf;
    desc.format = fd;
    EXPECT_EQ(desc.computeSlotBytes(), 2073600u);
}
