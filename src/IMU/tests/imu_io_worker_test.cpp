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

// ImuIoWorker — the per-packet IMU hot path that moves onto the dedicated
// I/O thread (docs/implementation/IMU_IO_THREAD_IMPL.md, stage W0). Validates
// the migration's load-bearing invariants WITHOUT hardware:
//   1. hot-path correctness (anat transform, display remap, velocity, rate)
//   2. snapshot coherence under cross-thread hammering (no torn reads)
//   3. atomic calibration swap mid-stream (old pair or new pair, never a mix)
//   4. EventBuffer ring writes + the detachBuffer() producer STOP BARRIER
//   5. impact detection across the thread boundary (deterministic clock)
//   6. attach/detach/teardown churn against a live producer
// Build with -DIMU_TESTS_TSAN=ON to put 2/3/6 under ThreadSanitizer.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

#include <QQuaternion>

#include "../imu_io_worker.h"
#include "../imu_calibration.h"
#include "event_buffer.h"
#include "imu_sample.h"

static int g_fail = 0;

#define CHECK(label, cond)                                                       \
    do { bool ok = (cond);                                                       \
         std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                \
         if (!ok) ++g_fail; } while (0)

#define CHECK_NEAR(label, got, want, tol)                                        \
    do { double g = (got), w = (want); bool ok = std::abs(g - w) <= (tol);       \
         std::printf("  [%s] %-46s got %10.4f  want %10.4f (tol %g)\n",          \
                     ok ? "PASS" : "FAIL", label, g, w, double(tol));            \
         if (!ok) ++g_fail; } while (0)

namespace {

bool quatNear(const QQuaternion &a, const QQuaternion &b, float tol = 1e-5f)
{
    // double-cover: q and −q are the same rotation
    const float dot = qAbs(QQuaternion::dotProduct(a, b));
    return dot >= 1.0f - tol;
}

// 200 Hz synthetic clock, µs domain (mirrors EventBuffer::nowMicros usage).
struct Clock {
    qint64 t = 1'000'000;
    qint64 step(qint64 us = 5000) { t += us; return t; }
};

ImuIoWorker::RawSample mk(qint64 t, float qw, float qx, float qy, float qz,
                          float ax, float ay, float az,
                          float gx, float gy, float gz,
                          float er = 0.f, float ep = 0.f, float ey = 0.f)
{
    return ImuIoWorker::RawSample{ t, qw, qx, qy, qz, ax, ay, az, gx, gy, gz, er, ep, ey };
}

pinpoint::SourceId registerImuSource(pinpoint::EventBuffer &buf, const char *ident)
{
    pinpoint::SourceDescriptor desc;
    desc.name       = "test-imu";
    desc.identifier = ident;
    pinpoint::ImuFormat fmt{};
    fmt.device         = pinpoint::DeviceKind::IMU_WitMotion;
    fmt.sample_rate_hz = 200;
    fmt.packet_bytes   = sizeof(pinpoint::ImuSample);
    fmt.packet_schema  = "imu_sample_v2";
    desc.format.device            = pinpoint::DeviceKind::IMU_WitMotion;
    desc.format.format            = fmt;
    desc.window_duration          = std::chrono::milliseconds(5000);
    desc.expected_interarrival_us = std::chrono::microseconds(5000);
    desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;
    return buf.registerSource(desc);
}

} // namespace

