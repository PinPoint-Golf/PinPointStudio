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
#include "imu_sample.h"
#include "source_descriptor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>
#include <cmath>
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
        fmt.packet_schema  = "imu_sample_v2";   // raw sensor-frame vectors (see imu_sample.h)

        desc.format.device            = pinpoint::DeviceKind::IMU_WitMotion;
        desc.format.format            = fmt;
        desc.window_duration          = std::chrono::milliseconds(5000);
        desc.expected_interarrival_us = std::chrono::microseconds(10000); // 100 Hz
        desc.sync_source              = pinpoint::SyncSource::SoftwareTimestamp;

        m_imuSourceId = m_eventBuffer->registerSource(desc);
    }

    // Impact detector latency is owned here, not in the math header (P4
    // replaces the constant with auto-calibration).
    m_impactDetector.config().bleLatencyUs = kImuBleLatencyUs;

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

    // 60 Hz display tick — the ONLY emitter of the high-rate change signals
    // (quat/euler, accel, angular velocity, data rate). Packet handlers write
    // members and set the dirty flag; consumers that need live values between
    // ticks (calibration flow, impact detector) read the members directly.
    m_displayTimer.setInterval(16);
    m_displayTimer.setSingleShot(false);
    connect(&m_displayTimer, &QTimer::timeout, this, [this]() {
        if (!m_displayDirty)
            return;
        m_displayDirty = false;
        emit quatChanged();
        emit accelChanged();
        if (qAbs(m_angularVelocityDps - m_lastSentVelDps) > 0.5f) {
            m_lastSentVelDps = m_angularVelocityDps;
            emit angularVelocityDpsChanged();
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

    connect(m_imu, &WT9011DCL_Base::accelUpdated,
            this, [this](const WT9011DCL_Base::AccelData &d) {
        // Remap from sensor frame to display/world frame:
        //   sensor X (Roll axis)  → display X  (unchanged)
        //   sensor Z (Yaw axis)   → display Y
        //   sensor Y (Pitch axis) → display Z, negated
        m_accelX =  d.x;
        m_accelY =  d.z;
        m_accelZ = -d.y;
        m_displayDirty = true;   // emitted from the 60 Hz display tick
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

    connect(m_imu, &WT9011DCL_Base::eulerAnglesUpdated,
            this, [this](const WT9011DCL_Base::EulerAngles &e) {
        m_eulerRoll  = e.roll;
        m_eulerPitch = e.pitch;
        m_eulerYaw   = e.yaw;
    });

    // Display update — queued to main thread, drives QML bindings.
    connect(m_imu, &WT9011DCL_Base::quaternionUpdated,
            this, [this](const WT9011DCL_Base::QuaternionData &q) {
        m_quatW = q.w; m_quatX = q.x; m_quatY = q.y; m_quatZ = q.z;
        // Anatomical orientation for all consumers (computed before quatChanged so
        // the anatQuat property is current when bindings re-evaluate).
        if (m_anatCalibrated)
            m_anatQuat = imu_calibration::toAnatomical(m_alignA, QQuaternion(q.w, q.x, q.y, q.z), m_mountM);
        else
            m_anatQuat = QQuaternion(q.w, q.x, q.y, q.z);
        m_displayDirty = true;   // quatChanged emitted from the 60 Hz display tick

        // Raw packet stream (beginRawDump/endRawDump diagnostic; off by default).
        // m_eulerRoll/Pitch/Yaw were just set by the eulerAnglesUpdated slot, which
        // is connected before this one so fires first for the same packet.
        if (m_rawDump) {
            const auto a = m_imu->accelData();
            const auto g = m_imu->gyroData();
            m_rawDumpLines.append(
                QStringLiteral("euler=(%1,%2,%3) accel=(%4,%5,%6) gyro=(%7,%8,%9) qraw=(%10,%11,%12,%13)")
                    .arg(m_eulerRoll, 0, 'f', 4).arg(m_eulerPitch, 0, 'f', 4).arg(m_eulerYaw, 0, 'f', 4)
                    .arg(a.x, 0, 'f', 5).arg(a.y, 0, 'f', 5).arg(a.z, 0, 'f', 5)
                    .arg(g.x, 0, 'f', 4).arg(g.y, 0, 'f', 4).arg(g.z, 0, 'f', 4)
                    .arg(q.w, 0, 'f', 6).arg(q.x, 0, 'f', 6).arg(q.y, 0, 'f', 6).arg(q.z, 0, 'f', 6));
        }

        // Angular velocity from successive quaternions, for the Calibrate step's
        // stillness-gated capture. QElapsedTimer is not started until the first
        // packet, so skip the first tick.
        const qint64 dtMs = m_prevQuatTimer.isValid() ? m_prevQuatTimer.elapsed() : -1;
        m_prevQuatTimer.restart();

        if (dtMs >= 5 && dtMs < 500) {
            const QQuaternion cur(q.w, q.x, q.y, q.z);
            // |dot| of two unit quaternions = cos(half-angle). abs() for double-cover.
            const float dot      = qBound(0.0f, qAbs(QQuaternion::dotProduct(m_prevQuat, cur)), 1.0f);
            const float angleDeg = qRadiansToDegrees(2.0f * qAcos(dot));
            m_angularVelocityDps = angleDeg / (dtMs * 0.001f);   // emitted from the display tick
            m_prevQuat = cur;
        } else {
            m_prevQuat = QQuaternion(q.w, q.x, q.y, q.z);
        }

        // IMU impact auto-trigger (shot detection P1) — same-thread math on
        // the raw synchronous accel/gyro (NOT angularVelocityDps, which is
        // quaternion-derived and coarse) plus the fused quaternion. The club
        // shaft lies along the sensor long axis (+Y, IMU_FRAME_CONTRACT.md);
        // the world frame is +Z up, so |z| of the rotated axis is the
        // orientation scalar.
        {
            const auto a = m_imu->accelData();
            const auto g = m_imu->gyroData();
            const QVector3D shaftWorld = QQuaternion(q.w, q.x, q.y, q.z)
                                             .rotatedVector(QVector3D(0, 1, 0));
            pinpoint::ImpactSample sample;
            sample.accelMag     = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
            sample.gyroMag      = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
            sample.clubVertical = std::fabs(shaftWorld.z());
            sample.t_us = static_cast<int64_t>(pinpoint::EventBuffer::nowMicros());
            const pinpoint::ImpactResult r = m_impactDetector.push(sample);
            if (r.impact)
                emit impactDetected(static_cast<qint64>(r.est_t_us), r.confidence);
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
                    // makeImuSample owns the stored frame (imu_sample.h) — the single
                    // source of truth shared with the offline reader / exporter.
                    const pinpoint::ImuSample s = pinpoint::makeImuSample(
                        a.x, a.y, a.z, g.x, g.y, g.z, q.w, q.x, q.y, q.z);
                    std::memcpy(slot.data, &s, sizeof(pinpoint::ImuSample));
                    *slot.bytes_written = static_cast<uint32_t>(sizeof(pinpoint::ImuSample));
                    *slot.timestamp_us  = static_cast<int64_t>(pinpoint::EventBuffer::nowMicros());
                    m_eventBuffer->publish(m_imuSourceId, slot.sequence);
                }, Qt::DirectConnection);
    }
}

ImuInstance::~ImuInstance()
{
    m_zeroSettleTimer.stop();
    m_zeroConfirmTimer.stop();
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

void ImuInstance::setOrientationFilter(OrientationFilterType type)
{
    if (m_imu)
        m_imu->setOrientationFilter(type);
}

void ImuInstance::setImpactSensitivity(float thresholdScale)
{
    m_impactDetector.config().thresholdScale = thresholdScale;
}

void ImuInstance::beginZeroing()
{
    if (!m_connected || !m_imu || m_zeroing) return;
    const QString msg = timestamp() + "  Zeroing sequence started — sending CALSW commands + RegCalSw fence read.";
    ppWarn() << "[ImuInstance]" << m_deviceDescription << "— zeroing started";
    appendLog(msg);
    m_zeroing = true;
    emit zeroingChanged();
    m_imu->zeroToCurrentPose();           // CALSW × 3 + readRegisters(RegCalSw, 0)
    m_zeroSettleTimer.start(500);         // fallback: confirm after 500 ms if no hardware ACK
    m_zeroConfirmTimer.start(30'000);     // absolute 30 s deadline
}

void ImuInstance::zeroOrientation()
{
    if (!m_connected) return;
    appendLog(timestamp() + "  Zeroing orientation…");
    m_imu->reinitialize();
    m_imu->zeroToCurrentPose();   // reinitialize() no longer zeros; must call explicitly

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

// Diagnostic logger (retained dev tool).
// Appends one flushed line to ~/pinpointstudio_imu_diag.log. `payload` carries the
// QML-side slerp-averaged values; here we also log the driver's instantaneous
// RAW accelerometer (gravity vector, sensor hardware frame) and RAW quaternion
// (eulerToQuat output) so the offline solve can relate the two frames.
void ImuInstance::logDiag(const QString &tag, const QString &payload)
{
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                             .filePath(QStringLiteral("pinpointstudio_imu_diag.log"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        ppWarn() << "[ImuInstance] logDiag: cannot open" << path;
        return;
    }

    const auto a = m_imu ? m_imu->accelData()      : WT9011DCL_Base::AccelData{};
    const auto g = m_imu ? m_imu->gyroData()       : WT9011DCL_Base::GyroData{};
    const auto q = m_imu ? m_imu->quaternionData() : WT9011DCL_Base::QuaternionData{};

    QTextStream out(&f);
    out << timestamp()
        << "  dev=" << m_deviceId
        << "  tag=" << tag.leftJustified(10, QLatin1Char(' '))
        << "  euler=(" << QString::number(m_eulerRoll, 'f', 4) << ',' << QString::number(m_eulerPitch, 'f', 4)
                << ',' << QString::number(m_eulerYaw, 'f', 4) << ')'
        << "  qraw=("  << QString::number(q.w, 'f', 6) << ',' << QString::number(q.x, 'f', 6)
                << ',' << QString::number(q.y, 'f', 6) << ',' << QString::number(q.z, 'f', 6) << ')'
        << "  accel=(" << QString::number(a.x, 'f', 4) << ',' << QString::number(a.y, 'f', 4)
                << ',' << QString::number(a.z, 'f', 4) << ")g"
        << "  gyro=(" << QString::number(g.x, 'f', 3) << ',' << QString::number(g.y, 'f', 3)
                << ',' << QString::number(g.z, 'f', 3) << ')'
        << "  anatcal=" << (m_anatCalibrated ? '1' : '0')
        << "  anatquat=(" << QString::number(m_anatQuat.scalar(), 'f', 6) << ',' << QString::number(m_anatQuat.x(), 'f', 6)
                << ',' << QString::number(m_anatQuat.y(), 'f', 6) << ',' << QString::number(m_anatQuat.z(), 'f', 6) << ')'
        << "  " << payload
        << '\n';
    out.flush();

    appendLog(timestamp() + QStringLiteral("  [diag] ") + tag + QStringLiteral("  ") + payload);
}

// Raw packet streaming (retained dev tool) — see header.
void ImuInstance::beginRawDump(const QString &tag)
{
    m_rawDumpTag = tag;
    m_rawDumpLines.clear();
    m_rawDump = true;
}

void ImuInstance::endRawDump()
{
    m_rawDump = false;
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                             .filePath(QStringLiteral("pinpointstudio_imu_raw.log"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        ppWarn() << "[ImuInstance] endRawDump: cannot open" << path;
        return;
    }
    QTextStream out(&f);
    out << "# BEGIN " << m_rawDumpTag << "  dev=" << m_deviceId
        << "  samples=" << m_rawDumpLines.size() << '\n';
    for (const QString &line : std::as_const(m_rawDumpLines))
        out << line << '\n';
    out << "# END " << m_rawDumpTag << '\n';
    out.flush();
    m_rawDumpLines.clear();
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
    m_mountDeviationDeg = 0.0;   // nominal = no fine-tune applied

    // Gravity-direction (flip) check: express the current arm-down accelerometer
    // reading in the anatomical body frame (M^-1 * accel). At arm-down it must be
    // ~ anatomical "up" (0,-1,0); a flipped / upside-down mount points it elsewhere
    // (phi/mount-deviation is blind to this). Caller treats a large value as a
    // mount FAIL (re-seat). Assumes the sensor is held at the arm-down pose.
    if (m_imu) {
        const auto a = m_imu->accelData();
        QVector3D acc(a.x, a.y, a.z);
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

    emit anatCalibratedChanged();
    emit quatChanged();
}

void ImuInstance::logCalibHoldReset(const QString &stage, double thresholdDps)
{
    ppInfo() << "[Calib]" << stage << "hold reset —" << m_deviceDescription
             << "angular velocity" << m_angularVelocityDps << "deg/s (threshold"
             << thresholdDps << "deg/s)";
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
        m_mountDeviationDeg = std::abs(phiDeg);
        ppWarn() << "[ImuInstance]" << m_deviceDescription
                 << "— precise refine REJECTED (phi" << phiDeg << "deg too large); kept nominal";
        appendLog(timestamp() + QStringLiteral("  Precise refine rejected (Δ %1° too large) — kept nominal.")
                                    .arg(phiDeg, 0, 'f', 1));
        emit anatCalibratedChanged();
        emit quatChanged();
        return;
    }

    const float h = static_cast<float>(qDegreesToRadians(phiDeg) * 0.5);
    const QQuaternion Ry(std::cos(h), 0.0f, std::sin(h), 0.0f);

    m_mountM = (nominal * Ry).normalized();
    m_alignA = (refRaw * m_mountM).conjugated().normalized();
    m_anatCalibrated   = true;
    m_mountDeviationDeg = std::abs(phiDeg);

    ppWarn() << "[ImuInstance]" << m_deviceDescription
             << "— precise refine: long-axis correction" << phiDeg << "deg";
    appendLog(timestamp() + QStringLiteral("  Precise refine: long-axis Δ %1°%2.")
                                .arg(phiDeg, 0, 'f', 1)
                                .arg(std::abs(phiDeg) > 15.0 ? QStringLiteral(" — CHECK SEATING") : QString()));

    emit anatCalibratedChanged();
    emit quatChanged();
}

void ImuInstance::clearFunctionalCalibration()
{
    m_anatCalibrated = false;
    m_alignA = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    m_mountM = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    emit anatCalibratedChanged();
    emit quatChanged();
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

    m_dataRateHz = hz;   // emitted (delta-gated) from the 60 Hz display tick
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
