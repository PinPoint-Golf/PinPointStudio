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

#include "imu_io_worker.h"

#include <QVector3D>
#include <QtMath>
#include <cmath>
#include <cstring>

#include "imu_calibration.h"   // toAnatomical (shared A·q·M)
#include "event_buffer.h"
#include "imu_sample.h"

ImuIoWorker::ImuIoWorker(QObject *parent)
    : QObject(parent)
{
}

void ImuIoWorker::processSample(const RawSample &in)
{
    const qint64 nowUs = in.nowUs;
    const float  qw = in.qw, qx = in.qx, qy = in.qy, qz = in.qz;
    const float  ax = in.ax, ay = in.ay, az = in.az;
    const float  gxDps = in.gx, gyDps = in.gy, gzDps = in.gz;
    const QQuaternion qRaw(qw, qx, qy, qz);

    // Angular velocity from successive quaternions, for the Calibrate step's
    // stillness-gated capture. Baseline pairs are ≥5 ms apart: intra-burst
    // packets (Windows BLE batches several per connection interval) keep the
    // baseline, so the next qualifying pair measures real motion across the
    // whole burst window instead of resetting on every sub-ms arrival.
    if (m_prevSampleUs < 0) {
        m_prevQuat     = qRaw;
        m_prevSampleUs = nowUs;
    } else {
        const qint64 dtMs = (nowUs - m_prevSampleUs) / 1000;
        if (dtMs >= 500) {               // transport stall — restart the baseline
            m_prevQuat     = qRaw;
            m_prevSampleUs = nowUs;
        } else if (dtMs >= 5) {
            // |dot| of two unit quaternions = cos(half-angle); abs() for double-cover.
            const float dot = qBound(0.0f,
                                     qAbs(QQuaternion::dotProduct(m_prevQuat, qRaw)),
                                     1.0f);
            const float angleDeg = qRadiansToDegrees(2.0f * qAcos(dot));
            m_velDps       = angleDeg / (float(dtMs) * 0.001f);
            m_prevQuat     = qRaw;
            m_prevSampleUs = nowUs;
        }
    }

    // Rolling 1 s data-rate window.
    const qint64 nowMs = nowUs / 1000;
    m_packetMs.push_back(nowMs);
    while (!m_packetMs.empty() && m_packetMs.front() < nowMs - 1000)
        m_packetMs.pop_front();
    const qint64 winMs = m_packetMs.size() > 1 ? nowMs - m_packetMs.front() : 0;
    const double rateHz = winMs > 0 ? double(m_packetMs.size()) * 1000.0 / double(winMs)
                                    : 0.0;

    // IMU impact auto-trigger (shot detection P1) — raw inertial magnitudes
    // plus the fused orientation; the shaft lies along sensor +Y
    // (imu_frame_contract.md), world +Z up.
    {
        const QVector3D shaftWorld = qRaw.rotatedVector(QVector3D(0, 1, 0));
        pinpoint::ImpactSample s;
        s.accelMag     = std::sqrt(ax * ax + ay * ay + az * az);
        s.gyroMag      = std::sqrt(gxDps * gxDps + gyDps * gyDps + gzDps * gzDps);
        s.clubVertical = std::fabs(shaftWorld.z());
        s.t_us         = nowUs;
        const pinpoint::ImpactResult r = m_impact.push(s);
        if (r.impact)
            emit impactDetected(static_cast<qint64>(r.est_t_us), r.confidence);
    }

    // Locked section: snapshot publish + ring write. Holding the mutex across
    // the write is what makes detachBuffer() a true stop barrier; the hold is
    // a 40-byte memcpy + a few quaternion ops, and the EventBuffer's own hot
    // path stays lock-free (this is a producer-side lock, not a ring lock).
    QMutexLocker lk(&m_mutex);

    m_live.quatW = qw; m_live.quatX = qx; m_live.quatY = qy; m_live.quatZ = qz;
    m_live.anatQuat = m_anatCalibrated
                          ? imu_calibration::toAnatomical(m_alignA, qRaw, m_mountM)
                          : qRaw;
    m_live.accelX = ax;        m_live.accelY = ay;        m_live.accelZ = az;
    m_live.gyroX  = gxDps;     m_live.gyroY  = gyDps;     m_live.gyroZ  = gzDps;
    m_live.eulerRoll  = in.eulerRoll;
    m_live.eulerPitch = in.eulerPitch;
    m_live.eulerYaw   = in.eulerYaw;
    m_live.angularVelocityDps = m_velDps;
    m_live.dataRateHz = rateHz;
    ++m_live.seq;

    if (m_buffer && m_sourceId != pinpoint::kInvalidSourceId
        && m_buffer->isCapturing()) {
        auto slot = m_buffer->acquireWriteSlot(m_sourceId);
        if (slot.valid && slot.capacity >= sizeof(pinpoint::ImuSample)) {
            // makeImuSample owns the stored frame (imu_sample.h v2: RAW
            // sensor-frame vectors) — single source of truth with the readers.
            const pinpoint::ImuSample smp = pinpoint::makeImuSample(
                ax, ay, az, gxDps, gyDps, gzDps, qw, qx, qy, qz);
            std::memcpy(slot.data, &smp, sizeof smp);
            *slot.bytes_written = static_cast<uint32_t>(sizeof smp);
            *slot.timestamp_us  = nowUs;
            m_buffer->publish(m_sourceId, slot.sequence);
        }
    }
}

ImuIoWorker::LiveSample ImuIoWorker::snapshot() const
{
    QMutexLocker lk(&m_mutex);
    return m_live;
}

void ImuIoWorker::setAnatomical(const QQuaternion &alignA, const QQuaternion &mountM,
                                bool calibrated)
{
    QMutexLocker lk(&m_mutex);
    m_alignA         = alignA;
    m_mountM         = mountM;
    m_anatCalibrated = calibrated;
}

void ImuIoWorker::attachBuffer(pinpoint::EventBuffer *buffer, pinpoint::SourceId id)
{
    QMutexLocker lk(&m_mutex);
    m_buffer   = buffer;
    m_sourceId = id;
}

void ImuIoWorker::detachBuffer()
{
    // STOP BARRIER: acquiring the mutex waits out any in-flight ring write;
    // nulling the pointer under it means no later sample can start one. When
    // this returns the caller may pause the buffer and deregister the source
    // (EventBuffer producer contract).
    QMutexLocker lk(&m_mutex);
    m_buffer   = nullptr;
    m_sourceId = pinpoint::kInvalidSourceId;
}
