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

// Latency benchmark: producer write → consumer-visible in TimelineIndex.
//
// Two simulated sources:
//   - Camera at 60 fps (one 64-byte "frame" every ~16.67ms)
//   - IMU at 200 Hz (one 32-byte packet every 5ms)
//
// The write timestamp is embedded in the first 8 bytes of the slot payload.
// The consumer reads it back and computes latency = nowMicros() - write_ts.
//
// Run for 5 seconds, collect samples, print p50 / p99 / p999 / max.
//
// Target (from design doc, without sanitizers):
//   IMU:   p50 < 100µs,  p99 < 500µs
//   Video: p50 < 1ms,    p99 < 2ms

#include "event_buffer.h"
#include "format_descriptor.h"
#include "source_descriptor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace pinpoint;
using namespace std::chrono_literals;

static void printPercentiles(const char* label, std::vector<int64_t>& samples) {
    if (samples.empty()) {
        printf("  %-10s  (no samples)\n", label);
        return;
    }
    std::sort(samples.begin(), samples.end());
    auto pct = [&](double p) -> int64_t {
        size_t idx = static_cast<size_t>(p / 100.0 * (samples.size() - 1));
        return samples[idx];
    };
    printf("  %-10s  n=%zu  p50=%lldµs  p99=%lldµs  p99.9=%lldµs  max=%lldµs\n",
           label,
           samples.size(),
           (long long)pct(50),
           (long long)pct(99),
           (long long)pct(99.9),
           (long long)samples.back());
}

int main() {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    printf("NOTE: running under sanitizer — latency numbers not representative\n");
#endif

    printf("Latency benchmark: 5-second run\n");
    printf("  Camera:  60 fps (~16.7ms interval)\n");
    printf("  IMU:     200 Hz (5ms interval)\n\n");

    // -----------------------------------------------------------------------
    // Setup
    // -----------------------------------------------------------------------

    EventBufferConfig cfg;
    cfg.reorder_window_us  = 0; // immediate emit for accurate latency measurement
    cfg.watchdog_interval_ms = 10;
    cfg.stall_threshold_mult = 5;
    EventBuffer buf(cfg);

    // Camera source — 64-byte payload (first 8 bytes hold the write timestamp)
    SourceDescriptor cam_desc;
    cam_desc.name             = "camera";
    cam_desc.window_duration  = 5000ms;
    cam_desc.expected_interarrival_us = std::chrono::microseconds(16667); // 60fps
    CameraFormat cam_fmt;
    cam_fmt.pixel_format        = PixelFormat::Unknown;
    cam_fmt.width               = 1;
    cam_fmt.height              = 1;
    cam_fmt.fps_numerator       = 60;
    cam_fmt.fps_denominator     = 1;
    cam_fmt.max_payload_bytes   = 64;
    cam_fmt.typical_payload_bytes = 64;
    cam_desc.format.device  = DeviceKind::Camera_UVC;
    cam_desc.format.format  = cam_fmt;
    SourceId cam_id = buf.registerSource(cam_desc);

    // IMU source — 32-byte payload (first 8 bytes hold the write timestamp)
    SourceDescriptor imu_desc;
    imu_desc.name             = "imu";
    imu_desc.window_duration  = 5000ms;
    imu_desc.expected_interarrival_us = std::chrono::microseconds(5000); // 200Hz
    ImuFormat imu_fmt;
    imu_fmt.device         = DeviceKind::IMU_WitMotion;
    imu_fmt.sample_rate_hz = 200;
    imu_fmt.packet_bytes   = 32;
    imu_desc.format.device  = DeviceKind::IMU_WitMotion;
    imu_desc.format.format  = imu_fmt;
    SourceId imu_id = buf.registerSource(imu_desc);

    buf.start();

    // -----------------------------------------------------------------------
    // Producers
    // -----------------------------------------------------------------------

    std::atomic<bool> running{true};

    // Camera producer: 60 fps
    std::thread cam_thread([&] {
        while (running.load(std::memory_order_relaxed)) {
            int64_t write_ts = EventBuffer::nowMicros();
            auto slot = buf.acquireWriteSlot(cam_id);
            if (slot.valid && slot.capacity >= 8) {
                std::memcpy(slot.data, &write_ts, 8);
                *slot.timestamp_us  = write_ts;
                *slot.bytes_written = 8;
                buf.publish(cam_id, slot.sequence);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(16667));
        }
    });

    // IMU producer: 200 Hz
    std::thread imu_thread([&] {
        while (running.load(std::memory_order_relaxed)) {
            int64_t write_ts = EventBuffer::nowMicros();
            auto slot = buf.acquireWriteSlot(imu_id);
            if (slot.valid && slot.capacity >= 8) {
                std::memcpy(slot.data, &write_ts, 8);
                *slot.timestamp_us  = write_ts;
                *slot.bytes_written = 8;
                buf.publish(imu_id, slot.sequence);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(5000));
        }
    });

    // -----------------------------------------------------------------------
    // Consumer
    // -----------------------------------------------------------------------

    std::vector<int64_t> cam_latencies, imu_latencies;
    cam_latencies.reserve(4096);
    imu_latencies.reserve(32768);

    auto sub = buf.subscribe();
    auto deadline = std::chrono::steady_clock::now() + 5s;

    while (std::chrono::steady_clock::now() < deadline) {
        IndexEntry entry;
        if (sub.waitNext(entry, 1ms)) {
            // Skip stall markers
            if (entry.flags & IndexEntryFlags::SourceStalled) continue;

            int64_t read_ts = EventBuffer::nowMicros();

            // Read the embedded write timestamp from the payload
            auto rh = buf.acquireReadHandle(entry.source_id, entry.source_sequence);
            if (rh.data && rh.bytes >= 8) {
                int64_t write_ts = 0;
                std::memcpy(&write_ts, rh.data, 8);
                if (write_ts > 0) {
                    int64_t latency = read_ts - write_ts;
                    if (latency >= 0 && latency < 10'000'000LL) { // sanity: < 10s
                        if (entry.source_id == cam_id)
                            cam_latencies.push_back(latency);
                        else if (entry.source_id == imu_id)
                            imu_latencies.push_back(latency);
                    }
                }
            }
        }
    }

    running.store(false, std::memory_order_relaxed);
    cam_thread.join();
    imu_thread.join();
    buf.stop();

    // -----------------------------------------------------------------------
    // Results
    // -----------------------------------------------------------------------

    printf("Results (producer write → consumer-visible latency):\n");
    printPercentiles("Camera", cam_latencies);
    printPercentiles("IMU",    imu_latencies);

    printf("\nTargets (no sanitizers):\n");
    printf("  Camera:  p50 < 1000µs,  p99 < 2000µs\n");
    printf("  IMU:     p50 < 100µs,   p99 < 500µs\n");

    return 0;
}
