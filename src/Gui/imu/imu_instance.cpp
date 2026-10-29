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

#include "imu_instance.h"

#include "pp_debug.h"
#include "ble_adapter_pool.h"
#include "event_buffer.h"
#include "imu_calibration.h"
#include "imu_io_worker.h"
#include "imu_sample.h"
#include "imu_sample_tap.h"
#include "source_descriptor.h"
#include "trunk_angles.h"
#include "trunk_stand_tall.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QtMath>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

// Dev raw recording (PINPOINT_IMU_RECORD_DIR): the hand-off between the I/O-thread
// data lambda (producer) and the GUI-thread flush timer (consumer). `active` is the
// lock-free fast path; it is re-checked under the mutex so a sample can never land in
// `pending` after stopRawRecording() drained it.
struct ImuRawRecordQueue
{
    std::atomic<bool>                      active { false };
    QMutex                                 mutex;
    std::vector<ImuIoWorker::RawSample>    pending;   // guarded by mutex
};

// The last kCapacity samples as the EventBuffer would store them (makeImuSample: raw sensor
// frame, the same values a recorded lane carries, so the live and offline stand-tall solves read
// the same numbers), stamped with the same host-arrival time. Producer: the I/O-thread data
// lambda; consumer: calibrateTrunk() on the GUI thread, which copies it out oldest-first.
struct ImuStandTallRing
{
    // ≥ the 2 s window at the fastest selectable rate (200 Hz), with room for the arrival
    // bursts at its edges. 16 KB + 5 KB per sensor.
    static constexpr size_t kCapacity = 640;

    QMutex                            mutex;
    std::vector<qint64>               tUs  = std::vector<qint64>(kCapacity, 0);   // guarded by mutex
    std::vector<pinpoint::ImuSample>  rows = std::vector<pinpoint::ImuSample>(kCapacity);
    size_t                            next  = 0;   // slot the next sample goes in
    size_t                            count = 0;   // valid samples, ≤ kCapacity

    void push(const ImuIoWorker::RawSample &in)
    {
        QMutexLocker lock(&mutex);
        tUs[next]  = in.nowUs;
        rows[next] = pinpoint::makeImuSample(in.ax, in.ay, in.az, in.gx, in.gy, in.gz,
                                             in.qw, in.qx, in.qy, in.qz);
        next = (next + 1) % kCapacity;
        if (count < kCapacity) ++count;
    }

    void copyOut(std::vector<int64_t> &t, std::vector<pinpoint::ImuSample> &r)
    {
        QMutexLocker lock(&mutex);
        t.clear(); r.clear();
        t.reserve(count); r.reserve(count);
        const size_t first = (next + kCapacity - count) % kCapacity;
        for (size_t k = 0; k < count; ++k) {
            const size_t i = (first + k) % kCapacity;
            t.push_back(int64_t(tUs[i]));
            r.push_back(rows[i]);
        }
    }
};

namespace {

// The IMU lane's format descriptor, spelled ONCE so registration and the
// post-negotiation correction cannot drift apart. rateHz means two different
// things at the two call sites — a ring-sizing ceiling at registerSource(), the
// negotiated nominal cadence afterwards — which is why it is a parameter rather
// than baked in. device_serial is deliberately left empty: registerSource()
// normalises it to the identifier, and updateSourceFormat() preserves the
// registered one when a re-stamp carries none (event_buffer.cpp:279-283).
pinpoint::FormatDescriptor imuFormatDescriptor(uint32_t rateHz)
{
    pinpoint::ImuFormat fmt{};
    fmt.device         = pinpoint::DeviceKind::IMU_WitMotion;
    fmt.sample_rate_hz = rateHz;
    // One decoded ImuSample per quaternion update — 40 bytes, quaternion-only rotation.
    fmt.packet_bytes   = sizeof(pinpoint::ImuSample);
    fmt.packet_schema  = "imu_sample_v2";   // raw sensor-frame vectors (see imu_sample.h)

    pinpoint::FormatDescriptor fd;
    fd.device = pinpoint::DeviceKind::IMU_WitMotion;
    fd.format = fmt;
    return fd;
}

// PINPOINT_IMU_RECORD_DIR, read once (the switch is "set at app start"). Empty = off.
const QString &rawRecordDir()
{
    static const QString dir = qEnvironmentVariable("PINPOINT_IMU_RECORD_DIR");
    return dir;
}

} // namespace

