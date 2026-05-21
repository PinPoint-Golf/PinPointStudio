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

#include "imu_controller.h"

#include "event_buffer.h"

ImuController::ImuController(pinpoint::EventBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_eventBuffer(buffer)
{
    // Start the async BLE scan.  Results arrive via deviceAdded and are
    // automatically reflected by imuList() / imuDeviceList() reading directly
    // from DeviceEnumerator — no local list copy needed.
    DeviceEnumerator::instance()->scanImu();

    // Emit property-change signals when new devices are registered so QML
    // Repeaters rebuild their chips / rows.
    connect(DeviceEnumerator::instance(), &DeviceEnumerator::deviceAdded,
            this, [this](const Device &dev) {
        if (dev.type != DeviceType::Imu) return;
        emit imuListChanged();
        emit imuDeviceListChanged();
        emit imuEnumeratedCountChanged();
    });
}

ImuController::~ImuController()
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

QVariantList ImuController::imuList() const
{
    const QList<Device> devs = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    QVariantList list;
    for (int i = 0; i < devs.size(); ++i) {
        const Device &dev = devs[i];
        const ImuEntry &entry = m_selected.value(dev.id);
        const bool connected  = entry.instance && entry.instance->imuConnected();
        const bool connecting = entry.instance && entry.instance->busy() && !connected;
        QVariantMap m;
        m[QStringLiteral("index")]       = i;
        m[QStringLiteral("id")]          = dev.id;
        m[QStringLiteral("description")] = dev.description;
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

QVariantList ImuController::imuDeviceList() const
{
    const QList<Device> devs = DeviceEnumerator::instance()->devices(DeviceType::Imu);
    QVariantList list;
    for (int i = 0; i < devs.size(); ++i) {
        const Device &dev = devs[i];
        const ImuCapabilities &cap = dev.imuCapabilities;
        QVariantMap entry;
        entry[QStringLiteral("index")]       = i;
        entry[QStringLiteral("id")]          = dev.id;
        entry[QStringLiteral("description")] = dev.description;
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

QVariantList ImuController::instances() const
{
    QVariantList list;
    for (const auto &entry : m_selected) {
        if (entry.selected && entry.instance)
            list.append(QVariant::fromValue(static_cast<QObject *>(entry.instance)));
    }
    return list;
}

bool ImuController::anySelected() const
{
    for (const auto &entry : m_selected)
        if (entry.selected) return true;
    return false;
}

int ImuController::imuEnumeratedCount() const
{
    return DeviceEnumerator::instance()->devices(DeviceType::Imu).size();
}

bool ImuController::imuConnected() const
{
    for (const auto &entry : m_selected)
        if (entry.instance && entry.instance->imuConnected()) return true;
    return false;
}

int ImuController::imuCount() const
{
    int n = 0;
    for (const auto &entry : m_selected)
        if (entry.instance && entry.instance->imuConnected()) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Invokables
// ---------------------------------------------------------------------------

void ImuController::setSelected(int index, bool selected)
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
    } else {
        ImuInstance *inst = entry.instance;
        entry.instance = nullptr;
        if (inst) {
            inst->stop();
            inst->deregisterFromBuffer();
        }
        if (wasCapturing) m_eventBuffer->resume();
        // Update chip colours / connecting state immediately.
        emit imuListChanged();
        // Defer removing the instance from the Repeater model by one event-loop
        // tick. A synchronous instancesChanged() destroys the ImuVizView (View3D)
        // while the QSGRenderThread may be inside QRhi::beginFrame(), which
        // processes pending Metal pipeline-state deletions. The AMD Radeon driver
        // then hits a use-after-free in GFX10_MtlRenderPipelineState → SIGBUS.
        // Deferring gives the render thread time to finish its current frame first.
        if (inst) {
            QTimer::singleShot(0, this, [this, inst]() {
                emit instancesChanged();
                inst->deleteLater();
            });
        } else {
            emit instancesChanged();
        }
        return;
    }
}

void ImuController::rescanImu()
{
    DeviceEnumerator::instance()->scanImu();
}

QObject *ImuController::instanceFor(const QString &deviceId) const
{
    const ImuEntry &entry = m_selected.value(deviceId);
    if (entry.selected && entry.instance)
        return static_cast<QObject *>(entry.instance);
    return nullptr;
}

QString ImuController::saveLog()
{
    QStringList paths;
    for (const auto &entry : m_selected) {
        if (entry.instance)
            paths.append(entry.instance->saveLog());
    }
    return paths.isEmpty() ? QStringLiteral("No active IMU instances") : paths.join(QStringLiteral("\n"));
}

void ImuController::zeroAll()
{
    for (const auto &entry : m_selected)
        if (entry.instance) entry.instance->zeroOrientation();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

ImuInstance *ImuController::createInstance(const Device &device)
{
    auto *inst = new ImuInstance(device, m_eventBuffer, this);

    // Forward log entries to any QML log view listening to imuController.
    connect(inst, &ImuInstance::logEntryAdded,
            this, &ImuController::logEntryAdded);

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
