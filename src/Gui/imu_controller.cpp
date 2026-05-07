#include "imu_controller.h"

#include <QDateTime>
#include <chrono>

ImuController::ImuController(QObject *parent)
    : QObject(parent)
    , m_imu(new WT9011DCL_BLE(this))
{
    connect(m_imu, &WT9011DCL_BLE::stateChanged,
            this,  &ImuController::onStateChanged);

    connect(m_imu, &WT9011DCL_BLE::rawDeviceFound,
            this, [this](const QBluetoothDeviceInfo &device) {
        const QString name = device.name().isEmpty()
                             ? QStringLiteral("(unnamed)")
                             : device.name();
        // macOS CoreBluetooth hides MAC addresses; use the per-host UUID instead.
        const QString id = device.address().isNull()
                           ? device.deviceUuid().toString()
                           : device.address().toString();
        appendLog(timestamp()
                  + QStringLiteral("  BLE: ") + name
                  + QStringLiteral(" [") + id
                  + QStringLiteral("] RSSI=") + QString::number(device.rssi())
                  + QStringLiteral(" dBm"));
    });

    connect(m_imu, &WT9011DCL_BLE::deviceDiscovered,
            this, [this](const QBluetoothDeviceInfo &device) {
        if (m_attemptingConn)
            return;
        m_attemptingConn = true;
        const QString name = device.name().isEmpty() ? QStringLiteral("(unnamed)")
                                                     : device.name();
        const QString id = device.address().isNull()
                           ? device.deviceUuid().toString()
                           : device.address().toString();
        appendLog(timestamp() + "  >>> Attempting: " + name + " [" + id + "]");
        m_imu->connectToDevice(device);
    });

    connect(m_imu, &WT9011DCL_BLE::scanFinished, this, [this]() {
        if (!m_connected && !m_attemptingConn)
            appendLog(timestamp() + "  Scan finished — no IMU found");
    });

    connect(m_imu, &WT9011DCL_Base::connected, this, [this]() {
        appendLog(timestamp() + "  BLE link up — notifications enabled");
    });

    connect(m_imu, &WT9011DCL_Base::errorOccurred, this, [this](const QString &msg) {
        appendLog(timestamp() + "  ERROR: " + msg);
    });

    connect(m_imu, &WT9011DCL_Base::accelUpdated,
            this, [this](const WT9011DCL_Base::AccelData &d) {
        appendLog(timestamp()
            + QString("  Accel   x=%1g  y=%2g  z=%3g  T=%4°C")
                .arg(d.x, 8, 'f', 4)
                .arg(d.y, 8, 'f', 4)
                .arg(d.z, 8, 'f', 4)
                .arg(d.temperature, 5, 'f', 1));
    });

    connect(m_imu, &WT9011DCL_Base::gyroUpdated,
            this, [this](const WT9011DCL_Base::GyroData &d) {
        appendLog(timestamp()
            + QString("  Gyro    x=%1°/s  y=%2°/s  z=%3°/s  T=%4°C")
                .arg(d.x, 8, 'f', 3)
                .arg(d.y, 8, 'f', 3)
                .arg(d.z, 8, 'f', 3)
                .arg(d.temperature, 5, 'f', 1));
    });

    connect(m_imu, &WT9011DCL_Base::eulerAnglesUpdated,
            this, [this](const WT9011DCL_Base::EulerAngles &a) {
        appendLog(timestamp()
            + QString("  Euler   roll=%1°  pitch=%2°  yaw=%3°")
                .arg(a.roll,  8, 'f', 3)
                .arg(a.pitch, 8, 'f', 3)
                .arg(a.yaw,   8, 'f', 3));
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

    connect(m_imu, &WT9011DCL_Base::quaternionUpdated,
            this, [this](const WT9011DCL_Base::QuaternionData &q) {
        appendLog(timestamp()
            + QString("  Quat    w=%1  x=%2  y=%3  z=%4")
                .arg(q.w, 8, 'f', 5)
                .arg(q.x, 8, 'f', 5)
                .arg(q.y, 8, 'f', 5)
                .arg(q.z, 8, 'f', 5));
    });
}

void ImuController::connectImu()
{
    m_imu->scan(30000);  // 30 seconds
}

void ImuController::disconnectImu()
{
    m_imu->stopScan();
    m_imu->disconnectFromDevice();
}

void ImuController::onStateChanged(WT9011DCL_BLE::State s)
{
    switch (s) {
    case WT9011DCL_BLE::State::Disconnected:
        setStateLabel(QStringLiteral("Disconnected"));
        m_attemptingConn = false;
        if (m_connected || m_busy) {
            m_connected = false;
            m_busy = false;
            emit imuConnectedChanged();
            emit busyChanged();
        }
        break;
    case WT9011DCL_BLE::State::Scanning:
        setStateLabel(QStringLiteral("Scanning…"));
        if (!m_busy) {
            m_busy = true;
            emit busyChanged();
        }
        appendLog(timestamp() + "  Scanning for IMU devices…");
        break;
    case WT9011DCL_BLE::State::Connecting:
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
        m_connected = true;
        m_busy = false;
        emit imuConnectedChanged();
        emit busyChanged();
        appendLog(timestamp() + "  IMU ready — receiving data");
        // Poke the device to start streaming: some WitMotion BLE firmware
        // won't output data until it receives a valid write-characteristic command.
        m_imu->setOutputRate(WT9011DCL_Base::OutputRate::Hz_10);
        break;
    case WT9011DCL_BLE::State::Error:
        setStateLabel(QStringLiteral("Error"));
        m_attemptingConn = false;
        if (m_busy) {
            m_busy = false;
            emit busyChanged();
        }
        break;
    }
}

QString ImuController::timestamp()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const qint64 us_total =
        duration_cast<microseconds>(now.time_since_epoch()).count();
    const qint64 secs = us_total / 1'000'000;
    const int    frac  = static_cast<int>(us_total % 1'000'000);
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(secs);
    return dt.toString(QStringLiteral("HH:mm:ss"))
           + QLatin1Char('.')
           + QString::number(frac).rightJustified(6, QLatin1Char('0'));
}

void ImuController::appendLog(const QString &text)
{
    emit logEntryAdded(text);
}

void ImuController::setStateLabel(const QString &s)
{
    if (m_stateLabel == s)
        return;
    m_stateLabel = s;
    emit stateLabelChanged();
}