ImuInstance::ImuInstance(const Device &device,
                         pinpoint::EventBuffer *buffer,
                         QThread *ioThread,
                         QObject *parent)
    : ImuDeviceBase(parent)
    , m_eventBuffer(buffer)
    , m_ioThread(ioThread)
    , m_imu(new WT9011DCL_BLE)        // no parent — lives on the I/O thread
    , m_worker(new ImuIoWorker)       // no parent — lives on the I/O thread
    , m_device(device)
    , m_deviceId(device.id)
    , m_deviceDescription(device.description)
    , m_standTallRing(std::make_shared<ImuStandTallRing>())
    , m_ceremonyTap(std::make_shared<pinpoint::ImuSampleTap>())
{
    // Host the driver and the hot-path worker on the shared IMU I/O thread:
    // the QLowEnergyController is created inside connectToDevice(), which runs
    // there (invokeMethod in start()), so its affinity is correct from birth.
    if (m_ioThread) {
        m_imu->moveToThread(m_ioThread);
        m_worker->moveToThread(m_ioThread);
    }

    if (m_eventBuffer) {
        pinpoint::SourceDescriptor desc;
        desc.name       = device.description.toStdString();
        desc.identifier = (device.imuCapabilities.serialNumber.isEmpty()
                           ? device.id
                           : device.imuCapabilities.serialNumber).toStdString();

        // Provisional: the device has not connected yet, so its rate is unknown.
        // Register at the ceiling so the ring is big enough for any of them, then
        // correct to the negotiated cadence in setOutputRateHz(). See
        // kRingSizingRateHz for why the two numbers are different jobs.
        desc.format                   = imuFormatDescriptor(kRingSizingRateHz);
        desc.window_duration          = std::chrono::milliseconds(5000);
        // ⚠ DELIBERATELY NOT kRingSizingRateHz'S PERIOD, and the two must not be
        // "fixed" to match. This feeds ONLY the stall watchdog, which wants the SLOW
        // end (a device configured for 10 Hz is healthy at 100 ms between samples);
        // the sizing rate above wants the fast end. Each number is right for its own
        // job — same split hm_instance.h documents for the wG3.
        desc.expected_interarrival_us = std::chrono::microseconds(10000); // 100 Hz
        desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;

        m_imuSourceId = m_eventBuffer->registerSource(desc);
    }

    // Impact detector latency is owned here, not in the math header (P4
    // replaces the constant with auto-calibration). Pre-stream configuration
    // (the worker's detector is producer-thread state once packets flow).
    m_worker->impactConfig().bleLatencyUs = kImuBleLatencyUs;
    if (m_eventBuffer && m_imuSourceId != pinpoint::kInvalidSourceId)
        m_worker->attachBuffer(m_eventBuffer, m_imuSourceId);

    // Dev raw recording: the queue exists only when PINPOINT_IMU_RECORD_DIR is set,
    // so with it unset the lambda below carries a null pointer and does nothing more.
    if (!rawRecordDir().isEmpty()) {
        m_rawRecorder = std::make_shared<ImuRawRecordQueue>();
        m_rawRecordTimer.setInterval(500);   // the format promises a flush at least once a second
        m_rawRecordTimer.setSingleShot(false);
        connect(&m_rawRecordTimer, &QTimer::timeout, this, [this]() { flushRawRecording(); });
    }

    // Data path, entirely on the I/O thread: one DirectConnection from the
    // driver's combined-packet signal into the worker's hot path. accel/gyro/
    // euler accessors are same-thread reads (dispatchCombinedPacket sets them
    // before emitting quaternionUpdated).
    connect(m_imu, &WT9011DCL_Base::quaternionUpdated, m_worker,
            [imu = m_imu, worker = m_worker, rec = m_rawRecorder, ring = m_standTallRing,
             tap = m_ceremonyTap](const WT9011DCL_Base::QuaternionData &q) {
                const auto a = imu->accelData();
                const auto g = imu->gyroData();
                const auto e = imu->eulerData();
                ImuIoWorker::RawSample in;
                in.nowUs = static_cast<qint64>(pinpoint::EventBuffer::nowMicros());
                in.qw = q.w; in.qx = q.x; in.qy = q.y; in.qz = q.z;
                in.ax = a.x; in.ay = a.y; in.az = a.z;
                in.gx = g.x; in.gy = g.y; in.gz = g.z;
                in.eulerRoll = e.roll; in.eulerPitch = e.pitch; in.eulerYaw = e.yaw;
                worker->processSample(in);
                ring->push(in);   // calibrateTrunk()'s window (a short mutex, no allocation)
                // The trunk ceremony recorder (W2.1c): the same stamp and sample, while it records.
                if (tap->isActive())
                    tap->push(in.nowUs, pinpoint::makeImuSample(in.ax, in.ay, in.az, in.gx, in.gy, in.gz,
                                                                in.qw, in.qx, in.qy, in.qz));
                // Dev raw recording: the SAME stamp the EventBuffer gets, queued for the
                // GUI-thread flush — never a disk write on this thread.
                if (rec && rec->active.load(std::memory_order_acquire)) {
                    QMutexLocker lock(&rec->mutex);
                    if (rec->active.load(std::memory_order_relaxed))
                        rec->pending.push_back(in);
                }
            },
            Qt::DirectConnection);

    // Impact trigger: worker (I/O thread) -> this signal (queued to GUI).
    connect(m_worker, &ImuIoWorker::impactDetected, this,
            [this](qint64 estUs, double conf) {
                emit impactDetected(estUs, float(conf));
            });

    // Soft confirmation — fires 500 ms after zeroToCurrentPose() is sent.
    // 500 ms = 50 BLE frames at 100 Hz; enough for the device to process CALSW writes.
    // Used as a fallback when the device does not respond to the RegCalSw fence read.
    m_zeroSettleTimer.setSingleShot(true);
    connect(&m_zeroSettleTimer, &QTimer::timeout, this, [this]() {
        if (!m_zeroing) return;  // hardware ACK already fired
        m_zeroConfirmTimer.stop();
        m_zeroing = false;
        emit zeroingChanged();
        emit zeroingConfirmed();
        const QString msg = timestamp() + "  Zeroing confirmed (settle timer — no hardware ACK received).";
        ppWarn() << "[ImuInstance]" << m_deviceDescription << "— zeroing confirmed via settle timer (device did not ACK RegCalSw read)";
        appendLog(msg);
    });

    m_zeroConfirmTimer.setSingleShot(true);
    connect(&m_zeroConfirmTimer, &QTimer::timeout, this, [this]() {
        if (!m_zeroing) return;
        m_zeroSettleTimer.stop();
        ppError() << "[ImuInstance]" << m_deviceDescription << "— zero confirmation timed out after 30 s";
        appendLog(timestamp() + "  ERROR: zeroing timed out after 30 s — tap Recalibrate.");
        m_zeroing = false;
        emit zeroingChanged();
        emit zeroingFailed();
    });

    // Hardware confirmation — fires when the device responds to the RegCalSw fence read.
    // Takes precedence over the settle timer; cancels it so settle does not double-fire.
    connect(m_imu, &ImuBase::zeroingConfirmed, this, [this]() {
        if (!m_zeroing) return;
        m_zeroSettleTimer.stop();
        m_zeroConfirmTimer.stop();
        m_zeroing = false;
        emit zeroingChanged();
        emit zeroingConfirmed();
        const QString msg = timestamp() + "  Zeroing confirmed (hardware ACK — RegCalSw readback received).";
        ppWarn() << "[ImuInstance]" << m_deviceDescription << "— zeroing confirmed via hardware ACK";
        appendLog(msg);
    });

    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        appendLog(timestamp() + QStringLiteral("  Auto-retrying connection…"));
        start();
    });

    m_logTimer.setSingleShot(false);
    connect(&m_logTimer, &QTimer::timeout, this, [this]() {
        const QString bat = m_batteryPercent >= 0
            ? QString("  BAT=%1%").arg(m_batteryPercent)
            : QString();
        appendLog(timestamp()
            + QString("  Data: %1 records total  (+%2 in last 10s)  %3 Hz avg%4")
                .arg(m_totalRecords)
                .arg(m_recordsSinceLog)
                .arg(m_dataRateHz, 0, 'f', 1)
                .arg(bat));
        m_recordsSinceLog = 0;
    });

    m_batteryTimer.setInterval(60'000);
    m_batteryTimer.setSingleShot(false);
    connect(&m_batteryTimer, &QTimer::timeout, this, [this]() {
        if (m_connected)
            QMetaObject::invokeMethod(m_imu, &WT9011DCL_BLE::requestBattery,
                                      Qt::QueuedConnection);
    });

    m_gimbalPollTimer.setInterval(kGimbalPollIntervalMs);
    m_gimbalPollTimer.setSingleShot(false);
    connect(&m_gimbalPollTimer, &QTimer::timeout, this, [this]() {
        const int driverCount = m_imu->gimbalDropCount();
        if (driverCount != m_gimbalDropCount) {
            m_gimbalDropCount = driverCount;
            emit gimbalDropCountChanged();
        }
    });

    // 60 Hz display tick — the ONLY emitter of the high-rate change signals
    // (quat/euler, accel, angular velocity, gyro peak, long axis, data rate). The hot path lives on
    // the I/O thread; this copies its latest LiveSample into the QML-facing
    // members (display-frame accel remap happens here: sensor X→X, Z→Y, −Y→Z)
    // and emits. Consumers that poll between ticks (calibration flow) read
    // these members — at worst one tick (16 ms) stale, well inside the 100 ms
    // poll cadence.
    m_displayTimer.setInterval(16);
    m_displayTimer.setSingleShot(false);
    connect(&m_displayTimer, &QTimer::timeout, this, [this]() {
        const ImuIoWorker::LiveSample snap = m_worker->snapshot();
        if (snap.seq == m_lastSeq)
            return;
        m_recordsSinceLog += snap.seq - m_lastSeq;
        m_lastSeq          = snap.seq;
        m_totalRecords     = snap.seq - m_seqBase;

        m_quatW = snap.quatW; m_quatX = snap.quatX;
        m_quatY = snap.quatY; m_quatZ = snap.quatZ;
        m_anatQuat   = snap.anatQuat;
        m_eulerRoll  = snap.eulerRoll;
        m_eulerPitch = snap.eulerPitch;
        m_eulerYaw   = snap.eulerYaw;
        m_accelX =  snap.accelX;
        m_accelY =  snap.accelZ;
        m_accelZ = -snap.accelY;
        m_angularVelocityDps = snap.angularVelocityDps;
        m_dataRateHz         = snap.dataRateHz;
        m_gyroDps            = snap.gyroPeakDps;
        {   // Sensor +Y (the long axis, away from the USB end) from up, raw frame — solveTrunkSegment's longAxisFromUpDeg.
            const float aLen = std::sqrt(snap.accelX * snap.accelX + snap.accelY * snap.accelY
                                         + snap.accelZ * snap.accelZ);
            m_longAxisFromUpDeg = aLen > 1e-6f
                ? qRadiansToDegrees(std::acos(qBound(-1.0f, snap.accelY / aLen, 1.0f)))
                : 180.0f;
        }

        emit quatChanged();
        emit accelChanged();
        if (qAbs(m_angularVelocityDps - m_lastSentVelDps) > 0.5f) {
            m_lastSentVelDps = m_angularVelocityDps;
            emit angularVelocityDpsChanged();
        }
        if (qAbs(m_gyroDps - m_lastSentGyroDps) > 0.1f) {
            m_lastSentGyroDps = m_gyroDps;
            emit gyroDpsChanged();
        }
        if (qAbs(m_longAxisFromUpDeg - m_lastSentLongAxisDeg) > 0.5f) {
            m_lastSentLongAxisDeg = m_longAxisFromUpDeg;
            emit longAxisFromUpDegChanged();
        }
        if (qAbs(m_dataRateHz - m_lastSentRateHz) > 0.1) {
            m_lastSentRateHz = m_dataRateHz;
            emit dataRateHzChanged();
        }
    });
    m_displayTimer.start();

    connect(m_imu, &WT9011DCL_BLE::stateChanged,
            this,  &ImuInstance::onStateChanged);

    connect(m_imu, &WT9011DCL_Base::connected, this, [this]() {
        appendLog(timestamp() + "  BLE link up — notifications enabled");
    });

    connect(m_imu, &WT9011DCL_Base::errorOccurred, this, [this](const QString &msg) {
        appendLog(timestamp() + "  ERROR: " + msg);
    });

    connect(m_imu, &WT9011DCL_Base::diagnosticInfo, this, [this](const QString &msg) {
        appendLog(timestamp() + "  [diag] " + msg);
    });

    connect(m_imu, &WT9011DCL_Base::batteryUpdated, this, [this](int percent) {
        if (percent == m_batteryPercent) return;
        m_batteryPercent = percent;
        emit batteryPercentChanged();
        appendLog(timestamp() + QString("  Battery: %1%").arg(percent));
    });

    connect(m_imu, &WT9011DCL_Base::batteryReadRetry, this, [this]() {
        if (m_batteryRetries >= kMaxBatteryRetries) return;
        ++m_batteryRetries;
        QTimer::singleShot(5000, this, [this]() {
            if (m_connected)
                QMetaObject::invokeMethod(m_imu, &WT9011DCL_BLE::requestBattery,
                                          Qt::QueuedConnection);
        });
    });

    connect(m_imu, &WT9011DCL_Base::magUpdated,
            this, [this](const WT9011DCL_Base::MagData &d) {
        appendLog(timestamp()
            + QString("  Mag     x=%1  y=%2  z=%3  T=%4°C")
                .arg(d.x, 8, 'f', 1)
                .arg(d.y, 8, 'f', 1)
                .arg(d.z, 8, 'f', 1)
                .arg(d.temperature, 5, 'f', 1));
    });

}

