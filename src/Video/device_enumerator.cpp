#include "device_enumerator.h"
#include <QDebug>

DeviceEnumerator* DeviceEnumerator::instance()
{
    static DeviceEnumerator inst;
    return &inst;
}

DeviceEnumerator::DeviceEnumerator(QObject *parent)
    : QObject(parent)
{
}

void DeviceEnumerator::enumerate()
{
    // Individual backends will register themselves when called.
}

QList<Device> DeviceEnumerator::devices() const
{
    return m_devices;
}

void DeviceEnumerator::registerDevice(DeviceType type, VideoInputFactory::Backend backend, const QString &id, const QString &description)
{
    // Check for duplicates
    for (const auto &dev : m_devices) {
        if (dev.type == type && dev.backend == backend && dev.id == id) return;
    }

    Device dev = { type, backend, id, description };
    m_devices.append(dev);
}
