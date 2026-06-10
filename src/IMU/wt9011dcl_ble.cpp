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

#include "wt9011dcl_ble.h"
#include "imu_capabilities.h"
#include <QtMath>

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

ImuCapabilities WT9011DCL_BLE::capabilities() const
{
    ImuCapabilities caps = wt901Defaults();
    caps.modelName = QStringLiteral("WT901 Series");
    caps.transport = Transport::Ble;

    // The WT901BLE67 firmware sends 0x61 combined frames (accel+gyro+euler).
    // These carry no magnetometer data and no temperature.
    caps.hasMagnetometer = false;
    caps.hasTemperature  = false;

    // Battery level is readable via register 0x64 (0x71 response frame)
    caps.hasBattery = true;

    // BLE transport has no baud rate concept
    caps.supportsBaudRateControl = false;

    // setOutputData() (RSW register) disrupts the 0x61 combined-frame stream
    // on this firmware — must not be used
    caps.supportsOutputDataSelection = false;

    caps.queriedAt = QDateTime::currentDateTime();
    return caps;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

WT9011DCL_BLE::WT9011DCL_BLE(QObject *parent)
    : WT9011DCL_Base(parent)
    , m_transport(new BleImuTransport({"ffe5", "ffe4", "ffe9"}, this))
{
    // Forward transport signals as our own so callers need not know about BleImuTransport.
    connect(m_transport, &BleImuTransport::stateChanged,
            this,        &WT9011DCL_BLE::stateChanged);
    connect(m_transport, &BleImuTransport::deviceDiscovered,
            this,        &WT9011DCL_BLE::deviceDiscovered);
    connect(m_transport, &BleImuTransport::rawDeviceFound,
            this,        &WT9011DCL_BLE::rawDeviceFound);
    connect(m_transport, &BleImuTransport::scanFinished,
            this,        &WT9011DCL_BLE::scanFinished);

    // Forward transport-level connection events to ImuBase signals.
    connect(m_transport, &BleImuTransport::connected,
            this,        &ImuBase::connected);
    connect(m_transport, &BleImuTransport::disconnected,
            this,        &ImuBase::disconnected);
    connect(m_transport, &BleImuTransport::errorOccurred,
            this,        &ImuBase::errorOccurred);
    connect(m_transport, &BleImuTransport::diagnosticInfo,
            this,        &ImuBase::diagnosticInfo);

    // gattReady fires after CCCD descriptor is written — perform WT901-specific
    // device initialisation before the transport transitions to State::Ready.
    connect(m_transport, &BleImuTransport::gattReady,
            this,        &WT9011DCL_BLE::initializeDevice);

    // Route incoming BLE bytes to the WT901 protocol parser.
    connect(m_transport, &BleImuTransport::dataReceived,
            this, [this](const QByteArray &data) { receiveData(data); });
}

WT9011DCL_BLE::~WT9011DCL_BLE() = default;

// ---------------------------------------------------------------------------
// Transport write (WT9011DCL_Base abstract)
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::writeToDevice(const QByteArray &data)
{
    m_transport->writeToDevice(data);
}

// ---------------------------------------------------------------------------
// Device initialisation
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::setOutputRate(OutputRate rate)
{
    m_rate = rate;
    WT9011DCL_Base::setOutputRate(rate);
}

void WT9011DCL_BLE::initializeDevice()
{
    // Re-seed local orientation fusion from the current pose on every (re)connect
    // and rate change, so it never starts from a stale quaternion.
    resetOrientationFilter();

    // Vertical installation: device mounted on wrist/forearm, not lying flat.
    sendCommand(RegOrient, 0x0001);

    // 6-axis fusion: gyro + accelerometer only. No magnetometer — faster response
    // and more accurate over the short duration of a golf swing.
    sendCommand(RegAxis6, 0x0001);

    // Output rate — defaults to 100 Hz, updated by setOutputRate().
    sendCommand(RegRRate, static_cast<quint16>(m_rate));

    // Verify ORIENT and AXIS6 were accepted. Responses arrive asynchronously via
    // dispatchReadResponse() and are logged via diagnosticInfo(). This guarantees
    // every device comes up in the same known state (vertical mount + 6-axis),
    // runtime-only — we never SAVE to the device's flash.
    readRegisters(RegOrient, 0);
    readRegisters(RegAxis6, 0);

    // NOTE: CALSW zeroing (0x0008, 0x0004) is intentionally NOT done here.
    // The sensor zero is set at calibration time (arm-down pose) via
    // zeroToCurrentPose(), which is called from ImuInstance::setCalibration().
    // This ensures the reference frame is set at a known anatomical position
    // rather than at the arbitrary moment of BLE connection.
}

void WT9011DCL_BLE::zeroToCurrentPose()
{
    // Zero roll and pitch to the device's current physical orientation.
    sendCommand(RegCalSw, 0x0008);

    // Zero yaw heading to the current orientation.
    sendCommand(RegCalSw, 0x0004);

    // Return CALSW to normal working mode.
    sendCommand(RegCalSw, 0x0000);

    // Request a read of RegCalSw as a pipeline fence.  The device processes
    // BLE writes in order; when the 0x71 response arrives in
    // dispatchReadResponse() all three CALSW writes above have been delivered.
    // If the device does not respond, ImuInstance's settle timer confirms after
    // 500 ms as a fallback.
    readRegisters(RegCalSw, 0);
}

// ---------------------------------------------------------------------------
// Orientation correction
// ---------------------------------------------------------------------------

// LEGACY / UNUSED for the 0x61 combined frame. Orientation now comes from our own
// Madgwick 6-axis fusion of raw gyro+accel (orientation_filter.h), because the
// device's on-board Euler output proved non-rigid. This override is retained only
// for the old 0x55/0x53 serial packet path and is not on the WT901BLE67 hot path.
// See docs/reference/IMU_AXIS_REFERENCE.md.
//
// (Historical note — axis mapping it attempted: Roll→X, Yaw→Y, Pitch→Z, pitch
// negated; gated on |pitch| as the ZYX middle-angle singularity.)
std::optional<WT9011DCL_BLE::QuaternionData>
WT9011DCL_BLE::eulerToQuat(const EulerAngles &e) const
{
    if (std::abs(e.pitch) >= kGimbalLockThresholdDeg)
        return std::nullopt;
    const float hx = qDegreesToRadians( e.roll)  * 0.5f;  // R → X
    const float hy = qDegreesToRadians( e.yaw)   * 0.5f;  // Y → Y
    const float hz = qDegreesToRadians(-e.pitch) * 0.5f;  // P → Z, negated
    const float cx = qCos(hx), sx = qSin(hx);
    const float cy = qCos(hy), sy = qSin(hy);
    const float cz = qCos(hz), sz = qSin(hz);
    return QuaternionData{ cx*cy*cz + sx*sy*sz,
                           sx*cy*cz - cx*sy*sz,
                           cx*sy*cz + sx*cy*sz,
                           cx*cy*sz - sx*sy*cz };
}