double ImuInstance::trunkHeadingDeg() const
{
    if (!m_anatCalibrated) return std::numeric_limits<double>::quiet_NaN();
    return trunk_angles::liveHeadingDeg(m_anatQuat);
}

ImuInstance::~ImuInstance()
{
    m_zeroSettleTimer.stop();
    m_zeroConfirmTimer.stop();
    m_retryTimer.stop();
    m_logTimer.stop();
    m_batteryTimer.stop();
    m_gimbalPollTimer.stop();
    stopRawRecording();

    // Defensive: ImuManager always calls stop() (the producer barrier) before
    // destroying an instance; detachBuffer() is idempotent if it didn't.
    if (m_worker)
        m_worker->detachBuffer();

    // The driver and worker live on the I/O thread — destroy them there. The
    // thread outlives the instances (ImuManager joins it after deleting them),
    // so these deferred deletes always run.
    if (m_imu)    m_imu->deleteLater();
    if (m_worker) m_worker->deleteLater();
}

void ImuInstance::start()
{
    if (m_attemptingConn) return;

    // A fresh attempt supersedes any pending backoff retry (this is also the
    // entry point the retry timer calls — stop() on an already-fired single-shot
    // is a harmless no-op).
    m_retryTimer.stop();

    // Look up fresh from the enumerator at connection time — the same path the
    // old ImuManager::connectToEnumeratedDevice() used.
    QBluetoothDeviceInfo deviceInfo;
    for (const Device &dev : DeviceEnumerator::instance()->devices(DeviceType::Imu)) {
        if (dev.id == m_deviceId) {
            deviceInfo = dev.platformHandle.value<QBluetoothDeviceInfo>();
            break;
        }
    }

    if (!deviceInfo.isValid()) {
        appendLog(timestamp() + QStringLiteral("  ERROR: device not found in enumerator: ") + m_deviceId);
        setStateLabel(QStringLiteral("Not found"));
        return;
    }

    m_attemptingConn = true;
    m_connecting     = true;
    appendLog(timestamp() + QStringLiteral("  >>> Connecting to: ")
              + m_deviceDescription + QStringLiteral(" [") + m_deviceId + QStringLiteral("]"));

    // connectToDevice runs ON the I/O thread so the QLowEnergyController (and
    // the WinRT watchers behind it) are created with the right affinity.
#ifdef Q_OS_LINUX
    // On Linux, assign adapters round-robin across connections so multiple IMUs
    // can stream simultaneously without contending for the same HCI adapter.
    // nextAdapter() returns null when only one adapter is present, which falls
    // back to the default-adapter path inside BleImuTransport.
    const QBluetoothAddress adapter = BleAdapterPool::instance()->nextAdapter();
    if (!adapter.isNull())
        appendLog(timestamp() + QStringLiteral("  BT adapter: ") + adapter.toString());
    QMetaObject::invokeMethod(m_imu, [imu = m_imu, deviceInfo, adapter]() {
        imu->connectToDevice(deviceInfo, adapter);
    }, Qt::QueuedConnection);
#else
    QMetaObject::invokeMethod(m_imu, [imu = m_imu, deviceInfo]() {
        imu->connectToDevice(deviceInfo);
    }, Qt::QueuedConnection);
#endif

    if (!m_busy) { m_busy = true; emit busyChanged(); }
}

