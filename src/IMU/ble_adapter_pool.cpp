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

#include "ble_adapter_pool.h"
#include "pp_debug.h"

#include <QBluetoothLocalDevice>

BleAdapterPool *BleAdapterPool::instance()
{
    static BleAdapterPool pool;
    return &pool;
}

void BleAdapterPool::initialize()
{
    if (m_initialized) return;
    m_initialized = true;

    const QList<QBluetoothHostInfo> hosts = QBluetoothLocalDevice::allDevices();
    for (const QBluetoothHostInfo &info : hosts)
        m_adapters.append(info.address());

    if (m_adapters.isEmpty()) {
        ppWarn() << "[BT] No local Bluetooth adapters found";
    } else if (m_adapters.size() == 1) {
        ppInfo() << "[BT] 1 Bluetooth adapter:" << m_adapters.first().toString();
    } else {
        QStringList addrs;
        for (const QBluetoothAddress &a : m_adapters)
            addrs.append(a.toString());
        ppInfo() << "[BT]" << m_adapters.size() << "Bluetooth adapters:"
                 << addrs.join(QStringLiteral(", "));
    }
}

QBluetoothAddress BleAdapterPool::nextAdapter()
{
    if (m_adapters.size() <= 1)
        return QBluetoothAddress();
    return m_adapters[m_index++ % m_adapters.size()];
}
