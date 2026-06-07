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

#include "imu_manager.h"

#include "ble_adapter_pool.h"
#include "event_buffer.h"

ImuManager::ImuManager(pinpoint::EventBuffer *buffer, AppSettings *appSettings, QObject *parent)
    : QObject(parent)
    , m_eventBuffer(buffer)
    , m_appSettings(appSettings)
{
    // Enumerate local BT adapters before scanning so multi-adapter discovery
    // and round-robin connection assignment are both ready from the start.
    BleAdapterPool::instance()->initialize();

    // Start the async BLE scan.  Results arrive via deviceAdded and are
    // automatically reflected by imuList() / imuDeviceList() reading directly
    // from DeviceEnumerator — no local list copy needed.
    DeviceEnumerator::instance()->scanImu();

    // Emit property-change signals when new devices are registered so QML
    // Repeaters rebuild their chips / rows.
    connect(DeviceEnumerator::instance(), &DeviceEnumerator::deviceAdded,
            this, [this](const Device &dev) {
        if (dev.type != DeviceType::Imu) return;
        // Seed alias for newly-seen device so it always has a value.
        const QString imuKey = dev.description + QStringLiteral("|") + dev.id;
        AppSettings  fallback;
        AppSettings *s = m_appSettings ? m_appSettings : &fallback;
        QVariantMap aliasMap = s->imuAlias();
        if (!aliasMap.contains(imuKey)) {
            aliasMap[imuKey] = QString(imuKey).replace(QLatin1Char('|'), QLatin1Char(' '));
            s->setImuAlias(aliasMap);
        }
        emit imuListChanged();
        emit imuDeviceListChanged();
        emit imuEnumeratedCountChanged();
    });
}

ImuManager::~ImuManager()
{
    // Stop and deregister all active instances.
    if (m_eventBuffer) {
        const bool wasCapturing =
            m_eventBuffer->state() == pinpoint::BufferState::Capturing;
        if (wasCapturing) m_eventBuffer->pause();

        if (m_eventBuffer->state() == pinpoint::BufferState::Paused) {
            for (auto &entry : m_selected) {
                if (entry.instance) {
                    entry.instance->stop();
                    entry.instance->deregisterFromBuffer();
                }
            }
        }
    }
    for (auto &entry : m_selected)
        delete entry.instance;
}

// ---------------------------------------------------------------------------
// Properties — both list accessors read from DeviceEnumerator directly
// ---------------------------------------------------------------------------

QVariantList ImuManager::imuList() const
{
    const QList<Device> devs = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    AppSettings  fallback;
    AppSettings *s = m_appSettings ? m_appSettings : &fallback;
    const QVariantMap aliasMap = s->imuAlias();
    QVariantList list;
    for (int i = 0; i < devs.size(); ++i) {
        const Device &dev = devs[i];
        const ImuEntry &entry = m_selected.value(dev.id);
        const bool connected  = entry.instance && entry.instance->imuConnected();
        const bool connecting = entry.instance && entry.instance->busy() && !connected;
        const QString imuKey  = dev.description + QStringLiteral("|") + dev.id;
        QVariantMap m;
        m[QStringLiteral("index")]       = i;
        m[QStringLiteral("id")]          = dev.id;
        m[QStringLiteral("description")] = dev.description;
        m[QStringLiteral("alias")]       = aliasMap.value(imuKey).toString();
        m[QStringLiteral("transport")]   = (dev.imuTransport == ImuBase::Transport::Ble)
                                               ? QStringLiteral("BLE")
                                               : QStringLiteral("Serial");
        m[QStringLiteral("selected")]    = entry.selected;
        m[QStringLiteral("connected")]   = connected;
        m[QStringLiteral("connecting")]  = connecting;
        list.append(m);
    }
    return list;
}