void ImuInstance::stop()
{
    m_retryTimer.stop();
    m_retryCount     = 0;
    m_connecting     = false;
    m_attemptingConn = false;

    // Producer stop barrier, ON the I/O thread (blocking): sever the
    // driver→worker data path and drop the BLE link; when this returns no new
    // sample can enter processSample(). Then detachBuffer() waits out any
    // write already in flight. After both, the caller may pause the buffer
    // and deregister the source (EventBuffer producer contract).
    QMetaObject::invokeMethod(m_imu, [imu = m_imu, worker = m_worker]() {
        QObject::disconnect(imu, &WT9011DCL_Base::quaternionUpdated, worker, nullptr);
        imu->disconnectFromDevice();
    }, Qt::BlockingQueuedConnection);
    m_worker->detachBuffer();
    stopRawRecording();   // after the barrier, so the drain catches every queued sample
}

void ImuInstance::deregisterFromBuffer()
{
    if (m_eventBuffer && m_imuSourceId != pinpoint::kInvalidSourceId) {
        m_eventBuffer->deregisterSource(m_imuSourceId);
        m_imuSourceId = pinpoint::kInvalidSourceId;
    }
}

void ImuInstance::setOrientationFilter(OrientationFilterType type)
{
    if (m_imu)
        QMetaObject::invokeMethod(m_imu, [imu = m_imu, type]() {
            imu->setOrientationFilter(type);
        }, Qt::QueuedConnection);
}

void ImuInstance::setImpactSensitivity(float thresholdScale)
{
    // The detector is producer-thread state — mutate it on the I/O thread so
    // a mid-stream sensitivity change never races processSample().
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, thresholdScale]() {
        worker->impactConfig().thresholdScale = thresholdScale;
    }, Qt::QueuedConnection);
}

void ImuInstance::beginZeroing()
{
    if (!m_connected || !m_imu || m_zeroing) return;
    const QString msg = timestamp() + "  Zeroing sequence started — sending CALSW commands + RegCalSw fence read.";
    ppWarn() << "[ImuInstance]" << m_deviceDescription << "— zeroing started";
    appendLog(msg);
    m_zeroing = true;
    emit zeroingChanged();
    QMetaObject::invokeMethod(m_imu, &WT9011DCL_Base::zeroToCurrentPose,
                              Qt::QueuedConnection);   // CALSW × 3 + RegCalSw fence
    m_zeroSettleTimer.start(500);         // fallback: confirm after 500 ms if no hardware ACK
    m_zeroConfirmTimer.start(30'000);     // absolute 30 s deadline
}

void ImuInstance::zeroOrientation()
{
    if (!m_connected) return;
    appendLog(timestamp() + "  Zeroing orientation…");
    QMetaObject::invokeMethod(m_imu, [imu = m_imu]() {
        imu->reinitialize();
        imu->zeroToCurrentPose();   // reinitialize() no longer zeros; explicit
    }, Qt::QueuedConnection);

    // Reinitialising changes the device configuration. If calibration was
    // previously set, it is now invalid — the user must recalibrate.
    if (m_calibrated) {
        appendLog(timestamp() + "  Calibration cleared — recalibration required.");
        clearCalibration();
    }
}

QString ImuInstance::saveLog()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString fileName = QStringLiteral("imu_log_%1_%2.txt")
        .arg(QString(m_deviceId).replace(QStringLiteral(":"), QStringLiteral("")))
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = dir + QDir::separator() + fileName;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("ERROR: could not write to %1").arg(path);

    QTextStream out(&f);
    for (const QString &line : std::as_const(m_logEntries))
        out << line << '\n';

    appendLog(timestamp() + QStringLiteral("  Log saved to ") + path);
    return path;
}

// Dev raw recording (PINPOINT_IMU_RECORD_DIR; trunk IMU design §16 step 2). One file
// per connection, named <dir>/imu_<serial-or-address>_<yyyyMMdd-HHmmss>.csv, in the
// pinpoint.imu_record/1 format another tool parses — keep the header lines and the
// column order exactly as written here.
void ImuInstance::startRawRecording()
{
    if (!m_rawRecorder) return;
    stopRawRecording();   // a Ready without the loss that should have closed the last one

    const QString dirPath = rawRecordDir();
    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath)) {
        ppWarn() << "[ImuInstance]" << m_deviceDescription
                 << "— IMU raw recording: cannot create" << dirPath;
        return;
    }

    const QString serial = m_device.imuCapabilities.serialNumber;
    QString tag = serial.isEmpty() ? m_deviceId : serial;
    tag.remove(QLatin1Char(':'));
    tag.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path = dir.filePath(QStringLiteral("imu_%1_%2.csv").arg(tag, stamp));
    // A reconnect inside the same second must not clobber the file it just closed.
    for (int n = 2; QFile::exists(path); ++n)
        path = dir.filePath(QStringLiteral("imu_%1_%2_%3.csv").arg(tag, stamp).arg(n));

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        ppWarn() << "[ImuInstance]" << m_deviceDescription
                 << "— IMU raw recording: cannot open" << path << file->errorString();
        return;
    }

    // The placement role, from the owning ImuManager (Q_INVOKABLE roleForDevice) — looked
    // up by name so this class takes no dependency on the manager; "" when unassigned or
    // when the parent is not a manager (tests).
    QString role;
    if (QObject *mgr = parent()) {
        if (mgr->metaObject()->indexOfMethod("roleForDevice(QString)") >= 0)
            QMetaObject::invokeMethod(mgr, "roleForDevice", Qt::DirectConnection,
                                      Q_RETURN_ARG(QString, role), Q_ARG(QString, m_deviceId));
    }

    const QByteArray header =
        QByteArrayLiteral("# pinpoint.imu_record/1\n")
        + QStringLiteral("# serial=%1 name=%2 role=%3 configuredRateHz=%4 startedUtc=%5\n")
              .arg(serial, m_deviceDescription, role)
              .arg(m_outputRateHz)
              .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
              .toUtf8()
        + QByteArrayLiteral("t_us,ax,ay,az,gx,gy,gz,qw,qx,qy,qz\n");
    file->write(header);
    file->flush();

    m_rawRecordFile    = std::move(file);
    m_rawRecordPath    = path;
    m_rawRecordSamples = 0;
    {
        QMutexLocker lock(&m_rawRecorder->mutex);
        m_rawRecorder->pending.clear();
        m_rawRecorder->active.store(true, std::memory_order_release);
    }
    m_rawRecordTimer.start();
    ppInfo() << "[ImuInstance]" << m_deviceDescription
             << "— IMU raw recording started:" << m_rawRecordPath;
}

