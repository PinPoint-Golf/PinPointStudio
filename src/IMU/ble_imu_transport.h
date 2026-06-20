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
#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QTimer>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>

// Generic BLE connection state machine — device-independent.
//
// Handles scanning, GATT service discovery, characteristic setup, and the
// Linux 6.x BlueZ scan-before-connect requirement.  Does not know about any
// specific IMU protocol; data bytes arrive via dataReceived() and are written
// back via writeToDevice().
//
// UUID discovery is driven by short fragment strings (e.g. "ffe5") that are
// matched case-insensitively against the full 128-bit UUIDs reported by the
// device.  The notify and write characteristic UUIDs are derived from the same
// base as the matched service UUID, allowing for firmware variants that use a
// non-standard base suffix.
//
// Ownership: create one per BLE connection.  The owning class wires up its
// signals:
//
//   connect(transport, &BleImuTransport::gattReady,    owner, [owner]{ owner->initDevice(); });
//   connect(transport, &BleImuTransport::dataReceived, owner, [owner](const QByteArray &d){ owner->receiveData(d); });
//   transport->connectToDevice(deviceInfo);

class BleImuTransport : public QObject
{
    Q_OBJECT

public:
    // Fragment strings used to locate GATT service and derive characteristic UUIDs.
    // serviceFragment:  short hex ID embedded in the 128-bit service UUID  (e.g. "ffe5")
    // notifyFragment:   short hex ID for the notify characteristic           (e.g. "ffe4")
    // writeFragment:    short hex ID for the write characteristic            (e.g. "ffe9")
    struct UuidConfig {
        QString serviceFragment;
        QString notifyFragment;
        QString writeFragment;
    };

    enum class State {
        Disconnected,
        Scanning,
        Connecting,
        DiscoveringServices,
        Ready,
        Error,
    };
    Q_ENUM(State)

    explicit BleImuTransport(const UuidConfig &uuids, QObject *parent = nullptr);
    ~BleImuTransport() override;

    State state()   const { return m_state; }
    bool  isReady() const { return m_state == State::Ready; }

    // Scan for nearby BLE devices; durationMs=0 scans until stopScan().
    void scan(int durationMs = 10000);
    void stopScan();

    // localAdapter: the HCI adapter to use for scanning and GATT connection.
    // Pass QBluetoothAddress() (null) to use the system default (single-adapter path).
    void connectToDevice(const QBluetoothDeviceInfo &device,
                         const QBluetoothAddress &localAdapter = QBluetoothAddress());
    void disconnectFromDevice();

    // Write raw bytes to the connected device.  Safe to call only when isReady().
    void writeToDevice(const QByteArray &data);

signals:
    void stateChanged(BleImuTransport::State state);
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void rawDeviceFound(const QBluetoothDeviceInfo &device);
    void scanFinished();

    // Emitted when CCCD descriptor has been written and the device is ready to
    // receive commands.  The owning class should perform device initialisation
    // in response to this signal.
    void gattReady();

    // Emitted when a notify characteristic value arrives from the device.
    // Connect to the protocol parser's receiveData() equivalent.
    void dataReceived(const QByteArray &data);

    void diagnosticInfo(const QString &message);
    void errorOccurred(const QString &message);

    // Emitted after gattReady() + setState(Ready).  Owning class can forward
    // to ImuBase::connected() if desired.
    void connected();
    void disconnected();

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);

    void onControllerConnected();
    void onControllerDisconnected();
    void onServiceDiscovered(const QBluetoothUuid &uuid);
    void onServiceDiscoveryFinished();
    void onControllerError(QLowEnergyController::Error error);

    void onServiceStateChanged(QLowEnergyService::ServiceState newState);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                 const QByteArray &value);
    void onServiceError(QLowEnergyService::ServiceError error);

private:
    void setState(State s);
    void teardownController();
    // Single failure path for every connect/discovery error: tears the
    // controller/service down, drops any connect-phase scan, transitions to
    // Error and emits errorOccurred(). Leaves the transport in a clean state so
    // the owner's retry starts from scratch (no leaked HCI link / armed lambda).
    void failConnection(const QString &message);
    void setupService();
    void enableNotifications();
    void ensureScannerCreated();
    void doConnect();
    bool matchesPendingDevice(const QBluetoothDeviceInfo &d) const;

    static constexpr int kConnectScanMs = 10'000;
    // Watchdog covering the post-scan connect→service-discovery→CCCD-write phase.
    // BlueZ/WinRT can wedge mid-discovery (esp. the CCCD write) without ever
    // emitting errorOccurred(), leaving the UI stuck in "Connecting…"/"Discovering
    // services…" forever. On expiry we fail the attempt so the owner's retry runs.
    static constexpr int kConnectWatchdogMs = 20'000;

    UuidConfig m_uuids;
    State      m_state = State::Disconnected;

    QBluetoothDeviceDiscoveryAgent *m_scanner    = nullptr;
    QLowEnergyController           *m_controller = nullptr;
    QLowEnergyService              *m_service    = nullptr;

    QLowEnergyCharacteristic m_writeChar;
    QLowEnergyCharacteristic m_notifyChar;
    QBluetoothUuid           m_resolvedServiceUuid;
    QBluetoothUuid           m_resolvedNotifyUuid;
    QBluetoothUuid           m_resolvedWriteUuid;
    QElapsedTimer            m_connectTimer;
    QTimer                   m_connectWatchdog;   // fires if connect/discovery never reaches Ready

    QBluetoothDeviceInfo m_pendingDevice;
    QBluetoothAddress    m_localAdapter;
    bool                 m_waitingForScanConfirm = false;
};
