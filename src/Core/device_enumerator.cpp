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

#include "device_enumerator.h"
#include "ble_adapter_pool.h"
#include "wt9011dcl_base.h"
#include "pp_debug.h"
#include <QCoreApplication>

#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QTimer>

// ---------------------------------------------------------------------------
// ImuBleScanner — internal, runs in a QThread started by DeviceEnumerator.
//
// Uses the same WT901 filter as WT9011DCL_BLE::onDeviceDiscovered():
//   accept if name starts with "WT901" OR service UUID contains "ffe5".
// ---------------------------------------------------------------------------

class ImuBleScanner : public QObject
{
    Q_OBJECT
public:
    explicit ImuBleScanner(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void start()
    {
        // On Linux with multiple adapters, start one scan agent per adapter so
        // devices in range of any adapter are discovered.  On single-adapter
        // setups (or non-Linux), a single default-adapter agent is used.
        const QList<QBluetoothAddress> adapterAddrs = BleAdapterPool::instance()->adapters();
        if (adapterAddrs.size() > 1) {
            for (const QBluetoothAddress &addr : adapterAddrs)
                createAgent(addr);
        } else {
            createAgent(QBluetoothAddress()); // null → default constructor
        }
        m_pendingAgents = m_agents.size();
    }

signals:
    void deviceFound(const QBluetoothDeviceInfo &info);
    void finished();

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &device)
    {
        // Same filter logic as WT9011DCL_BLE::onDeviceDiscovered().
        const bool hasKnownName   = device.name().startsWith(
                                        QStringLiteral("WT901"), Qt::CaseInsensitive);
        const bool hasServiceUuid = device.serviceUuids().contains(
                                        QBluetoothUuid(QStringLiteral("0000ffe5-0000-1000-8000-00805f9b34fb")));
        if (hasKnownName || hasServiceUuid) {
            ppInfo() << "[IMU] BLE candidate:" << device.name()
                     << (device.address().isNull()
                             ? device.deviceUuid().toString()
                             : device.address().toString());
            emit deviceFound(device);
        }
    }

    void onScanFinished()
    {
        ppInfo() << "[IMU] BLE scan agent finished";
        if (--m_pendingAgents <= 0) {
            ppInfo() << "[IMU] All BLE scan agents finished";
            emit finished();
        }
    }

    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
    {
        auto *agent = qobject_cast<QBluetoothDeviceDiscoveryAgent *>(sender());
        ppWarn() << "[IMU] BLE scan error:" << error
                 << (agent ? agent->errorString() : QString{});
        if (--m_pendingAgents <= 0)
            emit finished();
    }

private:
    void createAgent(const QBluetoothAddress &addr)
    {
        QBluetoothDeviceDiscoveryAgent *agent =
            addr.isNull() ? new QBluetoothDeviceDiscoveryAgent(this)
                          : new QBluetoothDeviceDiscoveryAgent(addr, this);
        agent->setLowEnergyDiscoveryTimeout(kTimeoutMs);

        connect(agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
                this,  &ImuBleScanner::onDeviceDiscovered);
        connect(agent, &QBluetoothDeviceDiscoveryAgent::finished,
                this,  &ImuBleScanner::onScanFinished);
        connect(agent, &QBluetoothDeviceDiscoveryAgent::canceled,
                this,  &ImuBleScanner::onScanFinished);
        connect(agent,
                QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(
                    &QBluetoothDeviceDiscoveryAgent::errorOccurred),
                this, &ImuBleScanner::onScanError);

        ppInfo() << "[IMU] BLE scan started on"
                 << (addr.isNull() ? QStringLiteral("default adapter")
                                   : addr.toString())
                 << "(timeout" << kTimeoutMs / 1000 << "s)";
        agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
        m_agents.append(agent);
    }

    QList<QBluetoothDeviceDiscoveryAgent *> m_agents;
    int m_pendingAgents = 0;
    static constexpr int kTimeoutMs = 30'000;
};

// ---------------------------------------------------------------------------
// DeviceEnumerator
// ---------------------------------------------------------------------------

DeviceEnumerator* DeviceEnumerator::instance()
{
    static DeviceEnumerator inst;
    return &inst;
}

DeviceEnumerator::DeviceEnumerator(QObject *parent)
    : QObject(parent)
{
    // Stop any running scan thread before Qt's parent-child destruction runs,
    // otherwise destroying a running QThread causes a fatal abort.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, [this]() {
        if (m_imuScanThread && m_imuScanThread->isRunning()) {
            m_imuScanThread->quit();
            if (!m_imuScanThread->wait(3000))
                m_imuScanThread->terminate();
        }
    });
}