void ImuInstance::flushRawRecording()
{
    if (!m_rawRecorder || !m_rawRecordFile) return;
    std::vector<ImuIoWorker::RawSample> batch;
    {
        QMutexLocker lock(&m_rawRecorder->mutex);
        batch.swap(m_rawRecorder->pending);
    }
    if (batch.empty()) return;

    QByteArray out;
    out.reserve(int(batch.size()) * 128);
    char line[320];
    for (const ImuIoWorker::RawSample &r : batch) {
        const int n = std::snprintf(line, sizeof line,
            "%lld,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n",
            static_cast<long long>(r.nowUs),
            double(r.ax), double(r.ay), double(r.az),
            double(r.gx), double(r.gy), double(r.gz),
            double(r.qw), double(r.qx), double(r.qy), double(r.qz));
        if (n > 0) out.append(line, qMin(n, int(sizeof line) - 1));
    }
    m_rawRecordFile->write(out);
    m_rawRecordFile->flush();
    m_rawRecordSamples += batch.size();
}

void ImuInstance::stopRawRecording()
{
    if (!m_rawRecorder || !m_rawRecordFile) return;
    m_rawRecordTimer.stop();
    {
        QMutexLocker lock(&m_rawRecorder->mutex);
        m_rawRecorder->active.store(false, std::memory_order_release);
    }
    flushRawRecording();   // the tail queued before `active` went false
    m_rawRecordFile->close();
    ppInfo() << "[ImuInstance]" << m_deviceDescription
             << "— IMU raw recording stopped:" << m_rawRecordPath
             << "samples" << m_rawRecordSamples;
    m_rawRecordFile.reset();
    m_rawRecordPath.clear();
    m_rawRecordSamples = 0;
}

void ImuInstance::setCalibration(const QQuaternion &armDown, const QQuaternion &tPose)
{
    m_calibArmDown  = armDown;
    m_calibArmTPose = tPose;

    // The sensor reports quaternions relative to an arbitrary power-on origin,
    // not an absolute world frame. Use the arm-down pose as the session reference:
    //
    //   q_cal = conjugate(q_arm_down)
    //
    // At runtime:  q_relative = q_cal * q_raw = conjugate(q_arm_down) * q_raw
    //
    // When the arm is in arm-down position q_raw ≈ q_arm_down, so q_relative ≈
    // identity — the model arm hangs straight down regardless of power-on offset.
    m_calibTransform = armDown.conjugated();
    m_calibTransform.normalize();

    // Validation: compute the relative rotation between arm-down and T-pose and
    // check that it is approximately 90° of rotation. This catches cases where
    // the calibration sequence was performed incorrectly (e.g. arm not raised
    // to horizontal, or poses captured in the wrong order).
    //
    // angle = 2 * acos(|dot(q_arm_down, q_t_pose)|)
    const float dot      = qBound(-1.0f, qAbs(QQuaternion::dotProduct(armDown, tPose)), 1.0f);
    const float angleDeg = qRadiansToDegrees(2.0f * qAcos(dot));
    m_calibrationAngleValid = (angleDeg >= 60.0f && angleDeg <= 120.0f);
    if (!m_calibrationAngleValid) {
        qWarning() << "ImuInstance::setCalibration: arm-down→T-pose angle is"
                   << angleDeg << "° — expected ~90°. Calibration may be inaccurate.";
    }

    m_calibrated = true;
    emit calibratedChanged();
}

void ImuInstance::clearCalibration()
{
    m_calibrated            = false;
    m_calibrationAngleValid = true;
    m_calibArmDown   = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_calibArmTPose  = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_calibTransform = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    emit calibratedChanged();
}

void ImuInstance::setFunctionalCalibration(const QQuaternion &refRaw,
                                           const QVector3D   &gravityDownSensor,
                                           const QVector3D   &longAxisSensor,
                                           const QVector3D   &flexAxisSensor,
                                           bool               handMount)
{
    const imu_calibration::Alignment a =
        imu_calibration::solveSegment(refRaw, gravityDownSensor, longAxisSensor, flexAxisSensor);

    m_alignA         = a.A;
    m_mountM         = a.M;
    m_anatCalibrated = true;
    m_calibratedAtUtc = QDateTime::currentDateTimeUtc();
    clearTrunkProvenance();   // A/M are no longer the trunk solve's

    // Mis-mount check: angle between the solved mounting and the expected nominal.
    // δM = M_solved * conj(M_nominal); rotation angle = 2*acos(|w|).
    const QQuaternion nominal = handMount ? imu_calibration::nominalHandMount()
                                          : imu_calibration::nominalArmMount();
    const QQuaternion dM = (a.M * nominal.conjugated()).normalized();
    m_mountDeviationDeg = qRadiansToDegrees(2.0 * std::acos(qBound(0.0, std::abs((double)dM.scalar()), 1.0)));

    ppWarn() << "[ImuInstance]" << m_deviceDescription
             << "— functional calibration: axis angle" << a.axisAngleDeg
             << "deg, mount deviation" << m_mountDeviationDeg << "deg, valid" << a.valid;
    appendLog(timestamp() + QStringLiteral("  Precise calibration set (axis angle %1°, mount Δ %2°%3).")
                                .arg(a.axisAngleDeg, 0, 'f', 1)
                                .arg(m_mountDeviationDeg, 0, 'f', 1)
                                .arg(m_mountDeviationDeg > 15.0 ? QStringLiteral(" — CHECK SEATING")
                                   : (!a.valid ? QStringLiteral(" — axis POOR") : QString())));

    m_worker->setAnatomical(m_alignA, m_mountM, m_anatCalibrated);
    emit anatCalibratedChanged();
    emit quatChanged();   // refresh anatQuat consumers
}

