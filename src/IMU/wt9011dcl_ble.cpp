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

QBluetoothUuid WT9011DCL_BLE::ServiceUuid =
    QBluetoothUuid(QStringLiteral("0000ffe5-0000-1000-8000-00805f9b34fb"));

QBluetoothUuid WT9011DCL_BLE::NotifyCharUuid =
    QBluetoothUuid(QStringLiteral("0000ffe4-0000-1000-8000-00805f9b34fb"));

QBluetoothUuid WT9011DCL_BLE::WriteCharUuid =
    QBluetoothUuid(QStringLiteral("0000ffe9-0000-1000-8000-00805f9b34fb"));

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
std::optional<WT9011DCL_Base::QuaternionData>
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

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

WT9011DCL_BLE::WT9011DCL_BLE(QObject *parent)
    : WT9011DCL_Base(parent)
{}

WT9011DCL_BLE::~WT9011DCL_BLE()
{
    stopScan();
    teardownController();
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void WT9011DCL_BLE::teardownController()
{
    if (m_service) {
        m_service->deleteLater();
        m_service = nullptr;
    }
    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
    m_writeChar  = QLowEnergyCharacteristic{};
    m_notifyChar = QLowEnergyCharacteristic{};
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::ensureScannerCreated()
{
    if (m_scanner) return;
    m_scanner = new QBluetoothDeviceDiscoveryAgent(this);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this,      &WT9011DCL_BLE::onDeviceDiscovered);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::finished,
            this,      &WT9011DCL_BLE::onScanFinished);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::canceled,
            this,      &WT9011DCL_BLE::onScanFinished);
    connect(m_scanner,
            QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(
                &QBluetoothDeviceDiscoveryAgent::errorOccurred),
            this, &WT9011DCL_BLE::onScanError);
}

void WT9011DCL_BLE::scan(int durationMs)
{
    if (m_state == State::Scanning)
        return;

    ensureScannerCreated();
    m_scanner->setLowEnergyDiscoveryTimeout(durationMs);
    setState(State::Scanning);
    m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void WT9011DCL_BLE::stopScan()
{
    if (m_scanner && m_scanner->isActive())
        m_scanner->stop();
}

void WT9011DCL_BLE::onDeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    emit rawDeviceFound(device);

    // Only connect to devices whose name starts with "WT901" — unnamed devices
    // and other BLE peripherals in range cause false connection attempts.
    // Service UUID match is kept as a fallback for firmware that advertises it.
    const bool hasServiceUuid = device.serviceUuids().contains(ServiceUuid);
    const bool hasKnownName   = device.name().startsWith(QStringLiteral("WT901"), Qt::CaseInsensitive);

    if (hasServiceUuid || hasKnownName)
        emit deviceDiscovered(device);

#ifdef Q_OS_LINUX
    // If we are waiting for the target device to appear in the scan (connect phase),
    // call doConnect() as soon as we see it — the scan is still active so the BlueZ
    // advertising cache is warm at the moment connectToDevice() is issued.
    if (m_waitingForScanConfirm && matchesPendingDevice(device)) {
        m_waitingForScanConfirm = false;
        emit diagnosticInfo(QStringLiteral("Target seen in scan — connecting now (scan still active)"));
        doConnect();
    }
#endif
}

void WT9011DCL_BLE::onScanFinished()
{
    if (m_state == State::Scanning) {
        setState(State::Disconnected);
#ifdef Q_OS_LINUX
    } else if (m_waitingForScanConfirm) {
        // Scan timed out (kConnectScanMs) before the target device was seen.
        // Treat as a connection failure so ImuInstance's retry logic fires.
        m_waitingForScanConfirm = false;
        setState(State::Error);
        emit errorOccurred(QStringLiteral("Device not found during connection scan — check device is powered on and advertising"));
#endif
    }
    emit scanFinished();
}

void WT9011DCL_BLE::onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    Q_UNUSED(error)
    setState(State::Error);
    emit errorOccurred(m_scanner->errorString());
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::connectToDevice(const QBluetoothDeviceInfo &device)
{
    setState(State::Connecting);
    teardownController();
    m_pendingDevice = device;

#ifdef Q_OS_LINUX
    // BlueZ 6.x requires an active HCI scan when connectToDevice() is called —
    // the kernel's advertising cache expires quickly when no scan is running.
    // Start a scan now; doConnect() is called from onDeviceDiscovered() once the
    // target is confirmed in the cache, while the scan is still active.
    m_waitingForScanConfirm = true;
    ensureScannerCreated();
    if (!m_scanner->isActive()) {
        m_scanner->setLowEnergyDiscoveryTimeout(kConnectScanMs);
        m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    }
    emit diagnosticInfo(QStringLiteral("Scan started to warm BlueZ cache — will connect to %1 when seen")
                        .arg(device.address().toString()));
#else
    stopScan();
    doConnect();
#endif
}