QVariantList ImuManager::imuDeviceList() const
{
    const QList<Device> devs = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    AppSettings  fallback;
    AppSettings *s = m_appSettings ? m_appSettings : &fallback;
    const QVariantMap aliasMap = s->imuAlias();
    QVariantList list;
    for (int i = 0; i < devs.size(); ++i) {
        const Device &dev = devs[i];
        const ImuCapabilities &cap = dev.imuCapabilities;
        const QString imuKey = dev.description + QStringLiteral("|") + dev.id;
        QVariantMap entry;
        entry[QStringLiteral("index")]       = i;
        entry[QStringLiteral("id")]          = dev.id;
        entry[QStringLiteral("imuKey")]      = imuKey;
        entry[QStringLiteral("description")] = dev.description;
        entry[QStringLiteral("alias")]       = aliasMap.value(imuKey).toString();
        entry[QStringLiteral("transport")]   = (dev.imuTransport == ImuBase::Transport::Ble)
                                                   ? QStringLiteral("BLE")
                                                   : QStringLiteral("Serial");
        entry[QStringLiteral("vendorName")]     = cap.vendorName;
        entry[QStringLiteral("modelName")]      = cap.modelName;
        entry[QStringLiteral("serialNumber")]   = cap.serialNumber;
        entry[QStringLiteral("firmwareVersion")] = cap.firmwareVersion;
        entry[QStringLiteral("hasAccelerometer")]  = cap.hasAccelerometer;
        entry[QStringLiteral("hasGyroscope")]      = cap.hasGyroscope;
        entry[QStringLiteral("hasMagnetometer")]   = cap.hasMagnetometer;
        entry[QStringLiteral("hasBattery")]        = cap.hasBattery;
        entry[QStringLiteral("accelRangeMax")]     = cap.accelRange.max;
        entry[QStringLiteral("gyroRangeMax")]      = cap.gyroRange.max;
        QVariantList ratesList;
        for (int r : cap.supportedRatesHz) ratesList.append(r);
        entry[QStringLiteral("supportedRatesHz")]  = ratesList;
        entry[QStringLiteral("defaultRateHz")]     = cap.defaultRateHz;
        entry[QStringLiteral("supportsSixAxisFusion")]       = cap.supportsSixAxisFusion;
        entry[QStringLiteral("supportsNineAxisFusion")]      = cap.supportsNineAxisFusion;
        entry[QStringLiteral("supportsHorizontalMount")]     = cap.supportsHorizontalMount;
        entry[QStringLiteral("supportsVerticalMount")]       = cap.supportsVerticalMount;
        entry[QStringLiteral("supportsAngleReference")]      = cap.supportsAngleReference;
        entry[QStringLiteral("supportsHeadingZero")]         = cap.supportsHeadingZero;
        entry[QStringLiteral("supportsMagCalibration")]      = cap.supportsMagCalibration;
        entry[QStringLiteral("supportsAccelGyroCalibration")] = cap.supportsAccelGyroCalibration;
        entry[QStringLiteral("supportsConfigPersistence")]   = cap.supportsConfigPersistence;
        list.append(entry);
    }
    return list;
}

QVariantList ImuManager::instances() const
{
    QVariantList list;
    for (const auto &entry : m_selected) {
        if (entry.selected && entry.instance)
            list.append(QVariant::fromValue(static_cast<QObject *>(entry.instance)));
    }
    return list;
}

bool ImuManager::anySelected() const
{
    for (const auto &entry : m_selected)
        if (entry.selected) return true;
    return false;
}

int ImuManager::imuEnumeratedCount() const
{
    return DeviceEnumerator::instance()->devices(DeviceType::Imu).size();
}

bool ImuManager::imuConnected() const
{
    for (const auto &entry : m_selected)
        if (entry.instance && entry.instance->imuConnected()) return true;
    return false;
}

