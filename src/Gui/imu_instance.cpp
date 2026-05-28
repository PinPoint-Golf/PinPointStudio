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

#include "ble_adapter_pool.h"
#include "event_buffer.h"
#include "imu_sample.h"
#include "source_descriptor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>
#include <chrono>
#include <cstring>

ImuInstance::ImuInstance(const Device &device,
                         pinpoint::EventBuffer *buffer,
                         QObject *parent)
    : QObject(parent)
    , m_eventBuffer(buffer)
    , m_imu(new WT9011DCL_BLE(this))
    , m_device(device)
    , m_deviceId(device.id)
    , m_deviceDescription(device.description)
{
    if (m_eventBuffer) {
        pinpoint::SourceDescriptor desc;
        desc.name       = device.description.toStdString();
        desc.identifier = (device.imuCapabilities.serialNumber.isEmpty()
                           ? device.id
                           : device.imuCapabilities.serialNumber).toStdString();

        pinpoint::ImuFormat fmt{};
        fmt.device         = pinpoint::DeviceKind::IMU_WitMotion;
        fmt.sample_rate_hz = 100;
        // One decoded ImuSample per quaternion update — 40 bytes, quaternion-only rotation.
        fmt.packet_bytes   = sizeof(pinpoint::ImuSample);
        fmt.packet_schema  = "imu_sample_v1";

        desc.format.device            = pinpoint::DeviceKind::IMU_WitMotion;
        desc.format.format            = fmt;
        desc.window_duration          = std::chrono::milliseconds(5000);
        desc.expected_interarrival_us = std::chrono::microseconds(10000); // 100 Hz
        desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;

        m_imuSourceId = m_eventBuffer->registerSource(desc);
    }

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
        if (m_connected) m_imu->requestBattery();
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

    connect(m_imu, &WT9011DCL_Base::accelUpdated,
            this, [this](const WT9011DCL_Base::AccelData &d) {
        // Remap from sensor frame to display/world frame:
        //   sensor X (Roll axis)  → display X  (unchanged)
        //   sensor Z (Yaw axis)   → display Y
        //   sensor Y (Pitch axis) → display Z, negated
        m_accelX =  d.x;
        m_accelY =  d.z;
        m_accelZ = -d.y;
        emit accelChanged();
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
            if (m_connected) m_imu->requestBattery();
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

    // Display update — queued to main thread, drives QML bindings.
    connect(m_imu, &WT9011DCL_Base::quaternionUpdated,
            this, [this](const WT9011DCL_Base::QuaternionData &q) {
        m_quatW = q.w; m_quatX = q.x; m_quatY = q.y; m_quatZ = q.z;
        emit quatChanged();

        // Stability detection: compute angular velocity from successive quaternions.
        // QElapsedTimer is not started until the first packet, so skip the first tick.
        const qint64 dtMs = m_prevQuatTimer.isValid() ? m_prevQuatTimer.elapsed() : -1;
        m_prevQuatTimer.restart();

        if (dtMs >= 5 && dtMs < 500) {
            const QQuaternion cur(q.w, q.x, q.y, q.z);
            // |dot| of two unit quaternions = cos(half-angle). abs() for double-cover.
            const float dot      = qBound(0.0f, qAbs(QQuaternion::dotProduct(m_prevQuat, cur)), 1.0f);
            const float angleDeg = qRadiansToDegrees(2.0f * qAcos(dot));
            const float velDps   = angleDeg / (dtMs * 0.001f);
            m_prevQuat = cur;

            if (qAbs(velDps - m_angularVelocityDps) > 0.5f) {
                m_angularVelocityDps = velDps;
                emit angularVelocityDpsChanged();
            }

            if (velDps > kStableThresholdDps) {
                m_stableTimerRunning = false;
                if (m_stable) { m_stable = false; emit stableChanged(); }
            } else {
                if (!m_stableTimerRunning) { m_stableTimer.restart(); m_stableTimerRunning = true; }
                if (!m_stable && m_stableTimer.elapsed() >= kStableDurationMs) {
                    m_stable = true;
                    emit stableChanged();
                }
            }
        } else {
            m_prevQuat = QQuaternion(q.w, q.x, q.y, q.z);
        }

        onDataRecord();
    });

    // EventBuffer write — DirectConnection so the write happens on the BLE
    // notification thread. Safe because stop() disconnects quaternionUpdated
    // before deregisterFromBuffer(), severing the producer before ring memory
    // is freed (same guarantee as the old rawPacketReady pattern).
    // accelData()/gyroData() are current: dispatchCombinedPacket sets them
    // before emitting quaternionUpdated.
    if (m_eventBuffer && m_imuSourceId != pinpoint::kInvalidSourceId) {
        connect(m_imu, &WT9011DCL_Base::quaternionUpdated,
                this, [this](const WT9011DCL_Base::QuaternionData &q) {
                    if (!m_eventBuffer->isCapturing()) return;
                    auto slot = m_eventBuffer->acquireWriteSlot(m_imuSourceId);
                    if (!slot.valid || slot.capacity < sizeof(pinpoint::ImuSample)) return;
                    const auto a = m_imu->accelData();
                    const auto g = m_imu->gyroData();
                    pinpoint::ImuSample s;
                    // Same display-frame remap as the accelUpdated handler:
                    //   sensor X → display X, sensor Z → display Y, -sensor Y → display Z
                    s.accel_x =  a.x; s.accel_y =  a.z; s.accel_z = -a.y;
                    s.gyro_x  =  g.x; s.gyro_y  =  g.z; s.gyro_z  = -g.y;
                    s.quat_w  =  q.w; s.quat_x  =  q.x;
                    s.quat_y  =  q.y; s.quat_z  =  q.z;
                    std::memcpy(slot.data, &s, sizeof(pinpoint::ImuSample));
                    *slot.bytes_written = static_cast<uint32_t>(sizeof(pinpoint::ImuSample));
                    *slot.timestamp_us  = static_cast<int64_t>(pinpoint::EventBuffer::nowMicros());
                    m_eventBuffer->publish(m_imuSourceId, slot.sequence);
                }, Qt::DirectConnection);
    }
}

ImuInstance::~ImuInstance()
{
    m_retryTimer.stop();
    m_logTimer.stop();
    m_batteryTimer.stop();
    m_gimbalPollTimer.stop();
}

void ImuInstance::start()
{
    if (m_attemptingConn) return;

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
    m_inConnectPhase = true;
    appendLog(timestamp() + QStringLiteral("  >>> Connecting to: ")
              + m_deviceDescription + QStringLiteral(" [") + m_deviceId + QStringLiteral("]"));

#ifdef Q_OS_LINUX
    // On Linux, assign adapters round-robin across connections so multiple IMUs
    // can stream simultaneously without contending for the same HCI adapter.
    // nextAdapter() returns null when only one adapter is present, which falls
    // back to the default-adapter path inside BleImuTransport.
    const QBluetoothAddress adapter = BleAdapterPool::instance()->nextAdapter();
    if (!adapter.isNull())
        appendLog(timestamp() + QStringLiteral("  BT adapter: ") + adapter.toString());
    m_imu->connectToDevice(deviceInfo, adapter);
#else
    m_imu->connectToDevice(deviceInfo);
#endif

    if (!m_busy) { m_busy = true; emit busyChanged(); }
}

void ImuInstance::stop()
{
    m_retryTimer.stop();
    m_retryCount     = 0;
    m_inConnectPhase = false;
    m_attemptingConn = false;
    disconnect(m_imu, &WT9011DCL_Base::quaternionUpdated, this, nullptr);
    m_imu->disconnectFromDevice();
}

void ImuInstance::deregisterFromBuffer()
{
    if (m_eventBuffer && m_imuSourceId != pinpoint::kInvalidSourceId) {
        m_eventBuffer->deregisterSource(m_imuSourceId);
        m_imuSourceId = pinpoint::kInvalidSourceId;
    }
}

void ImuInstance::zeroOrientation()
{
    if (!m_connected) return;
    appendLog(timestamp() + "  Zeroing orientation…");
    m_imu->reinitialize();
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

void ImuInstance::setCalibration(const QQuaternion &armDown,
                                 const QQuaternion &tPose,
                                 bool rightHanded)
{
    m_calibArmDown  = armDown;
    m_calibArmTPose = tPose;

    // Anatomical target for the arm-down pose depends on which arm is the lead arm.
    // Left arm  (right-handed golfer): 180° about world Z → (w=0, x=0, y=0, z=1)
    // Right arm (left-handed golfer):  180° about world X → (w=0, x=1, y=0, z=0)
    const QQuaternion target = rightHanded
        ? QQuaternion(0.0f, 0.0f, 0.0f, 1.0f)
        : QQuaternion(0.0f, 1.0f, 0.0f, 0.0f);

    // q_cal maps raw sensor space to anatomical world frame.
    // At runtime: q_segment = q_cal * q_raw
    m_calibTransform = armDown.conjugated() * target;
    m_calibTransform.normalize();

    m_calibrated = true;
    emit calibratedChanged();
}

void ImuInstance::clearCalibration()
{
    m_calibrated     = false;
    m_calibArmDown   = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_calibArmTPose  = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_calibTransform = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    emit calibratedChanged();
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
    // Always sync the driver's internal rate register so initializeDevice() uses
    // the right value from the first connection. writeToDevice() is a no-op when
    // disconnected so this is safe to call at any time.
    m_imu->setOutputRate(rate);
    if (m_connected)
        m_imu->reinitialize();
}

void ImuInstance::onStateChanged(WT9011DCL_BLE::State s)
{
    switch (s) {
    case WT9011DCL_BLE::State::Disconnected:
        m_attemptingConn = false;
        m_inConnectPhase = false;
        m_logTimer.stop();
        m_batteryTimer.stop();
        m_gimbalPollTimer.stop();
        m_packetTimes.clear();
        if (m_dataRateHz != 0.0)    { m_dataRateHz = 0.0; emit dataRateHzChanged(); }
        if (m_batteryPercent != -1) { m_batteryPercent = -1; emit batteryPercentChanged(); }
        if (m_connected || m_busy) {
            m_connected = false;
            m_busy      = false;
            emit imuConnectedChanged();
            emit busyChanged();
        }
        // On Linux/BlueZ, QLowEnergyController fires both errorOccurred and
        // disconnected when a connection fails. If the retry timer is already
        // running we must NOT reset m_retryCount here — doing so would undo the
        // kMaxRetries accounting and turn a one-shot retry into an infinite loop.
        if (m_retryTimer.isActive()) {
            const int remainSec = (m_retryTimer.remainingTime() + 999) / 1000;
            appendLog(timestamp()
                      + QStringLiteral("  BLE disconnected (post-error cleanup) — "
                                       "retry %1/%2 still due in ~%3 s")
                        .arg(m_retryCount).arg(kMaxRetries).arg(remainSec));
        } else {
            m_retryCount = 0;
            setStateLabel(QStringLiteral("Disconnected"));
        }
        break;

    case WT9011DCL_BLE::State::Scanning:
        setStateLabel(QStringLiteral("Scanning…"));
        if (!m_busy) { m_busy = true; emit busyChanged(); }
        appendLog(timestamp() + "  Scanning for IMU devices…");
        break;

    case WT9011DCL_BLE::State::Connecting:
        m_inConnectPhase = true;
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
        m_inConnectPhase = false;
        m_retryCount     = 0;
        m_connected = true;
        m_busy      = false;
        emit imuConnectedChanged();
        emit busyChanged();
        m_totalRecords    = 0;
        m_recordsSinceLog = 0;
        m_batteryRetries  = 0;
        m_packetTimes.clear();
        m_imu->resetGimbalDropCount();
        if (m_gimbalDropCount != 0) { m_gimbalDropCount = 0; emit gimbalDropCountChanged(); }
        m_logTimer.start(kLogIntervalMs);
        m_gimbalPollTimer.start();
        appendLog(timestamp() + "  IMU ready — receiving data");
        // Poke the device to start streaming.
        setOutputRateHz(m_outputRateHz);
        QTimer::singleShot(1500, this, [this]() {
            if (m_connected) {
                appendLog(timestamp() + "  Requesting battery level…");
                m_imu->requestBattery();
            }
        });
        m_batteryTimer.start();
        break;

    case WT9011DCL_BLE::State::Error:
        m_attemptingConn = false;
        if (m_inConnectPhase && m_retryCount < kMaxRetries) {
            // After a failed attempt BlueZ corrects its stored address type; retry succeeds.
            ++m_retryCount;
            m_inConnectPhase = false;
            const int delaySec = kRetryDelayMs / 1000;
            setStateLabel(QStringLiteral("Retrying…"));
            appendLog(timestamp()
                      + QStringLiteral("  Connection failed — auto-retry %1/%2 in %3 s")
                        .arg(m_retryCount).arg(kMaxRetries).arg(delaySec));
            m_retryTimer.start(kRetryDelayMs);
        } else {
            m_inConnectPhase = false;
            m_retryCount     = 0;
            setStateLabel(QStringLiteral("Error"));
            if (m_busy) { m_busy = false; emit busyChanged(); }
            appendLog(timestamp()
                      + QStringLiteral("  Connection failed — all retries exhausted."
                                       " Check device is powered on and within range."));
        }
        break;
    }
}

void ImuInstance::onDataRecord()
{
    ++m_totalRecords;
    ++m_recordsSinceLog;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_packetTimes.append(nowMs);
    const qint64 cutoff = nowMs - kRollingWindowMs;
    while (!m_packetTimes.isEmpty() && m_packetTimes.first() < cutoff)
        m_packetTimes.removeFirst();

    const qint64 windowMs = m_packetTimes.size() > 1
        ? (nowMs - m_packetTimes.first()) : 0;
    const double hz = (windowMs > 0)
        ? (m_packetTimes.size() * 1000.0 / windowMs) : 0.0;

    if (qAbs(hz - m_dataRateHz) > 0.1) {
        m_dataRateHz = hz;
        emit dataRateHzChanged();
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