void WT9011DCL_BLE::doConnect()
{
    // On Linux the scan is still active here — BlueZ cache is warm.
    // On other platforms stopScan() was already called before this.
    m_controller = QLowEnergyController::createCentral(m_pendingDevice, this);

    emit diagnosticInfo(QStringLiteral("Connecting to %1 (RSSI=%2 dBm)")
                        .arg(m_pendingDevice.address().toString())
                        .arg(m_pendingDevice.rssi()));

    m_connectTimer.start();

    connect(m_controller, &QLowEnergyController::connected,
            this,         &WT9011DCL_BLE::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this,         &WT9011DCL_BLE::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this,         &WT9011DCL_BLE::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this,         &WT9011DCL_BLE::onServiceDiscoveryFinished);
    connect(m_controller,
            QOverload<QLowEnergyController::Error>::of(
                &QLowEnergyController::errorOccurred),
            this, &WT9011DCL_BLE::onControllerError);

    m_controller->connectToDevice();
}

bool WT9011DCL_BLE::matchesPendingDevice(const QBluetoothDeviceInfo &d) const
{
    if (!m_pendingDevice.isValid()) return false;
    if (!d.address().isNull() && !m_pendingDevice.address().isNull())
        return d.address() == m_pendingDevice.address();
    if (!d.deviceUuid().isNull() && !m_pendingDevice.deviceUuid().isNull())
        return d.deviceUuid() == m_pendingDevice.deviceUuid();
    return false;
}

void WT9011DCL_BLE::disconnectFromDevice()
{
    teardownController();
    setState(State::Disconnected);
    emit disconnected();
}

// ---------------------------------------------------------------------------
// Controller slots
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::onControllerConnected()
{
    // HCI connection established — the scan is no longer needed. GATT service
    // discovery proceeds over the L2CAP link and does not require advertising.
    stopScan();
    emit diagnosticInfo(
        QStringLiteral("BLE link established in %1 ms — discovering services")
        .arg(m_connectTimer.elapsed()));
    setState(State::DiscoveringServices);
    m_controller->discoverServices();
}

void WT9011DCL_BLE::onControllerDisconnected()
{
    teardownController();
    setState(State::Disconnected);
    emit disconnected();
}

void WT9011DCL_BLE::onServiceDiscovered(const QBluetoothUuid &uuid)
{
    emit diagnosticInfo(QStringLiteral("BLE service: %1").arg(uuid.toString()));
}

void WT9011DCL_BLE::onServiceDiscoveryFinished()
{
    setupService();
}

void WT9011DCL_BLE::onControllerError(QLowEnergyController::Error error)
{
    stopScan();
    setState(State::Error);
    const QString errStr = m_controller ? m_controller->errorString()
                                        : QStringLiteral("(controller already torn down)");
    emit errorOccurred(
        QStringLiteral("Controller error %1: %2 (after %3 ms)")
        .arg(static_cast<int>(error))
        .arg(errStr)
        .arg(m_connectTimer.elapsed()));
}

