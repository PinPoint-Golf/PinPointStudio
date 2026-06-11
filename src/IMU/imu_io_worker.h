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

#pragma once

// The per-packet IMU hot path, extracted from ImuInstance so it can run on
// the dedicated IMU I/O thread (docs/implementation/IMU_IO_THREAD_IMPL.md).
// One worker per device. Consumes parsed combined packets (fused quaternion +
// raw sensor-frame accel/gyro) and does everything that must happen at packet
// rate: anatomical transform, angular-velocity estimate, data-rate window,
// impact detection, EventBuffer ring write, and the GUI-facing snapshot.
//
// Threading contract:
//  - processSample() is called by exactly ONE producer thread (the IMU I/O
//    thread live; the test's synthetic driver thread).
//  - snapshot() / setAnatomical() / attachBuffer() / detachBuffer() may be
//    called from any thread; they synchronise with the producer through one
//    internal mutex (worker-internal — the EventBuffer ring stays lock-free,
//    per the producer contract).
//  - detachBuffer() is the producer STOP BARRIER: when it returns, no write
//    to the ring is in flight and none can start — the caller may then pause
//    the buffer and deregister the source safely.
//
// No QML, no logging, no settings: pure hot-path machinery, standalone-tested
// in src/IMU/tests/imu_io_worker_test.cpp without hardware or BLE.

#include <QMutex>
#include <QObject>
#include <QQuaternion>
#include <deque>

#include "impact_detector.h"
#include "types.h"            // pinpoint::SourceId / kInvalidSourceId

namespace pinpoint { class EventBuffer; }

class ImuIoWorker : public QObject
{
    Q_OBJECT

public:
    // GUI-facing state, copied out under the lock. seq increments once per
    // processed sample so consumers (and the coherence tests) can detect
    // change and torn reads.
    struct LiveSample {
        float quatW = 1.f, quatX = 0.f, quatY = 0.f, quatZ = 0.f;  // fused, raw frame
        QQuaternion anatQuat;                  // A·q·M when calibrated, else raw
        float  accelX = 0.f, accelY = 0.f, accelZ = 0.f;   // DISPLAY frame (x, z, −y)
        float  angularVelocityDps = 0.f;
        double dataRateHz = 0.0;
        quint64 seq = 0;
    };

    explicit ImuIoWorker(QObject *parent = nullptr);

    // ── Producer-thread entry point ──────────────────────────────────────────
    // nowUs is the host-arrival steady-clock instant (EventBuffer::nowMicros()
    // domain), passed in rather than read here so tests can drive a synthetic
    // clock. accel is g, gyro is °/s, both RAW sensor frame (imu_sample.h v2);
    // the quaternion is the host-fused orientation.
    void processSample(qint64 nowUs,
                       float qw, float qx, float qy, float qz,
                       float ax, float ay, float az,
                       float gxDps, float gyDps, float gzDps);

    // ── Any-thread API ───────────────────────────────────────────────────────
    LiveSample snapshot() const;

    // Anatomical calibration used by the per-packet A·q·M transform. Swapped
    // atomically vs the producer — a sample sees either the old pair or the
    // new pair, never a mix.
    void setAnatomical(const QQuaternion &alignA, const QQuaternion &mountM,
                       bool calibrated);

    // Ring-write attachment. attachBuffer() before streaming; detachBuffer()
    // is the stop barrier described above (idempotent, never-attached safe).
    void attachBuffer(pinpoint::EventBuffer *buffer, pinpoint::SourceId id);
    void detachBuffer();

    // Impact-detector tuning (sensitivity scale, BLE latency). Configure
    // BEFORE streaming starts — the detector is producer-thread state.
    pinpoint::ImpactDetectorConfig &impactConfig() { return m_impact.config(); }

signals:
    // Emitted from the producer thread; connect queued for GUI consumers.
    void impactDetected(qint64 estTimestampUs, double confidence);

private:
    // Producer-thread-only state — touched exclusively from processSample().
    pinpoint::ImpactDetector m_impact;
    QQuaternion m_prevQuat;
    qint64      m_prevSampleUs = -1;
    float       m_velDps = 0.f;
    std::deque<qint64> m_packetMs;          // rolling 1 s rate window

    // Cross-thread state, guarded by m_mutex.
    mutable QMutex m_mutex;
    LiveSample  m_live;
    QQuaternion m_alignA, m_mountM;
    bool        m_anatCalibrated = false;
    pinpoint::EventBuffer *m_buffer   = nullptr;
    pinpoint::SourceId     m_sourceId = pinpoint::kInvalidSourceId;
};
