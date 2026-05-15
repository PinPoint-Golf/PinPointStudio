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

// Two benchmark modes selected by command-line flag:
//
//   --merger   Single IMU source, measures publish → ring-visible latency.
//              Reads the ring directly, bypassing the merger thread entirely.
//              Bounded only by atomic visibility and OS scheduler.
//
//   --e2e      Camera (60Hz) + IMU (200Hz), measures publish → Subscription-
//              visible latency. Passes through the merger and reorder window.
//              Bounded by cross-source ordering delay (design floor ~13.3ms).
//
//   (no arg)   Runs both modes in sequence, separated by a blank line.
//
// Targets (Mode A, without sanitizers):
//   p50 < 100µs,  p99 < 500µs,  p99.9 < 1000µs
//
// Targets (Mode B, without sanitizers):
//   p50 < 2× floor (~26,666µs),  p99 < 30,000µs

#include "event_buffer.h"
#include "format_descriptor.h"
#include "source_descriptor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace pinpoint;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Sanitizer detection
// ---------------------------------------------------------------------------

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
  #define PINPOINT_UNDER_SANITIZER 1
#elif defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    #define PINPOINT_UNDER_SANITIZER 1
  #endif
#endif

// ---------------------------------------------------------------------------
// Source descriptors
// ---------------------------------------------------------------------------

static SourceDescriptor makeImu200(const char* name = "imu") {
    SourceDescriptor d;
    d.name                     = name;
    d.window_duration          = 5000ms;
    d.expected_interarrival_us = std::chrono::microseconds(5000);
    ImuFormat f;
    f.device         = DeviceKind::IMU_WitMotion;
    f.sample_rate_hz = 200;
    f.packet_bytes   = 32;
    d.format.device  = DeviceKind::IMU_WitMotion;
    d.format.format  = f;
    return d;
}

static SourceDescriptor makeCamera60(const char* name = "camera") {
    SourceDescriptor d;
    d.name                     = name;
    d.window_duration          = 5000ms;
    d.expected_interarrival_us = std::chrono::microseconds(16667);
    CameraFormat f;
    f.pixel_format          = PixelFormat::Unknown;
    f.width                 = 1;
    f.height                = 1;
    f.fps_numerator         = 60;
    f.fps_denominator       = 1;
    f.max_payload_bytes     = 64;
    f.typical_payload_bytes = 64;
    d.format.device         = DeviceKind::Camera_UVC;
    d.format.format         = f;
    return d;
}

// ---------------------------------------------------------------------------
// Percentile helper — vector must be sorted by caller
// ---------------------------------------------------------------------------

static int64_t pct(const std::vector<int64_t>& v, double p) {
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size()));
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

// ---------------------------------------------------------------------------
// Mode A — Merger latency (single source, direct ring read)
// ---------------------------------------------------------------------------

