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

#include <QObject>
#include <QByteArray>
#include <QList>

struct ImuCapabilities;

// Abstract base class for all IMU devices, transport-agnostic.
//
// Concrete subclasses implement transport-specific connection and data
// acquisition. Use ImuFactory::create() to instantiate.
//
// Class hierarchy:
//   ImuBase
//     └─ WT9011DCL_Base  (WitMotion packet protocol, transport-agnostic)
//          ├─ WT9011DCL       (UART/serial transport)
//          └─ WT9011DCL_BLE   (WT901 BLE config; delegates connection to BleImuTransport)
//
//   BleImuTransport  (generic BLE state machine — scan, GATT, Linux fix;
//                     owned by composition inside WT9011DCL_BLE)

class ImuBase : public QObject
{
    Q_OBJECT

public:
    enum class Transport { Serial, Ble };
    Q_ENUM(Transport)

    struct AccelData      { float x = 0, y = 0, z = 0, temperature = 0; };
    struct GyroData       { float x = 0, y = 0, z = 0, temperature = 0; };
    struct EulerAngles    { float roll = 0, pitch = 0, yaw = 0; };
    struct MagData        { float x = 0, y = 0, z = 0, temperature = 0; };
    struct QuaternionData { float w = 1, x = 0, y = 0, z = 0; };

    explicit ImuBase(QObject *parent = nullptr);
    ~ImuBase() override = default;

    virtual Transport transport() const = 0;

    // Returns a fully-populated description of what this device supports.
    // The result is static per device type — call once and cache.
    virtual ImuCapabilities capabilities() const = 0;

    virtual AccelData      accelData()      const = 0;
    virtual GyroData       gyroData()       const = 0;
    virtual MagData        magData()        const = 0;
    virtual QuaternionData quaternionData() const = 0;

    virtual void reinitialize()      {}
    virtual void requestBattery()    {}

    // Zero the sensor's orientation reference to the current physical pose.
    // Implementations send device-specific commands and emit zeroingConfirmed()
    // when the device acknowledges. Default no-op for devices without zeroing
    // support — ImuInstance's 30 s timeout will fire and emit zeroingFailed().
    virtual void zeroToCurrentPose() {}

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void diagnosticInfo(const QString &message);

    void accelUpdated(const ImuBase::AccelData &data);
    void gyroUpdated(const ImuBase::GyroData &data);
    void eulerAnglesUpdated(const ImuBase::EulerAngles &angles);
    void magUpdated(const ImuBase::MagData &data);
    void quaternionUpdated(const ImuBase::QuaternionData &quat);
    void batteryUpdated(int percent);
    void batteryReadRetry();
    void zeroingConfirmed(); // device confirmed zeroing was applied

    void rawPacketReady(const QByteArray &data, qint64 timestamp_us);
};