// ---------------------------------------------------------------------------
// Service setup
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::setupService()
{
    // Search by the 16-bit service ID (FFE5) rather than an exact 128-bit match.
    // Different WitMotion hardware revisions use different base UUID suffixes
    // (00805f9b34fb on some, 00805f9a34fb on others).
    QBluetoothUuid svcUuid;
    for (const QBluetoothUuid &uuid : m_controller->services()) {
        if (uuid.toString().contains(QStringLiteral("ffe5"), Qt::CaseInsensitive)) {
            svcUuid = uuid;
            break;
        }
    }

    if (svcUuid.isNull()) {
        QString found;
        for (const QBluetoothUuid &uuid : m_controller->services())
            found += QStringLiteral("\n  ") + uuid.toString();
        setState(State::Error);
        emit errorOccurred(QStringLiteral("WITMOTION service not found. Device has:%1").arg(found));
        return;
    }

    // Derive the characteristic UUIDs from the same base as the discovered service,
    // then update the statics so enableNotifications() and onCharacteristicChanged()
    // use the correct UUIDs for this device.
    const QString base = svcUuid.toString().mid(1, 36); // strip Qt's surrounding braces
    QString notifyStr = base;
    notifyStr.replace(QStringLiteral("ffe5"), QStringLiteral("ffe4"), Qt::CaseInsensitive);
    QString writeStr = base;
    writeStr.replace(QStringLiteral("ffe5"), QStringLiteral("ffe9"), Qt::CaseInsensitive);
    ServiceUuid    = svcUuid;
    NotifyCharUuid = QBluetoothUuid(notifyStr);
    WriteCharUuid  = QBluetoothUuid(writeStr);

    m_service = m_controller->createServiceObject(ServiceUuid, this);
    if (!m_service) {
        setState(State::Error);
        emit errorOccurred(QStringLiteral("Failed to create BLE service object"));
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this,      &WT9011DCL_BLE::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this,      &WT9011DCL_BLE::onCharacteristicChanged);
    connect(m_service,
            QOverload<QLowEnergyService::ServiceError>::of(
                &QLowEnergyService::errorOccurred),
            this, &WT9011DCL_BLE::onServiceError);

    m_service->discoverDetails();
}

void WT9011DCL_BLE::enableNotifications()
{
    m_notifyChar = m_service->characteristic(NotifyCharUuid);
    m_writeChar  = m_service->characteristic(WriteCharUuid);

    if (!m_notifyChar.isValid() || !m_writeChar.isValid()) {
        setState(State::Error);
        emit errorOccurred(QStringLiteral("Required BLE characteristics not found"));
        return;
    }

    const bool canNotify   = m_notifyChar.properties() & QLowEnergyCharacteristic::Notify;
    const bool canIndicate = m_notifyChar.properties() & QLowEnergyCharacteristic::Indicate;

    if (!canNotify && !canIndicate) {
        setState(State::Error);
        emit errorOccurred(QStringLiteral("Notify characteristic supports neither Notify nor Indicate (props=0x%1)")
                           .arg(static_cast<int>(m_notifyChar.properties()), 0, 16));
        return;
    }

    const QLowEnergyDescriptor cccd = m_notifyChar.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);

    if (!cccd.isValid()) {
        setState(State::Error);
        emit errorOccurred(QStringLiteral("Notify characteristic has no CCCD descriptor"));
        return;
    }

    // On macOS, setNotifyValue:YES is asynchronous — data won't flow until
    // peripheral:didUpdateNotificationStateForCharacteristic: fires. Defer
    // Ready state until descriptorWritten confirms the subscription is live.
    auto *svc = m_service;
    connect(svc, &QLowEnergyService::descriptorWritten, this,
            [this, svc](const QLowEnergyDescriptor &d, const QByteArray &) {
        if (d.type() != QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration)
            return;
        disconnect(svc, &QLowEnergyService::descriptorWritten, this, nullptr);
        initializeDevice();
        setState(State::Ready);
        emit connected();
    });

    const QByteArray cccdValue = canNotify ? QByteArray::fromHex("0100")
                                           : QByteArray::fromHex("0200");
    m_service->writeDescriptor(cccd, cccdValue);
}

// ---------------------------------------------------------------------------
// Service slots
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::onServiceStateChanged(QLowEnergyService::ServiceState newState)
{
    if (newState == QLowEnergyService::RemoteServiceDiscovered)
        enableNotifications();
}

void WT9011DCL_BLE::onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                             const QByteArray &value)
{
    Q_UNUSED(c)
    receiveData(value);
}

void WT9011DCL_BLE::onServiceError(QLowEnergyService::ServiceError error)
{
    setState(State::Error);
    emit errorOccurred(QStringLiteral("BLE service error (%1)").arg(static_cast<int>(error)));
}

// ---------------------------------------------------------------------------
// Transport write
// ---------------------------------------------------------------------------

void WT9011DCL_BLE::writeToDevice(const QByteArray &data)
{
    if (!m_service || !m_writeChar.isValid())
        return;

    const QLowEnergyService::WriteMode mode =
        (m_writeChar.properties() & QLowEnergyCharacteristic::WriteNoResponse)
            ? QLowEnergyService::WriteWithoutResponse
            : QLowEnergyService::WriteWithResponse;

    m_service->writeCharacteristic(m_writeChar, data, mode);
}