static int runMerger() {
    printf("Merger Latency Benchmark — Single IMU Source\n");
    printf("  Measures: publish() -> ring-visible (bypasses merger)\n");
    printf("  Source:   IMU at 200 Hz, 5-second run\n\n");

    EventBufferConfig cfg;
    cfg.reorder_window_us    = 0;
    cfg.watchdog_interval_ms = 100;
    EventBuffer buf(cfg);
    SourceId imu_id = buf.registerSource(makeImu200());
    buf.start();

    std::atomic<bool> running{true};
    std::vector<int64_t> latencies;
    latencies.reserve(2048);

    // Consumer: poll the ring via EventBuffer::acquireReadHandle incrementally.
    // validate() is omitted — EventBuffer does not expose ringFor() in its public
    // API and we will not break encapsulation for benchmark use. With a single
    // consumer tracking the write head there is no realistic overrun risk.
    std::thread consumer([&] {
        uint64_t last_seen = 0;
        while (running.load(std::memory_order_relaxed)) {
            auto h = buf.acquireReadHandle(imu_id, last_seen);
            if (h.data && h.bytes >= 8) {
                int64_t publish_ts = 0;
                h.copyBytesRacy(&publish_ts, sizeof(publish_ts));
                int64_t latency = EventBuffer::nowMicros() - publish_ts;
                if (latency >= 0 && latency < 1'000'000LL)
                    latencies.push_back(latency);
                ++last_seen;
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Producer: 200 Hz with deadline-based timing to correct for drift.
    {
        auto next = std::chrono::steady_clock::now();
        const auto end = next + 5s;
        while (std::chrono::steady_clock::now() < end) {
            next += std::chrono::microseconds(5000);
            std::this_thread::sleep_until(next);

            auto slot = buf.acquireWriteSlot(imu_id);
            if (!slot.valid || slot.capacity < 8) continue;
            int64_t publish_ts = EventBuffer::nowMicros();
            std::memcpy(slot.data, &publish_ts, sizeof(publish_ts));
            *slot.bytes_written = 32;
            *slot.timestamp_us  = publish_ts;
            buf.publish(imu_id, slot.sequence);
        }
    }

    running.store(false, std::memory_order_relaxed);
    consumer.join();
    buf.stop();

    if (latencies.empty()) {
        printf("  (no samples collected)\n");
        return 1;
    }

    std::sort(latencies.begin(), latencies.end());

    int64_t p50  = pct(latencies, 0.500);
    int64_t p99  = pct(latencies, 0.990);
    int64_t p999 = pct(latencies, 0.999);
    int64_t mx   = latencies.back();

    constexpr int64_t T_P50  =  100;
    constexpr int64_t T_P99  =  500;
    constexpr int64_t T_P999 = 1000;

    auto mark = [](bool ok) -> const char* { return ok ? "PASS" : "FAIL"; };

    printf("  n=%zu\n\n", latencies.size());
    printf("  %-8s  %-12s  %-14s  %s\n", "Metric", "Result (us)", "Target", "");
    printf("  --------  ------------  --------------  ----\n");
    printf("  %-8s  %-12lld  %-14s  %s\n", "p50",   (long long)p50,  "< 100 us",  mark(p50  < T_P50));
    printf("  %-8s  %-12lld  %-14s  %s\n", "p99",   (long long)p99,  "< 500 us",  mark(p99  < T_P99));
    printf("  %-8s  %-12lld  %-14s  %s\n", "p99.9", (long long)p999, "< 1000 us", mark(p999 < T_P999));
    printf("  %-8s  %-12lld  %-14s\n",     "max",   (long long)mx,   "--");

    bool all_pass = (p50 < T_P50) && (p99 < T_P99) && (p999 < T_P999);
    printf("\n  %s\n", all_pass ? "All targets PASSED." : "One or more targets FAILED.");
    return all_pass ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Mode B — End-to-end latency (multi-source, via Subscription)
// ---------------------------------------------------------------------------

static int runE2E() {
    printf("End-to-End Latency Benchmark — Camera 60 Hz + IMU 200 Hz\n");
    printf("  Measures: publish() -> Subscription-visible (merger-mediated)\n");
    printf("  Reorder window: 5000 us\n\n");

    // Use default reorder_window_us = 5000
    EventBufferConfig cfg;
    cfg.watchdog_interval_ms = 100;
    EventBuffer buf(cfg);
    SourceId cam_id = buf.registerSource(makeCamera60());
    SourceId imu_id = buf.registerSource(makeImu200());
    buf.start();

    std::atomic<bool> running{true};
    std::vector<int64_t> cam_latencies, imu_latencies;
    cam_latencies.reserve(400);
    imu_latencies.reserve(1200);

    // Camera producer: 60 Hz
    std::thread cam_thread([&] {
        auto next = std::chrono::steady_clock::now();
        const auto end = next + 5s;
        while (std::chrono::steady_clock::now() < end) {
            next += std::chrono::microseconds(16667);
            std::this_thread::sleep_until(next);
            auto slot = buf.acquireWriteSlot(cam_id);
            if (!slot.valid || slot.capacity < 8) continue;
            int64_t ts = EventBuffer::nowMicros();
            std::memcpy(slot.data, &ts, sizeof(ts));
            *slot.bytes_written = 8;
            *slot.timestamp_us  = ts;
            buf.publish(cam_id, slot.sequence);
        }
    });

    // IMU producer: 200 Hz
    std::thread imu_thread([&] {
        auto next = std::chrono::steady_clock::now();
        const auto end = next + 5s;
        while (std::chrono::steady_clock::now() < end) {
            next += std::chrono::microseconds(5000);
            std::this_thread::sleep_until(next);
            auto slot = buf.acquireWriteSlot(imu_id);
            if (!slot.valid || slot.capacity < 8) continue;
            int64_t ts = EventBuffer::nowMicros();
            std::memcpy(slot.data, &ts, sizeof(ts));
            *slot.bytes_written = 8;
            *slot.timestamp_us  = ts;
            buf.publish(imu_id, slot.sequence);
        }
    });

    // Consumer via Subscription — drain for 5.5s to catch the last batch
    {
        auto sub      = buf.subscribe();
        const auto deadline = std::chrono::steady_clock::now() + 5500ms;
        while (std::chrono::steady_clock::now() < deadline) {
            IndexEntry entry;
            if (!sub.waitNext(entry, std::chrono::microseconds(1000))) continue;
            if (entry.flags & IndexEntryFlags::SourceStalled) continue;

            auto h = buf.acquireReadHandle(entry.source_id, entry.source_sequence);
            if (!h.data || h.bytes < 8) continue;

            int64_t publish_ts = 0;
            h.copyBytesRacy(&publish_ts, sizeof(publish_ts));
            int64_t latency = EventBuffer::nowMicros() - publish_ts;
            if (latency < 0 || latency >= 10'000'000LL) continue;

            if      (entry.source_id == imu_id) imu_latencies.push_back(latency);
            else if (entry.source_id == cam_id) cam_latencies.push_back(latency);
        }
    }

    running.store(false, std::memory_order_relaxed);
    cam_thread.join();
    imu_thread.join();
    buf.stop();

    // Theoretical latency floor:
    //   safe_emit_until = min(all source last_ts) - reorder_window
    //   An IMU event at T is blocked until camera advances past T + reorder_window.
    //   Camera writes every 16667us so average wait = camera_interval/2 = 8333us.
    //   Floor = reorder_window + camera_interval/2 = 5000 + 8333 = 13333us.
    constexpr int64_t REORDER_WINDOW_US  = 5000;
    constexpr int64_t CAMERA_INTERVAL_US = 16667;
    constexpr int64_t FLOOR_US  = REORDER_WINDOW_US + CAMERA_INTERVAL_US / 2;
    constexpr int64_t T_P50     = FLOOR_US * 2;
    constexpr int64_t T_P99     = 30000;

    auto mark = [](bool ok) -> const char* { return ok ? "PASS" : "FAIL"; };

    int exit_code = 0;

    auto printSource = [&](const char* label, std::vector<int64_t>& v,
                           int expected_n) {
        printf("Source: %s\n", label);
        if (v.empty()) {
            printf("  (no samples collected)\n\n");
            exit_code = 1;
            return;
        }
        std::sort(v.begin(), v.end());

        int64_t p50 = pct(v, 0.50);
        int64_t p99 = pct(v, 0.99);
        int64_t mx  = v.back();

        printf("  n=%zu (expected ~%d)", (size_t)v.size(), expected_n);
        if ((int)v.size() < expected_n * 8 / 10)
            printf("  WARNING: sample count < 80%% of expected — floor formula may not apply");
        printf("\n\n");

        printf("  %-8s  %-12s  %-12s  %-22s  %s\n",
               "Metric", "Result (us)", "Floor (us)", "Target (2x floor)", "");
        printf("  --------  ------------  ------------  ----------------------  ----\n");
        printf("  %-8s  %-12lld  %-12lld  %-22lld  %s\n",
               "p50", (long long)p50, (long long)FLOOR_US, (long long)T_P50,
               mark(p50 < T_P50));
        printf("  %-8s  %-12lld  %-12s  %-22lld  %s\n",
               "p99", (long long)p99, "--", (long long)T_P99,
               mark(p99 < T_P99));
        printf("  %-8s  %-12lld  %-12s  %-22s\n",
               "max", (long long)mx, "--", "--");
        printf("\n");

        if (!(p50 < T_P50) || !(p99 < T_P99))
            exit_code = 1;
    };

    printSource("IMU (200 Hz)",   imu_latencies, 1000);
    printSource("Camera (60 Hz)", cam_latencies, 300);

    printf("%s\n", exit_code == 0 ? "All targets PASSED." : "One or more targets FAILED.");
    return exit_code;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef PINPOINT_UNDER_SANITIZER
    printf("NOTE: running under sanitizer — latency numbers not representative\n\n");
#endif

    bool do_merger = true;
    bool do_e2e    = true;

    if (argc >= 2) {
        std::string arg = argv[1];
        if (arg == "--merger") {
            do_e2e = false;
        } else if (arg == "--e2e") {
            do_merger = false;
        } else {
            fprintf(stderr, "Usage: %s [--merger|--e2e]\n", argv[0]);
            return 1;
        }
    }

    int ret = 0;
    if (do_merger) {
        ret |= runMerger();
        if (do_e2e) printf("\n");
    }
    if (do_e2e) {
        ret |= runE2E();
    }
    return ret;
}