void DeviceEnumerator::enumerate()
{
    // Individual camera / audio backends register themselves on demand.
}

QList<Device> DeviceEnumerator::devices() const
{
    return m_devices;
}

QList<Device> DeviceEnumerator::devices(DeviceType type) const
{
    QList<Device> result;
    for (const Device &dev : m_devices) {
        if (dev.type == type)
            result.append(dev);
    }
    return result;
}

void DeviceEnumerator::registerDevice(DeviceType type, VideoInputFactory::Backend backend,
                                       const QString &id, const QString &description,
                                       const CameraCapabilities &capabilities)
{
    for (const auto &dev : m_devices) {
        if (dev.type == type && dev.backend == backend && dev.id == id) return;
    }
    Device dev;
    dev.type         = type;
    dev.backend      = backend;
    dev.id           = id;
    dev.description  = description;
    dev.capabilities = capabilities;
    m_devices.append(dev);
    ppInfo() << "[Device] Registered:" << description;
    emit deviceAdded(dev);
}

void DeviceEnumerator::registerImuDevice(ImuBase::Transport transport,
                                          const QString &id,
                                          const QString &description,
                                          const ImuCapabilities &capabilities,
                                          const QVariant &platformHandle)
{
    for (const Device &dev : m_devices) {
        if (dev.type == DeviceType::Imu && dev.imuTransport == transport && dev.id == id)
            return;
    }
    Device dev;
    dev.type           = DeviceType::Imu;
    dev.imuTransport   = transport;
    dev.id             = id;
    dev.description    = description;
    dev.imuCapabilities = capabilities;
    dev.platformHandle = platformHandle;
    m_devices.append(dev);
    ppInfo() << "[IMU] Device found:" << description << id;
    emit deviceAdded(dev);
}

void DeviceEnumerator::scanImu()
{
    if (m_imuScanActive) return;
    m_imuScanActive = true;

    // --- Serial ---
    // TODO: enumerate serial ports (e.g. /dev/ttyUSB*, /dev/ttyACM*, COM*) and
    //       probe each one with WT9011DCL to confirm a WitMotion device responds.
    ppInfo() << "[IMU] Serial scan: stub — no serial devices enumerated";

    // --- BLE ---
    // Run in a worker thread so the 30 s discovery window doesn't block the
    // main thread. The ImuBleScanner object lives on the worker thread.
    m_imuScanThread = new QThread(this);
    auto *scanner = new ImuBleScanner;
    scanner->moveToThread(m_imuScanThread);

    // Kick the scanner once the thread's event loop is running
    connect(m_imuScanThread, &QThread::started,  scanner, &ImuBleScanner::start);
    // scanner is cleaned up when the thread finishes
    connect(m_imuScanThread, &QThread::finished, scanner, &QObject::deleteLater);

    // Each matched BLE device is forwarded to the main thread via queued connection
    connect(scanner, &ImuBleScanner::deviceFound,
            this, [this](const QBluetoothDeviceInfo &info) {
                const QString id = info.address().isNull()
                    ? info.deviceUuid().toString()
                    : info.address().toString();
                const QString name = info.name().isEmpty()
                    ? QStringLiteral("WT901 Series") : info.name();

                // Build capabilities using the shared WT901 defaults
                ImuCapabilities caps = WT9011DCL_Base::wt901Defaults();
                caps.modelName                    = QStringLiteral("WT901 Series");
                caps.transport                    = ImuBase::Transport::Ble;
                caps.hasMagnetometer              = false;
                caps.hasTemperature               = false;
                caps.hasBattery                   = true;
                caps.supportsBaudRateControl      = false;
                caps.supportsOutputDataSelection  = false;
                caps.queriedAt                    = QDateTime::currentDateTime();

                registerImuDevice(ImuBase::Transport::Ble, id, name, caps,
                                  QVariant::fromValue(info));
            }, Qt::QueuedConnection);

    connect(scanner, &ImuBleScanner::finished,
            this, [this]() {
                m_imuScanActive = false;
                m_imuScanThread->quit();
                emit imuScanFinished();
                ppInfo() << "[IMU] Scan complete —"
                          << devices(DeviceType::Imu).count() << "IMU device(s) registered";
            }, Qt::QueuedConnection);

    // Null the pointer after the thread has fully stopped
    connect(m_imuScanThread, &QThread::finished, this, [this]() {
        m_imuScanThread = nullptr;
    });

    m_imuScanThread->start();
}

#include "device_enumerator.moc"