int main()
{
    // ===== 1. Hot-path correctness (single thread, deterministic clock) =====
    std::printf("== hot path ==\n");
    {
        ImuIoWorker w;
        Clock clk;

        // Uncalibrated: anatQuat == raw; display accel remap (x, z, −y).
        const QQuaternion q1 = QQuaternion::fromAxisAndAngle(0, 1, 0, 30.f);
        w.processSample(mk(clk.step(), q1.scalar(), q1.x(), q1.y(), q1.z(),
                        0.1f, -0.3f, 0.95f, 1.f, 2.f, 3.f));
        ImuIoWorker::LiveSample s = w.snapshot();
        CHECK("seq advanced", s.seq == 1);
        CHECK("raw quat stored", quatNear(
                  QQuaternion(s.quatW, s.quatX, s.quatY, s.quatZ), q1));
        CHECK("anatQuat == raw while uncalibrated", quatNear(s.anatQuat, q1));
        CHECK_NEAR("raw accel x stored", s.accelX, 0.1, 1e-6);
        CHECK_NEAR("raw accel y stored", s.accelY, -0.3, 1e-6);
        CHECK_NEAR("raw accel z stored", s.accelZ, 0.95, 1e-6);
        CHECK_NEAR("raw gyro y stored", s.gyroY, 2.0, 1e-6);

        // Calibrated: anatQuat == A·q·M exactly (the shared composition).
        const QQuaternion A = QQuaternion::fromAxisAndAngle(0, 0, 1, 90.f);
        const QQuaternion M = QQuaternion::fromAxisAndAngle(1, 0, 0, 90.f);
        w.setAnatomical(A, M, true);
        w.processSample(mk(clk.step(), q1.scalar(), q1.x(), q1.y(), q1.z(),
                        0, 0, 1, 0, 0, 0));
        s = w.snapshot();
        CHECK("anatQuat == A*q*M when calibrated",
              quatNear(s.anatQuat, imu_calibration::toAnatomical(A, q1, M)));

        // Angular velocity: 9° rotation over a 5 ms step = 1800 °/s.
        const QQuaternion q2 = QQuaternion::fromAxisAndAngle(0, 1, 0, 39.f);
        w.processSample(mk(clk.step(), q2.scalar(), q2.x(), q2.y(), q2.z(),
                        0, 0, 1, 0, 0, 0));
        s = w.snapshot();
        CHECK_NEAR("angular velocity (9 deg / 5 ms)", s.angularVelocityDps, 1800.0, 1.0);

        // Burst delivery (Windows BLE batching): sub-5 ms arrivals keep the
        // baseline; the next qualifying pair measures across the whole gap.
        ImuIoWorker wb;
        Clock bclk;
        const QQuaternion b0 = QQuaternion::fromAxisAndAngle(1, 0, 0, 0.f);
        wb.processSample(mk(bclk.step(), b0.scalar(), b0.x(), b0.y(), b0.z(), 0, 0, 1, 0, 0, 0));
        for (int i = 1; i <= 3; ++i) {   // burst: 3 packets 100 µs apart
            const QQuaternion bq = QQuaternion::fromAxisAndAngle(1, 0, 0, float(i));
            wb.processSample(mk(bclk.step(100), bq.scalar(), bq.x(), bq.y(), bq.z(), 0, 0, 1, 0, 0, 0));
        }
        // Gap packet at +15 ms from baseline: 4° over ~15.3 ms ≈ 261 °/s.
        const QQuaternion bg = QQuaternion::fromAxisAndAngle(1, 0, 0, 4.f);
        wb.processSample(mk(bclk.step(15000), bg.scalar(), bg.x(), bg.y(), bg.z(), 0, 0, 1, 0, 0, 0));
        CHECK_NEAR("burst-tolerant velocity (4 deg over burst window)",
                   wb.snapshot().angularVelocityDps, 4.0 / 0.015, 30.0);

        // Data rate: 200 Hz synthetic feed reports ~200 Hz.
        ImuIoWorker wr;
        Clock rclk;
        for (int i = 0; i < 400; ++i)
            wr.processSample(mk(rclk.step(), 1, 0, 0, 0, 0, 0, 1, 0, 0, 0));
        CHECK_NEAR("data rate at 200 Hz feed", wr.snapshot().dataRateHz, 200.0, 5.0);
    }

    // ===== 2. Snapshot coherence under cross-thread hammering =====
    std::printf("== snapshot coherence ==\n");
    {
        ImuIoWorker w;
        std::atomic<bool> done{false};
        std::thread producer([&] {
            Clock clk;
            for (quint64 k = 1; k <= 300000; ++k) {
                // Pattern-encoded sample: y = 2x, z = 3x (raw fields).
                const float kf = float(k % 100000);
                w.processSample(mk(clk.step(), 1, 0, 0, 0,
                                kf, 2.0f * kf, 3.0f * kf, 0, 0, 0));
            }
            done.store(true, std::memory_order_release);
        });

        quint64 reads = 0, lastSeq = 0;
        bool coherent = true, monotonic = true;
        while (!done.load(std::memory_order_acquire)) {
            const ImuIoWorker::LiveSample s = w.snapshot();
            ++reads;
            if (s.seq < lastSeq) monotonic = false;
            lastSeq = s.seq;
            if (s.accelX != 0.0f
                && (s.accelY != 2.0f * s.accelX || s.accelZ != 3.0f * s.accelX))
                coherent = false;
        }
        producer.join();
        CHECK("no torn snapshot across threads", coherent);
        CHECK("seq monotonic across threads", monotonic);
        std::printf("    (%llu snapshots while producer wrote 300k samples)\n",
                    static_cast<unsigned long long>(reads));
    }

    // ===== 3. Atomic calibration swap mid-stream =====
    std::printf("== calibration swap ==\n");
    {
        ImuIoWorker w;
        const QQuaternion q  = QQuaternion::fromAxisAndAngle(0, 1, 0, 37.f);
        const QQuaternion A1 = QQuaternion::fromAxisAndAngle(0, 0, 1, 90.f);
        const QQuaternion M1 = QQuaternion::fromAxisAndAngle(1, 0, 0, 90.f);
        const QQuaternion A2 = QQuaternion::fromAxisAndAngle(0, 0, 1, -45.f);
        const QQuaternion M2 = QQuaternion::fromAxisAndAngle(1, 0, 0, -30.f);
        const QQuaternion expect1 = imu_calibration::toAnatomical(A1, q, M1);
        const QQuaternion expect2 = imu_calibration::toAnatomical(A2, q, M2);

        w.setAnatomical(A1, M1, true);
        std::atomic<bool> done{false};
        std::thread producer([&] {
            Clock clk;
            for (int k = 0; k < 200000; ++k)
                w.processSample(mk(clk.step(), q.scalar(), q.x(), q.y(), q.z(),
                                0, 0, 1, 0, 0, 0));
            done.store(true, std::memory_order_release);
        });

        bool pure = true;
        int flips = 0;
        bool usingFirst = true;
        while (!done.load(std::memory_order_acquire)) {
            usingFirst = !usingFirst;
            ++flips;
            if (usingFirst) w.setAnatomical(A1, M1, true);
            else            w.setAnatomical(A2, M2, true);
            const ImuIoWorker::LiveSample s = w.snapshot();
            if (s.seq > 0 && !quatNear(s.anatQuat, expect1, 1e-4f)
                          && !quatNear(s.anatQuat, expect2, 1e-4f))
                pure = false;   // a blended A1/M2 (or torn) composition
        }
        producer.join();
        CHECK("anatQuat is always one WHOLE calibration pair", pure);
        std::printf("    (%d calibration swaps against a 200k-sample stream)\n", flips);
    }

    // ===== 4. Ring writes + the detachBuffer stop barrier =====
    std::printf("== ring write + stop barrier ==\n");
    {
        pinpoint::EventBuffer buf;
        const pinpoint::SourceId sid = registerImuSource(buf, "stop-barrier");
        buf.start();   // Idle -> Capturing (spawns the merger)

        ImuIoWorker w;
        w.attachBuffer(&buf, sid);

        std::atomic<bool>    stop{false};
        std::atomic<quint64> pushed{0};
        std::thread producer([&] {
            Clock clk;
            while (!stop.load(std::memory_order_acquire)) {
                w.processSample(mk(clk.step(), 1, 0, 0, 0, 0, 0, 1, 0, 0, 0));
                pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });

        while (pushed.load(std::memory_order_relaxed) < 20000)
            std::this_thread::yield();
        const uint64_t writtenBefore = buf.statsFor(sid).events_written.load();
        CHECK("ring writes flow while attached", writtenBefore > 1000);

        // STOP BARRIER: after detachBuffer returns, the producer may keep
        // hammering but the write counter must be frozen.
        w.detachBuffer();
        const uint64_t writtenAtDetach = buf.statsFor(sid).events_written.load();
        const quint64 pushedAtDetach = pushed.load(std::memory_order_relaxed);
        while (pushed.load(std::memory_order_relaxed) < pushedAtDetach + 50000)
            std::this_thread::yield();
        const uint64_t writtenAfter = buf.statsFor(sid).events_written.load();
        CHECK("no ring writes after detachBuffer returns",
              writtenAfter == writtenAtDetach);

        // Producer contract: pause, then deregister — must not crash/assert
        // while the (detached) producer thread is still running.
        buf.pause();
        buf.deregisterSource(sid);
        stop.store(true, std::memory_order_release);
        producer.join();
        CHECK("deregister with live (detached) producer survives", true);
    }

    // ===== 5. Impact detection across the thread boundary =====
    std::printf("== impact detection ==\n");
    {
        ImuIoWorker wk;
        std::atomic<int>    impacts{0};
        std::atomic<qint64> estUs{0};
        QObject::connect(&wk, &ImuIoWorker::impactDetected,
                         [&](qint64 t, double) { ++impacts; estUs = t; });

        qint64 peakT = 0;
        std::thread producer([&] {
            Clock clk;
            auto push = [&](float accel, float gyro, float vertical) {
                const QQuaternion q = vertical > 0.5f
                    ? QQuaternion::fromAxisAndAngle(1, 0, 0, 90.f)   // +Y -> +Z (vertical)
                    : QQuaternion();                                 // +Y horizontal
                wk.processSample(mk(clk.step(), q.scalar(), q.x(), q.y(), q.z(),
                                accel, 0, 0, gyro, 0, 0));
            };
            for (int i = 0; i < 160; ++i) push(1.0f, 5.0f, 0.9f);        // address ~0.8 s
            for (int i = 1; i <= 60; ++i)                                 // downswing ramp
                push(1.0f + 1.5f * i / 60.f, 800.f * i / 60.f, 0.9f);
            peakT = clk.t + 5000;                                         // strike peak
            push(14.0f, 800.0f, 0.8f);
            push(2.0f, 400.0f, 0.8f);
            push(1.5f, 200.0f, 0.8f);
            for (int i = 0; i < 100; ++i) push(1.0f, 5.0f, 0.9f);        // follow-through
        });
        producer.join();

        CHECK("strike fires exactly once across threads", impacts.load() == 1);
        if (impacts.load() == 1)
            CHECK_NEAR("impact back-dated by bleLatencyUs",
                       double(estUs.load()),
                       double(peakT - wk.impactConfig().bleLatencyUs), 1.0);
    }

    // ===== 6. Attach/detach/teardown churn =====
    std::printf("== teardown churn ==\n");
    {
        pinpoint::EventBuffer buf;
        bool survived = true;
        for (int cycle = 0; cycle < 25 && survived; ++cycle) {
            const pinpoint::SourceId sid =
                registerImuSource(buf, "churn");   // same identifier — same device
            if (cycle == 0)
                buf.start();                       // Idle -> Capturing
            else if (!buf.isCapturing())
                buf.resume();                      // re-register may auto-resume
            auto *w = new ImuIoWorker;
            w->attachBuffer(&buf, sid);
            std::atomic<bool> stop{false};
            std::thread producer([&] {
                Clock clk;
                while (!stop.load(std::memory_order_acquire))
                    w->processSample(mk(clk.step(), 1, 0, 0, 0, 0, 0, 1, 0, 0, 0));
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            w->detachBuffer();          // stop barrier
            buf.pause();
            buf.deregisterSource(sid);  // asserts paused + no producer in ring
            stop.store(true, std::memory_order_release);
            producer.join();
            delete w;
        }
        CHECK("25 attach/hammer/detach/deregister cycles survive", survived);
    }

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