void ImuInstance::setNominalCalibration(const QQuaternion &refRaw, bool handMount)
{
    // M is the known strap-enforced mounting; A places the reference pose at
    // identity so anatQuat = A * q_raw * M is identity at arm-down. The hand sensor
    // seats 90 deg about the long axis differently from forearm/upper-arm.
    m_mountM = handMount ? imu_calibration::nominalHandMount()
                         : imu_calibration::nominalArmMount();
    m_alignA = (refRaw * m_mountM).conjugated().normalized();
    m_anatCalibrated    = true;
    m_calibratedAtUtc   = QDateTime::currentDateTimeUtc();
    clearTrunkProvenance();   // A/M are no longer the trunk solve's
    m_mountDeviationDeg = 0.0;   // nominal = no fine-tune applied

    // Gravity-direction (flip) check: express the current arm-down accelerometer
    // reading in the anatomical body frame (M^-1 * accel). At arm-down it must be
    // ~ anatomical "up" (0,-1,0); a flipped / upside-down mount points it elsewhere
    // (phi/mount-deviation is blind to this). Caller treats a large value as a
    // mount FAIL (re-seat). Assumes the sensor is held at the arm-down pose.
    {
        const ImuIoWorker::LiveSample snap = m_worker->snapshot();
        QVector3D acc(snap.accelX, snap.accelY, snap.accelZ);
        if (acc.lengthSquared() > 1e-6f) {
            const QVector3D gBody = m_mountM.conjugated().rotatedVector(acc.normalized());
            const float dot = qBound(-1.0f, QVector3D::dotProduct(gBody, QVector3D(0.0f, -1.0f, 0.0f)), 1.0f);
            m_mountGravityErrorDeg = qRadiansToDegrees(std::acos(dot));
        }
    }

    ppWarn() << "[ImuInstance]" << m_deviceDescription << "— nominal (quick) calibration set; gravity error"
             << m_mountGravityErrorDeg << "deg";
    appendLog(timestamp() + QStringLiteral("  Nominal calibration set (mandated mount, arm-down reference); gravity Δ %1°%2.")
                                .arg(m_mountGravityErrorDeg, 0, 'f', 1)
                                .arg(m_mountGravityErrorDeg > 25.0 ? QStringLiteral(" — MOUNT FLIPPED?") : QString()));

    m_worker->setAnatomical(m_alignA, m_mountM, m_anatCalibrated);
    emit anatCalibratedChanged();
    emit quatChanged();
}

void ImuInstance::refineMountAboutLongAxis(const QQuaternion &refRaw, double phiDeg, bool handMount)
{
    // M' = M_nominal * Ry(phi)  (Ry about the anatomical long axis +Y); then A'
    // re-anchors so anatQuat(refRaw) stays identity. Derived as a similarity
    // transform: a mounting error about the long axis shows up as the abducted
    // pose's rotation axis being rotated about Y; phi nulls it.
    const QQuaternion nominal = handMount ? imu_calibration::nominalHandMount()
                                          : imu_calibration::nominalArmMount();

    // Safety guard: a genuine strap-slop correction is small. A large |phi| means
    // the second pose didn't isolate a clean long-axis rotation for this segment
    // (e.g. the user's arm wasn't rigid), so trust the validated nominal instead
    // of applying a corrupting correction.
    if (std::abs(phiDeg) > 25.0) {
        m_mountM = nominal;
        m_alignA = (refRaw * m_mountM).conjugated().normalized();
        m_anatCalibrated    = true;
        m_calibratedAtUtc   = QDateTime::currentDateTimeUtc();
        clearTrunkProvenance();   // A/M are no longer the trunk solve's
        m_mountDeviationDeg = std::abs(phiDeg);
        ppWarn() << "[ImuInstance]" << m_deviceDescription
                 << "— precise refine REJECTED (phi" << phiDeg << "deg too large); kept nominal";
        appendLog(timestamp() + QStringLiteral("  Precise refine rejected (Δ %1° too large) — kept nominal.")
                                    .arg(phiDeg, 0, 'f', 1));
        m_worker->setAnatomical(m_alignA, m_mountM, m_anatCalibrated);
        emit anatCalibratedChanged();
        emit quatChanged();
        return;
    }

    const float h = static_cast<float>(qDegreesToRadians(phiDeg) * 0.5);
    const QQuaternion Ry(std::cos(h), 0.0f, std::sin(h), 0.0f);

    m_mountM = (nominal * Ry).normalized();
    m_alignA = (refRaw * m_mountM).conjugated().normalized();
    m_anatCalibrated   = true;
    m_calibratedAtUtc  = QDateTime::currentDateTimeUtc();
    clearTrunkProvenance();   // A/M are no longer the trunk solve's
    m_mountDeviationDeg = std::abs(phiDeg);

    ppWarn() << "[ImuInstance]" << m_deviceDescription
             << "— precise refine: long-axis correction" << phiDeg << "deg";
    appendLog(timestamp() + QStringLiteral("  Precise refine: long-axis Δ %1°%2.")
                                .arg(phiDeg, 0, 'f', 1)
                                .arg(std::abs(phiDeg) > 15.0 ? QStringLiteral(" — CHECK SEATING") : QString()));

    m_worker->setAnatomical(m_alignA, m_mountM, m_anatCalibrated);
    emit anatCalibratedChanged();
    emit quatChanged();
}

void ImuInstance::clearFunctionalCalibration()
{
    m_anatCalibrated  = false;
    m_calibratedAtUtc = QDateTime();
    clearTrunkProvenance();   // A/M are no longer the trunk solve's
    m_alignA = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_mountM = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_worker->setAnatomical(m_alignA, m_mountM, false);
    emit anatCalibratedChanged();
    emit quatChanged();
}

void ImuInstance::clearTrunkProvenance()
{
    m_trunkProv   = pinpoint::TrunkCalibProvenance();
    m_trunkRecord.clear();
}

