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

#include "wt9011dcl_base.h"
#include "ble_imu_transport.h"
#include "imu_capabilities.h"
#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>

// WT9011DCL driver — Bluetooth LE transport.
//
// Provides the WT901-specific layer on top of BleImuTransport: GATT UUID
// configuration, device initialisation register sequence, axis-remapping
// eulerToQuat() override, and capabilities declaration.
//
// The generic BLE connection state machine (scanning, GATT setup, Linux 6.x
// scan-before-connect fix) lives entirely in BleImuTransport.
//
// Usage:
//   WT9011DCL_BLE imu;
//   connect(&imu, &WT9011DCL_BLE::deviceDiscovered, &imu, [&](auto &info){
//       imu.connectToDevice(info);
//   });
//   connect(&imu, &WT9011DCL_BLE::eulerAnglesUpdated, this, &MyClass::onAngles);
//   imu.scan();

class WT9011DCL_BLE : public WT9011DCL_Base
{
    Q_OBJECT

public:
    // Re-export BleImuTransport::State under this class name so existing callers
    // (ImuInstance, etc.) don't need to know about BleImuTransport.
    using State = BleImuTransport::State;

    explicit WT9011DCL_BLE(QObject *parent = nullptr);
    ~WT9011DCL_BLE() override;

    Transport       transport()    const override { return Transport::Ble; }
    ImuCapabilities capabilities() const override;

    State state()   const { return m_transport->state(); }
    bool  isReady() const { return m_transport->isReady(); }

    // Scan for nearby BLE devices; durationMs=0 scans until stopScan().
    void scan(int durationMs = 10000) { m_transport->scan(durationMs); }
    void stopScan()                   { m_transport->stopScan(); }

    void connectToDevice(const QBluetoothDeviceInfo &device,
                         const QBluetoothAddress &localAdapter = QBluetoothAddress())
    { m_transport->connectToDevice(device, localAdapter); }
    void disconnectFromDevice() { m_transport->disconnectFromDevice(); }

signals:
    void stateChanged(WT9011DCL_BLE::State state);
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void rawDeviceFound(const QBluetoothDeviceInfo &device);
    void scanFinished();

public:
    void reinitialize() override { initializeDevice(); }
    void setOutputRate(OutputRate rate) override;

protected:
    void writeToDevice(const QByteArray &data) override;
    std::optional<QuaternionData> eulerToQuat(const EulerAngles &e) const override;

private:
    void initializeDevice();

    BleImuTransport *m_transport;
    OutputRate       m_rate = OutputRate::Hz_100;
};