int ImuManager::imuCount() const
{
    int n = 0;
    for (const auto &entry : m_selected)
        if (entry.instance && entry.instance->imuConnected()) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Invokables
// ---------------------------------------------------------------------------

void ImuManager::setSelected(int index, bool selected)
{
    const QList<Device> devs = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    if (index < 0 || index >= devs.size()) return;

    const Device &device = devs[index];
    const QString  id    = device.id;

    ImuEntry &entry = m_selected[id];   // creates entry with defaults if absent
    if (entry.selected == selected) return;

    entry.selected = selected;

    // Pause the buffer around EventBuffer register/deregister (same pattern as CameraManager).
    const bool wasCapturing = m_eventBuffer &&
                              m_eventBuffer->state() == pinpoint::BufferState::Capturing;
    if (wasCapturing) m_eventBuffer->pause();

    if (selected) {
        entry.instance = createInstance(device);
        entry.instance->start();
        if (wasCapturing) m_eventBuffer->resume();
        emit imuListChanged();
        emit instancesChanged();
        // registerSource() may have silently auto-resumed the buffer (first
        // source); let CameraManager re-apply the user capture intent.
        emit bufferStateChanged();
    } else {
        ImuInstance *inst = entry.instance;
        entry.instance = nullptr;
        if (inst) {
            disconnect(inst, nullptr, this, nullptr);
            inst->stop();
            inst->deregisterFromBuffer();
        }
        if (wasCapturing)
            m_eventBuffer->resume();
        emit imuListChanged();
        if (inst) {
            QTimer::singleShot(0, this, [this, inst]() {
                emit instancesChanged();
                inst->deleteLater();
            });
        } else {
            emit instancesChanged();
        }
        // deregisterSource() auto-pauses when the last source is removed.
        emit bufferStateChanged();
        return;
    }
}

void ImuManager::rescanImu()
{
    DeviceEnumerator::instance()->scanImu();
}

QObject *ImuManager::instanceFor(const QString &deviceId) const
{
    const ImuEntry &entry = m_selected.value(deviceId);
    if (entry.selected && entry.instance)
        return static_cast<QObject *>(entry.instance);
    return nullptr;
}

ImuManager::ImuDeviceStats ImuManager::liveDeviceStats(const QString &deviceId) const
{
    ImuDeviceStats stats;
    const ImuEntry &e = m_selected.value(deviceId);
    if (e.selected && e.instance) {
        stats.sourceId        = e.instance->sourceId();
        stats.dataRateHz      = e.instance->dataRateHz();
        stats.batteryPercent  = e.instance->batteryPercent();
        stats.gimbalDropCount = e.instance->gimbalDropCount();
        stats.connected       = e.instance->imuConnected();
        stats.busy            = e.instance->busy();
    }
    return stats;
}

QString ImuManager::saveLog()
{
    QStringList paths;
    for (const auto &entry : m_selected) {
        if (entry.instance)
            paths.append(entry.instance->saveLog());
    }
    return paths.isEmpty() ? QStringLiteral("No active IMU instances") : paths.join(QStringLiteral("\n"));
}

void ImuManager::zeroAll()
{
    for (const auto &entry : m_selected)
        if (entry.instance) entry.instance->zeroOrientation();
}

void ImuManager::setOrientationFilter(const QString &name)
{
    AppSettings  fallback;
    AppSettings *s = m_appSettings ? m_appSettings : &fallback;
    s->setImuOrientationFilter(name);

    const OrientationFilterType type =
        orientationFilterFromString(name.toUtf8().constData());
    for (const auto &entry : m_selected)
        if (entry.instance) entry.instance->setOrientationFilter(type);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

ImuInstance *ImuManager::createInstance(const Device &device)
{
    auto *inst = new ImuInstance(device, m_eventBuffer, this);

    // Restore persisted output rate so it is applied from the very first
    // initializeDevice() call rather than only after the State::Ready reinit.
    AppSettings  fallback;
    AppSettings *s = m_appSettings ? m_appSettings : &fallback;
    const int savedRate = s->imuOutputRateHz().value(device.id, 100).toInt();
    if (savedRate != 100)
        inst->setOutputRateHz(savedRate);

    // Apply the persisted orientation-fusion algorithm so it is in effect from the
    // first packet (the deferred swap in the driver picks it up on connect).
    inst->setOrientationFilter(
        orientationFilterFromString(s->imuOrientationFilter().toUtf8().constData()));

    // Forward log entries to any QML log view listening to imuManager.
    connect(inst, &ImuInstance::logEntryAdded,
            this, &ImuManager::logEntryAdded);

    // Re-emit imuListChanged when connection state changes so chip colours update.
    connect(inst, &ImuInstance::imuConnectedChanged, this, [this]() {
        emit imuListChanged();
        emit instancesChanged(); // instanceFor() rebinds in QML
    });
    connect(inst, &ImuInstance::busyChanged, this, [this]() {
        emit imuListChanged();
    });

    return inst;
}

void ImuManager::setImuAlias(const QString &key, const QString &alias)
{
    AppSettings  fallback;
    AppSettings *s = m_appSettings ? m_appSettings : &fallback;
    QVariantMap map = s->imuAlias();
    const QString trimmed = alias.trimmed();
    const QString current = map.value(key).toString();

    const bool changed = trimmed.isEmpty() ? map.contains(key) : (current != trimmed);
    if (!changed) return;

    if (trimmed.isEmpty())
        map.remove(key);
    else
        map[key] = trimmed;
    s->setImuAlias(map);
    emit imuListChanged();
    emit imuDeviceListChanged();
}