QVariantMap ImuInstance::calibrateTrunk(const QString &role)
{
    // Trunk design §6.3 step 1 / §16.2 W2.1a: the live stand-tall solve. The window summary is
    // trunk_stand_tall::summarise — the same arithmetic swinglab --trunk-calib-from replays.
    const QString r = role.trimmed().toLower();
    imu_calibration::TrunkSensor sensor;
    if (r == QLatin1String("pelvis"))
        sensor = imu_calibration::TrunkSensor::Pelvis;
    else if (r == QLatin1String("thorax"))
        sensor = imu_calibration::TrunkSensor::Thorax;
    else {
        ppWarn() << "[ImuInstance]" << m_deviceDescription << "— calibrateTrunk: unknown role"
                 << role << "(pelvis | thorax) — nothing solved";
        return QVariantMap{ { QStringLiteral("valid"), false }, { QStringLiteral("applied"), false },
                            { QStringLiteral("role"), role }, { QStringLiteral("deviceId"), m_deviceId },
                            { QStringLiteral("failure"),
                              QStringLiteral("Unknown trunk role \"%1\" (pelvis or thorax)").arg(role) } };
    }

    std::vector<int64_t>            tUs;
    std::vector<pinpoint::ImuSample> rows;
    m_standTallRing->copyOut(tUs, rows);
    const int64_t tEndUs = int64_t(pinpoint::EventBuffer::nowMicros());
    const trunk_stand_tall::LiveSolve s = trunk_stand_tall::solveLatest(tUs, rows, tEndUs, m_outputRateHz, sensor);
    const imu_calibration::TrunkAlignment &al = s.alignment;
    const imu_calibration::TrunkSolveRecord &rec = al.record;

    QVariantMap out = trunk_stand_tall::toVariantMap(s);
    out.insert(QStringLiteral("role"),     r);
    out.insert(QStringLiteral("deviceId"), m_deviceId);
    out.insert(QStringLiteral("applied"),  al.valid);
    out.insert(QStringLiteral("serial"),   laneSerial());
    out.insert(QStringLiteral("windowEndUs"), qint64(tEndUs));   // the ceremony replay ends its window here

    const QString line = QStringLiteral(
        "— trunk stand-tall (%1): %2%3 — samples %4/%5 (%6) gyro %7 dps gravity %8 % long axis %9 deg "
        "face %10 deg, %11 sigma %12 deg")
        .arg(r, al.valid ? QStringLiteral("VALID") : QStringLiteral("INVALID"),
             rec.failure.isEmpty() ? QString() : QStringLiteral(" (") + rec.failure + QStringLiteral(")"))
        .arg(s.slice.w.samplesReceived).arg(s.slice.w.samplesExpected)
        .arg(double(rec.sampleFraction), 0, 'f', 3).arg(double(rec.maxAbsGyroDps), 0, 'f', 2)
        .arg(double(rec.gravityErrPct), 0, 'f', 2).arg(double(rec.longAxisFromUpDeg), 0, 'f', 1)
        .arg(double(rec.faceFromHorizontalDeg), 0, 'f', 1).arg(rec.deltaTier)
        .arg(double(rec.sigmaDeltaDeg), 0, 'f', 1);

    if (!al.valid) {
        // Never applied: the instance keeps whatever calibration it had (see the header).
        ppWarn() << "[ImuInstance]" << m_deviceDescription << qPrintable(line + QStringLiteral(" — NOT applied"));
        appendLog(timestamp() + QStringLiteral("  Trunk stand-tall (%1) failed: %2").arg(r, rec.failure));
        return out;
    }

    m_alignA          = al.A;
    m_mountM          = al.M;
    m_anatCalibrated  = true;
    m_calibratedAtUtc = QDateTime::currentDateTimeUtc();
    // The arm routines' two mount measures do not apply to a trunk sensor (its own mount gates
    // passed, or the solve would not be valid); 0 = not measured, as before any calibration.
    m_mountDeviationDeg    = 0.0;
    m_mountGravityErrorDeg = 0.0;
    m_trunkProv = trunk_stand_tall::provenanceFrom(al);
    out.insert(QStringLiteral("calibratedAt"), m_calibratedAtUtc.toString(Qt::ISODateWithMs));
    m_trunkRecord = out;

    // The legacy two-pose flag too, so `calibrated` reads true like every other calibrated
    // sensor. Its arm-down/T-pose quaternions mean nothing for a trunk sensor: identity, and the
    // transform with them — anatCalibrated takes precedence wherever both are read (ArmVizView).
    m_calibrated            = true;
    m_calibrationAngleValid = true;
    m_calibArmDown   = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_calibArmTPose  = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_calibTransform = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);

    ppInfo() << "[ImuInstance]" << m_deviceDescription << qPrintable(line + QStringLiteral(" — applied"));
    appendLog(timestamp() + QStringLiteral("  Trunk stand-tall (%1) applied (long axis %2° from up, face %3°).")
                                .arg(r).arg(double(rec.longAxisFromUpDeg), 0, 'f', 1)
                                .arg(double(rec.faceFromHorizontalDeg), 0, 'f', 1));

    m_worker->setAnatomical(m_alignA, m_mountM, m_anatCalibrated);
    emit calibratedChanged();
    emit anatCalibratedChanged();
    emit quatChanged();
    return out;
}

void ImuInstance::setOutputRateHz(int hz)
{
    using R = WT9011DCL_Base::OutputRate;
    R rate;
    switch (hz) {
    case 10:  rate = R::Hz_10;  break;
    case 20:  rate = R::Hz_20;  break;
    case 50:  rate = R::Hz_50;  break;
    case 200: rate = R::Hz_200; break;
    default:  rate = R::Hz_100; hz = 100; break;
    }
    m_outputRateHz = hz;
    emit outputRateHzChanged();

    // ⚠ THE BUFFER HAS TO BE TOLD TOO, or the offline re-fusion readers integrate
    // at a rate the live fusion never used. registerSource() could only record a
    // provisional ceiling (kRingSizingRateHz) because the rate is chosen here, on
    // connect; setNominalSampleRateHz() below keeps the LIVE filter's dt right,
    // and this keeps the recorded descriptor's in lockstep with it. Without the
    // pair, a 200 Hz device fuses at 1/200 live while imu_refusion_check.h and
    // imu_vision_fuser.cpp re-fuse at the stale rate — and orientation_refuser.h
    // is explicit that a dt mismatch fails parity for reasons unrelated to the
    // data under test. Metadata only, no ring resize (event_buffer.h:148-150), and
    // synchronous on the caller's thread exactly as CameraInstance stamps its
    // negotiated resolution; every caller of this is GUI-thread.
    if (m_eventBuffer && m_imuSourceId != pinpoint::kInvalidSourceId)
        m_eventBuffer->updateSourceFormat(m_imuSourceId, imuFormatDescriptor(uint32_t(hz)));

    // Always sync the driver's internal rate register so initializeDevice() uses
    // the right value from the first connection. writeToDevice() is a no-op when
    // disconnected so this is safe to call at any time. Driver state — I/O thread.
    QMetaObject::invokeMethod(m_imu, [imu = m_imu, rate, hz, connected = m_connected]() {
        imu->setOutputRate(rate);
        imu->setNominalSampleRateHz(hz);   // keep fusion's integration dt in lockstep
        if (connected)
            imu->reinitialize();
    }, Qt::QueuedConnection);
}

void ImuInstance::resetStreamingState()
{
    m_logTimer.stop();
    m_batteryTimer.stop();
    m_gimbalPollTimer.stop();
    if (m_dataRateHz != 0.0)    { m_dataRateHz = 0.0; emit dataRateHzChanged(); }
    if (m_batteryPercent != -1) { m_batteryPercent = -1; emit batteryPercentChanged(); }
}

