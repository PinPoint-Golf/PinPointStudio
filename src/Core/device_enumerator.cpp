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
#include "pp_debug.h"

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
