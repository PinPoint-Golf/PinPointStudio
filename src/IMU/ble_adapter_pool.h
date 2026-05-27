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

#include <QBluetoothAddress>
#include <QList>

// Enumerates local Bluetooth adapters at startup and dispenses them round-robin
// for BLE IMU connections on Linux.
//
// On non-Linux platforms (or when only one adapter is present) nextAdapter()
// returns a null QBluetoothAddress, which callers treat as "use the default
// adapter" — preserving existing single-adapter behaviour unchanged.
//
// Usage:
//   BleAdapterPool::instance()->initialize();  // once, at startup
//   QBluetoothAddress a = BleAdapterPool::instance()->nextAdapter();
//   transport->connectToDevice(info, a);

class BleAdapterPool
{
public:
    static BleAdapterPool *instance();

    // Populate the adapter list via QBluetoothLocalDevice::allDevices().
    // Safe to call multiple times; subsequent calls are no-ops.
    void initialize();

    int                      adapterCount() const { return m_adapters.size(); }
    QList<QBluetoothAddress> adapters()     const { return m_adapters; }

    // Returns the next adapter address in round-robin order.
    // Returns QBluetoothAddress() (null) when 0 or 1 adapters are present —
    // callers must treat null as "use default", never pass it to the
    // adapter-specific QBluetoothDeviceDiscoveryAgent constructor.
    QBluetoothAddress nextAdapter();

private:
    BleAdapterPool() = default;

    bool                     m_initialized = false;
    QList<QBluetoothAddress> m_adapters;
    int                      m_index = 0;
};