int ImuInstance::retryDelayMs(int attempt) const
{
    // Exponential backoff from kRetryBaseDelayMs (attempt 1 = base), capped at
    // kRetryMaxDelayMs: 2s, 4s, 8s, 16s, 30s(cap)…
    const long long d = static_cast<long long>(kRetryBaseDelayMs) << (attempt - 1);
    return static_cast<int>(qMin<long long>(d, kRetryMaxDelayMs));
}

void ImuInstance::handleConnectFailure()
{
    // Exactly one call per failed connect attempt (the first of BlueZ's
    // error+disconnect pair consumed m_connecting in onConnectionLost).
    if (m_retryCount < kMaxRetries) {
        ++m_retryCount;
        const int delayMs = retryDelayMs(m_retryCount);
        setStateLabel(QStringLiteral("Retrying…"));
        if (!m_busy) { m_busy = true; emit busyChanged(); }   // still working
        appendLog(timestamp()
                  + QStringLiteral("  Connection failed — auto-retry %1/%2 in %3 s")
                    .arg(m_retryCount).arg(kMaxRetries).arg((delayMs + 999) / 1000));
        m_retryTimer.start(delayMs);
    } else {
        m_retryCount = 0;
        setStateLabel(QStringLiteral("Error"));
        if (m_busy) { m_busy = false; emit busyChanged(); }
        appendLog(timestamp()
                  + QStringLiteral("  Connection failed — all %1 retries exhausted."
                                   " Check device is powered on and within range.")
                    .arg(kMaxRetries));
    }
}

void ImuInstance::onConnectionLost(bool fromError)
{
    m_attemptingConn = false;
    resetStreamingState();
    stopRawRecording();   // the next Ready opens a new file
    if (m_connected) { m_connected = false; emit imuConnectedChanged(); }

    if (m_connecting) {
        // An unresolved connect attempt failed. BlueZ pairs errorOccurred() and
        // disconnected() in EITHER order; whichever lands first consumes
        // m_connecting and owns the retry decision, so it is taken exactly once
        // (the old code keyed off m_inConnectPhase, which both handlers cleared —
        // a Disconnected-before-Error ordering silently dropped the retry).
        m_connecting = false;
        handleConnectFailure();
    } else if (m_retryTimer.isActive()) {
        // The paired signal already scheduled a retry — this is the second half
        // of the pair. Leave the retry, the "Retrying…" label and busy intact.
        const int remainSec = (m_retryTimer.remainingTime() + 999) / 1000;
        appendLog(timestamp()
                  + QStringLiteral("  BLE %1 (post-failure cleanup) — retry %2/%3 still due in ~%4 s")
                    .arg(fromError ? QStringLiteral("error") : QStringLiteral("disconnected"))
                    .arg(m_retryCount).arg(kMaxRetries).arg(remainSec));
    } else {
        // A genuine disconnect/loss outside any connect attempt (user disconnect,
        // mid-stream drop, or retries already exhausted).
        m_retryCount = 0;
        if (m_busy) { m_busy = false; emit busyChanged(); }
        setStateLabel(fromError ? QStringLiteral("Error") : QStringLiteral("Disconnected"));
    }
}

void ImuInstance::onStateChanged(WT9011DCL_BLE::State s)
{
    switch (s) {
    case WT9011DCL_BLE::State::Disconnected:
        onConnectionLost(/*fromError=*/false);
        break;

    case WT9011DCL_BLE::State::Scanning:
        setStateLabel(QStringLiteral("Scanning…"));
        if (!m_busy) { m_busy = true; emit busyChanged(); }
        appendLog(timestamp() + "  Scanning for motion sensors…");
        break;

    case WT9011DCL_BLE::State::Connecting:
        setStateLabel(QStringLiteral("Connecting…"));
        appendLog(timestamp() + "  Connecting…");
        break;

    case WT9011DCL_BLE::State::DiscoveringServices:
        setStateLabel(QStringLiteral("Discovering services…"));
        appendLog(timestamp() + "  Discovering BLE services…");
        break;

    case WT9011DCL_BLE::State::Ready:
        setStateLabel(QStringLiteral("Connected"));
        m_attemptingConn = false;
        m_connecting     = false;
        m_retryCount     = 0;
        m_connected = true;
        m_busy      = false;
        emit imuConnectedChanged();
        emit busyChanged();
        m_totalRecords    = 0;
        m_recordsSinceLog = 0;
        m_batteryRetries  = 0;
        m_seqBase         = m_lastSeq;   // per-connection record totals
        m_imu->resetGimbalDropCount();   // atomic — safe cross-thread
        if (m_gimbalDropCount != 0) { m_gimbalDropCount = 0; emit gimbalDropCountChanged(); }
        m_logTimer.start(kLogIntervalMs);
        m_gimbalPollTimer.start();
        appendLog(timestamp() + "  Motion sensor ready — receiving data");
        // Poke the device to start streaming.
        setOutputRateHz(m_outputRateHz);
        startRawRecording();   // dev-only (PINPOINT_IMU_RECORD_DIR); no-op when unset
        QTimer::singleShot(1500, this, [this]() {
            if (m_connected) {
                appendLog(timestamp() + "  Requesting battery level…");
                QMetaObject::invokeMethod(m_imu, &WT9011DCL_BLE::requestBattery,
                                          Qt::QueuedConnection);
            }
        });
        m_batteryTimer.start();
        break;

    case WT9011DCL_BLE::State::Error:
        // A connect-phase failure (m_connecting) or a mid-stream controller error
        // (m_connected) is a connection loss — route through the shared path so
        // the retry decision stays order-independent vs the paired disconnected().
        // A stray Error with neither set (and no retry pending) just shows Error.
        if (m_connecting || m_connected) {
            onConnectionLost(/*fromError=*/true);
        } else if (!m_retryTimer.isActive()) {
            m_attemptingConn = false;
            if (m_busy) { m_busy = false; emit busyChanged(); }
            setStateLabel(QStringLiteral("Error"));
        }
        break;
    }
}

QString ImuInstance::timestamp()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const qint64 us_total = duration_cast<microseconds>(now.time_since_epoch()).count();
    const qint64 secs = us_total / 1'000'000;
    const int    frac  = static_cast<int>(us_total % 1'000'000);
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(secs);
    return dt.toString(QStringLiteral("HH:mm:ss"))
           + QLatin1Char('.')
           + QString::number(frac).rightJustified(6, QLatin1Char('0'));
}

void ImuInstance::appendLog(const QString &text)
{
    m_logEntries.append(text);
    emit logEntryAdded(text);
}

void ImuInstance::setStateLabel(const QString &s)
{
    if (m_stateLabel == s) return;
    m_stateLabel = s;
    emit stateLabelChanged();
}
