#include "wt9011dcl_ble.h"

QBluetoothUuid WT9011DCL_BLE::ServiceUuid =
    QBluetoothUuid(QStringLiteral("0000ffe5-0000-1000-8000-00805f9b34fb"));

QBluetoothUuid WT9011DCL_BLE::NotifyCharUuid =
    QBluetoothUuid(QStringLiteral("0000ffe4-0000-1000-8000-00805f9b34fb"));

QBluetoothUuid WT9011DCL_BLE::WriteCharUuid =
    QBluetoothUuid(QStringLiteral("0000ffe9-0000-1000-8000-00805f9b34fb"));

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

void WT9011DCL_BLE::scan(int durationMs)
{
    if (m_state == State::Scanning)
        return;

    if (!m_scanner) {
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
}

void WT9011DCL_BLE::onScanFinished()
{
    if (m_state == State::Scanning)
        setState(State::Disconnected);
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
    // Set Connecting *before* stopScan so that onScanFinished does not
    // transition back through Disconnected and clear the busy/connected state.
    setState(State::Connecting);
    stopScan();
    teardownController();

    m_controller = QLowEnergyController::createCentral(device, this);

    emit diagnosticInfo(QStringLiteral("Connecting to %1 (RSSI=%2 dBm)")
                        .arg(device.address().toString())
                        .arg(device.rssi()));

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
    Q_UNUSED(uuid)
}

void WT9011DCL_BLE::onServiceDiscoveryFinished()
{
    setupService();
}

void WT9011DCL_BLE::onControllerError(QLowEnergyController::Error error)
{
    setState(State::Error);
    emit errorOccurred(
        QStringLiteral("Controller error %1: %2 (after %3 ms)")
        .arg(static_cast<int>(error))
        .arg(m_controller->errorString())
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
