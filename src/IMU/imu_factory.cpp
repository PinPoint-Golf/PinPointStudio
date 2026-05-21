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

#include "imu_factory.h"
#include "wt9011dcl.h"
#include "wt9011dcl_ble.h"

ImuBase *ImuFactory::create(ImuBase::Transport transport, QObject *parent)
{
    switch (transport) {
    case ImuBase::Transport::Serial:
        return new WT9011DCL(parent);
    case ImuBase::Transport::Ble:
        return new WT9011DCL_BLE(parent);
    }
    return nullptr;
}

QList<ImuBase::Transport> ImuFactory::available()
{
    return { ImuBase::Transport::Serial, ImuBase::Transport::Ble };
}
