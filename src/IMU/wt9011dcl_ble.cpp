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
    // Vertical installation — device is mounted on a club shaft, not lying flat.
    sendCommand(RegOrient, 0x0001);

    // 6-axis algorithm (gyro integration only, no magnetometer).
    sendCommand(RegAxis6, 0x0001);

    // Set output rate — defaults to 100Hz, updated whenever setOutputRate() is called.
    sendCommand(RegRRate, static_cast<quint16>(m_rate));

    // Zero roll and pitch to the current physical orientation.
    sendCommand(RegCalSw, 0x0008);

    // Zero yaw heading.
    sendCommand(RegCalSw, 0x0004);

    // Return CALSW to normal working mode.
    sendCommand(RegCalSw, 0x0000);
}

// ---------------------------------------------------------------------------
// Orientation correction
// ---------------------------------------------------------------------------

// WT901BLE67 confirmed axis mapping: Roll→X, Yaw→Y, Pitch→Z (RYP order),
// with pitch negated. Gate on |pitch|, not |yaw|: the WT901 firmware uses
// standard ZYX (R_z(yaw)*R_y(pitch)*R_x(roll)) internally, so pitch is the
// device's middle angle and the singularity that produces garbage Euler output
// is pitch = ±90°. Yaw is free to rotate without limit during normal motion.
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
